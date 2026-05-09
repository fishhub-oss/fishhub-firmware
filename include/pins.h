#pragma once

#define ONE_WIRE_PIN       4
#define RESET_BUTTON_PIN   0
#define RELAY_LIGHT_PIN    16
#define SERVO_PIN          17

// Mode-toggle button for the OLED display (active LOW, INPUT_PULLUP).
// Must be a pin that supports internal pull-ups (not GPIO 34–39 on ESP32,
// which are input-only and have no pull-up hardware).
#define DISPLAY_BUTTON_PIN 18
