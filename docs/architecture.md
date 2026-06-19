# Architecture

## Overview

The FishHub device is **online full-time**. After provisioning it holds a
persistent TLS MQTT connection to the broker, **publishes sensor readings** and
**trigger events** over MQTT, and **reacts to inbound MQTT messages** that
configure peripherals, push automation triggers, set device config, and send
one-shot/scheduled commands. There is no deep-sleep / wake-and-POST cycle — the
main loop runs continuously.

HTTP is used **only during provisioning/activation** (device activation and
MQTT-credential hand-off); it is not used on the steady-state data path.

Peripherals are **not hardcoded**. The server provisions them at runtime over
MQTT (`peripherals/<name>`, carrying a `kind` and a server-assigned `pin`); the
firmware reconstructs them through a serializer registry and persists them to
NVS so they survive reboots without waiting for the broker to redeliver.

## Source layout

```
fishhub-firmware/
├── platformio.ini              # build environments (nodemcu-32s, native, per-component test envs)
├── include/
│   ├── pins.h                  # the two on-board button pins (peripheral pins are server-assigned)
│   ├── nvs_store.h             # NVSStore class — persistent key-value storage
│   ├── provisioning.h          # startProvisioning(), ActivationError
│   ├── peripheral.h            # Peripheral interface
│   ├── peripheral_manager.h    # PeripheralManager (peripherals + triggers + event queue)
│   ├── mqtt_client.h           # FishHubMqttClient
│   ├── schedule.h              # Schedule — windowed value logic for actuators
│   ├── config.h                # credentials fallback for development (gitignored)
│   └── config.h.example        # template for config.h
├── src/
│   ├── main.cpp                # state machine + setup()/loop()
│   ├── nvs_store.cpp           # NVSStore implementation; global nvsStore instance
│   ├── provisioning.cpp        # captive portal AP, HTTP device activation, status polling
│   ├── wifi_ntp.h/.cpp         # Wi-Fi connection + NTP time sync
│   ├── mqtt_client.cpp         # FishHubMqttClient — TLS MQTT, publish + inbound dispatch
│   ├── button.h/.cpp           # reset button + display-mode button polling
│   ├── peripheral_manager.cpp  # PeripheralManager implementation (incl. trigger evaluation)
│   ├── peripheral_serializer_registry.h/.cpp  # kind -> (de)serializer registry
│   ├── schedule.cpp            # Schedule implementation
│   ├── trigger.h/.cpp          # Trigger — condition tree + actions
│   ├── trigger_event_queue.h/.cpp  # bounded drop-oldest queue of fired-trigger events
│   ├── cron_trigger.h/.cpp     # reusable 5-field cron entry used by servo peripherals
│   ├── display/
│   │   ├── oled_display.h       # OledDisplay class + global oledDisplay instance
│   │   └── oled_display.cpp     # SSD1306 rendering — MEASUREMENTS + DEBUG modes
│   └── peripherals/
│       ├── ds18b20_sensor.h/.cpp     # DS18B20Sensor (kind "ds18b20")
│       ├── relay_actuator.h/.cpp     # RelayActuator (kind "relay")
│       ├── servo_cr.h/.cpp           # ServoCR — continuous-rotation servo (kind "servo_cr")
│       └── servo_positional.h/.cpp   # ServoPositional — positional servo (kind "servo_positional")
├── lib/                        # (reserved for local libraries)
└── test/                       # native unit tests (SenML, trigger event queue, servos)
```

> Note: most headers now live under `include/` (not `src/`). Peripheral and
> trigger-subsystem headers live next to their `.cpp` under `src/`.

## State machine

`main.cpp` runs an explicit state machine rather than a linear boot sequence:

```
            ┌──────────────┐
            │ PROVISIONING │  startProvisioning() — never returns (reboots on success)
            └──────┬───────┘
   not provisioned │   (isProvisioned() == false at boot)
                   ▼
            ┌──────────────┐   fail   ┌─────────────┐
   boot ───▶│ CONNECT_WIFI │─────────▶│ ERROR_RETRY │
            └──────┬───────┘          └──────┬──────┘
                ok │                         │ after 10 s → CONNECT_WIFI
                   ▼                         │
            ┌──────────────┐   fail          │
            │   NTP_SYNC   │─────────────────┘
            └──────┬───────┘
                ok │
                   ▼
        ┌────────────────────┐
        │  NORMAL_OPERATION  │  init once, then sensorTick() every loop
        └────────────────────┘
```

- **PROVISIONING** — entered at boot when `nvsStore.isProvisioned()` is false.
  Runs the captive portal; never returns (reboots on success).
- **CONNECT_WIFI** — `connectWifi()`. On failure → `ERROR_RETRY` (re-enters
  `CONNECT_WIFI` after the backoff).
- **NTP_SYNC** — `waitForNtp()`. On failure → `ERROR_RETRY` (back to
  `CONNECT_WIFI`).
- **NORMAL_OPERATION** — on first entry runs `initNormalOperation()` once
  (registers serializers, restores peripherals + triggers from NVS, begins
  peripherals, wires the event queue, connects MQTT). Every loop after that runs
  `sensorTick()`.
