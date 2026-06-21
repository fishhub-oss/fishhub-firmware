#include "mqtt_client.h"
#include "nvs_store.h"
#include "config_defaults.h"
#include "version.h"
#include "semver.h"
#include "peripherals/relay_actuator.h"
#include "peripherals/ds18b20_sensor.h"
#include "peripheral_serializer_registry.h"
#include "trigger.h"
#include "trigger_event_queue.h"
#include "display/oled_display.h"
#include <ArduinoJson.h>

static const unsigned long RECONNECT_INTERVAL_MS = 5000;

// ISRG Root X1 — root CA for Let's Encrypt, used by HiveMQ Cloud
static const char ISRG_ROOT_X1[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";

void FishHubMqttClient::begin(PeripheralManager &manager, TriggerEventQueue &eventQueue)
{
  _manager = &manager;
  _eventQueue = &eventQueue;

  _deviceId = nvsStore.get("device_id");
  if (_deviceId.isEmpty())
    _deviceId = DEVICE_ID;
  _mqttUsername = nvsStore.get("mqtt_username");
  _mqttPassword = nvsStore.get("mqtt_password");

  _mqttHost = nvsStore.get("mqtt_host");
  if (_mqttHost.isEmpty())
    _mqttHost = MQTT_HOST;
  _mqttPort = MQTT_PORT;

#ifdef MQTT_TLS
  _tlsClient.setCACert(ISRG_ROOT_X1);
  _client.setClient(_tlsClient);
#else
  _client.setClient(_plainClient);
#endif
  _client.setBufferSize(1024);
  _client.setKeepAlive(30);
  _client.setServer(_mqttHost.c_str(), _mqttPort);
  _client.setCallback([this](char *topic, byte *payload, unsigned int len)
                      { onMessage(topic, payload, len); });

  connect();
  _lastConnectAttempt = millis();
}

void FishHubMqttClient::loop()
{
  if (!_client.connected())
  {
    unsigned long now = millis();
    if (now - _lastConnectAttempt >= RECONNECT_INTERVAL_MS)
    {
      _lastConnectAttempt = now;
      Serial.printf("MQTT: disconnected (state=%d) — reconnecting\n", _client.state());
      oledDisplay.logEvent("~ mqtt reconnect");
      connect();
    }
  }

  _client.loop();
  drainEventQueue();
}

void FishHubMqttClient::drainEventQueue()
{
  if (!_eventQueue || !_client.connected())
    return;

  while (!_eventQueue->empty())
  {
    const TriggerEvent *ev = _eventQueue->front();

    char topic[80];
    snprintf(topic, sizeof(topic), "fishhub/%s/trigger_events", _deviceId.c_str());

    JsonDocument doc;
    doc["trigger_event_id"] = ev->triggerEventId;
    doc["trigger_id"] = ev->triggerId;
    doc["device_id"] = _deviceId.c_str();

    char firedAtBuf[32];
    struct tm *utc = gmtime(&ev->firedAt);
    strftime(firedAtBuf, sizeof(firedAtBuf), "%Y-%m-%dT%H:%M:%SZ", utc);
    doc["fired_at"] = firedAtBuf;

    JsonArray readings = doc["readings"].to<JsonArray>();
    for (size_t i = 0; i < ev->readingCount; i++)
    {
      JsonObject r = readings.add<JsonObject>();
      r["peripheral"] = ev->readings[i].peripheral;
      r["value"] = ev->readings[i].value;
    }

    char payload[512];
    size_t payloadLen = serializeJson(doc, payload, sizeof(payload));

    Serial.printf("MQTT: publishing trigger_event to '%s' (%u bytes)\n", topic, (unsigned)payloadLen);
    if (_client.publish(topic, payload, /*retained=*/false))
    {
      Serial.println("MQTT: trigger_event published OK");
      _eventQueue->pop();
    }
    else
    {
      Serial.printf("MQTT: trigger_event publish failed (state=%d) — will retry next loop\n", _client.state());
      break;
    }
  }
}

void FishHubMqttClient::connect()
{
  if (_deviceId.isEmpty() || _mqttUsername.isEmpty() || _mqttPassword.isEmpty())
  {
    Serial.println("MQTT: device_id, mqtt_username, or mqtt_password missing — skipping connect");
    return;
  }

  Serial.printf("MQTT: connecting as %s...\n", _mqttUsername.c_str());
  bool ok = _client.connect(
      _deviceId.c_str(),     // client ID
      _mqttUsername.c_str(), // username
      _mqttPassword.c_str()  // password
  );

  if (!ok)
  {
    Serial.printf("MQTT: connect failed, rc=%d\n", _client.state());
    return;
  }

  String cmdTopic = "fishhub/" + _deviceId + "/commands/#";
  String peripheralsTopic = "fishhub/" + _deviceId + "/peripherals/#";
  String configTopic = "fishhub/" + _deviceId + "/config";
  String triggersTopic = "fishhub/" + _deviceId + "/triggers/#";
  String firmwareTopic = "fishhub/" + _deviceId + "/firmware";
  _client.subscribe(cmdTopic.c_str());
  _client.subscribe(peripheralsTopic.c_str());
  _client.subscribe(configTopic.c_str());
  _client.subscribe(triggersTopic.c_str());
  _client.subscribe(firmwareTopic.c_str());
  Serial.printf("MQTT: connected, subscribed to commands, peripherals, config, triggers and firmware topics\n");

  // Confirm OTA health — new firmware reached MQTT connect, so it's good.
  if (nvsStore.get("fw_pending") == "1") {
    nvsStore.remove("fw_pending");
    nvsStore.remove("fw_boot_count");
    nvsStore.remove("fw_prev_part");
    Serial.println("[OTA] Firmware validated — MQTT connected");
    publishStatus("ok");
    return; // publishStatus already called; skip the default call below
  }

  publishStatus();
}

void FishHubMqttClient::onMessage(char *topic, byte *payload, unsigned int len)
{
  String t(topic);
  String prefix = "fishhub/" + _deviceId + "/";
  if (!t.startsWith(prefix))
    return;

  String rest = t.substring(prefix.length()); // e.g. "commands/light", "peripherals/light", "config"

  if (rest == "config")
  {
    onConfig(payload, len);
    return;
  }

  if (rest == "firmware")
  {
    onFirmwareManifest(payload, len);
    return;
  }

  int slash = rest.indexOf('/');
  if (slash < 0)
    return;

  String segment = rest.substring(0, slash);
  String name = rest.substring(slash + 1);

  if (segment == "peripherals")
  {
    onPeripheralConfig(name, payload, len);
    return;
  }

  if (segment == "triggers")
  {
    onTriggerConfig(name, payload, len);
    return;
  }

  if (segment != "commands")
    return;

  // Command dispatch
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err)
  {
    Serial.printf("MQTT: JSON parse error on topic %s: %s\n", topic, err.c_str());
    return;
  }

  const char *cmdId = doc["id"];
  if (!cmdId || strlen(cmdId) == 0)
  {
    Serial.printf("MQTT: command missing 'id' field on topic %s — ignored\n", topic);
    return;
  }

  Peripheral *peripheral = _manager->find(name);
  if (!peripheral)
  {
    Serial.printf("MQTT: no peripheral named '%s'\n", name.c_str());
    return;
  }

  if (!peripheral->replayCommand())
  {
    String nvsKey = String("cmd_") + name;
    String lastId = nvsStore.get(nvsKey.c_str());
    if (lastId == cmdId)
    {
      Serial.printf("MQTT: duplicate command id=%s for %s — skipped\n",
                    cmdId, name.c_str());
      return;
    }
    nvsStore.set(nvsKey.c_str(), String(cmdId));
  }

  Serial.printf("MQTT: dispatching command id=%s to %s\n", cmdId, name.c_str());
  {
    // Build a compact one-liner for the debug display
    const char* cmdType = doc["command"] | (doc["windows"] ? "schedule" : "set");
    String evLine = String("< cmd ") + name + " " + cmdType;
    if (doc["value"].is<int>() || doc["value"].is<float>())
      evLine += "=" + String(doc["value"].as<float>(), 1);
    oledDisplay.logEvent(evLine);
  }
  _manager->dispatchCommand(name, doc.as<JsonObjectConst>());
}

