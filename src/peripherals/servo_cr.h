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

class ServoCR : public Peripheral {
public:
  ServoCR(const char* name, uint8_t pin,
          int sensePin = -1, const char* purpose = "");

  void        begin() override;
  uint32_t    intervalMs() const override { return 1000; }
  bool        tick(time_t now) override;
  void        appendSenML(JsonArray& records, time_t now) override;
  void        currentMeasurements(std::map<std::string, float>& out) const override;
  void        applyCommand(JsonObjectConst cmd) override;
  const char* name()    const override { return _name.c_str(); }
  const char* kind()    const override { return "servo_cr"; }
  const char* purpose() const override { return _purpose.c_str(); }
  int         sensePin() const         { return _sensePin; }

  bool replayCommand() const override { return false; }
  bool isBusy()        const override { return _pendingRotation || _sensePending; }

private:
  void _actuate(int rotationMs);
  void _persistLastFired();
  void _restoreLastFired();
  void _loadEntries(JsonArrayConst arr);

  std::string              _name;
  std::string              _purpose;
  uint8_t                  _pin;
  int                      _sensePin;
#ifdef ARDUINO
  Servo                    _servo;
#endif
  std::vector<CronTrigger> _entries;

  bool  _pendingRotation = false;
  int   _lastRotationMs  = 0;
  bool  _senseValue      = false;
  bool  _sensePending    = false;
};

class ServoCRSerializer : public PeripheralSerializer {
public:
  void serialize(Peripheral* p, JsonObject& out) const override {
    auto* s = static_cast<ServoCR*>(p);
    out["sense_pin"] = s->sensePin();
    out["purpose"]   = s->purpose();
  }

  Peripheral* deserialize(const char* name, JsonObjectConst obj) const override {
    int pin      = obj["pin"]       | -1;
    int sensePin = obj["sense_pin"] | -1;
    if (pin < 0) return nullptr;
    const char* purpose = obj["purpose"] | "";
    return new ServoCR(name, (uint8_t)pin, sensePin, purpose);
  }
};
