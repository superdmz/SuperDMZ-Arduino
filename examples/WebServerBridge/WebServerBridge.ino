// SuperDMZ — WebServerBridge
//
// Shows integration with ESPAsyncWebServer (the common choice for serious
// IoT projects). Serve sensors, control GPIOs, OTA — all via
// https://<your-host>.dmzgate.com.

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SuperDMZ.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* SUPERDMZ_TOKEN = "PASTE_YOUR_TOKEN_HERE";

const uint16_t WEB_PORT = 80;
AsyncWebServer server(WEB_PORT);
SuperDMZ       tunnel;

float readTempC() {
  // Replace with your actual sensor reading (DS18B20, DHT, BMP280, etc.)
  return 22.5 + (random(-50, 50) / 10.0);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[wifi] connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(200); Serial.print('.'); }
  Serial.printf("\n[wifi] OK, IP = %s\n", WiFi.localIP().toString().c_str());

  // ── Rotas do AsyncWebServer ────────────────────────────────────────────────
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html",
      "<!doctype html><meta charset='utf-8'>"
      "<style>body{font-family:system-ui;background:#0a0f1e;color:#e5e7eb;padding:2rem}"
      "h1{color:#60a5fa}a{color:#93c5fd}</style>"
      "<h1>ESP32 IoT Dashboard</h1>"
      "<p><a href='/api/temp'>GET /api/temp</a> &mdash; leitura JSON</p>"
      "<p><a href='/api/led?state=on'>GET /api/led?state=on</a> &mdash; controle LED</p>");
  });

  server.on("/api/temp", HTTP_GET, [](AsyncWebServerRequest* req) {
    StaticJsonDocument<128> doc;
    doc["temp_c"] = readTempC();
    doc["uptime_s"] = millis() / 1000;
    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
  });

  server.on("/api/led", HTTP_GET, [](AsyncWebServerRequest* req) {
    String state = req->hasParam("state") ? req->getParam("state")->value() : "off";
    bool on = (state == "on" || state == "1");
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
    req->send(200, "application/json",
              String("{\"led\":\"") + (on ? "on" : "off") + "\"}");
  });

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "not found");
  });

  pinMode(LED_BUILTIN, OUTPUT);
  server.begin();
  Serial.printf("[http] AsyncWebServer on port %u\n", WEB_PORT);

  // ── SuperDMZ ───────────────────────────────────────────────────────────────
  tunnel.onStatus([](bool online, const char* publicUrl) {
    Serial.printf("[tunnel] %s %s\n", online ? "ONLINE" : "OFFLINE", publicUrl);
  });
  tunnel.begin(SUPERDMZ_TOKEN, WEB_PORT);
}

void loop() {
  tunnel.loop();
  // AsyncWebServer is non-blocking, no handleClient() needed.
  // Put your regular loop work here (sensors, control, etc.)
  delay(10);
}
