#include "http_client.h"
#include <HTTPClient.h>
#include "config.h"

static bool doPost(const String& payload, int& statusCode) {
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " DEVICE_TOKEN);

  unsigned long start = millis();
  statusCode = http.POST(payload);
  unsigned long elapsed = millis() - start;

  http.end();

  if (statusCode > 0) {
    Serial.printf("POST %d in %lums\n", statusCode, elapsed);
  } else {
    Serial.printf("POST error: %s\n", HTTPClient::errorToString(statusCode).c_str());
  }

  return statusCode >= 200 && statusCode < 300;
}

bool postReading(const String& payload) {
  int statusCode;

  if (doPost(payload, statusCode)) {
    return true;
  }

  if (statusCode >= 500 || statusCode < 0) {
    Serial.println("Retrying POST...");
    delay(1000);
    return doPost(payload, statusCode);
  }

  return false;
}
