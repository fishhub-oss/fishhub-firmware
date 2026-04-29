#include "relay_actuator.h"
#include <Arduino.h>
#ifdef ARDUINO
#include "nvs_store.h"
#endif

RelayActuator::RelayActuator(std::string name, uint8_t pin)
  : _name(std::move(name)), _pin(pin) {}

void RelayActuator::begin() {
  pinMode(_pin, OUTPUT);
  // Start de-energized (HIGH = off for active-low relay)
  digitalWrite(_pin, HIGH);
  _currentState = false;
  _restoreFromNVS();
}

bool RelayActuator::tick(time_t now) {
  bool desired = _schedule.activeValue(now) >= 0.5f;
  bool changed = desired != _currentState;
  if (changed) {
    // Active-low: LOW energizes the relay (on), HIGH de-energizes (off)
    digitalWrite(_pin, desired ? LOW : HIGH);
    _currentState = desired;
    Serial.printf("Relay %s: %s\n", _name.c_str(), desired ? "ON" : "OFF");
  }
  if (changed || now - _lastSentAt >= ACTUATOR_HEARTBEAT_S) {
    _lastChanged = changed;
    _lastSentAt  = now;
    return true;
  }
  return false;
}

void RelayActuator::appendSenML(JsonArray& records, time_t /*now*/) {
  JsonObject state = records.add<JsonObject>();
  String n = String(_name.c_str()) + "/state";
  state["n"]  = n;
  state["vb"] = _currentState;

  JsonObject src = records.add<JsonObject>();
  String ns = String(_name.c_str()) + "/source";
  src["n"]  = ns;
  src["vs"] = _lastChanged ? "change" : "heartbeat";
}

void RelayActuator::applyCommand(JsonObjectConst cmd) {
  const char* action = cmd["action"];
  if (!action) return;

  if (strcmp(action, "set") == 0) {
    // Set value and implicitly enter Manual mode.
    _schedule.setManualValue(cmd["value"] | 1.0f);
    _schedule.setControlMode(ControlMode::Manual);
  } else if (strcmp(action, "set_mode") == 0) {
    const char* mode = cmd["mode"];
    if (!mode) return;
    if (strcmp(mode, "manual") == 0) {
      _schedule.setControlMode(ControlMode::Manual);
    } else if (strcmp(mode, "automatic") == 0) {
      _schedule.setControlMode(ControlMode::Automatic);
    }
  } else if (strcmp(action, "schedule") == 0) {
    _schedule.loadWindows(cmd["windows"].as<JsonArrayConst>());
  }

  _persistToNVS();
}

void RelayActuator::_persistToNVS() {
#ifndef ARDUINO
  return;
#else
  String cmKey = String("cm_") + _name.c_str();
  nvsStore.set(cmKey.c_str(),
    _schedule.controlMode() == ControlMode::Manual ? "manual" : "automatic");

  String mvKey = String("mv_") + _name.c_str();
  nvsStore.set(mvKey.c_str(), String(_schedule.manualValue()));

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  _schedule.serializeWindows(arr);
  String json;
  serializeJson(doc, json);
  String scKey = String("sc_") + _name.c_str();
  nvsStore.set(scKey.c_str(), json);
#endif
}

void RelayActuator::_restoreFromNVS() {
#ifndef ARDUINO
  return;
#else
  String cmKey = String("cm_") + _name.c_str();
  String mode  = nvsStore.get(cmKey.c_str());
  if (mode == "manual") {
    String mvKey = String("mv_") + _name.c_str();
    float val = nvsStore.get(mvKey.c_str()).toFloat();
    _schedule.setManualValue(val);
    _schedule.setControlMode(ControlMode::Manual);
  }

  String scKey   = String("sc_") + _name.c_str();
  String scJson  = nvsStore.get(scKey.c_str());
  if (scJson.length() > 0) {
    JsonDocument doc;
    if (!deserializeJson(doc, scJson)) {
      _schedule.loadWindows(doc.as<JsonArrayConst>());
    }
  }
#endif
}
