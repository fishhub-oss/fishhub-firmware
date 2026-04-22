# fishhub-firmware — Documentation Index

ESP32 Arduino firmware for FishHub aquarium monitoring. On each boot cycle it reads water temperature from a DS18B20 probe, formats a SenML JSON payload, POSTs it to the FishHub backend, and loops.

## Contents

| Document | What it covers |
|---|---|
| [architecture.md](architecture.md) | Source layout, module responsibilities, boot flow |
| [configuration.md](configuration.md) | `config.h` required defines, device provisioning |
| [wire-format.md](wire-format.md) | SenML JSON structure, example payload, field semantics |
| [development.md](development.md) | Build, flash, Serial monitor, unit tests |

## Quick start

```bash
# 1. Copy and fill in credentials
cp include/config.h.example include/config.h
# edit config.h with your Wi-Fi, SERVER_URL, and DEVICE_TOKEN

# 2. Build
pio run

# 3. Flash
pio run --target upload

# 4. Monitor Serial output
pio device monitor
```

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
| Auth | Bearer token (hardcoded in `config.h`) |
