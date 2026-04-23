#include <Arduino.h>
#include "pins.h"
#include "nvs_store.h"
#include "provisioning.h"
#include "wifi_ntp.h"
#include "http_client.h"
#include "peripheral_manager.h"
#include "peripherals/ds18b20_sensor.h"

PeripheralManager manager;

static void logNvsKey(const char* key) {
  String val = nvsStore.get(key);
  Serial.printf("  NVS %-14s %s\n", key, val.isEmpty() ? "MISSING" : "present");
}

static bool buttonHeld(uint8_t pin, unsigned long durationMs) {
  if (digitalRead(pin) == HIGH) return false;
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    delay(50);
    if (digitalRead(pin) == HIGH) return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("FishHub firmware booting...");

  nvsStore.begin();
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  Serial.println("NVS key status:");
  logNvsKey("wifi_ssid");
  logNvsKey("wifi_pass");
  logNvsKey("server_url");
  logNvsKey("device_jwt");

  bool provisioned =
    !nvsStore.get("wifi_ssid").isEmpty() &&
    !nvsStore.get("wifi_pass").isEmpty() &&
    !nvsStore.get("server_url").isEmpty() &&
    !nvsStore.get("device_jwt").isEmpty();

  if (!provisioned) {
    Serial.println("One or more NVS keys missing — entering provisioning mode");
    startProvisioning(); // never returns — device reboots after activation
  }

  connectWifi();
  waitForNtp();

  manager.add(new DS18B20Sensor(ONE_WIRE_PIN));
  manager.beginAll();
}

void loop() {
  if (buttonHeld(RESET_BUTTON_PIN, 3000)) {
    Serial.println("Button held — entering reconfiguration mode...");
    startProvisioning(); // never returns
  }

  time_t now = time(nullptr);
  String payload = manager.tickAll(now, millis());
  if (!payload.isEmpty()) {
    postReading(payload);
  }
}
