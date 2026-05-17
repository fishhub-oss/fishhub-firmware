#include "peripheral_serializer_registry.h"
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdio>
#define Serial_printf(...) printf(__VA_ARGS__)
#endif

PeripheralSerializerRegistry peripheralSerializerRegistry;

void PeripheralSerializerRegistry::registerKind(const char* kind, PeripheralSerializer* s) {
  if (_count >= MAX_KINDS) {
#ifdef ARDUINO
    Serial.printf("WARN [registry]: max kinds (%d) reached — cannot register '%s'\n", MAX_KINDS, kind);
#endif
    return;
  }
  _entries[_count++] = {kind, s};
}

void PeripheralSerializerRegistry::serialize(Peripheral* p, JsonObject& out) const {
  for (int i = 0; i < _count; i++) {
    if (strcmp(_entries[i].kind, p->kind()) == 0) {
      _entries[i].serializer->serialize(p, out);
      return;
    }
  }
#ifdef ARDUINO
  Serial.printf("WARN [registry]: no serializer for kind '%s' — skipping extra fields\n", p->kind());
#endif
}

Peripheral* PeripheralSerializerRegistry::deserialize(const char* kind, const char* name, JsonObjectConst obj) const {
  for (int i = 0; i < _count; i++) {
    if (strcmp(_entries[i].kind, kind) == 0) {
      return _entries[i].serializer->deserialize(name, obj);
    }
  }
#ifdef ARDUINO
  Serial.printf("WARN [registry]: no serializer for kind '%s' — cannot restore '%s'\n", kind, name);
#endif
  return nullptr;
}
