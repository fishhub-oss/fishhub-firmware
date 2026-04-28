#pragma once
#include <cstdint>
#include <string>

using String = std::string;

#define OUTPUT 0
#define HIGH   1
#define LOW    0

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}

struct SerialClass {
  template<typename... Args>
  void printf(const char*, Args...) {}
} Serial;
