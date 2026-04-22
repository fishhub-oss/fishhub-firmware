#pragma once
#include <Arduino.h>

enum class ActivationError { None, WifiFailed, InvalidCode, ServerError };

void startProvisioning();
ActivationError activateDevice(const String& provisionCode);
