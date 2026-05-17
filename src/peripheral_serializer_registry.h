#pragma once

#include <ArduinoJson.h>
#include "peripheral.h"

class PeripheralSerializer {
public:
  virtual ~PeripheralSerializer() = default;

  // Writes kind-specific fields into `out`. `name`, `kind`, and `pin` are
  // already written by the caller; the serializer writes everything else
  // (purpose, sense_pin, etc.)
  virtual void serialize(Peripheral* p, JsonObject& out) const = 0;

  // Factory: reconstructs a Peripheral* from the JSON object.
  // `name` is passed separately (written by the caller, not the serializer).
  // Returns nullptr if required fields are missing or invalid.
  virtual Peripheral* deserialize(const char* name, JsonObjectConst obj) const = 0;
};

class PeripheralSerializerRegistry {
public:
  // Takes ownership of `s`.
  void registerKind(const char* kind, PeripheralSerializer* s);

  // Looks up the serializer for p->kind() and calls serialize(p, out).
  // Logs a warning and no-ops if no serializer is registered for that kind.
  void serialize(Peripheral* p, JsonObject& out) const;

  // Looks up the serializer for `kind` and calls deserialize(name, obj).
  // Logs a warning and returns nullptr if no serializer is registered.
  Peripheral* deserialize(const char* kind, const char* name, JsonObjectConst obj) const;

private:
  static constexpr int MAX_KINDS = 8;
  struct Entry {
    const char*          kind;
    PeripheralSerializer* serializer;
  };
  Entry _entries[MAX_KINDS] = {};
  int   _count              = 0;
};

extern PeripheralSerializerRegistry peripheralSerializerRegistry;
