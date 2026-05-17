#include "servo_actuator.h"
#ifdef ARDUINO
#include <Arduino.h>
#include "nvs_store.h"
#endif
#include <ArduinoJson.h>

ServoActuator::ServoActuator(const char* name, uint8_t pin,
                             int sensePin, const char* purpose)
  : _name(name), _purpose(purpose), _pin(pin), _sensePin(sensePin) {}

void ServoActuator::begin() {
#ifdef ARDUINO
  if (_sensePin >= 0)
    pinMode(_sensePin, INPUT_PULLUP);
#endif
}

bool ServoActuator::tick(time_t now) {
#ifdef ARDUINO
  if (_sensePin >= 0) {
    _senseValue   = !digitalRead(_sensePin);
    _sensePending = true;
  }
#endif

  for (auto& t : _triggers) {
    if (t.isDue(now)) {
      _actuate(t.value);
      t.markFired(now);
      _persistLastFired();
    }
  }

  return _pendingRotation || _sensePending;
}

void ServoActuator::appendSenML(JsonArray& records, time_t /*now*/) {
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

void ServoActuator::currentMeasurements(std::map<std::string, float>& out) const {
  if (_sensePin >= 0)
    out[_name + "/sense"] = _senseValue ? 1.0f : 0.0f;
}

void ServoActuator::applyCommand(JsonObjectConst cmd) {
  const char* op = cmd["op"];
  if (!op) return;

  if (strcmp(op, "actuate") == 0) {
    int rotationMs = cmd["rotation_ms"] | 500;
    Serial.printf("ServoActuator '%s': actuate %d ms\n", _name.c_str(), rotationMs);
    _actuate(rotationMs);
  } else if (strcmp(op, "set_triggers") == 0) {
    _loadTriggers(cmd["triggers"].as<JsonArrayConst>());
  }
}

void ServoActuator::_actuate(int rotationMs) {
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

void ServoActuator::_loadTriggers(JsonArrayConst arr) {
  _triggers.clear();
  for (JsonObjectConst obj : arr) {
    CronTrigger t = {};
    const char* id   = obj["id"]   | "";
    const char* cron = obj["cron"] | "";
    int value        = obj["value"] | 500;
    if (strlen(id) == 0 || !t.parseCron(cron)) {
      Serial.printf("ServoActuator '%s': skipping invalid trigger\n", _name.c_str());
      continue;
    }
    strncpy(t.id, id, sizeof(t.id) - 1);
    t.value     = value;
    t.lastFired = 0;
    _triggers.push_back(t);
  }
  _restoreLastFired();
}

void ServoActuator::_persistLastFired() {
#ifdef ARDUINO
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  for (auto& t : _triggers)
    obj[t.id] = (long)t.lastFired;
  String json;
  serializeJson(doc, json);
  String key = String("lf_") + _name.c_str();
  nvsStore.set(key.c_str(), json);
#endif
}

void ServoActuator::_restoreLastFired() {
#ifdef ARDUINO
  String key  = String("lf_") + _name.c_str();
  String json = nvsStore.get(key.c_str());
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  JsonObject obj = doc.as<JsonObject>();
  for (auto& t : _triggers) {
    JsonVariant v = obj[t.id];
    if (!v.isNull())
      t.lastFired = (time_t)v.as<long>();
  }
#endif
}
