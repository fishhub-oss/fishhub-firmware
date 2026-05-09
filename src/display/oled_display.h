#pragma once

#include <Arduino.h>
#include <vector>

enum class DisplayMode { MEASUREMENTS, DEBUG };

struct ReadingEntry {
  String name;
  String value;
};

class OledDisplay {
public:
  // Initialises the SSD1306. If the display is not detected, disables itself
  // gracefully — all subsequent calls become no-ops.
  void begin();

  // Must be called from loop(). Advances slide timer in MEASUREMENTS mode and
  // redraws when needed.
  void tick(unsigned long nowMs);

  // Switches between MEASUREMENTS and DEBUG modes.
  void setMode(DisplayMode m);

  // Updates the state machine label shown at the top of DEBUG mode.
  void setCurrentState(const char* s);

  // Appends a one-line event to the DEBUG ring buffer (max 21 chars printed).
  void logEvent(const String& line);

  // Replaces the cached peripheral readings used by MEASUREMENTS mode.
  void updateReadings(const std::vector<ReadingEntry>& readings);

private:
  void drawMeasurements();
  void drawDebug();

  bool         _available       = false;
  DisplayMode  _mode            = DisplayMode::MEASUREMENTS;
  unsigned long _lastSlideMs    = 0;
  int          _slideIndex      = 0;

  char         _currentState[24] = "UNKNOWN";

  static constexpr int RING_SIZE = 16;
  String       _ring[RING_SIZE];
  int          _ringHead = 0;   // index of oldest entry
  int          _ringCount = 0;

  std::vector<ReadingEntry> _readings;
};

// Global instance — declared here, defined in oled_display.cpp.
extern OledDisplay oledDisplay;
