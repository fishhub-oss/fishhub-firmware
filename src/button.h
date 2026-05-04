#pragma once

// Reads RESET_BUTTON_PIN hold duration.
// 3 s  → enters provisioning mode (via startProvisioning()).
// 10 s → clears NVS and reboots.
// Safe to call from any context, including a FreeRTOS task.
void checkButton();
