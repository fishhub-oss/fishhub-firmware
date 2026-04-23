#include "ds18b20_sensor.h"
#include <Arduino.h>

DS18B20Sensor::DS18B20Sensor(uint8_t pin, uint32_t intervalMs)
  : _ow(pin), _sensors(&_ow), _lastTemp(0.0f), _intervalMs(intervalMs) {}

void DS18B20Sensor::begin() {
  _sensors.begin();
  Serial.printf("DS18B20: %d sensor(s) found\n", _sensors.getDeviceCount());
}

bool DS18B20Sensor::tick(time_t /*now*/) {
  _sensors.requestTemperatures();
  float t = _sensors.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) {
    Serial.println("DS18B20: read error — sensor disconnected or CRC failure");
    return false;
  }
  _lastTemp = t;
  Serial.printf("DS18B20: %.2f °C\n", _lastTemp);
  return true;
}

void DS18B20Sensor::appendSenML(JsonArray& entries, time_t /*now*/) {
  JsonObject e = entries.add<JsonObject>();
  e["n"] = "temperature";
  e["u"] = "Cel";
  e["v"] = _lastTemp;
}
