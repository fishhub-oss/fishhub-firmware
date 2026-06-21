#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include "pins.h"
#include "version.h"
#include "ota_update.h"
#include "nvs_store.h"
#include "provisioning.h"
#include "wifi_ntp.h"
#include "mqtt_client.h"
#include "peripheral_manager.h"
#include "peripherals/ds18b20_sensor.h"
#include "peripherals/relay_actuator.h"
#include "peripherals/servo_cr.h"
#include "peripherals/servo_positional.h"
#include "peripheral_serializer_registry.h"
#include "trigger.h"
#include "trigger_event_queue.h"
#include "button.h"
#include "display/oled_display.h"

// ─── state machine ────────────────────────────────────────────────────────────

enum class State
{
  PROVISIONING,
  CONNECT_WIFI,
  NTP_SYNC,
  NORMAL_OPERATION,
  UPDATE_FIRMWARE,
  ERROR_RETRY,
};

static State state;

static unsigned long errorRetryUntil = 0;
static State errorNextState = State::CONNECT_WIFI;

static const unsigned long ERROR_RETRY_DELAY_MS = 10000;

static const char *stateName(State s)
{
  switch (s)
  {
  case State::PROVISIONING:
    return "PROVISIONING";
  case State::CONNECT_WIFI:
    return "CONNECT_WIFI";
  case State::NTP_SYNC:
    return "NTP_SYNC";
  case State::NORMAL_OPERATION:
    return "NORMAL_OPERATION";
  case State::UPDATE_FIRMWARE:
    return "UPDATE_FIRMWARE";
  case State::ERROR_RETRY:
    return "ERROR_RETRY";
  }
  return "UNKNOWN";
}

static void setState(State next)
{
  if (next != state)
  {
    Serial.printf("State: %s → %s\n", stateName(state), stateName(next));
    state = next;
    oledDisplay.setCurrentState(stateName(next));
  }
}

static void enterErrorRetry(State next, const char *reason)
{
  Serial.printf("Error: %s — retrying in %lu s\n", reason, ERROR_RETRY_DELAY_MS / 1000);
  errorRetryUntil = millis() + ERROR_RETRY_DELAY_MS;
  errorNextState = next;
  setState(State::ERROR_RETRY);
}

// Forward declaration — mqttClient is defined in the normal operation section below.
static FishHubMqttClient mqttClient;

// ─── OTA update ───────────────────────────────────────────────────────────────

static const int OTA_MAX_BOOT_ATTEMPTS = 3;

struct OtaParams { String url; String sha256; };
static OtaParams pendingOta;

static void startOtaUpdate(const String& url, const String& sha256) {
  pendingOta = {url, sha256};
  setState(State::UPDATE_FIRMWARE);
}

// Called early in setup() — rolls back to the previous OTA slot if the new
// firmware has failed to confirm healthy (MQTT connect) within the allowed boots.
static void checkOtaRollback() {
  if (nvsStore.get("fw_pending") != "1") return;

  int count = nvsStore.get("fw_boot_count").toInt() + 1;
  Serial.printf("[OTA] Boot attempt %d/%d after update\n", count, OTA_MAX_BOOT_ATTEMPTS);

  if (count < OTA_MAX_BOOT_ATTEMPTS) {
    nvsStore.set("fw_boot_count", String(count));
    return;
  }

  Serial.println("[OTA] Max boot attempts reached — rolling back");
  String prevLabel = nvsStore.get("fw_prev_part");
  nvsStore.remove("fw_pending");
  nvsStore.remove("fw_boot_count");
  nvsStore.remove("fw_prev_part");

  if (!prevLabel.isEmpty()) {
    const esp_partition_t* prev = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, prevLabel.c_str());
    if (prev) {
      esp_ota_set_boot_partition(prev);
      Serial.println("[OTA] Rebooting into previous firmware");
      delay(100);
      ESP.restart();
    }
  }
  Serial.println("[OTA] Rollback failed — previous partition not found");
}

