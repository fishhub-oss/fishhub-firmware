#pragma once

// Reads RESET_BUTTON_PIN hold duration.
// 3 s  → enters provisioning mode (via startProvisioning()).
// 10 s → clears NVS and reboots.
void checkButton();

// Reads DISPLAY_BUTTON_PIN for a short press (< 1 s) and toggles the
// OledDisplay between MEASUREMENTS and DEBUG mode.
void checkDisplayButton();
