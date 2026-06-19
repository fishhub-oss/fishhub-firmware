# fishhub-firmware — Documentation Index

ESP32 Arduino firmware for FishHub aquarium monitoring. On first boot (or after a
factory reset) the device starts a captive-portal Wi-Fi AP for provisioning. Once
provisioned, it holds a persistent TLS MQTT connection: it **publishes sensor
readings and trigger events** over MQTT and **reacts to inbound MQTT messages**
that provision peripherals, push automation triggers, set device config, and send
commands. The device is online full-time — there is no deep-sleep / wake-and-POST
cycle. HTTP is used only during provisioning/activation.

## Contents

| Document | What it covers |
|---|---|
| [architecture.md](architecture.md) | State machine, boot/loop flow, MQTT topic topology, trigger engine, peripheral model, NVS keys |
| [configuration.md](configuration.md) | `config.h` defines, device provisioning, NVS keys |
| [peripherals.md](peripherals.md) | `Peripheral` interface, `PeripheralManager`, concrete drivers, triggers, adding new peripherals |
| [wire-format.md](wire-format.md) | SenML JSON structure, example payload, field semantics |
| [development.md](development.md) | Build, flash, Serial monitor, unit tests |

## Quick start

```bash
# 1. Build and flash (no config.h required for provisioned devices)
pio run --target upload

# 2. On first boot the device starts AP "FishHub-Setup"
#    Connect to it from your phone or laptop, then open 192.168.4.1
#    Fill in Wi-Fi credentials and the provisioning code from the FishHub web app.
#    The device reboots into normal operation.

# 3. Monitor Serial output
pio device monitor
```

> For development without a provisioning code, copy `include/config.h.example` to
> `include/config.h`, fill in the credentials, and build. `config.h` values act as
> fallbacks when NVS is empty. See [configuration.md](configuration.md) for details.

## Tech stack

| Concern | Choice |
|---|---|
| Build system | PlatformIO |
| Board | NodeMCU-32S (ESP32) |
| Framework | Arduino |
| Temperature sensor | DS18B20 via OneWire / DallasTemperature |
| JSON serialization | ArduinoJson v7 |
| Data transport | MQTT (PubSubClient) over TLS — readings + trigger events published, config/commands/peripherals/triggers received |
| HTTP client | ESP32 built-in HTTPClient — provisioning/activation only |
| MQTT broker | HiveMQ Cloud (TLS, port 8883) in production; plain TCP supported for self-hosted brokers |
| Wire format | SenML JSON (RFC 8428) |
| Auth | Device JWT stored in NVS (provisioned via captive portal) |
| MQTT auth | Username / password stored in NVS (issued by the server at activation) |
| Persistent storage | ESP32 NVS via Arduino `Preferences` |
| Captive portal | Arduino `WebServer` (ESP32 built-in) |
| Peripheral model | `Peripheral` interface + `PeripheralManager`; peripherals are server-provisioned at runtime and restored from NVS via a serializer registry |
| Automation | `Trigger` condition trees evaluated each tick; fired events queued and published |
| Actuator scheduling | `Schedule` (windowed values) for relays; `CronTrigger` entries for servos |
| Background tasks | FreeRTOS tasks (Wi-Fi scan during provisioning) |
