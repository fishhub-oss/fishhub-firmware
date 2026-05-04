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

String NVSStore::readTimezone() {
  return get("timezone");
}

void NVSStore::writeTimezone(const String& tz) {
  set("timezone", tz);
}

bool NVSStore::isProvisioned() {
  bool allKeys = get("wifi_ssid")     != "" &&
                 get("wifi_pass")     != "" &&
                 get("device_id")     != "" &&
                 get("device_jwt")    != "" &&
                 get("mqtt_username") != "" &&
                 get("mqtt_password") != "" &&
                 get("mqtt_host")     != "";

  if (!allKeys)
    return false;

  // Migrate devices provisioned before the flag was introduced.
  if (get("provisioned") != "1") {
    set("provisioned", "1");
    Serial.println("NVS: migrated — set provisioned flag on existing device");
  }
  return true;
}
