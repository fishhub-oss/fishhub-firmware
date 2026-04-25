#include "mqtt_client.h"
#include "nvs_store.h"
#include "config.h"
#include <ArduinoJson.h>

static const unsigned long RECONNECT_INTERVAL_MS = 5000;

// Parse host and port out of "mqtts://host:port"
static void parseMqttUrl(const String& url, String& host, uint16_t& port) {
  int start = url.indexOf("://");
  start = (start < 0) ? 0 : start + 3;
  int colon = url.lastIndexOf(':');
  if (colon > start) {
    host = url.substring(start, colon);
    port = (uint16_t)url.substring(colon + 1).toInt();
  } else {
    host = url.substring(start);
    port = 8883;
  }
}

void FishHubMqttClient::begin(PeripheralManager& manager) {
  _manager = &manager;

  _deviceId  = nvsStore.get("device_id");
  _deviceJwt = nvsStore.get("device_jwt");
  if (_deviceId.isEmpty())  _deviceId  = DEVICE_ID;
  if (_deviceJwt.isEmpty()) _deviceJwt = DEVICE_TOKEN;

  String mqttUrl = nvsStore.get("mqtt_url");
  if (mqttUrl.isEmpty()) mqttUrl = MQTT_URL;

  String host;
  uint16_t port;
  parseMqttUrl(mqttUrl, host, port);

  // PoC: skip certificate verification — see issue #27 for proper CA pinning
  _tlsClient.setInsecure();

  _client.setClient(_tlsClient);
  _client.setServer(host.c_str(), port);
  _client.setCallback([this](char* topic, byte* payload, unsigned int len) {
    onMessage(topic, payload, len);
  });

  connect();
}

void FishHubMqttClient::loop() {
  if (!_client.connected()) {
    unsigned long now = millis();
    if (now - _lastConnectAttempt >= RECONNECT_INTERVAL_MS) {
      _lastConnectAttempt = now;
      connect();
    }
  }
  _client.loop();
}

void FishHubMqttClient::connect() {
  if (_deviceId.isEmpty() || _deviceJwt.isEmpty()) {
    Serial.println("MQTT: device_id or device_jwt missing — skipping connect");
    return;
  }

  Serial.printf("MQTT: connecting as %s...\n", _deviceId.c_str());
  // Authenticate with device JWT as password; broker verifies signature via JWKS
  bool ok = _client.connect(
    _deviceId.c_str(),  // client ID
    _deviceId.c_str(),  // username (must match JWT sub for HiveMQ topic filter)
    _deviceJwt.c_str()  // password — RS256 JWT verified by HiveMQ JWT IdP
  );

  if (!ok) {
    Serial.printf("MQTT: connect failed, rc=%d\n", _client.state());
    return;
  }

  String topic = "fishhub/" + _deviceId + "/commands/#";
  _client.subscribe(topic.c_str());
  Serial.printf("MQTT: connected, subscribed to %s\n", topic.c_str());
}

void FishHubMqttClient::onMessage(char* topic, byte* payload, unsigned int len) {
  // Extract peripheral name from last topic segment: fishhub/<id>/commands/<name>
  String t(topic);
  int lastSlash = t.lastIndexOf('/');
  if (lastSlash < 0) return;
  String peripheralName = t.substring(lastSlash + 1);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("MQTT: JSON parse error on topic %s: %s\n", topic, err.c_str());
    return;
  }

  const char* cmdId = doc["id"];
  if (!cmdId || strlen(cmdId) == 0) {
    Serial.printf("MQTT: command missing 'id' field on topic %s — ignored\n", topic);
    return;
  }

  // Find the peripheral to check its replay policy
  Peripheral* peripheral = _manager->find(peripheralName);
  if (!peripheral) {
    Serial.printf("MQTT: no peripheral named '%s'\n", peripheralName.c_str());
    return;
  }

  if (!peripheral->replayCommand()) {
    // Non-replayable: check last processed ID in NVS
    String nvsKey = String("cmd_") + peripheralName;
    String lastId = nvsStore.get(nvsKey.c_str());
    if (lastId == cmdId) {
      Serial.printf("MQTT: duplicate command id=%s for %s — skipped\n",
                    cmdId, peripheralName.c_str());
      return;
    }
    nvsStore.set(nvsKey.c_str(), String(cmdId));
  }

  Serial.printf("MQTT: dispatching command id=%s to %s\n", cmdId, peripheralName.c_str());
  _manager->dispatchCommand(peripheralName, doc.as<JsonObjectConst>());
}
