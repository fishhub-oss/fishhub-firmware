# Development Guide

## Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/) (CLI) or PlatformIO IDE extension
- USB cable to connect the NodeMCU-32S
- Credentials either provisioned via captive portal (recommended) or set in `include/config.h` as fallback (see [configuration.md](configuration.md))

## Common commands

```bash
# Build for ESP32 (default environment)
pio run

# Build + flash to connected device
pio run --target upload

# Open Serial monitor (115200 baud)
pio device monitor

# Build and flash, then open monitor
pio run --target upload && pio device monitor

# Run native unit tests (host machine, no device needed)
pio test -e native
```

## Serial output

### Normal boot (credentials present in NVS)

```
FishHub firmware booting...
NVS credentials found — skipping provisioning
Wi-Fi connecting (attempt 1/3)...
Wi-Fi connected — IP: 192.168.1.42
Waiting for NTP sync...
NTP synced: 2024-04-13 12:00:00 UTC
1 DS18B20 sensor(s) found
Temp: 23.40 °C
POST 201 in 83ms
Temp: 23.41 °C
POST 201 in 76ms
```

### Provisioning boot (missing NVS credentials)

```
FishHub firmware booting...
NVS credentials missing — starting provisioning AP
AP started: FishHub-Setup (192.168.4.1)
Scanning Wi-Fi networks...
Captive portal running — waiting for user input
```

The device stays in this state until the user submits the provisioning form.

### Reconfiguration (BOOT button held 3 s)

```
Reset button held — entering reconfiguration mode
AP started: FishHub-Setup (192.168.4.1)
Reconfiguration mode — provisioning code not required
Captive portal running — waiting for user input
```

A `POST error:` line during normal operation means a network failure. A `POST 401` means the stored token is invalid (re-provision the device). A `POST 400` means the server rejected the payload.

## Unit tests

Tests live in `test/test_senml/test_main.cpp` and run on the host machine using the `native` PlatformIO environment. They test `buildSenMLPayload` directly by re-implementing the logic without Arduino SDK dependencies.

```bash
pio test -e native
```

The test suite covers: happy path, negative temperature, and zero temperature.

Hardware-coupled code (`wifi_ntp`, `http_client`) is verified via Serial output after flashing — there are no automated tests for those modules.

## Adding a library dependency

Add it to `platformio.ini` under `[env:nodemcu-32s]` (and `[env:native]` if the test suite needs it):

```ini
lib_deps =
  author/Library @ ^version
```

PlatformIO downloads and pins dependencies automatically on next build.

## `platformio.ini` environments

```ini
[platformio]
default_envs = nodemcu-32s

[env:nodemcu-32s]
platform      = espressif32
board         = nodemcu-32s
framework     = arduino
monitor_speed = 115200
lib_deps =
  paulstoffregen/OneWire @ ^2.3.8
  milesburton/DallasTemperature @ ^3.11.0
  bblanchon/ArduinoJson @ ^7.3.1

[env:native]
platform = native
lib_deps =
  bblanchon/ArduinoJson @ ^7.3.1
build_src_filter = -<*>   # exclude src/ — tests import their own stubs
```
