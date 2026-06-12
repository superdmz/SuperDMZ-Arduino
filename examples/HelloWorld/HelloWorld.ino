// SuperDMZ — HelloWorld
//
// The simplest possible example: ESP32 brings up a WebServer on port 80
// serving "Hello", and SuperDMZ exposes it at https://<your-host>.dmzgate.com.
//
// Prerequisites:
//   1. Create an HTTP tunnel in the panel (https://superdmz.com/?page=tunnels&action=new),
//      copy the token. Use local_port = 80.
//   2. Paste your token and WiFi below.
//   3. Compile and upload.
//   4. Within ~5 seconds Serial should print "ONLINE: https://<host>.dmzgate.com".

#include <WiFi.h>
#include <WebServer.h>
#include <SuperDMZ.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* SUPERDMZ_TOKEN = "PASTE_YOUR_TOKEN_HERE";  // 48 hex chars

WebServer server(80);
SuperDMZ   tunnel;

void handleRoot() {
  String html = F("<!doctype html><html><head><meta charset='utf-8'><title>ESP32 + SuperDMZ</title>"
                  "<style>body{font-family:system-ui;background:#0a0f1e;color:#e5e7eb;"
                  "padding:3rem;text-align:center}h1{color:#60a5fa}</style></head><body>"
                  "<h1>Hello from ESP32 via SuperDMZ</h1>"
                  "<p>Uptime: ");
  html += String(millis() / 1000);
  html += F(" s</p></body></html>");
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[boot] ESP32 + SuperDMZ");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[wifi] connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(200); Serial.print('.'); }
  Serial.printf("\n[wifi] OK, IP = %s\n", WiFi.localIP().toString().c_str());

  server.on("/", handleRoot);
  server.begin();
  Serial.println("[http] local WebServer on port 80");

  tunnel.onStatus([](bool online, const char* publicUrl) {
    Serial.printf("[tunnel] %s -> %s\n", online ? "ONLINE" : "OFFLINE", publicUrl);
  });
  tunnel.begin(SUPERDMZ_TOKEN, 80);
}

void loop() {
  server.handleClient();
  tunnel.loop();
}
