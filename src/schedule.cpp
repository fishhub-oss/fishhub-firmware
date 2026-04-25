#include "schedule.h"
#include <cstring>
#include <cstdlib>

uint16_t Schedule::parseMinutes(const char* hhmm) {
  if (!hhmm || strlen(hhmm) < 5) return 0;
  uint16_t h = (uint16_t)atoi(hhmm);
  uint16_t m = (uint16_t)atoi(hhmm + 3);
  return h * 60 + m;
}

void Schedule::loadWindows(JsonArrayConst windows) {
  _windows.clear();
  _overridden = false;
  for (JsonArrayConst pair : windows) {
    if (pair.size() < 2) continue;
    const char* on  = pair[0].as<const char*>();
    const char* off = pair[1].as<const char*>();
    if (!on || !off) continue;
    _windows.push_back({parseMinutes(on), parseMinutes(off)});
  }
}

bool Schedule::isActive(time_t now) const {
  if (_overridden) return _overrideState;

  struct tm t;
#ifdef ARDUINO
  localtime_r(&now, &t);
#else
  localtime_r(&now, &t);
#endif
  uint16_t cur = (uint16_t)(t.tm_hour * 60 + t.tm_min);

  for (const auto& w : _windows) {
    if (w.onMinutes <= w.offMinutes) {
      // Normal window (e.g. 08:00–22:00)
      if (cur >= w.onMinutes && cur < w.offMinutes) return true;
    } else {
      // Overnight window (e.g. 22:00–06:00)
      if (cur >= w.onMinutes || cur < w.offMinutes) return true;
    }
  }
  return false;
}

void Schedule::setOverride(bool state) {
  _overridden    = true;
  _overrideState = state;
}
