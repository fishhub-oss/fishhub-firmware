#include "provisioning.h"
#include "nvs_store.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

// ─── shared scan state ───────────────────────────────────────────────────────

static SemaphoreHandle_t  scanMutex;
static std::vector<String> scannedSSIDs;
static bool               scanDone = false;

static void scanTask(void*) {
  int n = WiFi.scanNetworks();
  xSemaphoreTake(scanMutex, portMAX_DELAY);
  for (int i = 0; i < n; i++) scannedSSIDs.push_back(WiFi.SSID(i));
  scanDone = true;
  xSemaphoreGive(scanMutex);
  Serial.printf("Wi-Fi scan complete: %d network(s) found\n", n);
  vTaskDelete(nullptr);
}

// ─── HTML ────────────────────────────────────────────────────────────────────

static const char CSS[] =
  "<style>"
  "*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}"
  "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
       "font-size:14px;background:#ffffff;color:#0a0a0a;"
       "min-height:100dvh;display:flex;align-items:center;"
       "justify-content:center;padding:1rem}"
  ".card{background:#ffffff;border:1px solid #ebebeb;border-radius:0.625rem;"
         "padding:1.5rem;width:100%;max-width:22rem}"
  "h1{font-size:1.5rem;font-weight:600;text-align:center;margin-bottom:0.25rem}"
  ".subtitle{color:#737373;font-size:0.875rem;text-align:center;margin-bottom:1.5rem}"
  ".field{display:flex;flex-direction:column;gap:0.375rem;margin-bottom:1rem}"
  "label{font-size:0.875rem;font-weight:500}"
  "input,select{border:1px solid #ebebeb;border-radius:0.5rem;"
                "padding:0.5rem 0.75rem;font-size:0.875rem;"
                "font-family:inherit;outline:none;width:100%;background:#ffffff}"
  "input:focus,select:focus{border-color:#b3b3b3;box-shadow:0 0 0 3px rgba(0,0,0,0.06)}"
  ".code-input{font-family:monospace;font-size:1.25rem;"
               "letter-spacing:0.15em;text-transform:uppercase;text-align:center}"
  "button{width:100%;padding:0.5rem 1rem;background:#1a1a1a;color:#fafafa;"
          "border:none;border-radius:0.5rem;font-size:0.875rem;font-weight:500;"
          "font-family:inherit;cursor:pointer;margin-top:0.5rem}"
  "button:hover{background:#333333}"
  ".error{background:#fef2f2;border:1px solid #fecaca;border-radius:0.5rem;"
          "padding:0.75rem;font-size:0.875rem;color:#b91c1c;margin-bottom:1rem}"
  ".hint{font-size:0.75rem;color:#737373;margin-top:0.25rem}"
  "</style>";

static const char TOGGLE_SCRIPT[] =
  "<script>"
  "function toggleManual(sel){"
    "var m=document.getElementById('wifi_ssid_manual');"
    "m.style.display=sel.value==='__other__'?'block':'none';"
    "m.required=sel.value==='__other__';"
  "}"
  "</script>";

static String buildForm(const String& errorMsg, const String& prefillSsid,
                        const String& prefillUrl) {
  String html = "<!DOCTYPE html><html lang='en'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
    "<title>FishHub Setup</title>";
  html += CSS;
  html += "</head><body><div class='card'>"
    "<h1>FishHub</h1>"
    "<p class='subtitle'>Connect your device</p>";

  if (errorMsg.length() > 0) {
    html += "<div class='error'>" + errorMsg + "</div>";
  }

  html += "<form method='POST' action='/configure'>";

  // Wi-Fi network field
  html += "<div class='field'><label for='wifi_ssid'>Wi-Fi Network</label>";

  xSemaphoreTake(scanMutex, portMAX_DELAY);
  bool done = scanDone;
  std::vector<String> ssids = scannedSSIDs;
  xSemaphoreGive(scanMutex);

  if (done && !ssids.empty()) {
    html += "<select id='wifi_ssid' name='wifi_ssid' onchange='toggleManual(this)'>";
    for (const auto& s : ssids) {
      html += "<option value='" + s + "'";
      if (s == prefillSsid) html += " selected";
      html += ">" + s + "</option>";
    }
    html += "<option value='__other__'>Other (enter manually)</option>";
    html += "</select>";
    html += "<input id='wifi_ssid_manual' name='wifi_ssid_manual' "
            "placeholder='Network name' autocomplete='off' "
            "style='display:none;margin-top:0.5rem'>";
  } else {
    html += "<input name='wifi_ssid' placeholder='Scanning for networks\xe2\x80\xa6' "
            "autocomplete='off' value='" + prefillSsid + "'>";
  }
  html += "</div>";

  // Password
  html += "<div class='field'>"
    "<label for='wifi_password'>Wi-Fi Password</label>"
    "<input id='wifi_password' name='wifi_password' type='password' autocomplete='off'>"
    "</div>";

  // Server URL
  html += "<div class='field'>"
    "<label for='server_url'>Server URL</label>"
    "<input id='server_url' name='server_url' "
    "placeholder='http://192.168.1.10:8080' required value='" + prefillUrl + "'>"
    "</div>";

  // Provisioning code
  html += "<div class='field'>"
    "<label for='provision_code'>Provisioning Code</label>"
    "<input id='provision_code' name='provision_code' class='code-input' "
    "maxlength='6' required autocomplete='off'>"
    "</div>";

  html += "<button type='submit'>Connect</button>"
    "</form></div>";
  html += TOGGLE_SCRIPT;
  html += "</body></html>";
  return html;
}

