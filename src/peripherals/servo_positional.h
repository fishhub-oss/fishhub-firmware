#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#include <ESP32Servo.h>
#else
#include <string>
#endif

#include <vector>
#include <map>
#include <string>
#include <ArduinoJson.h>
#include "peripheral.h"
#include "../peripheral_serializer_registry.h"
#include "../cron_trigger.h"

class ServoPositional : public Peripheral {
public:
  ServoPositional(const char* name, uint8_t pin,
                  int openAngle  = 120,
                  int restAngle  = 0,
                  int holdMs     = 400,
                  int sensePin   = -1,
                  const char* purpose = "");

  void        begin() override;
  uint32_t    intervalMs() const override { return 1000; }
  bool        tick(time_t now) override;
  void        appendSenML(JsonArray& records, time_t now) override;
  void        currentMeasurements(std::map<std::string, float>& out) const override;
  void        applyCommand(JsonObjectConst cmd) override;
  const char* name()      const override { return _name.c_str(); }
  const char* kind()      const override { return "servo_positional"; }
  const char* purpose()   const override { return _purpose.c_str(); }
  int         sensePin()  const          { return _sensePin; }
  int         openAngle() const          { return _openAngle; }
  int         restAngle() const          { return _restAngle; }
  int         holdMs()    const          { return _holdMs; }

  bool replayCommand() const override { return false; }

private:
  void _actuate(int openAngle, int holdMs);
  void _persistLastFired();
  void _restoreLastFired();
  void _loadEntries(JsonArrayConst arr);

  std::string              _name;
  std::string              _purpose;
  uint8_t                  _pin;
  int                      _openAngle;
  int                      _restAngle;
  int                      _holdMs;
  int                      _sensePin;
#ifdef ARDUINO
  Servo                    _servo;
#endif
  std::vector<CronTrigger> _entries;

  bool  _pendingAngle  = false;
  int   _lastAngle     = 0;
  bool  _senseValue    = false;
  bool  _sensePending  = false;
};

class ServoPositionalSerializer : public PeripheralSerializer {
public:
  void serialize(Peripheral* p, JsonObject& out) const override {
    auto* s = static_cast<ServoPositional*>(p);
    out["open_angle"] = s->openAngle();
    out["rest_angle"] = s->restAngle();
    out["hold_ms"]    = s->holdMs();
    out["sense_pin"]  = s->sensePin();
    out["purpose"]    = s->purpose();
  }

  Peripheral* deserialize(const char* name, JsonObjectConst obj) const override {
    int pin      = obj["pin"]       | -1;
    if (pin < 0) return nullptr;
    int openAngle  = obj["open_angle"] | 120;
    int restAngle  = obj["rest_angle"] | 0;
    int holdMs     = obj["hold_ms"]    | 400;
    int sensePin   = obj["sense_pin"]  | -1;
    const char* purpose = obj["purpose"] | "";
    return new ServoPositional(name, (uint8_t)pin, openAngle, restAngle, holdMs, sensePin, purpose);
  }
};
