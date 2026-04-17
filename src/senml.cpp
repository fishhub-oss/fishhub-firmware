#include "senml.h"
#include <ArduinoJson.h>

String buildSenMLPayload(float tempCelsius, time_t timestamp) {
  JsonDocument doc;
  JsonObject record = doc.add<JsonObject>();
  record["bn"] = "fishhub/device/";
  record["bt"] = (long)timestamp;
  JsonObject entry = record["e"].add<JsonObject>();
  entry["n"] = "temperature";
  entry["u"] = "Cel";
  entry["v"] = tempCelsius;

  String output;
  serializeJson(doc, output);
  return output;
}
