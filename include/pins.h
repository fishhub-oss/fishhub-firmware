#pragma once

#define ONE_WIRE_PIN       4
#define RESET_BUTTON_PIN   0
#define RELAY_LIGHT_PIN    16
#define SERVO_PIN          17

// Mode-toggle button for the OLED display (active HIGH, INPUT_PULLDOWN).
// Press pulls the pin to 3.3 V; idle is LOW. Must be a pin that supports
// internal pull-downs (not GPIO 34–39, which are input-only with no pull hardware).
#define DISPLAY_BUTTON_PIN 19
