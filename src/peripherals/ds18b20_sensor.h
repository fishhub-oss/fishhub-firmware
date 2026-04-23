#pragma once

#include <OneWire.h>
#include <DallasTemperature.h>
#include "peripheral.h"

class DS18B20Sensor : public Peripheral {
public:
  explicit DS18B20Sensor(uint8_t pin);

  void     begin() override;
  uint32_t intervalMs() const override { return 30000; }
  bool     tick(time_t now) override;
  void     appendSenML(JsonArray& entries, time_t now) override;
  const char* name() const override { return "temperature"; }

private:
  OneWire           _ow;
  DallasTemperature _sensors;
  float             _lastTemp;
};