- **ERROR_RETRY** — waits `ERROR_RETRY_DELAY_MS` (10 s) then transitions to the
  stored next state.

The reset button can re-enter provisioning (3 s hold) or clear NVS and reboot
(10 s hold) from any state, via `checkButton()`.

## Boot & loop flow

```
setup()
  ├── Serial.begin(115200)
  ├── nvsStore.begin()
  ├── pinMode(RESET_BUTTON_PIN, INPUT_PULLUP)
  ├── pinMode(DISPLAY_BUTTON_PIN, INPUT_PULLDOWN)
  ├── oledDisplay.begin()
  ├── log NVS key status
  └── state = isProvisioned() ? CONNECT_WIFI : PROVISIONING

loop()
  ├── checkButton()          — reset button: 3 s → provisioning; 10 s → clear NVS + reboot
  ├── checkDisplayButton()   — short press toggles OLED MEASUREMENTS ↔ DEBUG
  ├── runState()             — dispatch on the current state
  └── oledDisplay.tick(millis())

initNormalOperation()        (runs once on first NORMAL_OPERATION entry)
  ├── register serializers   — "relay", "ds18b20", "servo_cr", "servo_positional"
  ├── restorePeripherals()   — rebuild peripherals from NVS "peripherals" via the registry
  ├── restoreTriggers()      — rebuild triggers from NVS "trig_index" + "tr_<prefix>"
  ├── manager.beginAll()
  ├── manager.setEventQueue(eventQueue)
  └── mqttClient.begin(manager, eventQueue)

sensorTick()                 (every loop while in NORMAL_OPERATION)
  ├── mqttClient.loop()              — service MQTT, reconnect if needed, drain event queue
  ├── manager.tickAll(now, millis()) — evaluate triggers, then tick peripherals → SenML or ""
  ├── if payload: mqttClient.publishReading(payload) + refresh OLED MEASUREMENTS
  └── mqttClient.drainEventQueue()   — flush any queued trigger events
```

## MQTT topic topology

All topics are namespaced `fishhub/<device_id>/…`. The connection is TLS
(ISRG Root X1 CA, port 8883 on HiveMQ Cloud) when `MQTT_TLS` is defined;
plain TCP otherwise (e.g. a self-hosted broker behind a TCP proxy).

**Subscribes (inbound):**

| Topic | Purpose |
|---|---|
| `…/commands/<name>` | One-shot or immediate command to a peripheral. Requires an `id`; one-shot peripherals (`replayCommand() == false`) are de-duplicated via NVS `cmd_<name>`. |
| `…/peripherals/<name>` | Provision a peripheral (`op: "create"` with `kind` + `pin` + config, or `op: "delete"`). Persisted to NVS. |
| `…/triggers/<id>` | Automation rule upsert/delete (`op: "upsert"` / `"delete"`). Retained; persisted to NVS. |
| `…/config` | Device config — currently `timezone`; persisted to NVS and applied via `setenv("TZ", …)`. |

**Publishes (outbound):**

| Topic | Purpose |
|---|---|
| `…/readings` | SenML JSON batch produced by `PeripheralManager::tickAll()`. |
| `…/trigger_events` | One message per fired trigger (`trigger_event_id`, `trigger_id`, `device_id`, `fired_at`, `readings[]`). |

Reconnect uses a 5 s cooldown; the client buffer is 1024 bytes and keep-alive
is 30 s.

## Tick cycle: triggers then peripherals

`PeripheralManager::tickAll(now, nowMs)` does two things, in order:

1. **Trigger evaluation.** It builds a `values` map by calling
   `currentMeasurements()` on every peripheral, then evaluates each enabled
   `Trigger` against it. When a trigger fires, its actions are dispatched
   **immediately** via `dispatchCommand()` (so the command is applied before the
   peripheral tick loop drives the hardware), and a `TriggerEvent` is pushed onto
   the bounded event queue for later publish.
2. **Peripheral ticks.** Each peripheral whose `intervalMs()` has elapsed is
   `tick()`ed; those that report new data contribute SenML records via
   `appendSenML()`. If any produced data, a base record
   (`bn:"fishhub/device/"`, `bt:<now>`) is prepended and the batch is returned as
   a JSON string (empty string otherwise).

## Trigger engine

A `Trigger` is a server-pushed automation rule. It is independent of the
`Peripheral` interface — `PeripheralManager` owns a separate list of triggers and
evaluates them at the top of each `tickAll()`.

- **Condition** is a recursive JSON expression tree. Every node has an `op`:
  `value` (reads a named measurement), `literal`, `not`, `and`, `or`
  (short-circuiting), the comparisons `lt`/`lte`/`gt`/`gte`/`eq`, and the
  arithmetic ops `add`/`sub`/`mul`/`div`. `evaluate()` returns a float; any
  non-zero, non-NaN result is truthy. Unknown ops and division-by-zero return 0.
- **Actions** are an array of `{"type":"peripheral_action","config":{…}}` where
  `config.peripheral` names the target and the rest of `config` is the command
  payload dispatched to that peripheral.