// Persist the current peripheral list to NVS as a JSON array.
static void savePeripheralsToNVS(PeripheralManager *mgr)
{
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  mgr->forEach([&arr](PeripheralEntry &e) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = e.peripheral->name();
    obj["kind"] = e.peripheral->kind();
    obj["pin"]  = e.pin;
    peripheralSerializerRegistry.serialize(e.peripheral, obj);
  });
  String json;
  serializeJson(doc, json);
  nvsStore.set("peripherals", json);
}

// Returns "tr_" + first 10 chars of trigger id — fits the 15-char NVS key limit.
static String triggerNvsKey(const std::string &id)
{
  return String("tr_") + id.substr(0, 10).c_str();
}

static void saveTriggerToNVS(Trigger *t)
{
  JsonDocument doc;
  t->serializeTo(doc.as<JsonObject>());
  String json;
  serializeJson(doc, json);
  nvsStore.set(triggerNvsKey(t->id()).c_str(), json);

  String idxJson = nvsStore.get("trig_index");
  JsonDocument idxDoc;
  if (idxJson.isEmpty() || deserializeJson(idxDoc, idxJson))
  {
    idxDoc.to<JsonArray>();
  }
  JsonArray idx = idxDoc.as<JsonArray>();
  String prefix = String(t->id().substr(0, 10).c_str());
  for (JsonVariant v : idx)
  {
    if (v.as<String>() == prefix)
      return;
  }
  idx.add(prefix);
  String newIdx;
  serializeJson(idxDoc, newIdx);
  nvsStore.set("trig_index", newIdx);
}

