// SuperDMZ — HelloWorld
//
// O exemplo mais simples: ESP32 sobe um WebServer na porta 80 servindo "Hello"
// e o SuperDMZ expoe ele em https://<seu-host>.dmzgate.com.
//
// Pre-requisitos:
//   1. Crie um tunel HTTP no painel (https://superdmz.com/?page=tunnels&action=new),
//      anote o token. Use local_port = 80.
//   2. Coloque o token e o WiFi abaixo.
//   3. Compile e grave.
//   4. Em ~5 segundos a serial deve mostrar "ONLINE: https://<host>.dmzgate.com".

#include <WiFi.h>
#include <WebServer.h>
#include <SuperDMZ.h>

const char* WIFI_SSID = "SUA_REDE_WIFI";
const char* WIFI_PASS = "SUA_SENHA_WIFI";
const char* SUPERDMZ_TOKEN = "COLE_SEU_TOKEN_AQUI";  // 48 hex chars

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
