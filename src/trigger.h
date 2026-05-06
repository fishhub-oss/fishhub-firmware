#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
#include <cstdint>
using String = std::string;
#endif

#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <time.h>

struct TriggerFired {
    bool fired = false;
    // (peripheral name, action doc) pairs to dispatch
    std::vector<std::pair<std::string, JsonDocument>> actions;
    // measurement values referenced in the condition tree
    std::map<std::string, float> readings;
    time_t      firedAt   = 0;
    std::string triggerId;
};

class Trigger {
public:
    bool        load(JsonObjectConst json);
    TriggerFired evaluate(const std::map<std::string, float>& values, time_t now);
    void        serializeTo(JsonObject out) const;
    const std::string& id() const { return _id; }
    bool        enabled() const { return _enabled; }

private:
    float evalNode(JsonObjectConst node,
                   const std::map<std::string, float>& values, bool log) const;
    void  collectMeasurements(JsonObjectConst node,
                              const std::map<std::string, float>& values,
                              TriggerFired& out) const;

    struct PeripheralAction {
        std::string  peripheral;
        JsonDocument actionDoc;
    };

    std::string                   _id;
    bool                          _enabled   = false;
    JsonDocument                  _condDoc;
    std::vector<PeripheralAction> _actions;
    uint32_t                      _cooldownS  = 60;
    time_t                        _lastFiredAt = 0;
    uint32_t                      _evalCount  = 0;
};