static void runUpdateFirmware() {
  Serial.println("[OTA] Entering UPDATE_FIRMWARE state");

  // Save the current partition label for rollback before touching anything.
  const esp_partition_t* current = esp_ota_get_running_partition();

  bool ok = performOtaUpdate(pendingOta.url, pendingOta.sha256);

  if (!ok) {
    Serial.println("[OTA] Update failed — returning to NORMAL_OPERATION");
    mqttClient.publishStatus("failed:flash");
    setState(State::NORMAL_OPERATION);
    return;
  }

  // Flash succeeded. Set rollback guards before rebooting so the new firmware
  // must confirm health (MQTT connect) within OTA_MAX_BOOT_ATTEMPTS boots.
  nvsStore.set("fw_prev_part", String(current->label));
  nvsStore.set("fw_pending",   "1");
  nvsStore.set("fw_boot_count","0");
  Serial.println("[OTA] Rebooting into new firmware");
  delay(500);
  ESP.restart();
}

// ─── normal operation helpers ─────────────────────────────────────────────────

static PeripheralManager manager;
static TriggerEventQueue eventQueue;
static bool normalOperationInitDone = false;

static void restorePeripherals()
{
  String json = nvsStore.get("peripherals");
  if (json.isEmpty())
    return;

  JsonDocument doc;
  if (deserializeJson(doc, json))
  {
    Serial.println("NVS: failed to parse peripherals JSON — skipping restore");
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject p : arr)
  {
    const char *name = p["name"];
    const char *kind = p["kind"];
    int pin = p["pin"] | -1;
    if (!name || !kind || pin < 0)
      continue;

    Peripheral *peripheral = peripheralSerializerRegistry.deserialize(kind, name, p);
    if (!peripheral)
    {
      Serial.printf("NVS: failed to restore peripheral '%s' (kind '%s')\n", name, kind);
      continue;
    }
    manager.add(peripheral, pin);
    Serial.printf("NVS: restored %s '%s' on pin %d\n", kind, name, pin);
  }
}

static void restoreTriggers()
{
  String idxJson = nvsStore.get("trig_index");
  if (idxJson.isEmpty())
    return;

  JsonDocument idxDoc;
  if (deserializeJson(idxDoc, idxJson))
  {
    Serial.println("NVS: failed to parse trig_index — skipping trigger restore");
    return;
  }

  for (JsonVariant v : idxDoc.as<JsonArray>())
  {
    String prefix = v.as<String>();
    String key = String("tr_") + prefix;
    String json = nvsStore.get(key.c_str());
    if (json.isEmpty())
      continue;

    JsonDocument doc;
    if (deserializeJson(doc, json))
    {
      Serial.printf("NVS: failed to parse trigger '%s' — skipping\n", prefix.c_str());
      continue;
    }

    Trigger *t = new Trigger();
    if (t->load(doc.as<JsonObjectConst>()))
    {
      manager.addTrigger(t);
      Serial.printf("NVS: restored trigger '%s'\n", t->id().c_str());
    }
    else
    {
      delete t;
    }
  }
}

static void initNormalOperation()
{
  peripheralSerializerRegistry.registerKind("relay",          new RelaySerializer());
  peripheralSerializerRegistry.registerKind("ds18b20",        new DS18B20Serializer());
  peripheralSerializerRegistry.registerKind("servo_cr",         new ServoCRSerializer());
  peripheralSerializerRegistry.registerKind("servo_positional", new ServoPositionalSerializer());

  restorePeripherals();
  restoreTriggers();
  manager.beginAll();
  manager.setEventQueue(eventQueue);
  mqttClient.begin(manager, eventQueue);
  normalOperationInitDone = true;
}