static void removeTriggerFromNVS(const String &id)
{
  std::string sid = id.c_str();
  nvsStore.remove(triggerNvsKey(sid).c_str());

  String idxJson = nvsStore.get("trig_index");
  if (idxJson.isEmpty())
    return;
  JsonDocument idxDoc;
  if (deserializeJson(idxDoc, idxJson))
    return;

  String prefix = String(sid.substr(0, 10).c_str());
  JsonDocument newDoc;
  JsonArray dst = newDoc.to<JsonArray>();
  for (JsonVariant v : idxDoc.as<JsonArray>())
  {
    if (v.as<String>() != prefix)
      dst.add(v);
  }
  String newIdx;
  serializeJson(newDoc, newIdx);
  nvsStore.set("trig_index", newIdx);
}

bool FishHubMqttClient::publishReading(const String &payload)
{
  if (!_client.connected())
  {
    Serial.println("MQTT: not connected — skipping publish");
    return false;
  }
  String topic = "fishhub/" + _deviceId + "/readings";
  bool ok = _client.publish(topic.c_str(), payload.c_str(), false);
  if (!ok) {
    Serial.println("MQTT: publishReading failed");
  } else {
    // Build compact display summary from first non-base SenML record
    JsonDocument doc;
    if (!deserializeJson(doc, payload)) {
      JsonArray arr = doc.as<JsonArray>();
      for (JsonObject rec : arr) {
        if (rec["bn"]) continue; // skip base record
        String n = rec["n"] | "?";
        String evLine;
        if (rec["v"].is<float>() || rec["v"].is<int>()) {
          evLine = String("> ") + n + " " + String(rec["v"].as<float>(), 1);
        } else if (!rec["vb"].isNull()) {
          evLine = String("> ") + n + " " + (rec["vb"].as<bool>() ? "ON" : "OFF");
        } else {
          evLine = String("> ") + n;
        }
        oledDisplay.logEvent(evLine);
        break;
      }
    }
  }
  return ok;
}

void FishHubMqttClient::publishStatus(const char *lastUpdateResult)
{
  if (!_client.connected())
    return;

  JsonDocument doc;
  doc["firmware_version"] = FIRMWARE_VERSION;
  if (lastUpdateResult && strlen(lastUpdateResult) > 0)
    doc["last_update_result"] = lastUpdateResult;

  char payload[128];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  String topic = "fishhub/" + _deviceId + "/status";
  bool ok = _client.publish(topic.c_str(), (const uint8_t *)payload, len, /*retained=*/true);
  Serial.printf("MQTT: publishStatus %s (%s)\n", payload, ok ? "OK" : "FAILED");
}

void FishHubMqttClient::onConfig(byte *payload, unsigned int len)
{
  if (len == 0)
    return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, len))
  {
    Serial.println("MQTT: bad JSON on config topic");
    return;
  }

  const char *tz = doc["timezone"];
  if (!tz || strlen(tz) == 0)
  {
    Serial.println("MQTT: config message missing 'timezone' — ignored");
    return;
  }

  nvsStore.writeTimezone(String(tz));
  setenv("TZ", tz, 1);
  tzset();
  Serial.printf("MQTT: timezone set to %s\n", tz);
}

