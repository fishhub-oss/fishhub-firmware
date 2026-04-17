#pragma once

#include <Arduino.h>
#include <time.h>

String buildSenMLPayload(float tempCelsius, time_t timestamp);
