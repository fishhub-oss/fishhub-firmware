# fishhub-firmware

ESP32 firmware for the FishHub aquarium monitoring device. It reads sensors
(water temperature via DS18B20) and drives actuators (relay for lighting) on
a NodeMCU-32S board. Sensors are provisioned and managed dynamically over
MQTT — no reflashing needed to add or reconfigure peripherals. Readings are
published as SenML JSON to the FishHub MQTT broker.

## Hardware

| Component | Part | Connection |
|---|---|---|
| Board | NodeMCU-32S (ESP32) | — |
| Temperature sensor | DS18B20 | Data → GPIO 4; 4.7 kΩ pull-up between Data and 3.3 V |
| Light relay | Single-channel 5 V relay module | Signal → GPIO 16 |
| Reset button | Onboard BOOT button | GPIO 0 (active LOW, built-in pull-up) |

Pin assignments are in `include/pins.h`. Change `ONE_WIRE_PIN` or
`RELAY_LIGHT_PIN` there if you wire to different GPIOs.

## Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/) (CLI) **or** the PlatformIO VS Code extension
- USB cable for the NodeMCU-32S
- A FishHub backend running and reachable on your network

## Getting started

### 1. Build and flash

```bash
# Clone and enter the repo
git clone https://github.com/fishhub-oss/fishhub-firmware.git
cd fishhub-firmware

# Create config.h from the template and fill in your values (see below)
cp include/config.h.example include/config.h

# Build and flash
pio run --target upload
```

### 2. Provision the device

On first boot the device finds no credentials in NVS and starts a Wi-Fi AP
called **FishHub-Setup**.

1. Connect your phone or laptop to **FishHub-Setup** (no password).
2. Open `http://192.168.4.1` in a browser. The captive portal shows:
   - **Wi-Fi network** — dropdown from a background scan (manual entry fallback)
   - **Password** — Wi-Fi password (show/hide toggle)
   - **Provisioning code** — one-time code from the FishHub web app
3. Submit the form. The device:
   1. Connects to the specified Wi-Fi.
   2. POSTs the provisioning code to the FishHub server to activate the device.
   3. Polls the server every 2 s (up to 60 s) for MQTT credentials.
   4. Writes all credentials to NVS atomically and reboots into normal operation.

If activation fails (wrong code, Wi-Fi unreachable, server error, or poll
timeout) the portal shows an error. Retry without reflashing.

## Configuration reference

Copy `include/config.h.example` to `include/config.h` (gitignored) and fill in
your values. NVS values set during provisioning take precedence over these
defines at runtime; `SERVER_URL` is always read from this file at compile time.

| Define | Default | Description |
|---|---|---|
| `WIFI_SSID` | `"your-ssid"` | Wi-Fi network the ESP32 joins. NVS takes precedence on provisioned devices. |
| `WIFI_PASSWORD` | `"your-password"` | Wi-Fi password. NVS takes precedence. |
| `SERVER_URL` | _(required)_ | Base URL of the FishHub backend, e.g. `"http://192.168.1.10:8080"`. Compiled in at build time. Use the LAN IP — `localhost` won't resolve on the device. |
| `MQTT_HOST` | `"your-broker.hivemq.cloud"` | HiveMQ Cloud broker hostname. NVS takes precedence on provisioned devices. |
| `MQTT_PORT` | `8883` | HiveMQ Cloud broker port (TLS). |
| `DEVICE_ID` | `""` | Leave empty — populated automatically by provisioning. |
| `DS18B20_INTERVAL_MS` | `30000` | Milliseconds between temperature readings. |
| `ACTUATOR_HEARTBEAT_S` | `300` | Seconds between relay state heartbeats even when state is unchanged. |

## Operating modes

The firmware runs an explicit state machine. There is no deep sleep; the device
maintains a persistent MQTT connection throughout normal operation.

```
setup()
  └── boot: Serial init, NVS open, RESET_BUTTON_PIN configured
        └── isProvisioned()?
              ├── no  → PROVISIONING (captive portal — never returns, reboots on success)
              └── yes → CONNECT_WIFI

loop() — repeating state dispatch
  ├── checkButton()          — 3 s hold → reconfigure; 10 s hold → factory reset + reboot
  └── runState()
        CONNECT_WIFI   → connectWifi() success?  yes → NTP_SYNC      | no → ERROR_RETRY
        NTP_SYNC       → waitForNtp()  success?  yes → NORMAL_OPERATION | no → ERROR_RETRY
        ERROR_RETRY    → wait 10 s, then retry previous state
        NORMAL_OPERATION (after one-time init)
          ├── mqttClient.loop()           — keep MQTT alive, receive commands
          ├── manager.tickAll()           — tick each peripheral on its interval
          └── mqttClient.publishReading() — publish SenML payload if any data produced
```

### Normal operation detail

- **Peripherals are dynamic.** The server pushes `add`/`remove` commands over
  MQTT. Peripheral definitions (name, kind, pin) are persisted in NVS and
  restored on reboot — no reflashing required.
- **Sensors** (DS18B20) sample on a configurable interval and publish SenML
  records to the MQTT broker.
- **Actuators** (relay) accept schedule or override commands from the server and
  emit a heartbeat reading every `ACTUATOR_HEARTBEAT_S` seconds.

### Provisioning mode

Starts a Wi-Fi AP (`FishHub-Setup`, `192.168.4.1`) and serves the captive
portal. A background FreeRTOS task scans nearby networks (mutex-protected) to
populate the SSID dropdown. See [docs/configuration.md](docs/configuration.md)
for the full flow.

## Button actions

| Hold duration | Action |
|---|---|
| 3 seconds | Enter reconfiguration mode — starts the AP to update Wi-Fi credentials without re-provisioning. Existing MQTT credentials are preserved. |
| 10 seconds | Factory reset — clears all NVS keys (including `provisioned`) and reboots into fresh provisioning mode. |

## Build and test

```bash
# Compile for ESP32 (default environment)
pio run

# Compile and flash
pio run --target upload

# Open Serial monitor (115200 baud)
pio device monitor

# Run native unit tests (no device needed)
pio test -e native
```

See [docs/development.md](docs/development.md) for Serial output examples and
more detail on the build environments.
