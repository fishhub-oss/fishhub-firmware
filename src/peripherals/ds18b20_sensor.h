#pragma once

#include <string>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "peripheral.h"
#include "peripheral_serializer_registry.h"

#ifndef DS18B20_INTERVAL_MS
#define DS18B20_INTERVAL_MS 30000
#endif

class DS18B20Sensor : public Peripheral {
public:
  DS18B20Sensor(std::string name, uint8_t pin,
                std::string purpose = "",
                uint32_t intervalMs = DS18B20_INTERVAL_MS);

  void        begin() override;
  uint32_t    intervalMs() const override { return _intervalMs; }
  bool        tick(time_t now) override;
  void        appendSenML(JsonArray& records, time_t now) override;
  void        currentMeasurements(std::map<std::string, float>& out) const override;
  const char* name() const override { return _name.c_str(); }
  const char* kind() const override { return "ds18b20"; }
  const char* purpose() const override { return _purpose.c_str(); }

private:
  std::string       _name;
  std::string       _purpose;
  uint8_t           _pin;
  OneWire           _ow;
  DallasTemperature _sensors;
  float             _lastTemp;
  uint32_t          _intervalMs;
};

class DS18B20Serializer : public PeripheralSerializer {
public:
  void serialize(Peripheral* p, JsonObject& out) const override {
    out["purpose"] = p->purpose();
  }

  Peripheral* deserialize(const char* name, JsonObjectConst obj) const override {
    int pin = obj["pin"] | -1;
    if (pin < 0) return nullptr;
    const char* purpose = obj["purpose"] | "";
    return new DS18B20Sensor(name, (uint8_t)pin, purpose);
  }
};