- **Cooldown** (`cooldown_s`, default 60) suppresses re-firing within the window.
- **Firing** returns a `TriggerFired` struct to `tickAll()`, which performs the
  dispatch and enqueues the event. The trigger does not dispatch or publish
  directly.

`TriggerEventQueue` is a fixed-capacity ring buffer (`TRIGGER_EVENT_QUEUE_CAPACITY`,
drop-oldest on overflow). `trigger_event_id` is a deterministic base64 of
`<trigger_id>:<fired_at>`; the server deduplicates on `(trigger_id, fired_at)`,
so redelivery after a reconnect is safe.

See [peripherals.md](peripherals.md) for the exact trigger MQTT payload shapes
and the condition-tree reference.

## Peripheral model

Peripherals implement the `Peripheral` interface (`include/peripheral.h`) and are
driven uniformly by `PeripheralManager`. Each peripheral exposes:

- `kind()` — firmware driver class (`"relay"`, `"ds18b20"`, `"servo_cr"`,
  `"servo_positional"`); used to pick a serializer.
- `purpose()` — optional application label (e.g. a feeder/light/heater meaning);
  no firmware logic inspects it.
- `name()` — unique id, used as the MQTT topic segment and SenML field prefix.
- `currentMeasurements(out)` — contributes values to the snapshot used for
  trigger evaluation.
- `replayCommand()` — `true` for idempotent peripherals (schedule-driven), so
  retained commands re-apply on reboot; `false` for one-shot peripherals, which
  are de-duplicated by command `id` in NVS.

Each `kind` registers a `PeripheralSerializer` in
`peripheralSerializerRegistry`, which knows how to serialize a live peripheral
to JSON and reconstruct one from JSON. This is what makes runtime provisioning
and NVS restore work without hardcoding peripherals in `main.cpp`.

`PeripheralManager::add(peripheral, pin)` takes ownership of the pointer
(`remove()` deletes it). The manager also owns triggers (`addTrigger`,
`removeTrigger`, `findTrigger`, `forEachTrigger`) and the injected
`TriggerEventQueue` (`setEventQueue`).

## NVS keys

| Key | Content |
|---|---|
| `wifi_ssid`, `wifi_pass` | Wi-Fi credentials |
| `device_id`, `device_jwt` | Device identity / activation token |
| `mqtt_username`, `mqtt_password`, `mqtt_host` | Broker credentials (from activation) |
| `provisioned` | `"1"` only when all required keys are present (atomic provisioning flag) |
| `peripherals` | JSON array of provisioned peripherals (`name`, `kind`, `pin`, + kind-specific fields) |
| `trig_index` | JSON array of 10-char trigger-id prefixes (enumeration index) |
| `tr_<prefix>` | Full trigger JSON in upsert shape (key ≤ 15-char `Preferences` limit) |
| `cmd_<name>` | Last processed command id for one-shot peripherals (dedup) |
| `timezone` | IANA TZ string from the `config` topic |

Servo peripherals additionally persist their last-fired cron state so scheduled
actuations are idempotent across reboots.

## Provisioning (HTTP)

The only HTTP on the device. `startProvisioning()` runs a Wi-Fi AP
(`"FishHub-Setup"`, `192.168.4.1`) serving a captive-portal form; a background
FreeRTOS task scans nearby SSIDs.

- **Fresh provisioning** (`device_jwt` absent): saves Wi-Fi creds, `POST`s the
  activation code to `…/devices/activate`, polls `GET …/devices/{id}/status`
  until `ready`, writes all NVS keys atomically (`provisioned = "1"` last), and
  reboots.
- **Reconfiguration** (`device_jwt` present): writes new Wi-Fi creds to temporary
  NVS keys, tries to connect, and either promotes them and reboots or rolls back
  and shows an error — existing MQTT credentials are never touched.

## OLED display

`OledDisplay` drives a 128×64 SSD1306 over I2C and is **not** a `Peripheral`. It
degrades to a no-op if no panel is detected. `DISPLAY_BUTTON_PIN` (GPIO 19,
active-HIGH, `INPUT_PULLDOWN`) short-press toggles modes:

- **MEASUREMENTS** — cycles through current peripheral readings.
- **DEBUG** — shows the current state-machine state plus a scrolling ring buffer
  of recent MQTT events (inbound commands, peripheral/trigger upserts & deletes,
  outbound readings, reconnects).

## PlatformIO environments

| Environment | Target | Purpose |
|---|---|---|
| `nodemcu-32s` | ESP32 hardware | Default build + flash |
| `native` | Host machine | SenML unit tests (`pio test -e native`) |
| `test_trigger_event_queue` | Host machine | Trigger event queue tests |
| `test_servo_cr` | Host machine | Continuous-rotation servo tests |
| `test_servo_positional` | Host machine | Positional servo tests |

The host environments use `build_src_filter = -<*>` and stubs under `test/stubs`
so they compile without the Arduino/ESP32 SDK.

## Libraries

OneWire, DallasTemperature, ArduinoJson v7, PubSubClient (MQTT), Adafruit
SSD1306 + Adafruit GFX (OLED), ESP32Servo. See `platformio.ini` for pinned
versions.
