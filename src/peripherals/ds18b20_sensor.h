#pragma once

#include <string>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "peripheral.h"

#ifndef DS18B20_INTERVAL_MS
#define DS18B20_INTERVAL_MS 30000
#endif

class DS18B20Sensor : public Peripheral {
public:
  DS18B20Sensor(std::string name, uint8_t pin, uint32_t intervalMs = DS18B20_INTERVAL_MS);

  void        begin() override;
  uint32_t    intervalMs() const override { return _intervalMs; }
  bool        tick(time_t now) override;
  void        appendSenML(JsonArray& records, time_t now) override;
  const char* name() const override { return _name.c_str(); }

private:
  std::string       _name;
  OneWire           _ow;
  DallasTemperature _sensors;
  float             _lastTemp;
  uint32_t          _intervalMs;
};
