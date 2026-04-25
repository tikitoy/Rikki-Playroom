#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <HTTPClient.h>

const char* host = "esp32";
const char* ssid = "Xiaomi 15T Pro";
const char* password = "ctxtmjtm95vdwkm";

// CHANGE THIS VERSION EVERY NEW FIRMWARE BUILD
const char* currentVersion = "1.0.2";

// GitHub raw files
const char* firmwareURL = "https://raw.githubusercontent.com/tikitoy/Rikki-Playroom/main/firmware.bin";
const char* versionURL  = "https://raw.githubusercontent.com/tikitoy/Rikki-Playroom/main/version.txt";

const int led = 2;
unsigned long previousMillis = 0;
const long interval = 4000;
int ledState = LOW;

enum LedMode {
  LED_IDLE,
  LED_CHECKING,
  LED_UPDATING,
  LED_SUCCESS,
  LED_ERROR
};

LedMode ledMode = LED_IDLE;

WebServer server(80);

String otaStatus = "Idle";
int otaProgress = 0;

const char* loginIndex =
"<form name='loginForm'>"
"<table width='20%' bgcolor='A09F9F' align='center'>"
"<tr><td colspan=2><center><font size=4><b>ESP32 Login Page</b></font></center><br></td></tr>"
"<tr><td>Username:</td><td><input type='text' size=25 name='userid'><br></td></tr>"
"<tr><td>Password:</td><td><input type='Password' size=25 name='pwd'><br></td></tr>"
"<tr><td><input type='submit' onclick='check(this.form)' value='Login'></td></tr>"
"</table>"
"</form>"
"<script>"
"function check(form){"
"if(form.userid.value=='admin' && form.pwd.value=='admin'){"
"window.open('/serverIndex');"
"}else{"
"alert('Error Password or Username');"
"}"
"}"
"</script>";

const char* serverIndex =
"<h2>ESP32 OTA Update</h2>"
"<p>Current version: <b id='ver'>Loading...</b></p>"

"<h3>GitHub OTA Update</h3>"
"<button onclick='startOTA()'>Check Version and Update</button>"
"<p>Status: <b id='status'>Idle</b></p>"
"<p>Progress: <b id='progress'>0%</b></p>"
"<progress id='bar' value='0' max='100' style='width:300px'></progress>"

"<script>"
"function refreshStatus(){"
"fetch('/otaStatus').then(r=>r.json()).then(d=>{"
"document.getElementById('status').innerHTML=d.status;"
"document.getElementById('progress').innerHTML=d.progress + '%';"
"document.getElementById('bar').value=d.progress;"
"document.getElementById('ver').innerHTML=d.version;"
"});"
"}"
"function startOTA(){"
"fetch('/githubUpdate').then(r=>r.text()).then(t=>{"
"document.getElementById('status').innerHTML=t;"
"});"
"}"
"setInterval(refreshStatus,1000);"
"refreshStatus();"
"</script>";

WiFiClientSecure makeSecureClient() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);
  return client;
}

String getLatestVersion() {
  WiFiClientSecure client = makeSecureClient();
  HTTPClient http;

  http.setTimeout(30000);
  http.setConnectTimeout(30000);
  http.setReuse(false);

  if (!http.begin(client, versionURL)) {
    Serial.println("Version HTTP begin failed");
    return "";
  }

  http.addHeader("User-Agent", "ESP32");

  int httpCode = http.GET();
  Serial.printf("Version HTTP Code: %d\n", httpCode);

  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return "";
  }

  String latestVersion = http.getString();
  latestVersion.trim();

  http.end();
  return latestVersion;
}

bool isNewVersionAvailable() {
  String latestVersion = getLatestVersion();

  if (latestVersion == "") {
    otaStatus = "Failed to check version";
    return false;
  }

  Serial.print("Current version: ");
  Serial.println(currentVersion);
  Serial.print("Latest version: ");
  Serial.println(latestVersion);

  if (latestVersion != String(currentVersion)) {
    otaStatus = "New version found: " + latestVersion;
    return true;
  }

  otaStatus = "Already latest version";
  return false;
}

