#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
#include <cstdint>
using String = std::string;
#endif

#include <ArduinoJson.h>
#include <map>
#include <time.h>

// Forward declaration to avoid circular include with peripheral_manager.h
class PeripheralManager;

class Trigger {
public:
  bool load(JsonObjectConst json);
  bool evaluate(const std::map<std::string, float>& values, time_t now,
                PeripheralManager& mgr);
  void serializeTo(JsonObject out) const;
  const std::string& id() const { return _id; }
  bool enabled() const { return _enabled; }

private:
  float evalNode(JsonObjectConst node,
                 const std::map<std::string, float>& values) const;
  void  applyTo(PeripheralManager& mgr) const;

  std::string  _id;
  bool         _enabled      = false;
  JsonDocument _condDoc;
  std::string  _targetPeripheral;
  JsonDocument _actionDoc;
  uint32_t     _cooldownS   = 60;
  time_t       _lastFiredAt = 0;
};
