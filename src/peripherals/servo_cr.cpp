#include "servo_cr.h"
#ifdef ARDUINO
#include <Arduino.h>
#include "nvs_store.h"
#endif
#include <ArduinoJson.h>

ServoCR::ServoCR(const char* name, uint8_t pin, int sensePin, const char* purpose)
  : _name(name), _purpose(purpose), _pin(pin), _sensePin(sensePin) {}

void ServoCR::begin() {
#ifdef ARDUINO
  if (_sensePin >= 0)
    pinMode(_sensePin, INPUT_PULLUP);
#endif
}

bool ServoCR::tick(time_t now) {
#ifdef ARDUINO
  if (_sensePin >= 0) {
    _senseValue   = !digitalRead(_sensePin);
    _sensePending = true;
  }
#endif

  for (auto& t : _entries) {
    if (t.isDue(now)) {
      _actuate(t.value);
      t.markFired(now);
      _persistLastFired();
    }
  }

  return _pendingRotation || _sensePending;
}

void ServoCR::appendSenML(JsonArray& records, time_t /*now*/) {
  if (_pendingRotation) {
    JsonObject r = records.add<JsonObject>();
    r["n"] = String(_name.c_str()) + "/rotation";
    r["u"] = "ms";
    r["v"] = _lastRotationMs;
    _pendingRotation = false;
  }
  if (_sensePending) {
    JsonObject r = records.add<JsonObject>();
    r["n"] = String(_name.c_str()) + "/sense";
    r["u"] = "/";
    r["v"] = _senseValue ? 1 : 0;
    _sensePending = false;
  }
}

void ServoCR::currentMeasurements(std::map<std::string, float>& out) const {
  if (_sensePin >= 0)
    out[_name + "/sense"] = _senseValue ? 1.0f : 0.0f;
}

void ServoCR::applyCommand(JsonObjectConst cmd) {
  const char* op = cmd["op"];
  if (!op) return;

  if (strcmp(op, "actuate") == 0) {
    int rotationMs = cmd["rotation_ms"] | 500;
#ifdef ARDUINO
    Serial.printf("ServoCR '%s': actuate %d ms\n", _name.c_str(), rotationMs);
#endif
    _actuate(rotationMs);
  } else if (strcmp(op, "schedule") == 0) {
    const char* type = cmd["type"] | "windows";
    if (strcmp(type, "cron") == 0)
      _loadEntries(cmd["entries"].as<JsonArrayConst>());
  }
}

void ServoCR::_actuate(int rotationMs) {
#ifdef ARDUINO
  _servo.attach(_pin);
  _servo.writeMicroseconds(2000);
  delay(rotationMs);
  _servo.writeMicroseconds(1500);
  _servo.detach();
#endif
  _lastRotationMs  = rotationMs;
  _pendingRotation = true;
}

void ServoCR::_loadEntries(JsonArrayConst arr) {
  _entries.clear();
  for (JsonObjectConst obj : arr) {
    CronTrigger t = {};
    const char* id   = obj["id"]   | "";
    const char* cron = obj["cron"] | "";
    int value        = obj["value"] | 500;
    if (strlen(id) == 0 || !t.parseCron(cron)) {
#ifdef ARDUINO
      Serial.printf("ServoCR '%s': skipping invalid schedule entry\n", _name.c_str());
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

void ServoCR::_persistLastFired() {
#ifdef ARDUINO
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  for (auto& t : _entries)
    obj[t.id] = (long)t.lastFired;
  String json;
  serializeJson(doc, json);
  String key = String("lf_") + _name.c_str();
  nvsStore.set(key.c_str(), json);
#endif
}

void ServoCR::_restoreLastFired() {
#ifdef ARDUINO
  String key  = String("lf_") + _name.c_str();
  String json = nvsStore.get(key.c_str());
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  JsonObject obj = doc.as<JsonObject>();
  for (auto& t : _entries) {
    JsonVariant v = obj[t.id];
    if (!v.isNull())
      t.lastFired = (time_t)v.as<long>();
  }
#endif
}
