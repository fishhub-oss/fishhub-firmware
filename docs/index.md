# fishhub-firmware — Documentation Index

ESP32 Arduino firmware for FishHub aquarium monitoring. On first boot (or after a factory reset) the device starts a captive-portal Wi-Fi AP for provisioning. Once provisioned, each boot cycle reads water temperature from a DS18B20 probe, formats a SenML JSON payload, POSTs it to the FishHub backend, and loops.

## Contents

| Document | What it covers |
|---|---|
| [architecture.md](architecture.md) | Source layout, module responsibilities, boot flow |
| [configuration.md](configuration.md) | `config.h` defines, provisioning flow, NVS keys, button actions |
| [peripherals.md](peripherals.md) | `Peripheral` interface contract, DS18B20 and RelayActuator, adding new peripherals |
| [wire-format.md](wire-format.md) | SenML JSON structure, example payload, field semantics |
| [development.md](development.md) | Build, flash, Serial monitor, unit tests |

## Quick start

```bash
# 1. Build and flash (no config.h required for provisioned devices)
pio run --target upload

# 2. On first boot the device starts AP "FishHub-Setup"
#    Connect to it from your phone or laptop, then open 192.168.4.1
#    Fill in Wi-Fi credentials, server URL, and the provisioning code
#    from the FishHub web app. The device reboots into normal operation.

# 3. Monitor Serial output
pio device monitor
```

> For development without a provisioning code, copy `include/config.h.example` to `include/config.h`, fill in the credentials, and build. `config.h` values act as fallbacks when NVS is empty. See [configuration.md](configuration.md) for details.

## Tech stack

| Concern | Choice |
|---|---|
| Build system | PlatformIO |
| Board | NodeMCU-32S (ESP32) |
| Framework | Arduino |
| Temperature sensor | DS18B20 via OneWire / DallasTemperature |
| JSON serialization | ArduinoJson v7 |
| HTTP client | ESP32 built-in HTTPClient |
| Wire format | SenML JSON (RFC 8428) |
| HTTP auth | RS256 device JWT stored in NVS as Bearer token, issued at activation |
| MQTT auth | Username + password stored in NVS, provisioned asynchronously by HiveMQ after activation |
| Persistent storage | ESP32 NVS via Arduino `Preferences` |
| Captive portal | Arduino `WebServer` (ESP32 built-in) |
| Background tasks | FreeRTOS tasks (Wi-Fi scan during provisioning) |
