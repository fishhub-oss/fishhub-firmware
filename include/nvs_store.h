#pragma once
#include <Arduino.h>
#include <Preferences.h>

class NVSStore {
public:
  void   begin();
  String get(const char* key);
  void   set(const char* key, const String& value);
  void   remove(const char* key);
  void   clear();

private:
  Preferences _prefs;
};

extern NVSStore nvsStore;
