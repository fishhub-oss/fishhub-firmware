#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
#include <cstdint>
using String = std::string;
#endif

#include <ArduinoJson.h>
#include <time.h>

class Peripheral {
public:
  virtual ~Peripheral() = default;
  virtual void begin() = 0;

  // How often PeripheralManager should call tick(), in milliseconds.
  virtual uint32_t intervalMs() const = 0;

  // Called when intervalMs() has elapsed. Returns true if there is a new reading/state.
  virtual bool tick(time_t now) = 0;

  // Appends one or more SenML sibling records to the top-level records array.
  // Only called when tick() returned true.
  virtual void appendSenML(JsonArray& records, time_t now) = 0;

  // Entry point for inbound MQTT commands. Sensors may leave this as the default no-op.
  virtual void applyCommand(JsonObjectConst cmd) {}

  // Unique name — used as MQTT topic segment and SenML field name prefix.
  virtual const char* name() const = 0;
};