void updateFromGitHub() {
  Serial.println("Starting GitHub OTA...");
  otaStatus = "Starting OTA";
  otaProgress = 0;

  if (WiFi.status() != WL_CONNECTED) {
    otaStatus = "WiFi not connected";
    Serial.println("WiFi not connected");
    return;
  }

  WiFiClientSecure client = makeSecureClient();
  HTTPClient http;

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(30000);
  http.setConnectTimeout(30000);
  http.setReuse(false);

  Serial.println("Connecting to firmware URL...");
  Serial.println(firmwareURL);
  otaStatus = "Connecting to GitHub";

  if (!http.begin(client, firmwareURL)) {
    otaStatus = "HTTP begin failed";
    Serial.println("HTTP begin failed");
    return;
  }

  http.addHeader("User-Agent", "ESP32");

  int httpCode = http.GET();
  Serial.printf("Firmware HTTP Code: %d\n", httpCode);

  if (httpCode != HTTP_CODE_OK) {
    otaStatus = "Failed to download firmware";
    Serial.println("Failed to download firmware");
    http.end();
    return;
  }

  int contentLength = http.getSize();
  Serial.printf("Firmware size: %d bytes\n", contentLength);

  if (contentLength <= 0) {
    otaStatus = "Invalid firmware size";
    Serial.println("Invalid firmware size");
    http.end();
    return;
  }

  if (!Update.begin(contentLength, U_FLASH)) {
    otaStatus = "Not enough space for OTA";
    Serial.println("Not enough space for OTA");
    Update.printError(Serial);
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();

  uint8_t buff[1024];
  int totalWritten = 0;
  unsigned long lastDataTime = millis();

  otaStatus = "Writing firmware";
  Serial.println("Starting firmware write...");

  while (http.connected() && totalWritten < contentLength) {
    size_t available = stream->available();

    if (available) {
      int readBytes = stream->readBytes(buff, min((int)available, 1024));
      int writtenBytes = Update.write(buff, readBytes);

      if (writtenBytes != readBytes) {
        otaStatus = "Write error";
        Serial.println("Write error!");
        Update.printError(Serial);
        Update.abort();
        http.end();
        return;
      }

      totalWritten += writtenBytes;
      lastDataTime = millis();
      otaProgress = (totalWritten * 100) / contentLength;

      Serial.printf("Progress: %d / %d bytes (%d%%)\n", totalWritten, contentLength, otaProgress);
    }

    if (millis() - lastDataTime > 30000) {
      otaStatus = "Download timeout";
      Serial.println("Download timeout!");
      Update.abort();
      http.end();
      return;
    }

    delay(1);
  }

  Serial.printf("Written: %d bytes\n", totalWritten);

  if (totalWritten != contentLength) {
    otaStatus = "Firmware write incomplete";
    Serial.println("Firmware write incomplete");
    Update.abort();
    http.end();
    return;
  }

  if (!Update.end()) {
    otaStatus = "Update failed";
    Serial.println("Update failed");
    Update.printError(Serial);
    http.end();
    return;
  }

  if (!Update.isFinished()) {
    otaStatus = "Update not finished";
    Serial.println("Update not finished");
    http.end();
    return;
  }

  otaProgress = 100;
  otaStatus = "OTA successful. Rebooting";
  Serial.println("GitHub OTA successful. Rebooting...");

  http.end();
  delay(2000);
  ESP.restart();
}

void checkVersionAndUpdate() {
  otaStatus = "Checking version";
  otaProgress = 0;

  if (isNewVersionAvailable()) {
    updateFromGitHub();
  }
}

void handleLED() {
  static unsigned long lastToggle = 0;
  static bool state = false;
  unsigned long blinkInterval = 1000;

  switch (ledMode) {
    case LED_IDLE:
      blinkInterval = 1000;
      break;

    case LED_CHECKING:
      blinkInterval = 500;
      break;

    case LED_UPDATING:
      blinkInterval = 100;
      break;

    case LED_ERROR:
      blinkInterval = 50;
      break;

    case LED_SUCCESS:
      digitalWrite(led, HIGH);
      return;
  }

  if (millis() - lastToggle >= blinkInterval) {
    lastToggle = millis();
    state = !state;
    digitalWrite(led, state);
  }
}

void setup(void) {
  pinMode(led, OUTPUT);
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.println("");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin(host)) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }

  Serial.println("mDNS responder started");

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });

  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });

  server.on("/otaStatus", HTTP_GET, []() {
    String json = "{";
    json += "\"status\":\"" + otaStatus + "\",";
    json += "\"progress\":" + String(otaProgress) + ",";
    json += "\"version\":\"" + String(currentVersion) + "\"";
    json += "}";

    server.send(200, "application/json", json);
  });

  server.on("/githubUpdate", HTTP_GET, []() {
    server.send(200, "text/plain", "Checking version...");
    delay(500);
    checkVersionAndUpdate();
  });

  server.begin();

  delay(3000);

  // AUTO UPDATE ON BOOT
 // checkVersionAndUpdate();
}

void loop(void) {
  server.handleClient();
  delay(1);

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(led, ledState);
  }
}