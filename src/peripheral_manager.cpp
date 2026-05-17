#include "peripheral_manager.h"
#include "trigger_event_queue.h"
#include <cstring>

#ifndef ARDUINO
// millis() not available on native — callers always inject nowMs
#endif

void PeripheralManager::add(Peripheral* p, int pin) {
  PeripheralEntry e;
  e.peripheral   = p;
  e.lastTickedAt = 0;
  e.pin          = pin;
  _entries.push_back(e);
  if (_begun) p->begin();
}

void PeripheralManager::forEach(std::function<void(PeripheralEntry&)> fn) {
  for (auto& e : _entries) {
    fn(e);
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
      TriggerFired result = t->evaluate(values, now);
      if (!result.fired) continue;

      for (size_t ai = 0; ai < result.actions.size(); ai++)
        dispatchCommand(String(result.actions[ai].first.c_str()),
                        result.actions[ai].second.as<JsonObjectConst>());

      if (_eventQueue) {
        TriggerEvent ev = {};
        strncpy(ev.triggerId, result.triggerId.c_str(), sizeof(ev.triggerId) - 1);
        ev.firedAt = result.firedAt;
        std::string id = makeTriggerEventId(ev.triggerId, ev.firedAt);
        strncpy(ev.triggerEventId, id.c_str(), sizeof(ev.triggerEventId) - 1);
        for (auto it = result.readings.begin(); it != result.readings.end(); ++it) {
          if (ev.readingCount >= TRIGGER_EVENT_MAX_READINGS) break;
          strncpy(ev.readings[ev.readingCount].peripheral,
                  it->first.c_str(), sizeof(ev.readings[0].peripheral) - 1);
          ev.readings[ev.readingCount].value = it->second;
          ev.readingCount++;
        }
        _eventQueue->push(ev);
      }
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
