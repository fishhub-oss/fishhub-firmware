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
  for (JsonObjectConst w : windows) {
    const char* from = w["from"];
    const char* to   = w["to"];
    if (!from || !to) continue;

    uint8_t days = 0;
    JsonArrayConst daysArr = w["days"];
    if (!daysArr.isNull()) {
      for (int d : daysArr) {
        if (d >= 1 && d <= 7) {
          // 1=Mon → bit0, 7=Sun → bit6
          days |= (1 << (d - 1));
        }
      }
    }

    float value = w["value"] | 1.0f;
    _windows.push_back({days, parseMinutes(from), parseMinutes(to), value});
  }
}

float Schedule::activeValue(time_t now) const {
  if (_mode == ControlMode::Manual) return _overrideValue;

  struct tm t;
  localtime_r(&now, &t);

  // tm_wday: 0=Sun, 1=Mon … 6=Sat → map to bit0=Mon … bit6=Sun
  uint8_t dayBit = (t.tm_wday == 0) ? (1 << 6) : (1 << (t.tm_wday - 1));
  uint16_t cur = (uint16_t)(t.tm_hour * 60 + t.tm_min);

  for (const auto& w : _windows) {
    if (w.days != 0 && !(w.days & dayBit)) continue;

    if (w.fromMinutes <= w.toMinutes) {
      if (cur >= w.fromMinutes && cur < w.toMinutes) return w.value;
    } else {
      // Overnight window (e.g. 22:00–06:00)
      if (cur >= w.fromMinutes || cur < w.toMinutes) return w.value;
    }
  }
  return 0.0f;
}

void Schedule::setManualValue(float value) {
  _overrideValue = value;
}

void Schedule::setControlMode(ControlMode mode) {
  _mode = mode;
}

void Schedule::serializeWindows(JsonArray& out) const {
  for (const auto& w : _windows) {
    JsonObject obj = out.add<JsonObject>();
    // Reconstruct HH:MM strings from minutes
    char from[6], to[6];
    snprintf(from, sizeof(from), "%02d:%02d", w.fromMinutes / 60, w.fromMinutes % 60);
    snprintf(to,   sizeof(to),   "%02d:%02d", w.toMinutes   / 60, w.toMinutes   % 60);
    obj["from"]  = from;
    obj["to"]    = to;
    obj["value"] = w.value;
    if (w.days != 0) {
      JsonArray daysArr = obj["days"].to<JsonArray>();
      for (int i = 0; i < 7; i++) {
        if (w.days & (1 << i)) daysArr.add(i + 1);
      }
    }
  }
}
