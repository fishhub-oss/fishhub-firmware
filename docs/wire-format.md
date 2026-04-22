# Wire Format

Sensor readings are sent as **SenML JSON** (RFC 8428).

## Payload structure

```json
[{
  "bn": "fishhub/device/",
  "bt": 1713000000,
  "e": [{"n": "temperature", "u": "Cel", "v": 23.4}]
}]
```

The payload is a JSON array containing a single SenML record (object).

| Field | Type | Description |
|---|---|---|
| `bn` | string | Base name — device namespace, always `"fishhub/device/"` |
| `bt` | int64 | Base time — Unix UTC timestamp of the reading (seconds since epoch) |
| `e` | array | Array of SenML entries (measurements) |
| `e[0].n` | string | Entry name — always `"temperature"` |
| `e[0].u` | string | Unit — always `"Cel"` (Celsius, per RFC 8428 unit registry) |
| `e[0].v` | float | Temperature value in Celsius |

## How it's built

`buildSenMLPayload(float tempCelsius, time_t timestamp)` in `src/senml.cpp` uses ArduinoJson to construct the payload:

```cpp
JsonDocument doc;
JsonObject record = doc.add<JsonObject>();
record["bn"] = "fishhub/device/";
record["bt"] = (long)timestamp;
JsonObject entry = record["e"].add<JsonObject>();
entry["n"] = "temperature";
entry["u"] = "Cel";
entry["v"] = tempCelsius;
```

`timestamp` comes from `time(nullptr)` — the current Unix time as synced via NTP on boot.

## Server expectations

The server's `POST /readings` endpoint parses this format. It requires:
- `bt` to be present and non-zero
- At least one entry in `e` with `n == "temperature"`

A `400` response means the payload was malformed. A `401` means the Bearer token in `Authorization` is missing or invalid.
