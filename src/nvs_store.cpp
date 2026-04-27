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

bool NVSStore::isProvisioned() {
  return get("wifi_ssid")     != "" &&
         get("wifi_pass")     != "" &&
         get("device_id")     != "" &&
         get("device_jwt")    != "" &&
         get("mqtt_username") != "" &&
         get("mqtt_password") != "" &&
         get("mqtt_host")     != "" &&
         get("provisioned")   == "1";
}
