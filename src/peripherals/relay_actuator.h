#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
#endif

#include "peripheral.h"
#include "peripheral_serializer_registry.h"
#include "schedule.h"

#ifndef ACTUATOR_HEARTBEAT_S
#define ACTUATOR_HEARTBEAT_S 300
#endif

class RelayActuator : public Peripheral {
public:
  RelayActuator(std::string name, uint8_t pin, std::string purpose = "");

  void        begin() override;
  uint32_t    intervalMs() const override { return 1000; }
  bool        tick(time_t now) override;
  void        appendSenML(JsonArray& records, time_t now) override;
  void        currentMeasurements(std::map<std::string, float>& out) const override;
  void        applyCommand(JsonObjectConst cmd) override;
  const char* name() const override { return _name.c_str(); }
  const char* kind() const override { return "relay"; }
  const char* purpose() const override { return _purpose.c_str(); }

  // Schedules are idempotent — retained messages should re-apply on reboot.
  bool replayCommand() const override { return true; }

private:
  void _persistToNVS();
  void _restoreFromNVS();

  std::string _name;
  std::string _purpose;
  uint8_t     _pin;
  Schedule    _schedule;
  bool        _currentState = false;
  bool        _lastChanged  = false;
  time_t      _lastSentAt   = 0;
};

class RelaySerializer : public PeripheralSerializer {
public:
  void serialize(Peripheral* p, JsonObject& out) const override {
    out["purpose"] = p->purpose();
  }

  Peripheral* deserialize(const char* name, JsonObjectConst obj) const override {
    int pin = obj["pin"] | -1;
    if (pin < 0) return nullptr;
    const char* purpose = obj["purpose"] | "";
    return new RelayActuator(name, (uint8_t)pin, purpose);
  }
};
