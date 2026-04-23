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
  JsonObject base   = records.add<JsonObject>();
  base["bn"] = "fishhub/device/";
  base["bt"] = (long)now;
  JsonArray entries = base["e"].to<JsonArray>();

  bool anyData = false;
  for (auto& e : _entries) {
    if (nowMs - e.lastTickedAt >= e.peripheral->intervalMs()) {
      e.lastTickedAt = nowMs;
      if (e.peripheral->tick(now)) {
        e.peripheral->appendSenML(entries, now);
        anyData = true;
      }
    }
  }

  if (!anyData) return String{};

  String out;
  serializeJson(doc, out);
  return out;

}

void PeripheralManager::dispatchCommand(const String& name, JsonObjectConst cmd) {
  for (auto& e : _entries) {
    if (name == e.peripheral->name()) {
      e.peripheral->applyCommand(cmd);
      return;
    }
  }
}
