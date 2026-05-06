#include "trigger.h"
#include "peripheral_manager.h"
#include <cmath>
#include <cstring>

#define TRIG_LOG_INTERVAL 10000

#ifdef ARDUINO
#define TRIG_WARN(fmt, ...) Serial.printf("WARN [trigger]: " fmt "\n", ##__VA_ARGS__)
#define TRIG_DBG(fmt, ...) Serial.printf("DBG  [trigger]: " fmt "\n", ##__VA_ARGS__)
#else
#include <cstdio>
#define TRIG_WARN(fmt, ...) fprintf(stderr, "WARN [trigger]: " fmt "\n", ##__VA_ARGS__)
#define TRIG_DBG(fmt, ...) fprintf(stderr, "DBG  [trigger]: " fmt "\n", ##__VA_ARGS__)
#endif

bool Trigger::load(JsonObjectConst json)
{
  _id = json["id"] | "";
  _enabled = json["enabled"] | false;
  _cooldownS = json["cooldown_s"] | 60u;
  _lastFiredAt = 0;
  _actions.clear();

  _condDoc.set(json["condition"]);

  for (JsonObjectConst a : json["actions"].as<JsonArrayConst>()) {
    const char* type = a["type"] | "";
    if (strcmp(type, "peripheral_action") != 0) continue;

    JsonObjectConst cfg = a["config"].as<JsonObjectConst>();
    PeripheralAction pa;
    pa.peripheral = cfg["peripheral"] | "";
    pa.actionDoc.set(cfg);
    _actions.push_back(std::move(pa));
  }

  return !_id.empty();
}

bool Trigger::evaluate(const std::map<std::string, float> &values, time_t now,
                       PeripheralManager &mgr)
{
  if (!_enabled)
    return false;

  bool periodic = (++_evalCount % TRIG_LOG_INTERVAL == 0);

  float result = evalNode(_condDoc.as<JsonObjectConst>(), values, periodic);

  if (result == 0.0f || std::isnan(result))
  {
    if (periodic)
      TRIG_DBG("'%s' condition false (tick %lu)", _id.c_str(), (unsigned long)_evalCount);
    return false;
  }

  if (_lastFiredAt != 0 && (now - _lastFiredAt) < (time_t)_cooldownS)
  {
    if (periodic)
      TRIG_DBG("'%s' condition true but in cooldown (%lus remaining)",
               _id.c_str(), (unsigned long)(_cooldownS - (now - _lastFiredAt)));
    return false;
  }

  TRIG_DBG("'%s' FIRING %zu action(s) (tick %lu)", _id.c_str(), _actions.size(),
           (unsigned long)_evalCount);
  applyTo(mgr);
  _lastFiredAt = now;
  return true;
}

float Trigger::evalNode(JsonObjectConst node,
                        const std::map<std::string, float> &values, bool log) const
{
  const char *op = node["op"] | "";

  if (strcmp(op, "value") == 0)
  {
    const char *name = node["measurement"] | "";
    auto it = values.find(name);
    float v = (it != values.end()) ? it->second : NAN;
    if (log)
      TRIG_DBG("    value('%s') = %.4f", name, v);
    return v;
  }

  if (strcmp(op, "literal") == 0)
  {
    float v = node["value"] | 0.0f;
    if (log)
      TRIG_DBG("    literal = %.4f", v);
    return v;
  }

  if (strcmp(op, "not") == 0)
  {
    float l = evalNode(node["left"].as<JsonObjectConst>(), values, log);
    float v = (l == 0.0f || std::isnan(l)) ? 1.0f : 0.0f;
    if (log)
      TRIG_DBG("    not(%.4f) = %.4f", l, v);
    return v;
  }

  if (strcmp(op, "and") == 0)
  {
    float l = evalNode(node["left"].as<JsonObjectConst>(), values, log);
    if (l == 0.0f || std::isnan(l))
    {
      if (log)
        TRIG_DBG("    and: left=%.4f => short-circuit false", l);
      return 0.0f;
    }
    float r = evalNode(node["right"].as<JsonObjectConst>(), values, log);
    float v = (r != 0.0f && !std::isnan(r)) ? 1.0f : 0.0f;
    if (log)
      TRIG_DBG("    and(%.4f, %.4f) = %.4f", l, r, v);
    return v;
  }

  if (strcmp(op, "or") == 0)
  {
    float l = evalNode(node["left"].as<JsonObjectConst>(), values, log);
    if (l != 0.0f && !std::isnan(l))
    {
      if (log)
        TRIG_DBG("    or: left=%.4f => short-circuit true", l);
      return 1.0f;
    }
    float r = evalNode(node["right"].as<JsonObjectConst>(), values, log);
    float v = (r != 0.0f && !std::isnan(r)) ? 1.0f : 0.0f;
    if (log)
      TRIG_DBG("    or(%.4f, %.4f) = %.4f", l, r, v);
    return v;
  }

  float l = evalNode(node["left"].as<JsonObjectConst>(), values, log);
  float r = evalNode(node["right"].as<JsonObjectConst>(), values, log);
  float v = 0.0f;

  if (strcmp(op, "add") == 0)
  {
    v = l + r;
  }
  else if (strcmp(op, "sub") == 0)
  {
    v = l - r;
  }
  else if (strcmp(op, "mul") == 0)
  {
    v = l * r;
  }
  else if (strcmp(op, "div") == 0)
  {
    if (r == 0.0f)
    {
      TRIG_WARN("division by zero in trigger '%s'", _id.c_str());
      return 0.0f;
    }
    v = l / r;
  }
  else if (strcmp(op, "lt") == 0)
  {
    v = (l < r) ? 1.0f : 0.0f;
  }
  else if (strcmp(op, "lte") == 0)
  {
    v = (l <= r) ? 1.0f : 0.0f;
  }
  else if (strcmp(op, "gt") == 0)
  {
    v = (l > r) ? 1.0f : 0.0f;
  }
  else if (strcmp(op, "gte") == 0)
  {
    v = (l >= r) ? 1.0f : 0.0f;
  }
  else if (strcmp(op, "eq") == 0)
  {
    v = (l == r) ? 1.0f : 0.0f;
  }
  else
  {
    TRIG_WARN("unknown op '%s' in trigger '%s'", op, _id.c_str());
  }

  if (log)
    TRIG_DBG("    %s(%.4f, %.4f) = %.4f", op, l, r, v);
  return v;
}

void Trigger::serializeTo(JsonObject out) const
{
  out["op"]         = "upsert";
  out["id"]         = _id.c_str();
  out["enabled"]    = _enabled;
  out["cooldown_s"] = _cooldownS;
  out["condition"]  = _condDoc.as<JsonObjectConst>();

  JsonArray arr = out["actions"].to<JsonArray>();
  for (const auto& a : _actions) {
    JsonObject entry = arr.add<JsonObject>();
    entry["type"]   = "peripheral_action";
    entry["config"] = a.actionDoc.as<JsonObjectConst>();
  }
}

void Trigger::applyTo(PeripheralManager &mgr) const
{
  for (const auto& a : _actions) {
    mgr.dispatchCommand(String(a.peripheral.c_str()),
                        a.actionDoc.as<JsonObjectConst>());
  }
}
