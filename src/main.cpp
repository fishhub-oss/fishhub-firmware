#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "pins.h"
#include "wifi_ntp.h"

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

float readTemperatureCelsius() {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  if (temp == DEVICE_DISCONNECTED_C) {
    Serial.println("Sensor read error: DS18B20 disconnected or CRC failure");
    return NAN;
  }
  return temp;
}

void setup() {
  Serial.begin(115200);
  Serial.println("FishHub firmware booting...");

  connectWifi();
  waitForNtp();

  sensors.begin();
  int deviceCount = sensors.getDeviceCount();
  if (deviceCount == 0) {
    Serial.println("No DS18B20 found on OneWire bus");
  } else {
    Serial.printf("%d DS18B20 sensor(s) found\n", deviceCount);
  }
}

void loop() {
  float temp = readTemperatureCelsius();
  if (!isnan(temp)) {
    Serial.printf("Temp: %.2f °C\n", temp);
  }
  delay(5000);
}
