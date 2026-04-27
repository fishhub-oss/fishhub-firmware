#include "provisioning.h"
#include "nvs_store.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

// ─── shared scan state ───────────────────────────────────────────────────────

static SemaphoreHandle_t scanMutex;
static std::vector<String> scannedSSIDs;
static bool scanDone = false;

static void scanTask(void *)
{
  int n = WiFi.scanNetworks();
  xSemaphoreTake(scanMutex, portMAX_DELAY);
  for (int i = 0; i < n; i++)
    scannedSSIDs.push_back(WiFi.SSID(i));
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

// reconfiguring = device already has a token; omit provisioning code field
static String buildForm(const String &errorMsg, const String &prefillSsid,
                        bool reconfiguring)
{
  String html = "<!DOCTYPE html><html lang='en'><head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
                "<title>FishHub Setup</title>";
  html += CSS;
  html += "</head><body><div class='card'>"
          "<h1>FishHub</h1>";
  html += reconfiguring
              ? "<p class='subtitle'>Update your device settings</p>"
              : "<p class='subtitle'>Connect your device</p>";

  if (errorMsg.length() > 0)
  {
    html += "<div class='error'>" + errorMsg + "</div>";
  }

  html += "<form method='POST' action='/configure'>";

  // Wi-Fi network field
  html += "<div class='field'><label for='wifi_ssid'>Wi-Fi Network</label>";

  xSemaphoreTake(scanMutex, portMAX_DELAY);
  bool done = scanDone;
  std::vector<String> ssids = scannedSSIDs;
  xSemaphoreGive(scanMutex);

  if (done && !ssids.empty())
  {
    html += "<select id='wifi_ssid' name='wifi_ssid' onchange='toggleManual(this)'>";
    for (const auto &s : ssids)
    {
      html += "<option value='" + s + "'";
      if (s == prefillSsid)
        html += " selected";
      html += ">" + s + "</option>";
    }
    html += "<option value='__other__'>Other (enter manually)</option>";
    html += "</select>";
    html += "<input id='wifi_ssid_manual' name='wifi_ssid_manual' "
            "placeholder='Network name' autocomplete='off' "
            "style='display:none;margin-top:0.5rem'>";
  }
  else
  {
    html += "<input name='wifi_ssid' placeholder='Scanning for networks\xe2\x80\xa6' "
            "autocomplete='off' value='" +
            prefillSsid + "'>";
  }
  html += "</div>";

  // Password
  html += "<div class='field'>"
          "<label for='wifi_password'>Wi-Fi Password</label>"
          "<div style='position:relative'>"
          "<input id='wifi_password' name='wifi_password' type='password' "
          "autocomplete='off' style='padding-right:2.5rem'>"
          "<button type='button' onclick=\""
          "var i=document.getElementById('wifi_password');"
          "i.type=i.type==='password'?'text':'password';"
          "this.textContent=i.type==='password'?'Show':'Hide';"
          "\" style='position:absolute;right:0.5rem;top:50%;transform:translateY(-50%);"
          "width:auto;margin:0;padding:0 0.5rem;background:none;color:#737373;"
          "border:none;font-size:0.75rem;cursor:pointer'>Show</button>"
          "</div>"
          "</div>";

  // Provisioning code — only shown on fresh provisioning
  if (!reconfiguring)
  {
    html += "<div class='field'>"
            "<label for='provision_code'>Provisioning Code</label>"
            "<input id='provision_code' name='provision_code' class='code-input' "
            "maxlength='6' required autocomplete='off'>"
            "</div>";
  }

  html += reconfiguring
              ? "<button type='submit'>Save &amp; Reconnect</button>"
              : "<button type='submit'>Connect</button>";
  html += "</form></div>";
  html += TOGGLE_SCRIPT;
  html += "</body></html>";
  return html;
}

static String buildConnecting()
{
  String html = "<!DOCTYPE html><html lang='en'><head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
                "<title>FishHub Setup</title>";
  html += CSS;
  html += "</head><body><div class='card'>"
          "<h1>FishHub</h1>"
          "<p class='subtitle'>Connecting to Wi-Fi and activating your device&hellip;</p>"
          "<p class='hint' style='text-align:center;margin-top:1rem'>"
          "This may take up to 30 seconds. The device will reboot when done.</p>"
          "</div></body></html>";
  return html;
}

// ─── WebServer + handlers ─────────────────────────────────────────────────────

static WebServer server(80);
static bool reconfiguring = false;
// Pending error message to show after AP restart on reconfigure failure
static String pendingError;

static void restartAP()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("FishHub-Setup");
}

