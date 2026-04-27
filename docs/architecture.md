# Architecture

## Source layout

```
fishhub-firmware/
├── platformio.ini          # build environments (nodemcu-32s, native)
├── include/
│   ├── pins.h              # GPIO pin definitions
│   ├── nvs_store.h         # NVSStore class — persistent key-value storage
│   ├── provisioning.h      # startProvisioning(), ActivationError
│   ├── peripheral.h        # Peripheral abstract interface
│   ├── peripheral_manager.h# PeripheralManager — owns and ticks all peripherals
│   ├── mqtt_client.h       # FishHubMqttClient — MQTT connection + dispatch
│   ├── schedule.h          # Schedule — time-window and override logic for actuators
│   ├── config.h            # credentials fallback for development (gitignored)
│   └── config.h.example    # template for config.h
├── src/
│   ├── main.cpp            # entry point: setup() + loop(), phase functions
│   ├── nvs_store.cpp       # NVSStore implementation; global nvsStore instance
│   ├── provisioning.cpp    # captive portal AP, Wi-Fi scan, device activation, MQTT polling
│   ├── peripheral_manager.cpp
│   ├── mqtt_client.cpp
│   ├── schedule.cpp
│   ├── wifi_ntp.h/.cpp     # Wi-Fi connection + NTP time sync
│   ├── http_client.h/.cpp  # HTTP POST with retry logic
│   └── peripherals/
│       ├── ds18b20_sensor.h/.cpp   # DS18B20 temperature sensor
│       └── relay_actuator.h/.cpp   # relay actuator (on/off + schedule)
├── lib/                    # (reserved for local libraries)
└── test/
    └── test_senml/
        └── test_main.cpp   # native unit tests for SenML builder
```

## Module responsibilities

### `main.cpp`

Entry point. `setup()` and `loop()` delegate entirely to named phase functions — no business logic is inline:

```cpp
void setup() {
  boot();
  if (!nvsStore.isProvisioned()) provisioningMode();
  connectToWifi();
  normalOperation();
}

void loop() {
  handleButton();
  sensorTick();
}
```

- `boot()` — initialises Serial, NVS, and the reset button pin; logs NVS key status.
- `provisioningMode()` — calls `startProvisioning()`, which never returns.
- `connectToWifi()` — connects Wi-Fi and syncs NTP.
- `normalOperation()` — registers peripherals and starts the MQTT client.
- `handleButton()` — polls the BOOT button; 3 s → reconfiguration mode, 10 s → NVS clear + reboot.
- `sensorTick()` — calls `mqttClient.loop()`, ticks peripherals, POSTs any new readings.

### `nvs_store.h` / `nvs_store.cpp`

`NVSStore` wraps the Arduino `Preferences` library to provide named key-value storage backed by ESP32 NVS flash. A global `nvsStore` instance is defined in `nvs_store.cpp` and shared across all modules.

`isProvisioned()` returns `true` only when all required keys are present **and** the `provisioned` flag is `"1"`. The flag is written last during activation, so a power loss mid-write leaves the device safely unprovisioned. Existing devices that predate the flag are migrated automatically on first boot.

NVS keys: `wifi_ssid`, `wifi_pass`, `device_id`, `device_jwt`, `mqtt_username`, `mqtt_password`, `mqtt_host`, `provisioned`.

### `provisioning.h` / `provisioning.cpp`

Captive-portal provisioning and device activation.

- `startProvisioning()` — starts a Wi-Fi AP (`"FishHub-Setup"`, mode `WIFI_AP_STA`) at `192.168.4.1`, serves an HTML form, and never returns. A FreeRTOS task on core 0 scans nearby networks (mutex-protected) to populate the SSID dropdown.

  Two modes are detected automatically:
  - **Fresh provisioning** (`device_jwt` absent in NVS): calls `postActivate()`, then `pollMqttCredentials()`, writes all credentials atomically, reboots.
  - **Reconfiguration** (`device_jwt` present in NVS): writes new Wi-Fi to temporary NVS keys, tests the connection, promotes to real keys only on success — existing MQTT credentials are never lost.

- Internal helpers (not exported):
  - `postActivate(code, serverUrl)` — POSTs `{"code":"..."}` to `/devices/activate`, returns `{token, deviceId}` on `202`.
  - `pollMqttCredentials(deviceId, jwt, serverUrl, out)` — polls `GET /devices/{id}/status` with the device JWT every 2 s for up to 60 s until the server reports `"ready"` and returns MQTT credentials.

- `enum class ActivationError { None, WifiFailed, InvalidCode, ServerError, Timeout }`.

