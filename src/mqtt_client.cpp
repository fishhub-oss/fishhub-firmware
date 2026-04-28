#include "mqtt_client.h"
#include "nvs_store.h"
#include "config.h"
#include "peripherals/relay_actuator.h"
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

void FishHubMqttClient::begin(PeripheralManager& manager) {
  _manager = &manager;

  _deviceId    = nvsStore.get("device_id");
  if (_deviceId.isEmpty()) _deviceId = DEVICE_ID;
  _mqttUsername = nvsStore.get("mqtt_username");
  _mqttPassword = nvsStore.get("mqtt_password");

  _mqttHost = nvsStore.get("mqtt_host");
  if (_mqttHost.isEmpty()) _mqttHost = MQTT_HOST;
  _mqttPort = MQTT_PORT;

  _tlsClient.setCACert(ISRG_ROOT_X1);

  _client.setClient(_tlsClient);
  _client.setBufferSize(1024);
  _client.setServer(_mqttHost.c_str(), _mqttPort);
  _client.setCallback([this](char* topic, byte* payload, unsigned int len) {
    onMessage(topic, payload, len);
  });

  connect();
  _lastConnectAttempt = millis();
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
  if (_deviceId.isEmpty() || _mqttUsername.isEmpty() || _mqttPassword.isEmpty()) {
    Serial.println("MQTT: device_id, mqtt_username, or mqtt_password missing — skipping connect");
    return;
  }

  Serial.printf("MQTT: connecting as %s...\n", _mqttUsername.c_str());
  bool ok = _client.connect(
    _deviceId.c_str(),      // client ID
    _mqttUsername.c_str(),  // username
    _mqttPassword.c_str()   // password
  );

  if (!ok) {
    Serial.printf("MQTT: connect failed, rc=%d\n", _client.state());
    return;
  }

  String cmdTopic         = "fishhub/" + _deviceId + "/commands/#";
  String peripheralsTopic = "fishhub/" + _deviceId + "/peripherals/#";
  _client.subscribe(cmdTopic.c_str());
  _client.subscribe(peripheralsTopic.c_str());
  Serial.printf("MQTT: connected, subscribed to commands and peripherals topics\n");
}

void FishHubMqttClient::onMessage(char* topic, byte* payload, unsigned int len) {
  String t(topic);
  String prefix = "fishhub/" + _deviceId + "/";
  if (!t.startsWith(prefix)) return;

  String rest  = t.substring(prefix.length()); // e.g. "commands/light" or "peripherals/light"
  int slash    = rest.indexOf('/');
  if (slash < 0) return;

  String segment = rest.substring(0, slash);
  String name    = rest.substring(slash + 1);

  if (segment == "peripherals") {
    onPeripheralConfig(name, payload, len);
    return;
  }

  if (segment != "commands") return;

  // Command dispatch
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

  Peripheral* peripheral = _manager->find(name);
  if (!peripheral) {
    Serial.printf("MQTT: no peripheral named '%s'\n", name.c_str());
    return;
  }

  if (!peripheral->replayCommand()) {
    String nvsKey = String("cmd_") + name;
    String lastId = nvsStore.get(nvsKey.c_str());
    if (lastId == cmdId) {
      Serial.printf("MQTT: duplicate command id=%s for %s — skipped\n",
                    cmdId, name.c_str());
      return;
    }
    nvsStore.set(nvsKey.c_str(), String(cmdId));
  }

  Serial.printf("MQTT: dispatching command id=%s to %s\n", cmdId, name.c_str());
  _manager->dispatchCommand(name, doc.as<JsonObjectConst>());
}

void FishHubMqttClient::onPeripheralConfig(const String& name, byte* payload, unsigned int len) {
  if (len == 0) return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) {
    Serial.printf("MQTT: bad JSON on peripherals/%s\n", name.c_str());
    return;
  }

  const char* op = doc["op"];
  if (!op) return;

  if (strcmp(op, "delete") == 0) {
    Serial.printf("MQTT: removing peripheral '%s'\n", name.c_str());
    _manager->remove(name);
    return;
  }

  if (strcmp(op, "create") == 0) {
    if (_manager->has(name)) {
      Serial.printf("MQTT: peripheral '%s' already registered — skipping\n", name.c_str());
      return;
    }
    const char* kind = doc["kind"];
    int pin = doc["pin"] | -1;
    if (!kind || pin < 0) {
      Serial.printf("MQTT: peripheral '%s' missing kind or pin\n", name.c_str());
      return;
    }
    if (strcmp(kind, "relay") == 0) {
      _manager->add(new RelayActuator(name.c_str(), (uint8_t)pin));
      Serial.printf("MQTT: registered relay '%s' on pin %d\n", name.c_str(), pin);
    }
  }
}
