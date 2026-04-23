#pragma once

#include <OneWire.h>
#include <DallasTemperature.h>
#include "peripheral.h"

class DS18B20Sensor : public Peripheral {
public:
  DS18B20Sensor(uint8_t pin, uint32_t intervalMs);

  void     begin() override;
  uint32_t intervalMs() const override { return _intervalMs; }
  bool     tick(time_t now) override;
  void     appendSenML(JsonArray& entries, time_t now) override;
  const char* name() const override { return "temperature"; }

private:
  OneWire           _ow;
  DallasTemperature _sensors;
  float             _lastTemp;
  uint32_t          _intervalMs;
};
