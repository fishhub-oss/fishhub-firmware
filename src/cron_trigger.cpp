#include "cron_trigger.h"
#include <stdio.h>

bool CronTrigger::parseCron(const char* expr) {
  if (!expr) return false;

  // Parse 5 fields: min hour dom month dow
  int fields[5] = {-1, -1, -1, -1, -1};
  const char* p = expr;

  for (int i = 0; i < 5; i++) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return false;

    if (*p == '*') {
      fields[i] = -1;
      p++;
    } else if (*p >= '0' && *p <= '9') {
      fields[i] = 0;
      while (*p >= '0' && *p <= '9') {
        fields[i] = fields[i] * 10 + (*p - '0');
        p++;
      }
    } else {
      return false;
    }

    if (i < 4) {
      if (*p != ' ' && *p != '\t') return false;
      p++;
    }
  }

  minute = fields[0];
  hour   = fields[1];
  dom    = fields[2];
  month  = fields[3];
  dow    = fields[4];
  return true;
}

bool CronTrigger::isDue(time_t now) const {
  struct tm* t = localtime(&now);
  if (minute != -1 && t->tm_min        != minute) return false;
  if (hour   != -1 && t->tm_hour       != hour)   return false;
  if (dom    != -1 && t->tm_mday       != dom)     return false;
  if (month  != -1 && t->tm_mon + 1    != month)   return false;
  if (dow    != -1 && t->tm_wday       != dow)     return false;
  time_t thisMinute = (now / 60) * 60;
  return lastFired < thisMinute;
}

void CronTrigger::markFired(time_t now) {
  lastFired = (now / 60) * 60;
}
