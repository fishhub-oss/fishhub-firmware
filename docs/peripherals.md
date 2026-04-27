# Peripherals

## The `Peripheral` interface

All sensors and actuators implement the `Peripheral` abstract class (`include/peripheral.h`):

```cpp
class Peripheral {
public:
  virtual void        begin() = 0;
  virtual uint32_t    intervalMs() const = 0;
  virtual bool        tick(time_t now) = 0;
  virtual void        appendSenML(JsonArray& records, time_t now) = 0;
  virtual void        applyCommand(JsonObjectConst cmd) {}
  virtual bool        replayCommand() const { return true; }
  virtual const char* name() const = 0;
};
```

| Method | When called | Purpose |
|---|---|---|
| `begin()` | Once at startup via `PeripheralManager::beginAll()` | Hardware initialisation (pin modes, sensor library init) |
| `intervalMs()` | By `PeripheralManager::tickAll()` | How often `tick()` should be called, in milliseconds |
| `tick(now)` | When `intervalMs()` has elapsed | Read sensor / evaluate actuator state. Return `true` if there is new output to report |
| `appendSenML(records, now)` | Only when `tick()` returned `true` | Append one or more SenML sibling records to the shared JSON array |
| `applyCommand(cmd)` | On inbound MQTT command for this peripheral | Apply the command — update a schedule, toggle a relay, etc. Sensors can leave this as the default no-op |
| `replayCommand()` | Before calling `applyCommand()` | Return `true` if retained/re-delivered MQTT commands should always be re-applied (idempotent actuators). Return `false` for one-shot operations — the MQTT client will deduplicate by persisting the last processed command ID in NVS |
| `name()` | By `PeripheralManager` and `FishHubMqttClient` | Unique string identifier — used as the MQTT topic segment and SenML field name prefix |

---

## Concrete implementations

### `DS18B20Sensor` (`src/peripherals/ds18b20_sensor.h/.cpp`)

Reads water temperature from a DS18B20 probe over the OneWire bus.

- `name()` → `"temperature"`
- `intervalMs()` → configured via `DS18B20_INTERVAL_MS` (default `30000` ms; overridable in `platformio.ini`)
- `tick()` → requests a conversion and reads Celsius; returns `true` when a valid reading is available
- `appendSenML()` → appends `{"n":"temperature","u":"Cel","v":<reading>,"t":<unix-ts>}`
- `applyCommand()` → no-op (sensor only)
- `replayCommand()` → `true` (default)

### `RelayActuator` (`src/peripherals/relay_actuator.h/.cpp`)

Controls a relay (e.g. aquarium light) via a GPIO pin. Supports time-window schedules and manual overrides.

- `name()` → set at construction time (e.g. `"light"`)
- `intervalMs()` → `1000` ms (evaluates state every second)
- `tick(now)` → evaluates `Schedule::isActive(now)` and applies the result to the relay pin. Returns `true` when the state changes **or** when the heartbeat interval (`ACTUATOR_HEARTBEAT_S`, default 300 s) has elapsed, ensuring the server always gets periodic state updates
- `appendSenML()` → appends `{"n":"<name>","vb":<true|false>,"t":<unix-ts>}`
- `applyCommand(cmd)` → accepts two command shapes:
  - `{"action":"schedule","windows":[["HH:MM","HH:MM"],...]}` — loads a new schedule
  - `{"action":"set","state":<true|false>}` — sets an immediate override
- `replayCommand()` → `true` (schedules and overrides are idempotent)

#### Command flow

```
MQTT broker
  └── FishHubMqttClient::onMessage()
        └── PeripheralManager::dispatchCommand("light", cmd)
              └── RelayActuator::applyCommand(cmd)
                    ├── schedule  → Schedule::loadWindows()
                    └── set       → Schedule::setOverride()

loop()
  └── sensorTick()
        └── PeripheralManager::tickAll()
              └── RelayActuator::tick(now)
                    └── Schedule::isActive(now) → digitalWrite(pin, state)
```

---

## Adding a new peripheral

1. Create `src/peripherals/<name>.h` and `src/peripherals/<name>.cpp`.
2. Subclass `Peripheral` and implement all pure virtual methods.
3. Decide on `replayCommand()`: return `true` for idempotent actuators (schedules, state), `false` for one-shot operations (feeders, dosers).
4. Register it in `main.cpp` inside `normalOperation()`:
   ```cpp
   manager.add(new MyPeripheral("my-name", MY_PIN));
   ```
5. The MQTT client will automatically subscribe to `fishhub/<device_id>/peripherals/my-name/commands` and route inbound commands to `applyCommand()`.
