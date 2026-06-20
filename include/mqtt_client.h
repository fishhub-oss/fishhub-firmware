#pragma once

#include "config_defaults.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include "peripheral_manager.h"
#include "../src/trigger_event_queue.h"

class FishHubMqttClient {
public:
  void begin(PeripheralManager& manager, TriggerEventQueue& eventQueue);
  void loop();
  void drainEventQueue();
  bool publishReading(const String& payload);
  void publishStatus(const char* lastUpdateResult = nullptr);

private:
  void connect();
  void onMessage(char* topic, byte* payload, unsigned int len);
  void onConfig(byte* payload, unsigned int len);
  void onPeripheralConfig(const String& name, byte* payload, unsigned int len);
  void onTriggerConfig(const String& id, byte* payload, unsigned int len);

#ifdef MQTT_TLS
  WiFiClientSecure    _tlsClient;
#else
  WiFiClient          _plainClient;
#endif
  PubSubClient        _client;
  PeripheralManager*  _manager    = nullptr;
  TriggerEventQueue*  _eventQueue = nullptr;
  String              _deviceId;
  String              _mqttUsername;
  String              _mqttPassword;
  String              _mqttHost;
  uint16_t            _mqttPort           = 8883;
  unsigned long       _lastConnectAttempt = 0;
};
