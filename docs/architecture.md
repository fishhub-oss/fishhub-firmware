# Architecture

## Source layout

```
fishhub-firmware/
├── platformio.ini              # build environments (nodemcu-32s, native)
├── include/
│   ├── pins.h                  # GPIO pin definitions
│   ├── nvs_store.h             # NVSStore class — persistent key-value storage
│   ├── provisioning.h          # startProvisioning(), ActivationError
│   ├── peripheral_manager.h    # PeripheralManager + Peripheral interface
│   ├── config.h                # credentials fallback for development (gitignored)
│   └── config.h.example        # template for config.h
├── src/
│   ├── main.cpp                # entry point: setup() + loop(), state dispatch
│   ├── nvs_store.cpp           # NVSStore implementation; global nvsStore instance
│   ├── provisioning.cpp        # captive portal AP, device activation, status polling
│   ├── wifi_ntp.h/.cpp         # Wi-Fi connection + NTP time sync
│   ├── http_client.h/.cpp      # HTTP POST for sensor readings with retry
│   ├── mqtt_client.h/.cpp      # FishHubMqttClient — TLS MQTT, command dispatch
│   ├── peripheral_manager.cpp  # PeripheralManager implementation
│   ├── schedule.h/.cpp         # Schedule — time-window + override state for actuators
│   └── peripherals/
│       ├── ds18b20_sensor.h/.cpp   # DS18B20Sensor — temperature reading via OneWire
│       └── relay_actuator.h/.cpp   # RelayActuator — GPIO relay driven by Schedule
├── lib/                        # (reserved for local libraries)
└── test/
    └── test_senml/
        └── test_main.cpp       # native unit tests for SenML builder
```

## Module responsibilities

### `main.cpp`
Entry point. Delegates all work to named helpers — no business logic is written inline:

- `boot()` — initialises Serial, NVS, and the reset button pin; logs NVS key status.
- `provisioningMode()` — calls `startProvisioning()`, which never returns.
- `connectToWifi()` — calls `connectWifi()` then `waitForNtp()`.
- `normalOperation()` — registers peripherals, calls `manager.beginAll()` and `mqttClient.begin()`.
- `handleButton()` — polls the reset button: 3 s hold → `provisioningMode()`; 10 s hold → `nvsStore.clear()` + reboot.
- `sensorTick()` — calls `mqttClient.loop()`, runs `manager.tickAll()`, and POSTs any resulting SenML payload.

`setup()` drives the linear boot sequence; `loop()` runs `handleButton()` then `sensorTick()` on every iteration.

### `nvs_store.h` / `nvs_store.cpp`
`NVSStore` wraps the Arduino `Preferences` library to provide named key-value storage backed by ESP32 NVS flash. A global `nvsStore` instance is defined in `nvs_store.cpp` and shared across all modules.

`isProvisioned()` returns `true` only when the `provisioned` flag is `"1"` **and** all required keys (`wifi_ssid`, `wifi_pass`, `device_id`, `device_jwt`, `mqtt_username`, `mqtt_password`, `mqtt_host`) are present. This implements atomic provisioning: if power is lost mid-write the flag will be absent and the device re-enters AP mode.

`clear()` removes all keys including `provisioned`.

### `provisioning.h` / `provisioning.cpp`
Captive-portal provisioning and device activation.

- `startProvisioning()` — starts a Wi-Fi AP (`"FishHub-Setup"`, mode `WIFI_AP_STA`) at `192.168.4.1`, serves an HTML form, and never returns. A FreeRTOS task on core 0 scans nearby networks (mutex-protected) to populate the SSID dropdown.

  Two modes are detected at form-submit time based on whether `device_jwt` is present in NVS:

  - **Fresh provisioning** (`device_jwt` absent): saves Wi-Fi credentials, calls `postActivate()`, polls `pollMqttCredentials()`, then writes all NVS keys atomically (setting `provisioned = "1"` last) and reboots.
  - **Reconfiguration** (`device_jwt` present): writes new Wi-Fi credentials to temporary NVS keys (`wifi_ssid_new`, `wifi_pass_new`), attempts connection, promotes them to real keys and reboots on success, or deletes them and shows an error on failure — existing MQTT credentials are never touched.

- `postActivate(code, serverUrl)` — POSTs `{"code":"..."}` to `SERVER_URL/devices/activate`, returns `{token, deviceId}` on 202 or an `ActivationError`.

- `pollMqttCredentials(deviceId, jwt, serverUrl, out)` — polls `GET /devices/{id}/status` every 2 s for up to 60 s until `status == "ready"`, then fills `out` with `mqtt_username`, `mqtt_password`, `mqtt_host`. Returns `ActivationError::Timeout` if the deadline passes.

- `enum class ActivationError { None, WifiFailed, InvalidCode, ServerError, Timeout }`

### `wifi_ntp.h` / `wifi_ntp.cpp`
- `connectWifi()` — reads `wifi_ssid` / `wifi_pass` from NVS (falls back to `config.h` defines). Attempts up to 3 connections (10 s timeout each). Halts on total failure.
- `waitForNtp()` — calls `configTime(0, 0, "pool.ntp.org")` and blocks until `getLocalTime()` succeeds (10 s timeout). Halts on failure.

### `http_client.h` / `http_client.cpp`
- `postReading(const String& payload)` — reads `device_jwt` from NVS, POSTs the SenML payload to `SERVER_URL/readings` with `Authorization: Bearer <token>`. Normalises `http://` to `https://` for non-local IPs. On a 5xx or network error, retries once after 1 s.

