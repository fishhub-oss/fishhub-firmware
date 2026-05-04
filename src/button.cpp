#include <Arduino.h>
#include "button.h"
#include "pins.h"
#include "nvs_store.h"
#include "provisioning.h"

void checkButton()
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
    startProvisioning(); // never returns
  }
}
