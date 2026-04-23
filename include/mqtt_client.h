#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "peripheral_manager.h"

class FishHubMqttClient {
public:
  void begin(PeripheralManager& manager);
  void loop();

private:
  void connect();
  void onMessage(char* topic, byte* payload, unsigned int len);

  WiFiClientSecure  _tlsClient;
  PubSubClient      _client;
  PeripheralManager* _manager = nullptr;
  String            _deviceId;
  String            _deviceJwt;
  unsigned long     _lastConnectAttempt = 0;
};
