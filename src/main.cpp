#include <Arduino.h>
#include "pins.h"
#include "nvs_store.h"
#include "provisioning.h"
#include "wifi_ntp.h"
#include "http_client.h"
#include "mqtt_client.h"
#include "peripheral_manager.h"
#include "peripherals/ds18b20_sensor.h"
#include "peripherals/relay_actuator.h"

#ifndef DS18B20_INTERVAL_MS
#define DS18B20_INTERVAL_MS 30000
#endif

PeripheralManager manager;
FishHubMqttClient mqttClient;

static void logNvsKey(const char *key)
{
  String val = nvsStore.get(key);
  Serial.printf("  NVS %-14s %s\n", key, val.isEmpty() ? "MISSING" : "present");
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
  logNvsKey("device_id");
  logNvsKey("device_jwt");

  bool provisioned =
      !nvsStore.get("wifi_ssid").isEmpty() &&
      !nvsStore.get("wifi_pass").isEmpty() &&
      !nvsStore.get("server_url").isEmpty() &&
      !nvsStore.get("device_id").isEmpty() &&
      !nvsStore.get("device_jwt").isEmpty();

  if (!provisioned)
  {
    Serial.println("One or more NVS keys missing — entering provisioning mode");
    startProvisioning(); // never returns — device reboots after activation
  }

  connectWifi();
  waitForNtp();

  manager.add(new DS18B20Sensor(ONE_WIRE_PIN, DS18B20_INTERVAL_MS));
  manager.add(new RelayActuator("light", RELAY_LIGHT_PIN));
  manager.beginAll();

  mqttClient.begin(manager);
}

void loop()
{
  if (digitalRead(RESET_BUTTON_PIN) == LOW)
  {
    Serial.println("Reset button pressed");
    Serial.println("- 3s  => enter provisioning mode");
    Serial.println("- 10s => clear data");

    unsigned long pressStart = millis();
    while (digitalRead(RESET_BUTTON_PIN) == LOW)
    {
      unsigned long heldUntilNow = millis() - pressStart;
      Serial.printf("Held for %d ms\n", heldUntilNow);
      delay(50);
    }

    unsigned long held = millis() - pressStart;

    if (held >= 10000)
    {
      Serial.println("Button held 10s — clearing NVS and rebooting...");
      nvsStore.clear();
      ESP.restart();
    }
    else if (held >= 3000)
    {
      Serial.println("Button held 3s — entering reconfiguration mode...");
      startProvisioning(); // never returns
    }
  }

  mqttClient.loop();

  time_t now = time(nullptr);
  String payload = manager.tickAll(now, millis());
  if (!payload.isEmpty())
  {
    postReading(payload);
  }
}