// ─── activation helpers ───────────────────────────────────────────────────────

struct ActivateResult {
  ActivationError err;
  String token;
  String deviceId;
};

// POST /devices/activate — returns token + device_id on 202, error otherwise.
static ActivateResult postActivate(const String &provisionCode, const String &serverUrl)
{
  String endpoint = serverUrl + "/devices/activate";
  String body = "{\"code\":\"" + provisionCode + "\"}";

  auto doPost = [&](int &statusOut) -> String
  {
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
  if (status <= 0 || status >= 500)
  {
    Serial.println("Activation: server error, retrying once...");
    delay(2000);
    resp = doPost(status);
  }

  if (status >= 400 && status < 500)
    return {ActivationError::InvalidCode, "", ""};
  if (status <= 0 || status >= 500)
    return {ActivationError::ServerError, "", ""};

  JsonDocument doc;
  if (deserializeJson(doc, resp))
    return {ActivationError::ServerError, "", ""};

  String token    = doc["token"].as<String>();
  String deviceId = doc["device_id"].as<String>();

  if (token.isEmpty())
  {
    Serial.println("Activation: empty token — DEVICE_JWT_PRIVATE_KEY not configured");
    return {ActivationError::ServerError, "", ""};
  }
  if (deviceId.isEmpty())
  {
    Serial.println("Activation: empty device_id in response");
    return {ActivationError::ServerError, "", ""};
  }

  return {ActivationError::None, token, deviceId};
}

struct MqttCredentials {
  String username;
  String password;
  String host;
};

// GET /devices/{id}/status — polls until ready or timeout (60 s).
static ActivationError pollMqttCredentials(const String &deviceId,
                                           const String &jwt,
                                           const String &serverUrl,
                                           MqttCredentials &out)
{
  String endpoint = serverUrl + "/devices/" + deviceId + "/status";
  unsigned long deadline = millis() + 60000UL;
  int attempt = 0;

  while (millis() < deadline)
  {
    attempt++;
    HTTPClient http;
    http.begin(endpoint);
    http.addHeader("Authorization", "Bearer " + jwt);
    int status = http.GET();
    String resp = (status > 0) ? http.getString() : "";
    http.end();

    Serial.printf("Status poll #%d: GET %s -> %d\n", attempt, endpoint.c_str(), status);

    if (status == 200)
    {
      JsonDocument doc;
      if (!deserializeJson(doc, resp))
      {
        String s = doc["status"].as<String>();
        if (s == "ready")
        {
          out.username = doc["mqtt_username"].as<String>();
          out.password = doc["mqtt_password"].as<String>();
          out.host     = doc["mqtt_host"].as<String>();
          if (!out.username.isEmpty() && !out.password.isEmpty() && !out.host.isEmpty())
          {
            Serial.println("Status poll: ready — MQTT credentials received");
            return ActivationError::None;
          }
        }
        else
        {
          Serial.println("Status poll: still provisioning...");
        }
      }
    }

    delay(2000);
  }

  Serial.println("Status poll: timed out after 60s");
  return ActivationError::Timeout;
}

// ─── handleConfigure ─────────────────────────────────────────────────────────

static void handleRoot()
{
  server.send(200, "text/html", buildForm(pendingError, "", reconfiguring));
  pendingError = "";
}

static void handleConfigure()
{
  String ssid = server.arg("wifi_ssid");
  if (ssid == "__other__")
    ssid = server.arg("wifi_ssid_manual");
  ssid.trim();

  String password = server.arg("wifi_password");

  // ── Reconfiguration path: update Wi-Fi only ──────────────────────────────
  if (reconfiguring)
  {
    Serial.printf("Reconfigure: ssid=%s\n", ssid.c_str());

    if (ssid.isEmpty() || password.isEmpty())
    {
      server.send(200, "text/html",
                  buildForm("All fields are required.", ssid, true));
      return;
    }

    // Write to temp keys first — existing credentials are untouched until
    // we confirm the new Wi-Fi actually works.
    nvsStore.set("wifi_ssid_new", ssid);
    nvsStore.set("wifi_pass_new", password);

    server.send(200, "text/html", buildConnecting());
    server.client().flush();
    delay(500);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
      delay(200);

    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Reconfigure: Wi-Fi connect failed — reverting");
      nvsStore.remove("wifi_ssid_new");
      nvsStore.remove("wifi_pass_new");
      restartAP();
      // pendingError shown when user's browser reconnects to the portal
      pendingError = "Could not connect to Wi-Fi. Check the network name and password.";
      return;
    }

    // Success — promote temp keys to real keys and reboot.
    nvsStore.set("wifi_ssid", ssid);
    nvsStore.set("wifi_pass", password);
    nvsStore.remove("wifi_ssid_new");
    nvsStore.remove("wifi_pass_new");
    Serial.println("Reconfigure: Wi-Fi updated. Rebooting...");
    delay(500);
    ESP.restart();
    return;
  }

  // ── Fresh provisioning path ───────────────────────────────────────────────
  String code = server.arg("provision_code");
  code.trim();

  Serial.printf("Configure: ssid=%s code=%s\n", ssid.c_str(), code.c_str());

  if (ssid.isEmpty() || password.isEmpty() || code.isEmpty())
  {
    server.send(200, "text/html",
                buildForm("All fields are required.", ssid, false));
    return;
  }

  // Send connecting page immediately — browser will lose connection once we
  // drop the AP to connect to the user's network.
  server.send(200, "text/html", buildConnecting());
  server.client().flush();
  delay(500);

  // Connect to Wi-Fi.
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.printf("Connecting to Wi-Fi SSID=%s\n", ssid.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
    delay(200);

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Wi-Fi connect timed out");
    restartAP();
    pendingError = "Could not connect to Wi-Fi. Check the network name and password.";
    return;
  }
  Serial.printf("Wi-Fi connected — IP: %s\n", WiFi.localIP().toString().c_str());

  // Normalize server URL to HTTPS on non-local addresses.
  String serverUrl = SERVER_URL;
  if (serverUrl.startsWith("http://") && !serverUrl.startsWith("http://192.") &&
      !serverUrl.startsWith("http://10.")  && !serverUrl.startsWith("http://172."))
  {
    serverUrl.replace("http://", "https://");
  }

  // POST /devices/activate — returns 202 with token + device_id.
  ActivateResult activation = postActivate(code, serverUrl);
  if (activation.err == ActivationError::InvalidCode)
  {
    restartAP();
    pendingError = "Invalid provisioning code. Generate a new one in the app.";
    return;
  }
  if (activation.err != ActivationError::None)
  {
    restartAP();
    pendingError = "Could not reach the server. Check the server configuration.";
    return;
  }

  // Store Wi-Fi and identity — but do NOT set provisioned flag yet.
  nvsStore.set("wifi_ssid",  ssid);
  nvsStore.set("wifi_pass",  password);
  nvsStore.set("device_jwt", activation.token);
  nvsStore.set("device_id",  activation.deviceId);

  // Poll GET /devices/{id}/status until MQTT credentials are ready.
  MqttCredentials creds;
  ActivationError pollErr = pollMqttCredentials(activation.deviceId,
                                                activation.token,
                                                serverUrl, creds);
  if (pollErr != ActivationError::None)
  {
    // Credentials not ready — clear partial NVS state and return to portal.
    nvsStore.remove("device_jwt");
    nvsStore.remove("device_id");
    restartAP();
    pendingError = "Server is still provisioning your device. Please try again in a moment.";
    return;
  }

  // All credentials confirmed — write atomically (provisioned flag last).
  nvsStore.set("mqtt_username", creds.username);
  nvsStore.set("mqtt_password", creds.password);
  nvsStore.set("mqtt_host",     creds.host);
  nvsStore.set("provisioned",   "1");

  Serial.println("Provisioning complete. Rebooting...");
  delay(1000);
  ESP.restart();
}

// ─── startProvisioning ────────────────────────────────────────────────────────

void startProvisioning()
{
  // Reconfiguring if the device already has a JWT — no activation code needed.
  reconfiguring = !nvsStore.get("device_jwt").isEmpty();
  Serial.printf("Provisioning mode: %s\n", reconfiguring ? "reconfiguration" : "fresh");

  scanMutex = xSemaphoreCreateMutex();
  scannedSSIDs.clear();
  scanDone = false;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("FishHub-Setup");

  xTaskCreatePinnedToCore(scanTask, "wifi_scan", 4096, nullptr, 1, nullptr, 0);
  Serial.printf("AP started — SSID: FishHub-Setup  IP: %s\n",
                WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/configure", HTTP_POST, handleConfigure);
  server.begin();
  Serial.println("Captive portal listening on port 80");

  while (true)
  {
    server.handleClient();
    yield();
  }
}
