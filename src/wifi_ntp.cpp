#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "wifi_ntp.h"
#include "nvs_store.h"
#include "config_defaults.h"

static const int WIFI_TIMEOUT_MS  = 10000;
static const int WIFI_MAX_RETRIES = 3;
static const int NTP_TIMEOUT_MS   = 10000;

bool connectWifi() {
  String ssid     = nvsStore.get("wifi_ssid");
  String password = nvsStore.get("wifi_pass");
  if (ssid.isEmpty())     ssid     = WIFI_SSID;
  if (password.isEmpty()) password = WIFI_PASSWORD;

  for (int attempt = 1; attempt <= WIFI_MAX_RETRIES; attempt++) {
    Serial.printf("Wi-Fi connecting (attempt %d/%d)...\n", attempt, WIFI_MAX_RETRIES);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Wi-Fi connected — IP: %s\n", WiFi.localIP().toString().c_str());
      return true;
    }

    WiFi.disconnect(true);
    Serial.println("Wi-Fi attempt timed out");
  }

  Serial.println("Wi-Fi connection failed after 3 attempts");
  return false;
}

bool waitForNtp() {
  configTime(0, 0, "pool.ntp.org");

  String tz = nvsStore.readTimezone();
  if (!tz.isEmpty()) {
    setenv("TZ", tz.c_str(), 1);
    tzset();
    Serial.printf("NTP: applying stored timezone %s\n", tz.c_str());
  }

  Serial.println("Waiting for NTP sync...");

  struct tm timeinfo;
  unsigned long start = millis();
  while (!getLocalTime(&timeinfo) && millis() - start < NTP_TIMEOUT_MS) {
    delay(200);
  }

  if (!getLocalTime(&timeinfo)) {
    Serial.println("NTP sync failed");
    return false;
  }

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
  Serial.printf("NTP synced: %s\n", buf);
  return true;
}
