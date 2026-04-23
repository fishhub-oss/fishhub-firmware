#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
#include <cstdint>
using String = std::string;
#endif

#include <ArduinoJson.h>
#include <vector>
#include <time.h>
#include "peripheral.h"

struct PeripheralEntry {
  Peripheral* peripheral;
  uint32_t    lastTickedAt;
};

class PeripheralManager {
public:
  void add(Peripheral* p);
  void beginAll();

  // Ticks each peripheral when its interval has elapsed.
  // nowMs is injected so tests can pass a fake counter instead of millis().
  // Returns a SenML JSON string if any peripheral produced output, "" otherwise.
  String tickAll(time_t now, uint32_t nowMs);

  // Routes an inbound MQTT command to the peripheral matching name().
  void dispatchCommand(const String& name, JsonObjectConst cmd);

private:
  std::vector<PeripheralEntry> _entries;
};
