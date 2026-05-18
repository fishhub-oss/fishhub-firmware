#include "servo_positional.h"
#ifdef ARDUINO
#include <Arduino.h>
#include "nvs_store.h"
#endif
#include <ArduinoJson.h>

ServoPositional::ServoPositional(const char* name, uint8_t pin,
                                 int openAngle, int restAngle, int holdMs,
                                 int sensePin, const char* purpose)
  : _name(name), _purpose(purpose), _pin(pin),
    _openAngle(openAngle), _restAngle(restAngle), _holdMs(holdMs),
    _sensePin(sensePin) {}

void ServoPositional::begin() {
#ifdef ARDUINO
  if (_sensePin >= 0)
    pinMode(_sensePin, INPUT_PULLUP);
#endif
}

bool ServoPositional::tick(time_t now) {
#ifdef ARDUINO
  if (_sensePin >= 0) {
    _senseValue   = !digitalRead(_sensePin);
    _sensePending = true;
  }
#endif

  for (auto& e : _entries) {
    if (e.isDue(now)) {
      _actuate(e.value, _holdMs);
      e.markFired(now);
      _persistLastFired();
    }
  }

  return _pendingAngle || _sensePending;
}

void ServoPositional::appendSenML(JsonArray& records, time_t /*now*/) {
  if (_pendingAngle) {
    JsonObject r = records.add<JsonObject>();
    r["n"] = String(_name.c_str()) + "/angle";
    r["u"] = "deg";
    r["v"] = _lastAngle;
    _pendingAngle = false;
  }
  if (_sensePending) {
    JsonObject r = records.add<JsonObject>();
    r["n"] = String(_name.c_str()) + "/sense";
    r["u"] = "/";
    r["v"] = _senseValue ? 1 : 0;
    _sensePending = false;
  }
}

void ServoPositional::currentMeasurements(std::map<std::string, float>& out) const {
  if (_sensePin >= 0)
    out[_name + "/sense"] = _senseValue ? 1.0f : 0.0f;
}

void ServoPositional::applyCommand(JsonObjectConst cmd) {
  const char* op = cmd["op"];
  if (!op) return;

  if (strcmp(op, "actuate") == 0) {
    int openAngle = cmd["open_angle"] | _openAngle;
    int holdMs    = cmd["hold_ms"]    | _holdMs;
#ifdef ARDUINO
    Serial.printf("ServoPositional '%s': actuate angle=%d hold=%d ms\n",
                  _name.c_str(), openAngle, holdMs);
#endif
    _actuate(openAngle, holdMs);
  } else if (strcmp(op, "schedule") == 0) {
    const char* type = cmd["type"] | "windows";
    if (strcmp(type, "cron") == 0)
      _loadEntries(cmd["entries"].as<JsonArrayConst>());
  }
}

void ServoPositional::_actuate(int openAngle, int holdMs) {
#ifdef ARDUINO
  _servo.attach(_pin);
  _servo.write(openAngle);
  delay(holdMs);
  _servo.write(_restAngle);
  delay(500);
  _servo.detach();
#endif
  _lastAngle    = openAngle;
  _pendingAngle = true;
}

void ServoPositional::_loadEntries(JsonArrayConst arr) {
  _entries.clear();
  for (JsonObjectConst obj : arr) {
    CronTrigger t = {};
    const char* id   = obj["id"]   | "";
    const char* cron = obj["cron"] | "";
    int value        = obj["value"] | _openAngle;
    if (strlen(id) == 0 || !t.parseCron(cron)) {
#ifdef ARDUINO
      Serial.printf("ServoPositional '%s': skipping invalid schedule entry\n", _name.c_str());
#endif
      continue;
    }
    strncpy(t.id, id, sizeof(t.id) - 1);
    t.value     = value;
    t.lastFired = 0;
    _entries.push_back(t);
  }
  _restoreLastFired();
}

void ServoPositional::_persistLastFired() {
#ifdef ARDUINO
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  for (auto& e : _entries)
    obj[e.id] = (long)e.lastFired;
  String json;
  serializeJson(doc, json);
  String key = String("lf_") + _name.c_str();
  nvsStore.set(key.c_str(), json);
#endif
}

void ServoPositional::_restoreLastFired() {
#ifdef ARDUINO
  String key  = String("lf_") + _name.c_str();
  String json = nvsStore.get(key.c_str());
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  JsonObject obj = doc.as<JsonObject>();
  for (auto& e : _entries) {
    JsonVariant v = obj[e.id];
    if (!v.isNull())
      e.lastFired = (time_t)v.as<long>();
  }
#endif
}
