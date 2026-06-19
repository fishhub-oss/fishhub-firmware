# fishhub-firmware — Claude Code Instructions

## What this repo is

ESP32 Arduino firmware for the FishHub aquarium monitoring device. After
provisioning, the device holds a persistent TLS MQTT connection to the broker and
runs a continuous main loop (no deep sleep). It **publishes sensor readings and
trigger events** over MQTT, and **reacts to inbound MQTT messages** that provision
peripherals, push automation triggers, set device config, and send commands.
Peripherals (sensors and actuators) are provisioned by the server at runtime and
restored from NVS on boot — they are not hardcoded. HTTP is used only during
provisioning/activation.

## Read the docs first

**Before making any changes, read the relevant docs in `docs/` if they exist.**
The docs folder is the authoritative source of context for this repo. Start with
`docs/architecture.md` for the state machine, MQTT topology, and trigger engine,
and `docs/peripherals.md` for the peripheral and trigger interfaces.

## Workflow

1. Before starting any issue, create a plan file in `../planning/` (e.g. `../planning/firmware-05-http-post.md`).
2. Discuss the plan with the user before executing.
3. Implement only after the user approves.
4. Never commit directly to `main`. Always create a feature branch, commit there, and open a PR.
5. After completing an issue, move the corresponding GitHub issue to the Done column on the FishHub PoC project (`org: fishhub-oss`, project ID 1).

## Git conventions

- **Branch naming:** `feat/<slug>`, `fix/<slug>`, `chore/<slug>`, `docs/<slug>`
- **Commit style:** [Conventional Commits](https://www.conventionalcommits.org/)
  - `feat:` new feature
  - `fix:` bug fix
  - `chore:` tooling, config, deps
  - `refactor:` code change with no behavior change
  - `docs:` documentation only
- **PRs:** descriptive but concise — what changed and why. Always use `Closes #<n>` in the PR body.

## GitHub

- Org: `fishhub-oss`
- Repo: `fishhub-oss/fishhub-firmware`
- Project board: https://github.com/orgs/fishhub-oss/projects/1
- Issues assigned to: `renanmzmendes`

## Key conventions

- **Build system:** PlatformIO (`pio run` to build, `pio run --target upload` to flash)
- **Board:** NodeMCU-32S (`nodemcu-32s` in `platformio.ini`)
- **Framework:** Arduino
- **Libraries:** OneWire, DallasTemperature, ArduinoJson v7, PubSubClient (MQTT), Adafruit SSD1306 + Adafruit GFX (OLED), ESP32Servo
- **Data transport:** MQTT over TLS. Readings and trigger events are published; peripherals/triggers/config/commands are received. HTTP is provisioning-only.
- **Wire format:** SenML JSON (RFC 8428)
- **MQTT topics:** namespaced `fishhub/<device_id>/…` — sub: `commands/#`, `peripherals/#`, `triggers/#`, `config`; pub: `readings`, `trigger_events`
- **Auth:** device JWT + MQTT username/password stored in NVS (gitignored `config.h` provides dev fallbacks — use `config.h.example` as template)
- **Peripherals:** server-provisioned at runtime via the `peripherals/<name>` topic and reconstructed through `peripheralSerializerRegistry`; a new `kind` needs a `PeripheralSerializer`. Pins are server-assigned, not in `pins.h`.
- **Pin definitions:** `include/pins.h` (only the two on-board buttons; peripheral pins come from the server)
- **State machine / entry point:** `src/main.cpp`
- **MQTT:** `include/mqtt_client.h` / `src/mqtt_client.cpp`
- **Peripheral interface / manager:** `include/peripheral.h`, `include/peripheral_manager.h`
- **Trigger engine:** `src/trigger.*`, `src/trigger_event_queue.*`, `src/cron_trigger.*`

## Testing

- Native (host-side) unit tests for pure functions: `pio test -e native` (SenML), plus `test_trigger_event_queue`, `test_servo_cr`, `test_servo_positional` environments.
- Hardware-coupled code is verified via Serial output (and the OLED DEBUG view) after flashing.
- `pio run` defaults to the ESP32 target (`default_envs = nodemcu-32s` in `platformio.ini`).
- Always run `pio run` to verify compilation before opening a PR.

## config.h

`include/config.h` is gitignored and must be created locally from
`include/config.h.example`. It provides development fallbacks (NVS values take
precedence once provisioned):

```cpp
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// MQTT broker — define MQTT_TLS for TLS (HiveMQ Cloud on 8883),
// leave undefined for plain TCP (e.g. self-hosted broker via TCP proxy).
#define MQTT_TLS
#define MQTT_HOST     "your-broker.hivemq.cloud"
#define MQTT_PORT     8883

#define DEVICE_ID     ""            // populated automatically by provisioning

#define DS18B20_INTERVAL_MS 30000   // sensor polling interval
// #define ACTUATOR_HEARTBEAT_S 300 // actuator heartbeat to InfluxDB (seconds)
```
