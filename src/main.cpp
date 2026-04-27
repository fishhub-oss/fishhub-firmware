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

static PeripheralManager manager;
static FishHubMqttClient mqttClient;

static void boot()
{
  Serial.begin(115200);
  Serial.println("FishHub firmware booting...");
  nvsStore.begin();
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  Serial.println("NVS key status:");
  for (const char *key : {"wifi_ssid", "wifi_pass", "device_id", "device_jwt",
                           "mqtt_username", "mqtt_host", "provisioned"})
  {
    Serial.printf("  NVS %-14s %s\n", key,
                  nvsStore.get(key).isEmpty() ? "MISSING" : "present");
  }
}

static void provisioningMode()
{
  Serial.println("Device not fully provisioned — entering provisioning mode");
  startProvisioning(); // never returns — reboots on success, loops on error
}

static void connectToWifi()
{
  connectWifi();
  waitForNtp();
}

static void normalOperation()
{
  manager.add(new DS18B20Sensor(ONE_WIRE_PIN, DS18B20_INTERVAL_MS));
  manager.add(new RelayActuator("light", RELAY_LIGHT_PIN));
  manager.beginAll();
  mqttClient.begin(manager);
}

static void handleButton()
{
  if (digitalRead(RESET_BUTTON_PIN) != LOW)
    return;

  Serial.println("Reset button pressed");
  Serial.println("- 3s  => enter provisioning mode");
  Serial.println("- 10s => clear all data");

  unsigned long pressStart = millis();
  while (digitalRead(RESET_BUTTON_PIN) == LOW)
  {
    Serial.printf("Held for %lu ms\n", millis() - pressStart);
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
    provisioningMode(); // never returns
  }
}

static void sensorTick()
{
  mqttClient.loop();
  time_t now = time(nullptr);
  String payload = manager.tickAll(now, millis());
  if (!payload.isEmpty())
    postReading(payload);
}

void setup()
{
  boot();
  if (!nvsStore.isProvisioned())
    provisioningMode();
  connectToWifi();
  normalOperation();
}

void loop()
{
  handleButton();
  sensorTick();
}
