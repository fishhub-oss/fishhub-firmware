#include "http_client.h"
#include <HTTPClient.h>
#include "nvs_store.h"
#include "config.h"

static bool doPost(const String &payload, int &statusCode)
{
  String serverUrl = nvsStore.get("server_url");
  String deviceToken = nvsStore.get("device_jwt");
  if (serverUrl.isEmpty())
    serverUrl = SERVER_URL;
  if (deviceToken.isEmpty())
    deviceToken = DEVICE_TOKEN;
  if (serverUrl.startsWith("http://") && !serverUrl.startsWith("http://192.") &&
      !serverUrl.startsWith("http://10.") && !serverUrl.startsWith("http://172."))
  {
    serverUrl.replace("http://", "https://");
  }

  HTTPClient http;
  http.begin(serverUrl + "/readings");
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + deviceToken);

  unsigned long start = millis();
  statusCode = http.POST(payload);
  unsigned long elapsed = millis() - start;

  http.end();

  if (statusCode > 0)
  {
    Serial.printf("POST %d in %lums\n", statusCode, elapsed);
  }
  else
  {
    Serial.printf("POST error: %s\n", HTTPClient::errorToString(statusCode).c_str());
  }

  return statusCode >= 200 && statusCode < 300;
}

bool postReading(const String &payload)
{
  int statusCode;

  if (doPost(payload, statusCode))
  {
    return true;
  }

  if (statusCode >= 500 || statusCode < 0)
  {
    Serial.println("Retrying POST...");
    delay(1000);
    return doPost(payload, statusCode);
  }

  return false;
}
