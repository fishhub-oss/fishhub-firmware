#pragma once

#include <time.h>
#include <string.h>

// Reusable one-shot cron trigger. Supports 5-field cron expressions
// (min hour dom month dow) with '*' and fixed integers only.
// Any peripheral that needs cron-based scheduling holds a vector of these.
struct CronTrigger {
  char   id[16];
  int    minute;   // -1 = *
  int    hour;     // -1 = *
  int    dom;      // -1 = *
  int    month;    // -1 = *
  int    dow;      // -1 = *
  int    value;    // peripheral-specific payload (e.g. rotation_ms)
  time_t lastFired;

  // Parses a 5-field cron expression into the fields above.
  // Returns false if the expression is malformed.
  bool parseCron(const char* expr);

  // Returns true if the current minute matches the cron fields AND the
  // trigger has not already fired during this occurrence.
  // Dedup: lastFired < floor(now / 60) * 60.
  bool isDue(time_t now) const;

  // Records that the trigger fired at `now`.
  void markFired(time_t now);
};
