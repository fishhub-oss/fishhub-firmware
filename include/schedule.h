#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#include <ctime>
#endif

#include <ArduinoJson.h>
#include <vector>

enum class ControlMode { Automatic, Manual };

class Schedule {
public:
  // Parses an array of {"from","to","value",?"days"} window objects.
  // Does NOT change the current control mode.
  void loadWindows(JsonArrayConst windows);

  // Returns _overrideValue if Manual, otherwise the value of the first matching
  // window, or 0.0f if none match. Handles overnight windows correctly.
  float activeValue(time_t now) const;

  // Sets the held value used in Manual mode. Does not change the mode itself.
  void setManualValue(float value);

  // Switches between Manual and Automatic. Does not change the held value.
  void        setControlMode(ControlMode mode);
  ControlMode controlMode() const { return _mode; }

  bool hasOverride() const { return _mode == ControlMode::Manual; }

private:
  struct Window {
    uint8_t  days;        // bitmask bit0=Mon…bit6=Sun; 0 = every day
    uint16_t fromMinutes;
    uint16_t toMinutes;
    float    value;
  };

  static uint16_t parseMinutes(const char* hhmm);

  std::vector<Window> _windows;
  ControlMode         _mode          = ControlMode::Automatic;
  float               _overrideValue = 0.0f;
};
