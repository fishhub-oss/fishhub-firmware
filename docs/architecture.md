# Architecture

## Source layout

```
fishhub-firmware/
├── platformio.ini          # build environments (nodemcu-32s, native)
├── include/
│   ├── pins.h              # GPIO pin definitions
│   ├── config.h            # credentials (gitignored)
│   └── config.h.example    # template for config.h
├── src/
│   ├── main.cpp            # entry point: setup() + loop()
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
Entry point. `setup()` connects Wi-Fi, syncs NTP, and initialises the DS18B20 sensor. `loop()` reads temperature, builds the SenML payload, and POSTs it — then calls `delay(5000)`.

### `wifi_ntp.h` / `wifi_ntp.cpp`
- `connectWifi()` — attempts up to 3 connections (10 s timeout each). Halts on total failure.
- `waitForNtp()` — calls `configTime(0, 0, "pool.ntp.org")` and blocks until `getLocalTime()` succeeds (10 s timeout). Halts on failure.

### `senml.h` / `senml.cpp`
- `buildSenMLPayload(float tempCelsius, time_t timestamp)` — serialises a single SenML record with ArduinoJson. Returns an Arduino `String`. See [wire-format.md](wire-format.md) for the schema.

### `http_client.h` / `http_client.cpp`
- `postReading(const String& payload)` — POSTs the payload to `SERVER_URL` with `Authorization: Bearer <DEVICE_TOKEN>`. On a 5xx or network error, retries once after 1 s.

### `include/pins.h`
Defines `ONE_WIRE_PIN 4` — the GPIO pin the DS18B20 data line is connected to.

## Boot flow

```
setup()
  ├── Serial.begin(115200)
  ├── connectWifi()        — blocks until connected or halts
  ├── waitForNtp()         — blocks until UTC time is synced or halts
  └── sensors.begin()      — initialises DS18B20 on OneWire bus

loop()  (runs repeatedly, no deep sleep in current implementation)
  ├── sensors.requestTemperatures()
  ├── sensors.getTempCByIndex(0) → float temp
  ├── [skip if NaN — sensor disconnected]
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
