// SuperDMZ — ProvisioningPortal
//
// Salva WiFi + token em NVS (Preferences). Na primeira boot (ou apos reset
// via botao no GPIO0) sobe um Soft-AP "SuperDMZ-Setup" + captive portal pra
// usuario configurar via celular. Depois reboota e roda em modo cliente.
//
// Util pra produto comercial: voce nao precisa recompilar pra cada cliente,
// nem distribuir token em texto plano no firmware.

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <SuperDMZ.h>

#define RESET_PIN  0   // botao BOOT (GPIO0) — segure 3s no boot pra resetar config

Preferences prefs;
WebServer    portalSrv(80);
DNSServer    dnsSrv;
SuperDMZ     tunnel;

bool portalMode = false;

String ssidSaved, passSaved, tokenSaved;

const char* PORTAL_HTML = R"HTML(
<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>SuperDMZ Setup</title>
<style>body{font-family:system-ui;background:#0a0f1e;color:#e5e7eb;padding:1.5rem;max-width:480px;margin:0 auto}
h1{color:#60a5fa}label{display:block;margin-top:.8rem;font-size:.9rem}
input{width:100%;padding:.6rem;border:1px solid #334155;background:#1e293b;color:#e5e7eb;border-radius:.4rem;box-sizing:border-box}
button{margin-top:1.2rem;width:100%;padding:.75rem;background:#2563eb;color:#fff;border:0;border-radius:.4rem;font-weight:600}
.hint{font-size:.78rem;color:#94a3b8;margin-top:.2rem}</style>
</head><body><h1>Configurar SuperDMZ</h1>
<form action='/save' method='POST'>
<label>WiFi SSID<input name='ssid' required></label>
<label>WiFi senha<input name='pass' type='password'></label>
<label>SuperDMZ token<input name='token' required pattern='[a-f0-9]{32,64}'>
<div class='hint'>48 caracteres hex, do painel superdmz.com</div></label>
<button>Salvar e reiniciar</button>
</form></body></html>)HTML";

void enterPortalMode() {
  portalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("SuperDMZ-Setup", "12345678");
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[portal] AP up at %s\n", ip.toString().c_str());

  dnsSrv.start(53, "*", ip);

  portalSrv.on("/", []() { portalSrv.send(200, "text/html", PORTAL_HTML); });
  portalSrv.on("/save", HTTP_POST, []() {
    prefs.begin("superdmz", false);
    prefs.putString("ssid",  portalSrv.arg("ssid"));
    prefs.putString("pass",  portalSrv.arg("pass"));
    prefs.putString("token", portalSrv.arg("token"));
    prefs.end();
    portalSrv.send(200, "text/html",
      "<h2 style='font-family:system-ui'>Salvo. Reiniciando...</h2>");
    delay(1500);
    ESP.restart();
  });
  portalSrv.onNotFound([]() {
    portalSrv.sendHeader("Location", "/");
    portalSrv.send(302, "text/plain", "");
  });
  portalSrv.begin();
}

bool shouldReset() {
  pinMode(RESET_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_PIN) == LOW) {
    Serial.println("[boot] BOOT button held — waiting 3s to confirm reset");
    uint32_t held = 0;
    while (digitalRead(RESET_PIN) == LOW && held < 3000) {
      delay(50); held += 50;
    }
    return held >= 3000;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  if (shouldReset()) {
    Serial.println("[reset] clearing NVS config");
    prefs.begin("superdmz", false);
    prefs.clear();
    prefs.end();
  }

  prefs.begin("superdmz", true);
  ssidSaved  = prefs.getString("ssid", "");
  passSaved  = prefs.getString("pass", "");
  tokenSaved = prefs.getString("token", "");
  prefs.end();

  if (ssidSaved.length() == 0 || tokenSaved.length() == 0) {
    Serial.println("[boot] no config — entering portal mode");
    enterPortalMode();
    return;
  }

  Serial.printf("[boot] connecting to WiFi '%s'\n", ssidSaved.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssidSaved.c_str(), passSaved.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) delay(200);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[boot] WiFi timeout — entering portal mode");
    enterPortalMode();
    return;
  }
  Serial.printf("[wifi] OK, IP = %s\n", WiFi.localIP().toString().c_str());

  // Aqui voce sobe seu WebServer normal (omitido por brevidade)
  // server.begin();

  tunnel.onStatus([](bool online, const char* url) {
    Serial.printf("[tunnel] %s %s\n", online ? "ONLINE" : "OFFLINE", url);
  });
  tunnel.begin(tokenSaved.c_str(), 80);
}

void loop() {
  if (portalMode) {
    dnsSrv.processNextRequest();
    portalSrv.handleClient();
  } else {
    tunnel.loop();
  }
}