### `mqtt_client.h` / `mqtt_client.cpp`
`FishHubMqttClient` manages the TLS MQTT connection to HiveMQ Cloud and dispatches inbound commands to `PeripheralManager`.

- `begin(manager)` — reads credentials from NVS (`device_id`, `mqtt_username`, `mqtt_password`, `mqtt_host`; falls back to `config.h` defines for host/port), sets up the TLS root CA (ISRG Root X1), connects, and subscribes to `fishhub/<device_id>/commands/#`.
- `loop()` — calls `_client.loop()`; reconnects if disconnected (5 s cooldown).
- On each inbound message, extracts the peripheral name from the last topic segment and calls `_manager->dispatchCommand()`. One-shot peripherals (`replayCommand() == false`) are deduplicated by persisting the last processed command ID in NVS (`cmd_<name>`); idempotent peripherals (schedules) always re-apply.

### `peripheral_manager.h` / `peripheral_manager.cpp`
`PeripheralManager` owns a list of `Peripheral*` entries and drives them uniformly. See [peripherals.md](peripherals.md) for the full interface and how to add a new peripheral.

- `add(p)` — registers a peripheral.
- `beginAll()` — calls `begin()` on every peripheral.
- `tickAll(now, nowMs)` — calls each peripheral's `tick()` when its `intervalMs()` has elapsed; if any return `true`, collects `appendSenML()` output and returns a complete SenML JSON string (with base record). Returns `""` if nothing produced output.
- `dispatchCommand(name, cmd)` — routes a parsed JSON command to the peripheral matching `name()`.
- `find(name)` — returns the matching peripheral or `nullptr`.

### `schedule.h` / `schedule.cpp`
`Schedule` implements time-window–based on/off logic for actuators.

- `loadWindows(windows)` — parses an array of `["HH:MM","HH:MM"]` pairs. Handles overnight windows (e.g. 22:00–06:00). Clears any active override.
- `isActive(now)` — returns `true` if `now` falls within any window (or if an override is set).
- `setOverride(state)` — forces a fixed state until the next `loadWindows()` call.

### `peripherals/ds18b20_sensor.h/.cpp`
`DS18B20Sensor` reads water temperature from a DS18B20 probe on a OneWire bus.

- `name()` returns `"temperature"`.
- `tick()` reads the sensor; `appendSenML()` emits a record with field `"v"` (Celsius).
- `intervalMs()` defaults to `DS18B20_INTERVAL_MS` (override via `platformio.ini` build flag; default 30 000 ms).

### `peripherals/relay_actuator.h/.cpp`
`RelayActuator` drives a GPIO relay using a `Schedule`.

- `name()` returns the name passed at construction (e.g. `"light"`).
- `applyCommand(cmd)` — parses `windows` (schedule) or `state` (immediate override) from the JSON command and updates the `Schedule`.
- `tick()` evaluates the schedule every second and sets the GPIO accordingly. Emits a heartbeat SenML record every `ACTUATOR_HEARTBEAT_S` seconds (default 300 s) even when the state is unchanged, so the server always has a recent reading.
- `replayCommand()` returns `true` — retained MQTT messages are re-applied on reboot, restoring schedule state without a round-trip to the server.

### `include/pins.h`
- `ONE_WIRE_PIN 4` — GPIO pin for the DS18B20 OneWire data line.
- `RESET_BUTTON_PIN 0` — GPIO 0 (BOOT button, active LOW). Holding for 3 s enters reconfiguration mode; 10 s clears all NVS data.
- `RELAY_LIGHT_PIN` — GPIO pin for the light relay (define in `pins.h`).

## Boot flow

```
setup()
  ├── boot()
  │     ├── Serial.begin(115200)
  │     ├── nvsStore.begin()
  │     └── pinMode(RESET_BUTTON_PIN, INPUT_PULLUP)
  ├── nvsStore.isProvisioned() == false → provisioningMode()  ← never returns
  ├── connectToWifi()
  │     ├── connectWifi()    — blocks until connected or halts
  │     └── waitForNtp()     — blocks until UTC time synced or halts
  └── normalOperation()
        ├── manager.add(new DS18B20Sensor(...))
        ├── manager.add(new RelayActuator("light", ...))
        ├── manager.beginAll()
        └── mqttClient.begin(manager)

loop()
  ├── handleButton()   — 3s → provisioningMode(); 10s → clear + reboot
  └── sensorTick()
        ├── mqttClient.loop()           — keep MQTT alive, dispatch inbound commands
        ├── manager.tickAll(now, nowMs) — tick each peripheral on its interval
        └── postReading(payload)        — POST SenML to server if any data produced
```

## Command flow (MQTT → peripheral)

```
HiveMQ Cloud
  └── topic: fishhub/<device_id>/commands/<peripheral_name>
        └── FishHubMqttClient::onMessage()
              ├── parse JSON
              ├── deduplicate (one-shot peripherals only, via NVS)
              └── PeripheralManager::dispatchCommand(name, cmd)
                    └── peripheral->applyCommand(cmd)
                          └── (RelayActuator) Schedule::loadWindows() or setOverride()
```

## PlatformIO environments

| Environment | Target | Purpose |
|---|---|---|
| `nodemcu-32s` | ESP32 hardware | Default build + flash |
| `native` | Host machine | Unit tests (`pio test -e native`) |

The `native` environment excludes all `src/` files (`build_src_filter = -<*>`) and only compiles `test/` code, avoiding Arduino/ESP32 SDK dependencies.
