#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
#include <cstdint>
using String = std::string;
#endif

#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include <time.h>
#include "peripheral.h"

struct PeripheralEntry {
  Peripheral* peripheral;
  uint32_t    lastTickedAt;
  const char* kind = nullptr;
  int         pin  = -1;
};

class PeripheralManager {
public:
  // Adds a peripheral. If beginAll() has already been called, begin() is
  // invoked immediately so late-registered peripherals are initialised.
  // Ownership of the pointer is transferred — remove() will delete it.
  void add(Peripheral* p, const char* kind = nullptr, int pin = -1);

  // Iterates all entries, calling fn(peripheral, kind, pin) for each.
  void forEach(std::function<void(Peripheral*, const char*, int)> fn) const;
  void beginAll();

  // Removes the peripheral with the given name and deletes the object.
  void remove(const String& name);

  // Returns true if a peripheral with the given name is registered.
  bool has(const String& name) const;

  // Ticks each peripheral when its interval has elapsed.
  // nowMs is injected so tests can pass a fake counter instead of millis().
  // Returns a SenML JSON string if any peripheral produced output, "" otherwise.
  String tickAll(time_t now, uint32_t nowMs);

  // Routes an inbound MQTT command to the peripheral matching name().
  void dispatchCommand(const String& name, JsonObjectConst cmd);

  // Returns the peripheral with the given name, or nullptr if not found.
  Peripheral* find(const String& name) const;

private:
  std::vector<PeripheralEntry> _entries;
  bool _begun = false;
};
