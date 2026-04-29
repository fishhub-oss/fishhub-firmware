#include <Arduino.h>
#include <ArduinoJson.h>
#include "pins.h"
#include "nvs_store.h"
#include "provisioning.h"
#include "wifi_ntp.h"
#include "http_client.h"
#include "mqtt_client.h"
#include "peripheral_manager.h"
#include "peripherals/ds18b20_sensor.h"
#include "peripherals/relay_actuator.h"

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

// Restore peripherals persisted in NVS so the device is functional
// immediately on boot, before retained MQTT messages are re-delivered.
static void restorePeripherals()
{
  String json = nvsStore.get("peripherals");
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    Serial.println("NVS: failed to parse peripherals JSON — skipping restore");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject p : arr) {
    const char* name = p["name"];
    const char* kind = p["kind"];
    int pin          = p["pin"] | -1;
    if (!name || !kind || pin < 0) continue;

    if (strcmp(kind, "ds18b20") == 0) {
      manager.add(new DS18B20Sensor(name, (uint8_t)pin), "ds18b20", pin);
      Serial.printf("NVS: restored ds18b20 '%s' on pin %d\n", name, pin);
    } else if (strcmp(kind, "relay") == 0) {
      manager.add(new RelayActuator(name, (uint8_t)pin), "relay", pin);
      Serial.printf("NVS: restored relay '%s' on pin %d\n", name, pin);
    }
  }
}

static void normalOperation()
{
  restorePeripherals();
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
