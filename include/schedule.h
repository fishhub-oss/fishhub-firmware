#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#include <ctime>
#endif

#include <ArduinoJson.h>
#include <vector>

class Schedule {
public:
  // Parses an array of ["HH:MM","HH:MM"] window pairs.
  // Clears any active override.
  void loadWindows(JsonArrayConst windows);

  // Returns true if now falls within any stored window.
  // Handles overnight windows (e.g. 22:00–06:00) correctly.
  bool isActive(time_t now) const;

  // Forces a fixed state regardless of windows until the next loadWindows() call.
  void setOverride(bool state);

  bool hasOverride() const { return _overridden; }

private:
  struct Window {
    uint16_t onMinutes;
    uint16_t offMinutes;
  };

  static uint16_t parseMinutes(const char* hhmm);

  std::vector<Window> _windows;
  bool _overridden    = false;
  bool _overrideState = false;
};