static void sensorTick()
{
  unsigned long t0 = millis();
  mqttClient.loop();

  // Safe-point gate: trigger OTA only when no peripheral has uncommitted state
  // and the trigger event queue is fully drained.
  if (mqttClient.hasPendingUpdate() && !manager.anyBusy() && eventQueue.empty()) {
    String url    = mqttClient.pendingUpdateUrl();
    String sha256 = mqttClient.pendingUpdateSha256();
    String nonce  = mqttClient.pendingUpdateNonce();
    mqttClient.clearPendingUpdate();
    // Persist nonce before flashing so a failed flash doesn't re-trigger on reboot.
    nvsStore.set("fw_nonce", nonce);
    mqttClient.publishStatus("updating");
    startOtaUpdate(url, sha256);
    return;
  }
  unsigned long t1 = millis();

  time_t now = time(nullptr);
  String payload = manager.tickAll(now, millis());
  unsigned long t2 = millis();

  if (!payload.isEmpty())
  {
    mqttClient.publishReading(payload);

    // Refresh MEASUREMENTS slide data from the current peripheral snapshot
    std::vector<ReadingEntry> entries;
    std::map<std::string, float> snap;
    manager.forEach([&snap](PeripheralEntry &e)
                    { e.peripheral->currentMeasurements(snap); });
    for (auto &kv : snap)
    {
      ReadingEntry e;
      e.name = String(kv.first.c_str());
      e.value = String(kv.second, 2);
      entries.push_back(e);
    }
    oledDisplay.updateReadings(entries);
  }
  unsigned long t3 = millis();

  mqttClient.drainEventQueue();
  unsigned long t4 = millis();

  if (t4 - t0 > 100)
    Serial.printf("DBG [tick timing]: loop=%lums tickAll=%lums publish=%lums drain=%lums total=%lums\n",
                  t1 - t0, t2 - t1, t3 - t2, t4 - t3, t4 - t0);
}

// ─── state dispatch ───────────────────────────────────────────────────────────

static void runState()
{
  switch (state)
  {
  case State::PROVISIONING:
    startProvisioning(); // never returns — reboots on success
    break;

  case State::CONNECT_WIFI:
    if (connectWifi())
    {
      setState(State::NTP_SYNC);
    }
    else
    {
      enterErrorRetry(State::CONNECT_WIFI, "Wi-Fi connection failed");
    }
    break;

  case State::NTP_SYNC:
    if (waitForNtp())
    {
      setState(State::NORMAL_OPERATION);
    }
    else
    {
      enterErrorRetry(State::CONNECT_WIFI, "NTP sync failed");
    }
    break;

  case State::NORMAL_OPERATION:
    if (!normalOperationInitDone)
      initNormalOperation();
    sensorTick();
    break;

  case State::UPDATE_FIRMWARE:
    runUpdateFirmware();
    break;

  case State::ERROR_RETRY:
    if (millis() >= errorRetryUntil)
      setState(errorNextState);
    break;
  }
}

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup()
{
  Serial.begin(115200);
  Serial.printf("FishHub firmware booting — version %s\n", FIRMWARE_VERSION);
  nvsStore.begin();
  checkOtaRollback();
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DISPLAY_BUTTON_PIN, INPUT_PULLDOWN);
  oledDisplay.begin();

  Serial.println("NVS key status:");
  for (const char *key : {"wifi_ssid", "wifi_pass", "device_id", "device_jwt",
                          "mqtt_username", "mqtt_host", "provisioned"})
  {
    Serial.printf("  NVS %-14s %s\n", key,
                  nvsStore.get(key).isEmpty() ? "MISSING" : "present");
  }

  state = nvsStore.isProvisioned() ? State::CONNECT_WIFI : State::PROVISIONING;
  Serial.printf("State: initial → %s\n", stateName(state));
  oledDisplay.setCurrentState(stateName(state));
}

void loop()
{
  checkButton();
  checkDisplayButton();
  runState();
  oledDisplay.tick(millis());
}