static String buildSuccess() {
  String html = "<!DOCTYPE html><html lang='en'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
    "<title>FishHub Setup</title>";
  html += CSS;
  html += "</head><body><div class='card'>"
    "<h1>FishHub</h1>"
    "<p class='subtitle' style='margin-bottom:0'>"
    "Device connected. You can close this page.</p>"
    "</div></body></html>";
  return html;
}

// ─── WebServer + handlers ─────────────────────────────────────────────────────

static WebServer server(80);

static void handleRoot() {
  server.send(200, "text/html", buildForm("", "", ""));
}

static void handleConfigure() {
  // Resolve SSID from select or manual input
  String ssid = server.arg("wifi_ssid");
  if (ssid == "__other__") ssid = server.arg("wifi_ssid_manual");
  ssid.trim();

  String password   = server.arg("wifi_password");
  String serverUrl  = server.arg("server_url");
  String code       = server.arg("provision_code");
  serverUrl.trim();
  code.trim();

  Serial.printf("Configure: ssid=%s server_url=%s code=%s\n",
                ssid.c_str(), serverUrl.c_str(), code.c_str());

  if (ssid.isEmpty() || password.isEmpty() || serverUrl.isEmpty() || code.isEmpty()) {
    server.send(400, "text/html",
      buildForm("All fields are required.", ssid, serverUrl));
    return;
  }

  nvsStore.set("wifi_ssid",   ssid);
  nvsStore.set("wifi_pass",   password);
  nvsStore.set("server_url",  serverUrl);

  ActivationError err = activateDevice(code);

  switch (err) {
    case ActivationError::None:
      server.send(200, "text/html", buildSuccess());
      delay(2000);
      ESP.restart();
      break;
    case ActivationError::WifiFailed:
      server.send(200, "text/html",
        buildForm("Could not connect to Wi-Fi. Check the network name and password.",
                  ssid, serverUrl));
      break;
    case ActivationError::InvalidCode:
      server.send(200, "text/html",
        buildForm("Invalid provisioning code. Generate a new one in the app.",
                  ssid, serverUrl));
      break;
    case ActivationError::ServerError:
      server.send(200, "text/html",
        buildForm("Could not reach the server. Check the Server URL.",
                  ssid, serverUrl));
      break;
  }
}

// ─── activation (#19) ────────────────────────────────────────────────────────

static void restartAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("FishHub-Setup");
}

ActivationError activateDevice(const String& provisionCode) {
  String ssid      = nvsStore.get("wifi_ssid");
  String password  = nvsStore.get("wifi_pass");
  String serverUrl = nvsStore.get("server_url");

  Serial.printf("Activation: connecting to Wi-Fi SSID=%s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Activation: Wi-Fi connect timed out");
    restartAP();
    return ActivationError::WifiFailed;
  }
  Serial.printf("Activation: Wi-Fi connected — IP: %s\n",
                WiFi.localIP().toString().c_str());

  String endpoint = serverUrl + "/devices/activate";
  String body     = "{\"code\":\"" + provisionCode + "\"}";

  auto doPost = [&](int& statusOut) -> String {
    HTTPClient http;
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json");
    statusOut = http.POST(body);
    String resp = (statusOut > 0) ? http.getString() : "";
    Serial.printf("Activation: POST %s -> %d\n", endpoint.c_str(), statusOut);
    http.end();
    return resp;
  };

  int status = 0;
  String resp = doPost(status);

  if (status <= 0 || status >= 500) {
    Serial.println("Activation: server error, retrying once...");
    delay(2000);
    resp = doPost(status);
  }

  if (status >= 400 && status < 500) {
    restartAP();
    return ActivationError::InvalidCode;
  }
  if (status <= 0 || status >= 500) {
    restartAP();
    return ActivationError::ServerError;
  }

  // Parse token from response
  JsonDocument doc;
  DeserializationError jsonErr = deserializeJson(doc, resp);
  if (jsonErr || !doc["token"].is<const char*>()) {
    Serial.println("Activation: failed to parse token from response");
    restartAP();
    return ActivationError::ServerError;
  }

  String token = doc["token"].as<String>();
  nvsStore.set("device_token", token);
  Serial.println("Activation successful. Rebooting...");
  delay(1000);
  ESP.restart();
  return ActivationError::None; // unreachable
}

void startProvisioning() {
  scanMutex = xSemaphoreCreateMutex();
  scannedSSIDs.clear();
  scanDone = false;

  // WIFI_AP_STA is required for WiFi.scanNetworks() to work while hosting an AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("FishHub-Setup");

  xTaskCreatePinnedToCore(scanTask, "wifi_scan", 4096, nullptr, 1, nullptr, 0);
  Serial.printf("AP started — SSID: FishHub-Setup  IP: %s\n",
                WiFi.softAPIP().toString().c_str());

  server.on("/",          HTTP_GET,  handleRoot);
  server.on("/configure", HTTP_POST, handleConfigure);
  server.begin();
  Serial.println("Captive portal listening on port 80");

  while (true) {
    server.handleClient();
    yield();
  }
}
