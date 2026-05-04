#include "trigger.h"
#include "peripheral_manager.h"
#include <cmath>
#include <cstring>

#ifdef ARDUINO
#define TRIG_WARN(fmt, ...) Serial.printf("WARN [trigger]: " fmt "\n", ##__VA_ARGS__)
#else
#include <cstdio>
#define TRIG_WARN(fmt, ...) fprintf(stderr, "WARN [trigger]: " fmt "\n", ##__VA_ARGS__)
#endif

bool Trigger::load(JsonObjectConst json) {
  _id               = json["id"] | "";
  _enabled          = json["enabled"] | false;
  _targetPeripheral = json["target"] | "";
  _cooldownS        = json["cooldown_s"] | 60u;
  _lastFiredAt      = 0;

  _condDoc.set(json["condition"]);
  _actionDoc.set(json["action"]);

  return !_id.empty();
}

bool Trigger::evaluate(const std::map<std::string, float>& values, time_t now,
                       PeripheralManager& mgr) {
  if (!_enabled) return false;

  float result = evalNode(_condDoc.as<JsonObjectConst>(), values);
  if (result == 0.0f || std::isnan(result)) return false;

  if (_lastFiredAt != 0 && (now - _lastFiredAt) < (time_t)_cooldownS) return false;

  applyTo(mgr);
  _lastFiredAt = now;
  return true;
}

float Trigger::evalNode(JsonObjectConst node,
                        const std::map<std::string, float>& values) const {
  const char* op = node["op"] | "";

  if (strcmp(op, "value") == 0) {
    const char* name = node["measurement"] | "";
    auto it = values.find(name);
    return (it != values.end()) ? it->second : NAN;
  }

  if (strcmp(op, "literal") == 0) {
    return node["value"] | 0.0f;
  }

  if (strcmp(op, "not") == 0) {
    float l = evalNode(node["left"].as<JsonObjectConst>(), values);
    return (l == 0.0f || std::isnan(l)) ? 1.0f : 0.0f;
  }

  if (strcmp(op, "and") == 0) {
    float l = evalNode(node["left"].as<JsonObjectConst>(), values);
    if (l == 0.0f || std::isnan(l)) return 0.0f;
    float r = evalNode(node["right"].as<JsonObjectConst>(), values);
    return (r != 0.0f && !std::isnan(r)) ? 1.0f : 0.0f;
  }

  if (strcmp(op, "or") == 0) {
    float l = evalNode(node["left"].as<JsonObjectConst>(), values);
    if (l != 0.0f && !std::isnan(l)) return 1.0f;
    float r = evalNode(node["right"].as<JsonObjectConst>(), values);
    return (r != 0.0f && !std::isnan(r)) ? 1.0f : 0.0f;
  }

  float l = evalNode(node["left"].as<JsonObjectConst>(), values);
  float r = evalNode(node["right"].as<JsonObjectConst>(), values);

  if (strcmp(op, "add") == 0) return l + r;
  if (strcmp(op, "sub") == 0) return l - r;
  if (strcmp(op, "mul") == 0) return l * r;
  if (strcmp(op, "div") == 0) {
    if (r == 0.0f) {
      TRIG_WARN("division by zero in trigger '%s'", _id.c_str());
      return 0.0f;
    }
    return l / r;
  }
  if (strcmp(op, "lt")  == 0) return (l <  r) ? 1.0f : 0.0f;
  if (strcmp(op, "lte") == 0) return (l <= r) ? 1.0f : 0.0f;
  if (strcmp(op, "gt")  == 0) return (l >  r) ? 1.0f : 0.0f;
  if (strcmp(op, "gte") == 0) return (l >= r) ? 1.0f : 0.0f;
  if (strcmp(op, "eq")  == 0) return (l == r) ? 1.0f : 0.0f;

  return 0.0f;
}

void Trigger::serializeTo(JsonObject out) const {
  out["op"]         = "upsert";
  out["id"]         = _id.c_str();
  out["enabled"]    = _enabled;
  out["target"]     = _targetPeripheral.c_str();
  out["cooldown_s"] = _cooldownS;
  out["condition"]  = _condDoc.as<JsonObjectConst>();
  out["action"]     = _actionDoc.as<JsonObjectConst>();
}

void Trigger::applyTo(PeripheralManager& mgr) const {
  if (_targetPeripheral.empty()) return;
  mgr.dispatchCommand(String(_targetPeripheral.c_str()), _actionDoc.as<JsonObjectConst>());
}
