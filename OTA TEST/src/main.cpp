#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <HTTPClient.h>

const char* host = "esp32";
const char* ssid = "Xiaomi 15T Pro";
const char* password = "ctxtmjtm95vdwkm";

const char* firmwareURL = "https://raw.githubusercontent.com/tikitoy/Rikki-Playroom/main/firmware.bin";

const int led = 2;
unsigned long previousMillis = 0;
const long interval = 1000;
int ledState = LOW;

WebServer server(80);

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
"<h3>Manual OTA Upload</h3>"
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
"<input type='file' name='update'>"
"<input type='submit' value='Upload Firmware'>"
"</form>"
"<div id='prg'>progress: 0%</div>"
"<hr>"
"<h3>GitHub OTA Update</h3>"
"<button onclick=\"location.href='/githubUpdate'\">Update from GitHub</button>"
"<script>"
"$('form').submit(function(e){"
"e.preventDefault();"
"var form = $('#upload_form')[0];"
"var data = new FormData(form);"
"$.ajax({"
"url: '/update',"
"type: 'POST',"
"data: data,"
"contentType: false,"
"processData:false,"
"xhr: function() {"
"var xhr = new window.XMLHttpRequest();"
"xhr.upload.addEventListener('progress', function(evt) {"
"if (evt.lengthComputable) {"
"var per = evt.loaded / evt.total;"
"$('#prg').html('progress: ' + Math.round(per*100) + '%');"
"}"
"}, false);"
"return xhr;"
"},"
"success:function(d, s) {"
"$('#prg').html('Upload complete. Rebooting...');"
"},"
"error: function (a, b, c) {"
"$('#prg').html('Upload failed');"
"}"
"});"
"});"
"</script>";

void updateFromGitHub() {
  Serial.println("Starting GitHub OTA...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(30000);
  http.setConnectTimeout(30000);
  http.setReuse(false);

  Serial.println("Connecting to firmware URL...");
  Serial.println(firmwareURL);

  if (!http.begin(client, firmwareURL)) {
    Serial.println("HTTP begin failed");
    return;
  }

  http.addHeader("User-Agent", "ESP32");

  int httpCode = http.GET();
  Serial.printf("HTTP Code: %d\n", httpCode);

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Failed to download firmware");
    http.end();
    return;
  }

  int contentLength = http.getSize();
  Serial.printf("Firmware size: %d bytes\n", contentLength);

  if (contentLength <= 0) {
    Serial.println("Invalid firmware size");
    http.end();
    return;
  }

  if (!Update.begin(contentLength, U_FLASH)) {
    Serial.println("Not enough space for OTA");
    Update.printError(Serial);
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();

  uint8_t buff[1024];
  int totalWritten = 0;
  unsigned long lastDataTime = millis();

  Serial.println("Starting firmware write...");

  while (http.connected() && totalWritten < contentLength) {
    size_t available = stream->available();

    if (available) {
      int readBytes = stream->readBytes(buff, min((int)available, 1024));
      int writtenBytes = Update.write(buff, readBytes);

      if (writtenBytes != readBytes) {
        Serial.println("Write error!");
        Update.printError(Serial);
        Update.abort();
        http.end();
        return;
      }

      totalWritten += writtenBytes;
      lastDataTime = millis();

      Serial.printf("Progress: %d / %d bytes\n", totalWritten, contentLength);
    }

    if (millis() - lastDataTime > 30000) {
      Serial.println("Download timeout!");
      Update.abort();
      http.end();
      return;
    }

    delay(1);
  }

  Serial.printf("Written: %d bytes\n", totalWritten);

  if (totalWritten != contentLength) {
    Serial.println("Firmware write incomplete");
    Update.printError(Serial);
    Update.abort();
    http.end();
    return;
  }

  if (!Update.end()) {
    Serial.println("Update failed");
    Update.printError(Serial);
    http.end();
    return;
  }

  if (!Update.isFinished()) {
    Serial.println("Update not finished");
    http.end();
    return;
  }

  Serial.println("GitHub OTA successful. Rebooting...");
  http.end();
  delay(1000);
  ESP.restart();
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

  server.on("/githubUpdate", HTTP_GET, []() {
    server.send(200, "text/plain", "Starting GitHub OTA update. Check Serial Monitor.");
    delay(500);
    updateFromGitHub();
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");

    if (Update.hasError()) {
      server.send(200, "text/plain", "FAIL");
    } else {
      server.send(200, "text/plain", "OK");
      delay(1000);
      ESP.restart();
    }
  }, []() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Manual OTA Start: %s\n", upload.filename.c_str());

      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        Update.printError(Serial);
      }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }

    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("Manual OTA Success: %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }

    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.end();
      Serial.println("Manual OTA Aborted");
    }
  });

  server.begin();
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