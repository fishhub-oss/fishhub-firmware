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

static constexpr unsigned long DEBOUNCE_MS = 20;

static bool _displayBtnState = LOW;          // last stable state (idle = LOW with INPUT_PULLDOWN)
static bool _displayBtnRaw = LOW;            // last raw read
static unsigned long _displayBtnChanged = 0; // when raw last changed
static unsigned long _displayBtnDownAt = 0;  // when stable HIGH began

void checkDisplayButton()
{
  bool raw = digitalRead(DISPLAY_BUTTON_PIN);
  unsigned long now = millis();

  // Reset the debounce timer whenever the raw reading changes
  if (raw != _displayBtnRaw)
  {
    _displayBtnRaw = raw;
    _displayBtnChanged = now;
  }

  // Only accept a new stable state after DEBOUNCE_MS of stability
  if (now - _displayBtnChanged < DEBOUNCE_MS)
    return;

  if (raw == _displayBtnState)
    return; // no stable-state change

  _displayBtnState = raw;

  if (raw == HIGH)
  {
    // Stable rising edge — button pressed (INPUT_PULLDOWN: press = HIGH)
    _displayBtnDownAt = now;
  }
  else
  {
    // Stable falling edge — button released
    unsigned long held = now - _displayBtnDownAt;
    if (held >= 20 && held < 1000)
    {
      static DisplayMode currentMode = DisplayMode::MEASUREMENTS;
      currentMode = (currentMode == DisplayMode::MEASUREMENTS)
                        ? DisplayMode::DEBUG
                        : DisplayMode::MEASUREMENTS;
      oledDisplay.setMode(currentMode);
      Serial.printf("Display mode: %s\n",
                    currentMode == DisplayMode::MEASUREMENTS ? "MEASUREMENTS" : "DEBUG");
    }
  }
}
