# Architecture

## Source layout

```
fishhub-firmware/
├── platformio.ini          # build environments (nodemcu-32s, native)
├── include/
│   ├── pins.h              # GPIO pin definitions (ONE_WIRE_PIN, RESET_BUTTON_PIN)
│   ├── nvs_store.h         # NVSStore class — persistent key-value storage
│   ├── provisioning.h      # startProvisioning(), activateDevice(), ActivationError
│   ├── config.h            # credentials fallback for development (gitignored)
│   └── config.h.example    # template for config.h
├── src/
│   ├── main.cpp            # entry point: setup() + loop()
│   ├── nvs_store.cpp       # NVSStore implementation; global nvsStore instance
│   ├── provisioning.cpp    # captive portal AP, Wi-Fi scan, device activation
│   ├── wifi_ntp.h/.cpp     # Wi-Fi connection + NTP time sync
│   ├── senml.h/.cpp        # SenML JSON payload builder
│   └── http_client.h/.cpp  # HTTP POST with retry logic
├── lib/                    # (reserved for local libraries)
└── test/
    └── test_senml/
        └── test_main.cpp   # native unit tests for SenML builder
```

## Module responsibilities

### `main.cpp`
Entry point. `setup()` initialises NVS, configures the reset button pin, checks that all required credentials are present in NVS (calling `startProvisioning()` if any are missing), then connects Wi-Fi, syncs NTP, and initialises the DS18B20 sensor. `loop()` checks the reset button first, then reads temperature, builds the SenML payload, and POSTs it — then calls `delay(5000)`.

Static helper `buttonHeld(pin, durationMs)` polls the pin every 50 ms and returns `true` if it is held LOW for the full duration.

### `nvs_store.h` / `nvs_store.cpp`
`NVSStore` wraps the Arduino `Preferences` library to provide named key-value storage backed by ESP32 NVS flash. A global `nvsStore` instance is defined in `nvs_store.cpp` and shared across all modules.

Stored keys: `wifi_ssid`, `wifi_pass`, `device_id`, `device_jwt`, `mqtt_username`, `mqtt_password`, `mqtt_host`.

### `provisioning.h` / `provisioning.cpp`
Captive-portal provisioning and device activation.

- `startProvisioning()` — starts a Wi-Fi AP (`"FishHub-Setup"`, mode `WIFI_AP_STA`) at `192.168.4.1`, serves an HTML form, and never returns. A FreeRTOS task on core 0 scans nearby networks (mutex-protected) to populate the SSID dropdown.

  Two modes are detected automatically at form-submit time:
  - **Fresh provisioning** (`device_jwt` absent in NVS): saves Wi-Fi credentials, calls `activateDevice()`, reboots on success, shows an error page on failure.
  - **Reconfiguration** (`device_jwt` present in NVS): saves Wi-Fi credentials only, reboots immediately — no server call, existing token and MQTT credentials are preserved.

- `activateDevice(const String& provisionCode)` — connects to Wi-Fi, POSTs `{"code":"..."}` to `SERVER_URL/devices/activate` (from `config.h`), parses the JWT and MQTT credentials from the response, stores them in NVS, and reboots.

- `enum class ActivationError { None, WifiFailed, InvalidCode, ServerError }` — result type returned by `activateDevice()`.

### `wifi_ntp.h` / `wifi_ntp.cpp`
- `connectWifi()` — reads `wifi_ssid` / `wifi_pass` from NVS (falls back to `config.h` defines). Attempts up to 3 connections (10 s timeout each). Halts on total failure.
- `waitForNtp()` — calls `configTime(0, 0, "pool.ntp.org")` and blocks until `getLocalTime()` succeeds (10 s timeout). Halts on failure.

### `senml.h` / `senml.cpp`
- `buildSenMLPayload(float tempCelsius, time_t timestamp)` — serialises a single SenML record with ArduinoJson. Returns an Arduino `String`. See [wire-format.md](wire-format.md) for the schema.

### `http_client.h` / `http_client.cpp`
- `postReading(const String& payload)` — uses `SERVER_URL` from `config.h` and reads `device_jwt` from NVS. Appends `"/readings"` to the server URL and normalises `http://` to `https://` for non-local IPs. POSTs the payload with `Authorization: Bearer <token>`. On a 5xx or network error, retries once after 1 s.

### `include/pins.h`
- `ONE_WIRE_PIN 4` — GPIO pin for the DS18B20 OneWire data line.
- `RESET_BUTTON_PIN 0` — GPIO 0 (BOOT button, active LOW). Holding it for 3 seconds during normal operation triggers reconfiguration mode.

## Boot flow

```
setup()
  ├── Serial.begin(115200)
  ├── nvsStore.begin()
  ├── pinMode(RESET_BUTTON_PIN, INPUT_PULLUP)
  ├── [check NVS for wifi_ssid, wifi_pass, device_id, device_jwt]
  │     └── any missing → startProvisioning()  ← never returns
  ├── connectWifi()        — blocks until connected or halts
  ├── waitForNtp()         — blocks until UTC time is synced or halts
  └── sensors.begin()      — initialises DS18B20 on OneWire bus

loop()  (runs repeatedly, no deep sleep in current implementation)
  ├── [buttonHeld(RESET_BUTTON_PIN, 3000) → startProvisioning() if held]
  ├── readTemperatureCelsius()
  ├── buildSenMLPayload(temp, time(nullptr)) → String payload
  ├── postReading(payload)
  └── delay(5000)          — 5-second pause before next reading
```

> **Note:** Deep sleep is not yet implemented — the device stays awake and loops every 5 seconds (see GitHub issue #6).

## PlatformIO environments

| Environment | Target | Purpose |
|---|---|---|
| `nodemcu-32s` | ESP32 hardware | Default build + flash |
| `native` | Host machine | Unit tests (`pio test -e native`) |

The `native` environment excludes all `src/` files (`build_src_filter = -<*>`) and only compiles `test/` code, avoiding Arduino/ESP32 SDK dependencies.
