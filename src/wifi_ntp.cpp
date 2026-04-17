#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "wifi_ntp.h"
#include "config.h"

static const int WIFI_TIMEOUT_MS  = 10000;
static const int WIFI_MAX_RETRIES = 3;
static const int NTP_TIMEOUT_MS   = 10000;

void connectWifi() {
  for (int attempt = 1; attempt <= WIFI_MAX_RETRIES; attempt++) {
    Serial.printf("Wi-Fi connecting (attempt %d/%d)...\n", attempt, WIFI_MAX_RETRIES);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Wi-Fi connected — IP: %s\n", WiFi.localIP().toString().c_str());
      return;
    }

    WiFi.disconnect(true);
    Serial.println("Wi-Fi attempt timed out");
  }

  Serial.println("Wi-Fi connection failed after 3 attempts — halting");
  while (true) delay(1000);
}

void waitForNtp() {
  configTime(0, 0, "pool.ntp.org");
  Serial.println("Waiting for NTP sync...");

  struct tm timeinfo;
  unsigned long start = millis();
  while (!getLocalTime(&timeinfo) && millis() - start < NTP_TIMEOUT_MS) {
    delay(200);
  }

  if (!getLocalTime(&timeinfo)) {
    Serial.println("NTP sync failed — halting");
    while (true) delay(1000);
  }

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
  Serial.printf("NTP synced: %s\n", buf);
}
