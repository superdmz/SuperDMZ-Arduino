// SuperDMZ — HelloWorld
//
// The simplest possible example: ESP32 brings up a WebServer on port 80
// serving "Hello", and SuperDMZ exposes it at https://<your-host>.dmzgate.com.
//
// Prerequisites:
//   1. Create an HTTP tunnel in the panel (https://superdmz.com/?page=tunnels&action=new),
//      copy the token. Use local_port = 80.
//   2. Paste your token and WiFi credentials below.
//   3. Compile and upload (Tools > Board > ESP32 ... pick yours).
//   4. Within ~5 seconds Serial should print "ONLINE: https://<host>.dmzgate.com".
//
// Each example must live in its own folder (Arduino IDE concatenates every
// .ino in the same folder into one sketch).

#include <WiFi.h>
#include <WebServer.h>
#include <SuperDMZ.h>

const char* WIFI_SSID       = "YOUR_WIFI_SSID";
const char* WIFI_PASS       = "YOUR_WIFI_PASSWORD";
const char* SUPERDMZ_TOKEN  = "PASTE_YOUR_TOKEN_HERE";   // 48 hex chars from panel

WebServer  server(80);
SuperDMZ   tunnel;

void handleRoot() {
  String html = F("<!doctype html><html><head><meta charset='utf-8'>"
                  "<title>ESP32 + SuperDMZ</title>"
                  "<style>body{font-family:system-ui,sans-serif;background:#0a0f1e;color:#e5e7eb;"
                  "padding:3rem;text-align:center}h1{color:#60a5fa;margin-bottom:.6rem}"
                  ".k{color:#94a3b8;font-size:.85rem}</style></head><body>"
                  "<h1>Hello from ESP32 via SuperDMZ</h1>"
                  "<p class='k'>Uptime: ");
  html += String(millis() / 1000);
  html += F(" s &middot; IP: ");
  html += WiFi.localIP().toString();
  html += F("</p></body></html>");
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[boot] ESP32 + SuperDMZ HelloWorld");

  WiFi.mode(WIFI_STA);
  // Knobs that help ESP32-C3/S3 stay associated with consumer routers:
  //  - sleep OFF: the AP can't deauth us during light beacons we miss
  //  - max TX power: shorter retransmits
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[wifi] connecting");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) { delay(200); Serial.print('.'); }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[wifi] FAILED — check SSID/pass and reboot");
    return;
  }
  Serial.printf("\n[wifi] OK, IP = %s, RSSI = %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());

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

  // Cheap watchdog: if STA dropped (rare with setAutoReconnect=true but
  // still happens on flaky APs), force a reconnect every 30 s.
  static uint32_t lastCheckMs = 0;
  if (millis() - lastCheckMs > 30000) {
    lastCheckMs = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[wifi] disconnected — forcing reconnect()");
      WiFi.reconnect();
    }
  }
}
