#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
#endif

#include "peripheral.h"
#include "schedule.h"

#ifndef ACTUATOR_HEARTBEAT_S
#define ACTUATOR_HEARTBEAT_S 300
#endif

class RelayActuator : public Peripheral {
public:
  RelayActuator(std::string name, uint8_t pin);

  void        begin() override;
  uint32_t    intervalMs() const override { return 1000; }
  bool        tick(time_t now) override;
  void        appendSenML(JsonArray& records, time_t now) override;
  void        applyCommand(JsonObjectConst cmd) override;
  const char* name() const override { return _name.c_str(); }

  // Schedules are idempotent — retained messages should re-apply on reboot.
  bool replayCommand() const override { return true; }

private:
  std::string _name;
  uint8_t     _pin;
  Schedule    _schedule;
  bool        _currentState = false;
  bool        _lastChanged  = false;
  time_t      _lastSentAt   = 0;
};
