# fishhub-firmware — Claude Code Instructions

## What this repo is

ESP32 Arduino firmware for the FishHub aquarium monitoring device. On each wake cycle it reads water temperature from a DS18B20 probe, formats it as a SenML JSON payload, POSTs it to the FishHub backend, then goes into deep sleep for 5 minutes.

## Read the docs first

**Before making any changes, read the relevant docs in `docs/` if they exist.** The docs folder is the authoritative source of context for this repo.

## Workflow

1. Before starting any issue, create a plan file in `../planning/` (e.g. `../planning/firmware-05-http-post.md`).
2. Discuss the plan with the user before executing.
3. Implement only after the user approves.
4. Never commit directly to `main`. Always create a feature branch, commit there, and open a PR.
5. After completing an issue, move the corresponding GitHub issue to the Done column on the FishHub PoC project (`org: fishhub-oss`, project ID 1).

## Git conventions

- **Branch naming:** `feat/<slug>`, `fix/<slug>`, `chore/<slug>`
- **Commit style:** [Conventional Commits](https://www.conventionalcommits.org/)
  - `feat:` new feature
  - `fix:` bug fix
  - `chore:` tooling, config, deps
  - `refactor:` code change with no behavior change
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
- **Libraries:** OneWire, DallasTemperature, ArduinoJson, HTTPClient (built-in)
- **Wire format:** SenML JSON (RFC 8428)
- **Auth:** Bearer token hardcoded in `include/config.h` (gitignored — use `config.h.example` as template)
- **Pin definitions:** `include/pins.h`
- **Wi-Fi / NTP:** `src/wifi_ntp.h` / `src/wifi_ntp.cpp`
- **SenML serialization:** `src/senml.h` / `src/senml.cpp`
- **HTTP POST:** `src/http_client.h` / `src/http_client.cpp`

## Testing

- Native (host-side) unit tests for pure functions: `pio test -e native`
- Hardware-coupled code is verified via Serial output after flashing
- `pio run` defaults to the ESP32 target (`default_envs = nodemcu-32s` in `platformio.ini`)
- Always run `pio run` to verify compilation before opening a PR

## config.h

`include/config.h` is gitignored and must be created locally from `include/config.h.example`. It contains:

```cpp
#define WIFI_SSID     "..."
#define WIFI_PASSWORD "..."
#define SERVER_URL    "http://<server-ip>:8080/readings"
#define DEVICE_TOKEN  "<64-char-hex-token>"
```
