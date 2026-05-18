#include "servo_positional.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif

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

bool ServoPositional::tick(time_t /*now*/) {
#ifdef ARDUINO
  if (_sensePin >= 0) {
    _senseValue   = !digitalRead(_sensePin);
    _sensePending = true;
  }
#endif
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
