# Peripherals

## `Peripheral` interface (`include/peripheral.h`)

Every sensor and actuator in FishHub implements the `Peripheral` interface. `PeripheralManager` drives all registered peripherals through this contract uniformly.

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

| Method | Called by | Contract |
|---|---|---|
| `begin()` | `PeripheralManager::beginAll()` once at startup | Initialise hardware (GPIO, bus, etc.) |
| `intervalMs()` | `PeripheralManager::tickAll()` | Minimum milliseconds between `tick()` calls |
| `tick(now)` | `PeripheralManager::tickAll()` | Sample or evaluate state; return `true` if there is new data to publish |
| `appendSenML(records, now)` | `PeripheralManager::tickAll()` | Append one or more SenML sibling records to the top-level array. Only called when `tick()` returned `true`. |
| `applyCommand(cmd)` | `PeripheralManager::dispatchCommand()` | Handle an inbound MQTT command (parsed JSON object). Default no-op — sensors can leave this unimplemented. |
| `replayCommand()` | `FishHubMqttClient::onMessage()` | Return `true` if retained/re-delivered MQTT commands should always be re-applied (idempotent peripherals, e.g. schedule-driven actuators). Return `false` for one-shot operations (e.g. a feeder) — the MQTT client will deduplicate by persisting the last processed command ID in NVS. |
| `name()` | MQTT routing, SenML field prefix | Unique identifier for this peripheral. Used as the last segment of the MQTT command topic (`fishhub/<device_id>/commands/<name>`) and as the SenML field name prefix. |

---

## `PeripheralManager` (`include/peripheral_manager.h`)

Owns a list of `Peripheral*` entries and drives them uniformly.

```cpp
class PeripheralManager {
public:
  void      add(Peripheral* p);
  void      beginAll();
  String    tickAll(time_t now, uint32_t nowMs);
  void      dispatchCommand(const String& name, JsonObjectConst cmd);
  Peripheral* find(const String& name);
};
```

- `add(p)` — registers a peripheral. Ownership remains with the caller.
- `beginAll()` — calls `begin()` on every registered peripheral.
- `tickAll(now, nowMs)` — for each peripheral, calls `tick()` if `nowMs - lastTickedAt >= intervalMs()`. If any `tick()` returns `true`, calls `appendSenML()` and collects the records into a complete SenML JSON string (with base record `bn` and `bt`). Returns `""` if nothing produced output.
- `dispatchCommand(name, cmd)` — finds the peripheral with `name()` matching `name` and calls `applyCommand(cmd)`.
- `find(name)` — returns the matching peripheral or `nullptr`.

---

## Concrete implementations

### `DS18B20Sensor` (`src/peripherals/ds18b20_sensor.h/.cpp`)

Reads water temperature from a DS18B20 probe via OneWire/DallasTemperature.

| Property | Value |
|---|---|
| `name()` | `"temperature"` |
| `intervalMs()` | `DS18B20_INTERVAL_MS` build flag (default: `30000` ms) |
| `replayCommand()` | `true` (default — sensors ignore commands) |

`tick()` triggers a synchronous temperature conversion. `appendSenML()` emits:

```json
{"n":"temperature","v":24.5,"u":"Cel"}
```

Override the interval by setting `DS18B20_INTERVAL_MS` in `platformio.ini`:

```ini
[env:nodemcu-32s]
build_flags = -DDS18B20_INTERVAL_MS=60000
```

---

### `RelayActuator` (`src/peripherals/relay_actuator.h/.cpp`)

Drives a GPIO relay using a `Schedule`. Supports both time-window scheduling and immediate state overrides via MQTT commands.

| Property | Value |
|---|---|
| `name()` | Set at construction (e.g. `"light"`) |
| `intervalMs()` | `1000` ms — evaluates the schedule every second |
| `replayCommand()` | `true` — retained MQTT messages re-apply the schedule on reboot |

**`applyCommand(cmd)` accepts two command shapes:**

Schedule (time windows):
```json
{
  "id": "cmd-uuid",
  "windows": [["08:00","22:00"]]
}
```

Immediate override:
```json
{
  "id": "cmd-uuid",
  "state": true
}
```

`tick()` evaluates the schedule and sets the GPIO pin. It emits a heartbeat SenML record every `ACTUATOR_HEARTBEAT_S` seconds (default 300 s), even when the state hasn't changed, so the server always has a recent reading:

```json
{"n":"light","vb":true}
```

Override the heartbeat interval via build flag:
```ini
build_flags = -DACTUATOR_HEARTBEAT_S=60
```

---

## `Schedule` (`src/schedule.h/.cpp`)

Time-window–based on/off logic used by `RelayActuator`.

- `loadWindows(windows)` — parses a JSON array of `["HH:MM","HH:MM"]` pairs. Clears any active override. Handles overnight windows (e.g. `["22:00","06:00"]`).
- `isActive(now)` — returns `true` if `now` falls within any loaded window, or if an override is set.
- `setOverride(state)` — forces a fixed `state` regardless of windows until the next `loadWindows()` call.

---

## `FishHubMqttClient` (`src/mqtt_client.h/.cpp`)

Manages the TLS MQTT connection and routes inbound commands to `PeripheralManager`.