Every error path restarts the AP and stores an error message shown when the user reconnects to the portal.

### `peripheral.h` — `Peripheral` interface

Abstract base class for all sensors and actuators. See [peripherals.md](peripherals.md) for the full contract and how to add a new peripheral.

### `peripheral_manager.h` / `peripheral_manager.cpp`

`PeripheralManager` owns a list of `Peripheral*` instances and drives the sensor loop:

- `add(Peripheral*)` — registers a peripheral.
- `beginAll()` — calls `begin()` on each peripheral.
- `tickAll(time_t now, uint32_t nowMs)` — calls `tick()` on each peripheral when its `intervalMs()` has elapsed. Collects SenML output from peripherals that returned `true` and returns a single JSON string (empty string if nothing to report).
- `dispatchCommand(name, cmd)` — routes an inbound MQTT command to the peripheral whose `name()` matches.
- `find(name)` — returns the peripheral with the given name, or `nullptr`.

### `mqtt_client.h` / `mqtt_client.cpp`

`FishHubMqttClient` manages the TLS MQTT connection to HiveMQ.

- `begin(PeripheralManager&)` — reads `device_id`, `mqtt_username`, `mqtt_password`, `mqtt_host` from NVS; subscribes to the command topic `fishhub/<device_id>/peripherals/+/commands`.
- `loop()` — reconnects if disconnected (with backoff), then calls `PubSubClient::loop()`.
- On inbound message: parses the JSON payload, extracts the peripheral name from the topic, calls `manager.dispatchCommand(name, cmd)`.

### `schedule.h` / `schedule.cpp`

`Schedule` implements time-window and override logic for actuators.

- `loadWindows(JsonArrayConst)` — parses an array of `["HH:MM", "HH:MM"]` on/off pairs. Clears any active override.
- `isActive(time_t now)` — returns `true` if `now` falls within any window. Handles overnight windows (e.g. 22:00–06:00) correctly.
- `setOverride(bool state)` — forces a fixed state regardless of windows until the next `loadWindows()` call.

### `wifi_ntp.h` / `wifi_ntp.cpp`

- `connectWifi()` — reads `wifi_ssid` / `wifi_pass` from NVS (falls back to `config.h` defines). Attempts up to 3 connections (10 s timeout each). Halts on total failure.
- `waitForNtp()` — calls `configTime(0, 0, "pool.ntp.org")` and blocks until `getLocalTime()` succeeds (10 s timeout). Halts on failure.

### `http_client.h` / `http_client.cpp`

- `postReading(const String& payload)` — reads `device_jwt` from NVS, appends `"/readings"` to `SERVER_URL`, normalises `http://` → `https://` for non-local IPs. POSTs with `Authorization: Bearer <token>`. Retries once after 1 s on 5xx or network error.

### `include/pins.h`

```cpp
#define ONE_WIRE_PIN      4   // DS18B20 data line
#define RESET_BUTTON_PIN  0   // BOOT button (active LOW)
#define RELAY_LIGHT_PIN   X   // relay for the light actuator
```

GPIO 0 is the onboard BOOT button. GPIO 4 is the DS18B20 OneWire data line.

---

## Boot flow

```
boot()
  ├── Serial.begin(115200)
  ├── nvsStore.begin()
  └── pinMode(RESET_BUTTON_PIN, INPUT_PULLUP)

isProvisioned()?
  └── no → provisioningMode() → startProvisioning()  ← never returns

connectToWifi()
  ├── connectWifi()    — blocks until connected or halts
  └── waitForNtp()     — blocks until UTC time is synced or halts

normalOperation()
  ├── manager.add(DS18B20Sensor)
  ├── manager.add(RelayActuator)
  ├── manager.beginAll()
  └── mqttClient.begin(manager)

loop()
  ├── handleButton()
  │     ├── held ≥ 10 s → nvsStore.clear() + reboot
  │     └── held ≥ 3 s  → provisioningMode()  ← never returns
  └── sensorTick()
        ├── mqttClient.loop()    — reconnect + dispatch inbound commands
        ├── manager.tickAll()    — tick each peripheral at its interval
        └── postReading(payload) — if any peripheral produced output
```

---

## PlatformIO environments

| Environment | Target | Purpose |
|---|---|---|
| `nodemcu-32s` | ESP32 hardware | Default build + flash |
| `native` | Host machine | Unit tests (`pio test -e native`) |

The `native` environment excludes all `src/` files and only compiles `test/` code, avoiding Arduino/ESP32 SDK dependencies.
