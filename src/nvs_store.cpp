#include "nvs_store.h"

NVSStore nvsStore;

void NVSStore::begin() {
  _prefs.begin("fishhub", false);
}

String NVSStore::get(const char* key) {
  return _prefs.getString(key, "");
}

void NVSStore::set(const char* key, const String& value) {
  _prefs.putString(key, value);
}

void NVSStore::remove(const char* key) {
  _prefs.remove(key);
}

void NVSStore::clear() {
  _prefs.clear();
}