void FishHubMqttClient::onPeripheralConfig(const String &name, byte *payload, unsigned int len)
{
  if (len == 0)
    return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, len))
  {
    Serial.printf("MQTT: bad JSON on peripherals/%s\n", name.c_str());
    return;
  }

  const char *op = doc["op"];
  if (!op)
    return;

  if (strcmp(op, "delete") == 0)
  {
    Serial.printf("MQTT: removing peripheral '%s'\n", name.c_str());
    _manager->remove(name);
    savePeripheralsToNVS(_manager);
    return;
  }

  if (strcmp(op, "create") == 0)
  {
    if (_manager->has(name))
    {
      Serial.printf("MQTT: peripheral '%s' already registered — skipping\n", name.c_str());
      return;
    }
    const char *kind = doc["kind"];
    int pin = doc["pin"] | -1;
    if (!kind || pin < 0)
    {
      Serial.printf("MQTT: peripheral '%s' missing kind or pin\n", name.c_str());
      return;
    }
    Peripheral *p = peripheralSerializerRegistry.deserialize(kind, name.c_str(), doc.as<JsonObjectConst>());
    if (!p)
    {
      Serial.printf("MQTT: unknown or invalid peripheral kind '%s'\n", kind);
      return;
    }
    _manager->add(p, pin);
    Serial.printf("MQTT: registered %s '%s' on pin %d\n", kind, name.c_str(), pin);
    savePeripheralsToNVS(_manager);
  }
}

void FishHubMqttClient::onTriggerConfig(const String &id, byte *payload, unsigned int len)
{
  if (len == 0)
    return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, len))
  {
    Serial.printf("MQTT: bad JSON on triggers/%s\n", id.c_str());
    return;
  }

  const char *op = doc["op"];
  if (!op)
    return;

  if (strcmp(op, "delete") == 0)
  {
    _manager->removeTrigger(id.c_str());
    removeTriggerFromNVS(id);
    Serial.printf("MQTT: removed trigger '%s'\n", id.c_str());
    oledDisplay.logEvent(String("< trig ") + id.substring(0, 6) + " del");
    return;
  }

  if (strcmp(op, "upsert") == 0)
  {
    Trigger *existing = _manager->findTrigger(id.c_str());
    if (existing)
    {
      existing->load(doc.as<JsonObjectConst>());
      Serial.printf("MQTT: updated trigger '%s'\n", id.c_str());
    }
    else
    {
      Trigger *t = new Trigger();
      t->load(doc.as<JsonObjectConst>());
      _manager->addTrigger(t);
      Serial.printf("MQTT: registered trigger '%s'\n", id.c_str());
    }
    saveTriggerToNVS(_manager->findTrigger(id.c_str()));
    oledDisplay.logEvent(String("< trig ") + id.substring(0, 6) + " upsert");
    return;
  }

  Serial.printf("MQTT: unknown op '%s' on triggers/%s\n", op, id.c_str());
}

void FishHubMqttClient::onFirmwareManifest(byte* payload, unsigned int len)
{
  if (len == 0)
    return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, len))
  {
    Serial.println("[OTA] Bad JSON on firmware topic — ignored");
    return;
  }

  const char* version = doc["version"];
  const char* url     = doc["url"];
  const char* sha256  = doc["sha256"];
  const char* nonce   = doc["nonce"];

  if (!version || !url || !sha256 || !nonce)
  {
    Serial.println("[OTA] Firmware manifest missing required fields — ignored");
    return;
  }

  // Nonce dedup: ignore if we already processed this nonce.
  String storedNonce = nvsStore.get("fw_nonce");
  if (storedNonce == nonce)
  {
    Serial.printf("[OTA] Manifest nonce %s already processed — ignored\n", nonce);
    return;
  }

  // Semver check: ignore if the manifest version is not newer than current.
  if (!semverGreater(version, FIRMWARE_VERSION))
  {
    Serial.printf("[OTA] Manifest version %s is not newer than current %s — ignored\n",
                  version, FIRMWARE_VERSION);
    return;
  }

  Serial.printf("[OTA] Newer firmware available: %s (current: %s)\n", version, FIRMWARE_VERSION);
  _pendingUpdateUrl    = String(url);
  _pendingUpdateSha256 = String(sha256);
  _pendingUpdateNonce  = String(nonce);
  _hasPendingUpdate    = true;
}
