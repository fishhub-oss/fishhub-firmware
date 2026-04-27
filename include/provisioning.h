#pragma once
#include <Arduino.h>

enum class ActivationError { None, WifiFailed, InvalidCode, ServerError, Timeout };

// Enters AP mode, serves captive portal. Never returns — transitions via reboot or
// by calling startProvisioning() again on error.
void startProvisioning();
