#include "peripheral_manager.h"

#ifndef ARDUINO
// millis() not available on native — callers always inject nowMs
#endif

void PeripheralManager::add(Peripheral* p) {
  _entries.push_back({p, 0});
}

void PeripheralManager::beginAll() {
  for (auto& e : _entries) {
    e.peripheral->begin();
  }
}

String PeripheralManager::tickAll(time_t now, uint32_t nowMs) {
  JsonDocument doc;
  JsonArray records = doc.to<JsonArray>();

  bool anyData = false;
  for (auto& e : _entries) {
    if (nowMs - e.lastTickedAt >= e.peripheral->intervalMs()) {
      e.lastTickedAt = nowMs;
      if (e.peripheral->tick(now)) {
        e.peripheral->appendSenML(records, now);
        anyData = true;
      }
    }
  }

  if (!anyData) return String{};

  // Prepend base record now that we know there is data
  JsonDocument out;
  JsonArray result = out.to<JsonArray>();
  JsonObject base  = result.add<JsonObject>();
  base["bn"] = "fishhub/device/";
  base["bt"] = (long)now;
  for (JsonObject r : records) {
    result.add(r);
  }

  String payload;
  serializeJson(out, payload);
  return payload;

}

void PeripheralManager::dispatchCommand(const String& name, JsonObjectConst cmd) {
  for (auto& e : _entries) {
    if (name == e.peripheral->name()) {
      e.peripheral->applyCommand(cmd);
      return;
    }
  }
}

Peripheral* PeripheralManager::find(const String& name) {
  for (auto& e : _entries) {
    if (name == e.peripheral->name()) return e.peripheral;
  }
  return nullptr;
}
