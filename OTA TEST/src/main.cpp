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
const char* currentVersion = "1.1.1";

// GitHub raw files
const char* firmwareURL = "https://raw.githubusercontent.com/tikitoy/Rikki-Playroom/main/firmware.bin";
const char* versionURL  = "https://raw.githubusercontent.com/tikitoy/Rikki-Playroom/main/version.txt";

const int led = 27;
unsigned long previousMillis = 0;
const long interval = 4000;
int ledState = 0;

#define LED_ON HIGH
#define LED_OFF LOW

String ledStatus = "BLINK";

bool ledManualMode = false;





WebServer server(80);

String otaStatus = "Idle";
int otaProgress = 0;

String latestVersionGlobal = "Unknown";
bool updateAvailableGlobal = false;


const char* loginIndex =
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body{font-family:Arial;background:#f4f6f8;margin:0;padding:20px;}"
".card{max-width:420px;margin:60px auto;background:white;padding:24px;border-radius:16px;box-shadow:0 2px 10px #bbb;}"
"h2{text-align:center;margin-top:0;}"
"input{width:100%;padding:14px;margin:8px 0 16px 0;font-size:18px;border:1px solid #ccc;border-radius:10px;box-sizing:border-box;}"
"button{width:100%;padding:16px;font-size:18px;border:0;border-radius:12px;background:#1976d2;color:white;}"
"</style>"

"<div class='card'>"
"<h2>ESP32 Login</h2>"
"<input type='text' id='userid' placeholder='Username'>"
"<input type='password' id='pwd' placeholder='Password'>"
"<button onclick='checkLogin()'>Login</button>"
"</div>"

"<script>"
"function checkLogin(){"
"var u=document.getElementById('userid').value;"
"var p=document.getElementById('pwd').value;"
"if(u=='admin' && p=='admin'){"
"window.location.href='/serverIndex';"
"}else{"
"alert('Error Password or Username');"
"}"
"}"
"</script>";


const char* serverIndex =
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body{font-family:Arial;background:#f4f6f8;margin:0;padding:16px;color:#222;}"
".card{background:white;padding:18px;margin-bottom:16px;border-radius:14px;box-shadow:0 2px 8px #ccc;}"
"h2,h3{margin-top:0;}"
"button{width:100%;padding:16px;margin:8px 0;font-size:18px;border:0;border-radius:12px;background:#1976d2;color:white;}"
"button.off{background:#d32f2f;}"
"button.blink{background:#388e3c;}"
"progress{width:100%;height:28px;}"
"</style>"

"<div class='card'>"
"<h2>ESP32 Control Panel</h2>"
"<p>Current version: <b id='currentVer'>Loading...</b></p>"
"<p>Latest version: <b id='latestVer'>Loading...</b></p>"
"<p>Update: <b id='updateBadge'>Checking...</b></p>"
"</div>"

"<div class='card'>"
"<h3>LED Control</h3>"
"<button onclick='ledOn()'>LED ON</button>"
"<button class='off' onclick='ledOff()'>LED OFF</button>"
"<button class='blink' onclick='ledBlink()'>LED BLINK</button>"
"<p>LED Status: <b id='ledStatus'>Unknown</b></p>"
"</div>"

"<div class='card'>"
"<h3>GitHub OTA Update</h3>"
"<button onclick='startOTA()'>Check Version and Update</button>"
"<p>Status: <b id='status'>Idle</b></p>"
"<p>Progress: <b id='progress'>0%</b></p>"
"<progress id='bar' value='0' max='100'></progress>"
"</div>"

"<script>"
"function refreshStatus(){"
"fetch('/otaStatus').then(r=>r.json()).then(d=>{"
"status.innerHTML=d.status;"
"progress.innerHTML=d.progress+'%';"
"bar.value=d.progress;"
"currentVer.innerHTML=d.current;"
"latestVer.innerHTML=d.latest;"
"updateBadge.innerHTML=d.updateAvailable?'AVAILABLE':'UP TO DATE';"
"ledStatus.innerHTML=d.led;"
"});"
"}"
"function ledOn(){fetch('/ledOn').then(()=>refreshStatus());}"
"function ledOff(){fetch('/ledOff').then(()=>refreshStatus());}"
"function ledBlink(){fetch('/ledBlink').then(()=>refreshStatus());}"
"function startOTA(){fetch('/githubUpdate').then(r=>r.text()).then(t=>{status.innerHTML=t;});}"
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
  latestVersionGlobal = latestVersion;

  if (latestVersion == "") {
    otaStatus = "Failed to check version";
    updateAvailableGlobal = false;
    return false;
  }

  Serial.print("Current version: ");
  Serial.println(currentVersion);
  Serial.print("Latest version: ");
  Serial.println(latestVersion);

  if (latestVersion != String(currentVersion)) {
    otaStatus = "New version found: " + latestVersion;
    updateAvailableGlobal = true;
    return true;
  }

  otaStatus = "Already latest version";
  updateAvailableGlobal = false;
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
    server.handleClient();

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

void blinkLED() {
  if (ledManualMode) return;

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(led, ledState);
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

  // Initial version check for webpage display only
  latestVersionGlobal = getLatestVersion();
  if (latestVersionGlobal == "") {
    latestVersionGlobal = "Unknown";
    updateAvailableGlobal = false;
  } else {
    updateAvailableGlobal = latestVersionGlobal != String(currentVersion);
  }

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
  json += "\"current\":\"" + String(currentVersion) + "\",";
  json += "\"latest\":\"" + latestVersionGlobal + "\",";
  json += "\"updateAvailable\":" + String(updateAvailableGlobal ? "true" : "false") + ",";
  json += "\"led\":\"" + ledStatus + "\"";
  json += "}";

  server.send(200, "application/json", json);
});

  server.on("/githubUpdate", HTTP_GET, []() {
    server.send(200, "text/plain", "Checking version...");
    delay(500);
    checkVersionAndUpdate();
  });

  server.on("/ledOn", HTTP_GET, []() {
  ledManualMode = true;
  ledState = LED_ON;
  digitalWrite(led, LED_ON);
  ledStatus = "ON";
  server.send(200, "text/plain", "LED ON");
});

server.on("/ledOff", HTTP_GET, []() {
  ledManualMode = true;
  ledState = LED_OFF;
  digitalWrite(led, LED_OFF);
  ledStatus = "OFF";
  server.send(200, "text/plain", "LED OFF");
});

server.on("/ledBlink", HTTP_GET, []() {
  ledManualMode = false;
  ledStatus = "BLINK";
  previousMillis = millis();
  server.send(200, "text/plain", "LED BLINK");
});


  server.begin();

  delay(3000);

  // Optional auto update on boot:
  // checkVersionAndUpdate();
}

void loop(void) {
  server.handleClient();
  blinkLED();
  delay(1);
}