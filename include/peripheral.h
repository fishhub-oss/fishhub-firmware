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
#include <string>
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

  // Returns true while the peripheral has an actuation in progress or uncommitted state
  // that should be reported before a reboot. Used as a safe-point gate for OTA updates.
  virtual bool isBusy() const { return false; }

  // Returns true if retained/redelivered commands should always be re-applied (default).
  // Idempotent peripherals (schedules, state) return true.
  // One-shot peripherals (feeder, doser) override to false — the MQTT client will
  // deduplicate by persisting the last processed command ID in NVS.
  virtual bool replayCommand() const { return true; }

  // Populates `out` with current measurement values keyed by SenML name
  // (e.g. "ds18b20-4/temperature"). Called each tick cycle to build the
  // snapshot used by trigger evaluation. Default: no-op.
  virtual void currentMeasurements(std::map<std::string, float>& out) const {}

  // Unique name — used as MQTT topic segment and SenML field name prefix.
  virtual const char* name() const = 0;

  // String constant identifying the peripheral class (e.g. "relay", "ds18b20").
  // Derived from the class itself; never stored in PeripheralEntry.
  virtual const char* kind() const = 0;

  // User-defined label describing what this peripheral is used for.
  // Stored at construction; no firmware logic inspects it.
  virtual const char* purpose() const { return ""; }
};
