#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <HTTPClient.h>

// WiFi
const char* ssid = "Xiaomi 15T Pro";
const char* password = "ctxtmjtm95vdwkm";

// VERSION (CHANGE EVERY BUILD)
const char* currentVersion = "1.0.3";

// GitHub
const char* firmwareURL = "https://raw.githubusercontent.com/tikitoy/Rikki-Playroom/main/firmware.bin";
const char* versionURL  = "https://raw.githubusercontent.com/tikitoy/Rikki-Playroom/main/version.txt";

// LED
const int led = 2;

enum LedMode {
  LED_IDLE,
  LED_CHECKING,
  LED_UPDATING,
  LED_SUCCESS,
  LED_ERROR
};

LedMode ledMode = LED_IDLE;

// Web server
WebServer server(80);

String otaStatus = "Idle";
int otaProgress = 0;

// ================= LED HANDLER =================
void handleLED() {
  static unsigned long lastToggle = 0;
  static bool state = false;
  unsigned long interval = 1000;

  switch (ledMode) {
    case LED_IDLE: interval = 1000; break;
    case LED_CHECKING: interval = 500; break;
    case LED_UPDATING: interval = 100; break;
    case LED_ERROR: interval = 50; break;
    case LED_SUCCESS:
      digitalWrite(led, HIGH);
      return;
  }

  if (millis() - lastToggle >= interval) {
    lastToggle = millis();
    state = !state;
    digitalWrite(led, state);
  }
}

// ================= HTTPS CLIENT =================
WiFiClientSecure makeClient() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);
  return client;
}

// ================= GET VERSION =================
String getLatestVersion() {
  WiFiClientSecure client = makeClient();
  HTTPClient http;

  http.setTimeout(30000);
  http.setConnectTimeout(30000);

  if (!http.begin(client, versionURL)) return "";

  http.addHeader("User-Agent", "ESP32");

  int code = http.GET();
  if (code != 200) {
    http.end();
    return "";
  }

  String v = http.getString();
  v.trim();
  http.end();
  return v;
}

// ================= CHECK VERSION =================
bool isNewVersion() {
  ledMode = LED_CHECKING;

  String latest = getLatestVersion();

  if (latest == "") {
    otaStatus = "Version check failed";
    ledMode = LED_ERROR;
    return false;
  }

  if (latest != String(currentVersion)) {
    otaStatus = "New version: " + latest;
    return true;
  }

  otaStatus = "Already latest";
  ledMode = LED_IDLE;
  return false;
}

// ================= OTA UPDATE =================
void updateFromGitHub() {
  ledMode = LED_UPDATING;
  otaStatus = "Starting OTA";
  otaProgress = 0;

  WiFiClientSecure client = makeClient();
  HTTPClient http;

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, firmwareURL)) {
    otaStatus = "HTTP begin failed";
    ledMode = LED_ERROR;
    return;
  }

  http.addHeader("User-Agent", "ESP32");

  int code = http.GET();
  if (code != 200) {
    otaStatus = "Download failed";
    ledMode = LED_ERROR;
    http.end();
    return;
  }

  int len = http.getSize();

  if (!Update.begin(len)) {
    otaStatus = "Not enough space";
    ledMode = LED_ERROR;
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buff[1024];
  int written = 0;

  while (http.connected() && written < len) {
    size_t available = stream->available();

    if (available) {
      int r = stream->readBytes(buff, min((int)available, 1024));
      int w = Update.write(buff, r);

      if (w != r) {
        otaStatus = "Write error";
        ledMode = LED_ERROR;
        Update.abort();
        http.end();
        return;
      }

      written += w;
      otaProgress = (written * 100) / len;
    }

    delay(1);
  }

  if (!Update.end()) {
    otaStatus = "Update failed";
    ledMode = LED_ERROR;
    http.end();
    return;
  }

  ledMode = LED_SUCCESS;
  otaStatus = "Rebooting...";
  delay(2000);
  ESP.restart();
}

// ================= WEB =================
const char* page =
"<h2>ESP32 OTA</h2>"
"<p>Current: <b id='cur'></b></p>"
"<p>Status: <b id='status'></b></p>"
"<p>Progress: <b id='progress'></b></p>"
"<progress id='bar' max='100'></progress>"
"<br><br>"
"<button onclick='start()'>Update</button>"
"<script>"
"function upd(){fetch('/status').then(r=>r.json()).then(d=>{"
"cur.innerHTML=d.ver;"
"status.innerHTML=d.status;"
"progress.innerHTML=d.prog+'%';"
"bar.value=d.prog;"
"});}"
"function start(){fetch('/update');}"
"setInterval(upd,1000);"
"</script>";

void setup() {
  pinMode(led, OUTPUT);
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  Serial.println(WiFi.localIP());

  server.on("/", []() { server.send(200, "text/html", page); });

  server.on("/status", []() {
    String json = "{";
    json += "\"ver\":\"" + String(currentVersion) + "\",";
    json += "\"status\":\"" + otaStatus + "\",";
    json += "\"prog\":" + String(otaProgress);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/update", []() {
    server.send(200, "text/plain", "Checking...");
    if (isNewVersion()) updateFromGitHub();
  });

  server.begin();

  delay(3000);

  // AUTO UPDATE ON BOOT
  if (isNewVersion()) updateFromGitHub();
}

void loop() {
  server.handleClient();
  handleLED();
}