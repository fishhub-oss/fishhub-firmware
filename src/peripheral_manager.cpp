#include "peripheral_manager.h"

#ifndef ARDUINO
// millis() not available on native — callers always inject nowMs
#endif

void PeripheralManager::add(Peripheral* p, const char* kind, int pin) {
  PeripheralEntry e;
  e.peripheral   = p;
  e.lastTickedAt = 0;
  e.kind         = kind;
  e.pin          = pin;
  _entries.push_back(e);
  if (_begun) p->begin();
}

void PeripheralManager::forEach(std::function<void(Peripheral*, const char*, int)> fn) const {
  for (const auto& e : _entries) {
    fn(e.peripheral, e.kind, e.pin);
  }
}

void PeripheralManager::beginAll() {
  for (auto& e : _entries) {
    e.peripheral->begin();
  }
  _begun = true;
}

void PeripheralManager::remove(const String& name) {
  for (auto it = _entries.begin(); it != _entries.end(); ++it) {
    if (name == it->peripheral->name()) {
      delete it->peripheral;
      _entries.erase(it);
      return;
    }
  }
}

bool PeripheralManager::has(const String& name) const {
  return find(name) != nullptr;
}

String PeripheralManager::tickAll(time_t now, uint32_t nowMs) {
  JsonDocument doc;
  JsonArray records = doc.to<JsonArray>();

  // Trigger evaluation runs first so any command it dispatches is applied
  // before the peripheral tick loop drives the hardware.
  if (!_triggers.empty()) {
    std::map<std::string, float> values;
    for (const auto& e : _entries) {
      e.peripheral->currentMeasurements(values);
    }
    for (auto* t : _triggers) {
      t->evaluate(values, now, *this);
    }
  }

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
#ifdef ARDUINO
      String dbg; serializeJson(cmd, dbg);
      Serial.printf("DBG  [dispatch]: -> '%s' cmd=%s\n", name.c_str(), dbg.c_str());
#endif
      e.peripheral->applyCommand(cmd);
      return;
    }
  }
#ifdef ARDUINO
  Serial.printf("WARN [dispatch]: no peripheral named '%s'\n", name.c_str());
#endif
}

Peripheral* PeripheralManager::find(const String& name) const {
  for (auto& e : _entries) {
    if (name == e.peripheral->name()) return e.peripheral;
  }
  return nullptr;
}

void PeripheralManager::addTrigger(Trigger* t) {
  _triggers.push_back(t);
}

void PeripheralManager::removeTrigger(const std::string& id) {
  for (auto it = _triggers.begin(); it != _triggers.end(); ++it) {
    if ((*it)->id() == id) {
      delete *it;
      _triggers.erase(it);
      return;
    }
  }
}

Trigger* PeripheralManager::findTrigger(const std::string& id) const {
  for (auto* t : _triggers) {
    if (t->id() == id) return t;
  }
  return nullptr;
}

void PeripheralManager::forEachTrigger(std::function<void(Trigger*)> fn) const {
  for (auto* t : _triggers) fn(t);
}
