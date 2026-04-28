#include "relay_actuator.h"
#include <Arduino.h>

RelayActuator::RelayActuator(const char* name, uint8_t pin)
  : _name(name), _pin(pin) {}

void RelayActuator::begin() {
  pinMode(_pin, OUTPUT);
  // Start de-energized (HIGH = off for active-low relay)
  digitalWrite(_pin, HIGH);
  _currentState = false;
}

bool RelayActuator::tick(time_t now) {
  bool desired = _schedule.isActive(now);
  bool changed = desired != _currentState;
  if (changed) {
    // Active-low: LOW energizes the relay (on), HIGH de-energizes (off)
    digitalWrite(_pin, desired ? LOW : HIGH);
    _currentState = desired;
    Serial.printf("Relay %s: %s\n", _name, desired ? "ON" : "OFF");
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
  String n = String(_name) + "/state";
  state["n"]  = n;
  state["vb"] = _currentState;

  JsonObject src = records.add<JsonObject>();
  String ns = String(_name) + "/source";
  src["n"]  = ns;
  src["vs"] = _lastChanged ? "change" : "heartbeat";
}

void RelayActuator::applyCommand(JsonObjectConst cmd) {
  const char* action = cmd["action"];
  if (!action) return;

  if (strcmp(action, "set") == 0) {
    _schedule.setOverride(cmd["state"].as<bool>());
  } else if (strcmp(action, "schedule") == 0) {
    _schedule.loadWindows(cmd["windows"].as<JsonArrayConst>());
  }
}
