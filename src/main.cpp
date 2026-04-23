#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "pins.h"
#include "nvs_store.h"
#include "provisioning.h"
#include "wifi_ntp.h"
#include "senml.h"
#include "http_client.h"

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

static void logNvsKey(const char *key)
{
  String val = nvsStore.get(key);
  Serial.printf("  NVS %-14s %s\n", key, val.isEmpty() ? "MISSING" : "present");
}

// Returns true if the given pin is held LOW for durationMs continuous milliseconds.
static bool buttonHeld(uint8_t pin, unsigned long durationMs)
{
  if (digitalRead(pin) == HIGH)
    return false;

  unsigned long start = millis();
  while (millis() - start < durationMs)
  {
    delay(50);
    if (digitalRead(pin) == HIGH)
      return false;
  }
  return true;
}

float readTemperatureCelsius()
{
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  if (temp == DEVICE_DISCONNECTED_C)
  {
    Serial.println("Sensor read error: DS18B20 disconnected or CRC failure");
    return NAN;
  }
  return temp;
}

void setup()
{
  Serial.begin(115200);
  Serial.println("FishHub firmware booting...");

  nvsStore.begin();
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  Serial.println("NVS key status:");
  logNvsKey("wifi_ssid");
  logNvsKey("wifi_pass");
  logNvsKey("server_url");
  logNvsKey("device_token");

  bool provisioned =
      !nvsStore.get("wifi_ssid").isEmpty() &&
      !nvsStore.get("wifi_pass").isEmpty() &&
      !nvsStore.get("server_url").isEmpty() &&
      !nvsStore.get("device_token").isEmpty();

  if (!provisioned)
  {
    Serial.println("One or more NVS keys missing — entering provisioning mode");
    startProvisioning(); // never returns — device reboots after activation
  }

  connectWifi();
  waitForNtp();

  sensors.begin();
  int deviceCount = sensors.getDeviceCount();
  if (deviceCount == 0)
  {
    Serial.println("No DS18B20 found on OneWire bus");
  }
  else
  {
    Serial.printf("%d DS18B20 sensor(s) found\n", deviceCount);
  }
}

void loop()
{
  if (buttonHeld(RESET_BUTTON_PIN, 3000))
  {
    Serial.println("Button held — entering reconfiguration mode...");
    startProvisioning(); // never returns
  }

  float temp = readTemperatureCelsius();
  if (!isnan(temp))
  {
    Serial.printf("Temp: %.2f °C\n", temp);
    String payload = buildSenMLPayload(temp, time(nullptr));
    postReading(payload);
  }
  delay(5000);
}
