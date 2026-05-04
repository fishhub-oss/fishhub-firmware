#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "peripheral_manager.h"

class FishHubMqttClient {
public:
  void begin(PeripheralManager& manager);
  void loop();
  bool publishReading(const String& payload);

private:
  void connect();
  void onMessage(char* topic, byte* payload, unsigned int len);
  void onConfig(byte* payload, unsigned int len);
  void onPeripheralConfig(const String& name, byte* payload, unsigned int len);

  WiFiClientSecure   _tlsClient;
  PubSubClient       _client;
  PeripheralManager* _manager = nullptr;
  String             _deviceId;
  String             _mqttUsername;
  String             _mqttPassword;
  String             _mqttHost;
  uint16_t           _mqttPort = 8883;
  unsigned long      _lastConnectAttempt = 0;
};