- Connects to HiveMQ Cloud on port 8883 using the ISRG Root X1 CA.
- Subscribes to `fishhub/<device_id>/commands/#`.
- On each message, extracts the peripheral name from the last topic segment and calls `PeripheralManager::dispatchCommand()`.
- For peripherals with `replayCommand() == false`, persists the last processed command ID in NVS under `cmd_<name>` and skips duplicates.
- Reconnects automatically with a 5 s cooldown when the connection drops.

---

## Triggers (`src/trigger.h/.cpp`)

A trigger is a server-pushed automation rule that fires when a condition evaluated against the latest sensor readings becomes true. It has no relationship to the `Peripheral` interface — it sits alongside peripherals and is driven by `PeripheralManager::evaluateTriggers()` after each sensor tick.

### `Trigger` class API

```cpp
bool load(JsonObjectConst json);
bool evaluate(const std::map<std::string, float>& values, time_t now, PeripheralManager& mgr);
void serializeTo(JsonObject out) const;
const std::string& id() const;
bool enabled() const;
```

- `load(json)` — populates all fields from the MQTT payload; returns `false` if `id` is missing.
- `evaluate(values, now, mgr)` — evaluates the condition tree against the current sensor readings. If the result is truthy and the cooldown has elapsed, calls `mgr.dispatchCommand()` on the target peripheral and updates `_lastFiredAt`. Returns `true` when the action was fired.
- `serializeTo(out)` — writes the trigger back to a JSON object in the same shape as the MQTT upsert payload; used by NVS persistence.

### Condition expression tree

The `condition` field is a recursive JSON expression tree. Every node has an `"op"` field:

| `op` | Children | Meaning |
|---|---|---|
| `"value"` | `"measurement"` (string) | Reads the named sensor value from the current readings map |
| `"literal"` | `"value"` (number) | Constant number |
| `"and"` | `"left"`, `"right"` | Logical AND (short-circuits) |
| `"or"` | `"left"`, `"right"` | Logical OR (short-circuits) |
| `"not"` | `"left"` | Logical NOT |
| `"lt"`, `"lte"`, `"gt"`, `"gte"`, `"eq"` | `"left"`, `"right"` | Numeric comparison — returns 1.0 or 0.0 |
| `"add"`, `"sub"`, `"mul"`, `"div"` | `"left"`, `"right"` | Arithmetic |

The evaluator returns a `float`; any non-zero, non-NaN result is truthy. An unknown `op` returns `0.0`. Division by zero logs a warning and returns `0.0`.

**Full example** — fire when temperature is below 19 °C and the heater relay is off:

```json
{
  "op": "and",
  "left": {
    "op": "lt",
    "left":  { "op": "value",   "measurement": "ds18b20-4/temperature" },
    "right": { "op": "literal", "value": 19 }
  },
  "right": {
    "op": "eq",
    "left":  { "op": "value",   "measurement": "relay-5/state" },
    "right": { "op": "literal", "value": 0 }
  }
}
```

### MQTT protocol

**Topic:** `fishhub/<device_id>/triggers/<trigger_id>` (retained)

The firmware subscribes to `fishhub/<device_id>/triggers/#`. The last topic segment is the trigger ID.

**Empty payloads are silently ignored** — the broker delivers an empty retained message when a retained topic is cleared; treating it as a delete would cause spurious removals on reconnect.

**Upsert** (create or replace):
```json
{
  "op": "upsert",
  "id": "a1b2c3d4-...",
  "enabled": true,
  "target": "relay-5",
  "cooldown_s": 60,
  "condition": { ... },
  "action": { "action": "set", "value": 1 }
}
```

**Delete:**
```json
{ "op": "delete" }
```

Any other `op` value is logged and ignored.

### NVS persistence

Triggers are persisted to NVS so they survive power cycles without waiting for the broker to re-deliver retained messages.

| NVS key | Content |
|---|---|
| `trig_index` | JSON array of 10-char ID prefixes, e.g. `["a1b2c3d4e5","f6g7h8i9j0"]` |
| `tr_<prefix>` | Full trigger JSON in upsert payload shape |

The key format `tr_<first-10-chars-of-id>` is 13 characters and fits within the 15-character `Preferences` key limit. Because `Preferences` has no key-listing API, `trig_index` acts as an enumeration index.

**On upsert:** the trigger is serialised and written under its NVS key; its prefix is appended to `trig_index` if not already present.

**On delete:** the NVS key is removed and the prefix is removed from `trig_index`.

**On boot** (`restoreTriggers()` in `src/main.cpp`): `trig_index` is read, each prefix is used to load the corresponding key, and each successfully parsed trigger is added to `PeripheralManager` before `FishHubMqttClient::begin()` is called. The theoretical limit is roughly 50 triggers before NVS space becomes a concern.

---

## Adding a new peripheral

1. Create `src/peripherals/my_peripheral.h` and `my_peripheral.cpp` implementing `Peripheral`.
2. Set `name()` to a unique lowercase string — this becomes the MQTT topic segment and the SenML field prefix.
3. Implement `begin()`, `intervalMs()`, `tick()`, and `appendSenML()`.
4. If the peripheral accepts commands, implement `applyCommand()`. If commands are idempotent (schedule-driven), leave `replayCommand()` returning `true`. If they are one-shot, override it to return `false`.
5. Register it in `normalOperation()` in `src/main.cpp`:
   ```cpp
   manager.add(new MyPeripheral(PIN, INTERVAL_MS));
   ```

No changes to `PeripheralManager` or `FishHubMqttClient` are needed — they discover peripherals at runtime through the `Peripheral` interface.
