#include <Arduino.h>
#include "button.h"
#include "pins.h"
#include "nvs_store.h"
#include "provisioning.h"
#include "display/oled_display.h"

// ─── Reset button ─────────────────────────────────────────────────────────────

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

// ─── Display mode-toggle button ───────────────────────────────────────────────

static bool      _displayBtnPrev    = HIGH;
static unsigned long _displayBtnDownAt = 0;

void checkDisplayButton()
{
  bool cur = digitalRead(DISPLAY_BUTTON_PIN);

  // Falling edge — button just pressed
  if (_displayBtnPrev == HIGH && cur == LOW)
    _displayBtnDownAt = millis();

  // Rising edge — button released
  if (_displayBtnPrev == LOW && cur == HIGH) {
    unsigned long held = millis() - _displayBtnDownAt;
    // Short press (< 1 s) → toggle mode
    if (held < 1000) {
      static DisplayMode currentMode = DisplayMode::MEASUREMENTS;
      currentMode = (currentMode == DisplayMode::MEASUREMENTS)
                    ? DisplayMode::DEBUG
                    : DisplayMode::MEASUREMENTS;
      oledDisplay.setMode(currentMode);
      Serial.printf("Display mode: %s\n",
                    currentMode == DisplayMode::MEASUREMENTS ? "MEASUREMENTS" : "DEBUG");
    }
  }

  _displayBtnPrev = cur;
}
