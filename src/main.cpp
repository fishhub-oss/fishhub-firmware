#include <Arduino.h>
#include <ArduinoJson.h>
#include "pins.h"
#include "nvs_store.h"
#include "provisioning.h"
#include "wifi_ntp.h"
#include "mqtt_client.h"
#include "peripheral_manager.h"
#include "peripherals/ds18b20_sensor.h"
#include "peripherals/relay_actuator.h"
#include "button.h"

// ─── state machine ────────────────────────────────────────────────────────────

enum class State {
  PROVISIONING,
  CONNECT_WIFI,
  NTP_SYNC,
  NORMAL_OPERATION,
  ERROR_RETRY,
};

static State state;

static unsigned long errorRetryUntil = 0;
static State         errorNextState  = State::CONNECT_WIFI;

static const unsigned long ERROR_RETRY_DELAY_MS = 10000;

static void enterErrorRetry(State next, const char *reason)
{
  Serial.printf("Error: %s — retrying in %lu s\n", reason, ERROR_RETRY_DELAY_MS / 1000);
  errorRetryUntil = millis() + ERROR_RETRY_DELAY_MS;
  errorNextState  = next;
  state           = State::ERROR_RETRY;
}

// ─── normal operation helpers ─────────────────────────────────────────────────

static PeripheralManager manager;
static FishHubMqttClient mqttClient;
static bool normalOperationInitDone = false;

static void restorePeripherals()
{
  String json = nvsStore.get("peripherals");
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    Serial.println("NVS: failed to parse peripherals JSON — skipping restore");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject p : arr) {
    const char* name = p["name"];
    const char* kind = p["kind"];
    int pin          = p["pin"] | -1;
    if (!name || !kind || pin < 0) continue;

    if (strcmp(kind, "ds18b20") == 0) {
      manager.add(new DS18B20Sensor(name, (uint8_t)pin), "ds18b20", pin);
      Serial.printf("NVS: restored ds18b20 '%s' on pin %d\n", name, pin);
    } else if (strcmp(kind, "relay") == 0) {
      manager.add(new RelayActuator(name, (uint8_t)pin), "relay", pin);
      Serial.printf("NVS: restored relay '%s' on pin %d\n", name, pin);
    }
  }
}

static void initNormalOperation()
{
  restorePeripherals();
  manager.beginAll();
  mqttClient.begin(manager);
  normalOperationInitDone = true;
}

static void sensorTick()
{
  mqttClient.loop();
  time_t now = time(nullptr);
  String payload = manager.tickAll(now, millis());
  if (!payload.isEmpty())
    mqttClient.publishReading(payload);
}

// ─── state dispatch ───────────────────────────────────────────────────────────

static void runState()
{
  switch (state) {
    case State::PROVISIONING:
      startProvisioning(); // never returns — reboots on success
      break;

    case State::CONNECT_WIFI:
      if (connectWifi()) {
        state = State::NTP_SYNC;
      } else {
        enterErrorRetry(State::CONNECT_WIFI, "Wi-Fi connection failed");
      }
      break;

    case State::NTP_SYNC:
      if (waitForNtp()) {
        state = State::NORMAL_OPERATION;
      } else {
        enterErrorRetry(State::CONNECT_WIFI, "NTP sync failed");
      }
      break;

    case State::NORMAL_OPERATION:
      if (!normalOperationInitDone)
        initNormalOperation();
      sensorTick();
      break;

    case State::ERROR_RETRY:
      if (millis() >= errorRetryUntil)
        state = errorNextState;
      break;
  }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup()
{
  Serial.begin(115200);
  Serial.println("FishHub firmware booting...");
  nvsStore.begin();
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  Serial.println("NVS key status:");
  for (const char *key : {"wifi_ssid", "wifi_pass", "device_id", "device_jwt",
                          "mqtt_username", "mqtt_host", "provisioned"})
  {
    Serial.printf("  NVS %-14s %s\n", key,
                  nvsStore.get(key).isEmpty() ? "MISSING" : "present");
  }

  state = nvsStore.isProvisioned() ? State::CONNECT_WIFI : State::PROVISIONING;
}

void loop()
{
  checkButton();
  runState();
}
