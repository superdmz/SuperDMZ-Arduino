// SuperDMZ — SmartIoT (production template)
//
// This is the "build a real product on top of me" example. It combines:
//   - The full ProvisioningPortal flow (captive portal with WiFi scan,
//     NVS persistence, GPIO0 button reconfig/factory reset, in-place AP↔STA).
//   - A telemetry endpoint /api/telemetry returning live sensor readings.
//     The example uses synthetic temperature/humidity values so it compiles
//     and runs on any board out of the box; replace readTemp()/readHumidity()
//     with your real sensor (DS18B20, BME280, SHT3x, etc.).
//   - A live dashboard at / served via the SuperDMZ tunnel with three cards
//     (SuperDMZ tunnel, WiFi, Sensors) and a Chart.js-free sparkline drawn
//     in vanilla JS for the temperature history.
//   - OTA over the tunnel: POST to /api/ota with a binary .bin file from
//     anywhere on the internet, the device flashes and reboots.
//
// All of this fits inside the SuperDMZ tunnel — your IoT device is now
// reachable globally at https://<your-host>.dmzgate.com with full HTTPS
// and zero router configuration.

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <SuperDMZ.h>

#define RESET_PIN          0
#define HOLD_RECONFIG_MS   3000
#define HOLD_FACTORY_MS    10000
#define AP_PASSWORD        "12345678"
#define TELEMETRY_HISTORY  60   // samples (1 per second → 1 min of history)

Preferences prefs;
WebServer   server(80);
DNSServer   dnsServer;
SuperDMZ    tunnel;

enum AppState { STATE_BOOT, STATE_AP, STATE_STA };
AppState appState = STATE_BOOT;

String savedSSID, savedPass, savedToken;
String apSSID;
uint32_t bootMillis = 0;
uint32_t resetHoldStart = 0;

// ── Forward declarations (Arduino IDE 3.x with esp32 core 3.x doesn't always
// ── auto-generate prototypes for functions called from within lambdas/handlers
// ── above their definition).
void enterAPMode(const String& reason);
void enterSTAMode();

// ── Sensor history ring buffer ───────────────────────────────────────────────
float    tempHistory[TELEMETRY_HISTORY];
uint8_t  histHead = 0;
uint32_t lastSampleMs = 0;

// ─── Synthetic sensor readings (replace with your hardware) ──────────────────
float readTempC() {
  // Drift around 22 °C with small random walk
  static float t = 22.0;
  t += (random(-20, 20) / 100.0);
  if (t < 15) t = 15; if (t > 30) t = 30;
  return t;
}

float readHumidity() {
  static float h = 55.0;
  h += (random(-15, 15) / 100.0);
  if (h < 30) h = 30; if (h > 80) h = 80;
  return h;
}

// ─── NVS ──────────────────────────────────────────────────────────────────────
void loadConfig() {
  prefs.begin("superdmz", true);
  savedSSID  = prefs.getString("ssid",  "");
  savedPass  = prefs.getString("pass",  "");
  savedToken = prefs.getString("token", "");
  prefs.end();
}

void saveConfig(const String& ssid, const String& pass, const String& token) {
  prefs.begin("superdmz", false);
  prefs.putString("ssid",  ssid);
  prefs.putString("pass",  pass);
  prefs.putString("token", token);
  prefs.end();
}

void clearWiFiOnly() {
  prefs.begin("superdmz", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

void factoryReset() {
  prefs.begin("superdmz", false);
  prefs.clear();
  prefs.end();
}

// ─── AP-mode portal ──────────────────────────────────────────────────────────
const char PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SuperDMZ Setup</title>
<style>
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#0a0f1e;color:#e5e7eb;padding:1.5rem;max-width:460px;margin:0 auto}
h1{color:#60a5fa;font-size:1.4rem;margin:0 0 .25rem}
p.lead{color:#94a3b8;font-size:.85rem;margin:0 0 1.25rem}
label{display:block;margin-top:.85rem;font-size:.85rem;font-weight:500}
input,select{width:100%;padding:.65rem .75rem;border:1px solid #334155;background:#1e293b;color:#e5e7eb;border-radius:.5rem;box-sizing:border-box;font-size:.95rem}
input:focus,select:focus{outline:none;border-color:#60a5fa}
.hint{font-size:.72rem;color:#94a3b8;margin-top:.25rem}
button{margin-top:1.4rem;width:100%;padding:.8rem;background:#2563eb;color:#fff;border:0;border-radius:.5rem;font-weight:600;font-size:.95rem;cursor:pointer}
button:hover{background:#1d4ed8}
button:disabled{opacity:.5;cursor:not-allowed}
.scan-btn{margin-top:.35rem;background:#334155;color:#cbd5e1;padding:.45rem .8rem;font-size:.78rem;border-radius:.4rem;border:0;cursor:pointer;float:right;width:auto}
</style></head><body>
<h1>SuperDMZ Setup</h1>
<p class="lead">Configure WiFi and your SuperDMZ token. The device switches to client mode without rebooting.</p>
<form action="/save" method="POST" onsubmit="document.getElementById('save').disabled=true;document.getElementById('save').textContent='Saving...'">
<label>WiFi network
  <button type="button" class="scan-btn" onclick="scan()">Scan</button>
  <select name="ssid" id="ssid" required><option value="">Pick a network…</option></select>
</label>
<div class="hint" id="scan-status">Tap Scan to list nearby networks.</div>
<label>WiFi password<input name="pass" type="password" autocomplete="off"></label>
<label>SuperDMZ token<input name="token" required pattern="[a-f0-9]{32,64}" autocomplete="off">
<div class="hint">48 hex chars from the superdmz.com panel.</div></label>
<button type="submit" id="save">Save and connect</button>
</form>
<script>
function scan(){var s=document.getElementById('scan-status');s.textContent='Scanning…';
fetch('/scan').then(r=>r.json()).then(nets=>{
var sel=document.getElementById('ssid');sel.innerHTML='<option value="">Pick a network…</option>';
nets.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
var b=n.rssi>-55?'▂▄▆█':n.rssi>-70?'▂▄▆_':n.rssi>-80?'▂▄__':'▂___';
var o=document.createElement('option');o.value=n.ssid;
o.textContent=n.ssid+'  '+b+'  '+n.rssi+'dBm'+(n.secure?' 🔒':'');sel.appendChild(o);});
s.textContent=nets.length+' network'+(nets.length===1?'':'s')+' found.';
}).catch(e=>{s.textContent='Scan failed: '+e;});}
</script></body></html>
)HTML";

void handlePortalRoot() { server.send_P(200, "text/html", PORTAL_HTML); }

void handleScan() {
  int n = WiFi.scanNetworks(false, true);
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.createNestedObject();
    o["ssid"]   = WiFi.SSID(i);
    o["rssi"]   = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
  WiFi.scanDelete();
}

void handleSave() {
  String ssid  = server.arg("ssid");
  String pass  = server.arg("pass");
  String token = server.arg("token");
  if (ssid.length() == 0 || token.length() < 32) {
    server.send(400, "text/plain", "missing ssid or token");
    return;
  }
  saveConfig(ssid, pass, token);
  savedSSID = ssid; savedPass = pass; savedToken = token;
  server.send(200, "text/html",
    "<style>body{font-family:system-ui;background:#0a0f1e;color:#e5e7eb;padding:2rem;text-align:center}h2{color:#34d399}</style>"
    "<h2>Saved. Connecting to '" + ssid + "'…</h2>");
  delay(800);
  enterSTAMode();
}

void enterAPMode(const String& reason) {
  Serial.printf("[portal] entering AP mode (%s)\n", reason.c_str());
  appState = STATE_AP;
  if (apSSID.length() == 0) {
    uint64_t mac = ESP.getEfuseMac();
    char suf[5]; snprintf(suf, sizeof(suf), "%04X", (uint16_t)(mac >> 32));
    apSSID = String("SuperDMZ-Setup-") + suf;
  }
  WiFi.disconnect(true, true); delay(50);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID.c_str(), AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[portal] AP up: SSID=%s pass=%s IP=%s\n",
                apSSID.c_str(), AP_PASSWORD, ip.toString().c_str());
  dnsServer.start(53, "*", ip);
  server.close();
  server.on("/", HTTP_GET, handlePortalRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
  });
  server.begin();
}

// ─── STA-mode dashboard with sensors + OTA ───────────────────────────────────
const char DASH_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SuperDMZ ESP32 — SmartIoT</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#0a0f1e;color:#e5e7eb;margin:0;padding:1.5rem;max-width:880px;margin:0 auto}
header{display:flex;align-items:center;gap:.75rem;margin-bottom:1.5rem}
header h1{font-size:1.25rem;margin:0;color:#fff;font-weight:600}
.badge{padding:.2rem .65rem;border-radius:1rem;font-size:.72rem;font-weight:600;letter-spacing:.04em}
.badge.online{background:rgba(16,185,129,.15);color:#34d399;border:1px solid rgba(16,185,129,.3)}
.badge.offline{background:rgba(239,68,68,.15);color:#f87171;border:1px solid rgba(239,68,68,.3)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:1rem}
.card{background:#1e293b;border:1px solid #334155;border-radius:.65rem;padding:1.1rem 1.25rem}
.card h2{margin:0 0 .9rem;font-size:.78rem;font-weight:700;color:#94a3b8;text-transform:uppercase;letter-spacing:.08em}
.row{display:flex;justify-content:space-between;align-items:center;padding:.4rem 0;border-bottom:1px solid #334155;font-size:.9rem}
.row:last-child{border-bottom:0}
.row .k{color:#94a3b8}
.row .v{color:#e5e7eb;font-weight:500;font-family:ui-monospace,monospace}
.big{font-size:2.4rem;font-weight:700;color:#60a5fa;margin:.4rem 0 .2rem;font-family:ui-monospace,monospace}
.big small{font-size:1rem;color:#94a3b8;font-weight:500;margin-left:.3rem}
.spark{width:100%;height:60px;display:block;margin-top:.4rem}
.actions{display:flex;gap:.5rem;margin-top:1rem;flex-wrap:wrap}
.btn{flex:1;min-width:120px;padding:.6rem;border:1px solid #475569;background:#334155;color:#e5e7eb;border-radius:.45rem;cursor:pointer;font-size:.82rem;font-weight:500}
.btn:hover{background:#475569}
.btn.danger{border-color:rgba(239,68,68,.4);background:rgba(239,68,68,.1);color:#f87171}
.btn.danger:hover{background:rgba(239,68,68,.2)}
footer{text-align:center;color:#475569;font-size:.72rem;margin-top:1.5rem}
</style></head><body>
<header><h1>SuperDMZ ESP32 — SmartIoT</h1><span id="status-badge" class="badge online">ONLINE</span></header>

<div class="grid">
  <div class="card">
    <h2>🌡 Temperature</h2>
    <div class="big" id="temp">—<small>°C</small></div>
    <canvas class="spark" id="spark"></canvas>
  </div>
  <div class="card">
    <h2>💧 Humidity</h2>
    <div class="big" id="humi">—<small>%</small></div>
    <div class="row"><span class="k">Samples</span><span class="v" id="samples">0</span></div>
  </div>
  <div class="card">
    <h2>🔌 SuperDMZ tunnel</h2>
    <div class="row"><span class="k">Public URL</span><span class="v" id="public-url">—</span></div>
    <div class="row"><span class="k">Bytes in</span><span class="v" id="bytes-in">—</span></div>
    <div class="row"><span class="k">Bytes out</span><span class="v" id="bytes-out">—</span></div>
  </div>
  <div class="card">
    <h2>📶 WiFi / System</h2>
    <div class="row"><span class="k">SSID</span><span class="v" id="wifi-ssid">—</span></div>
    <div class="row"><span class="k">IP</span><span class="v" id="wifi-ip">—</span></div>
    <div class="row"><span class="k">Signal</span><span class="v" id="rssi">—</span></div>
    <div class="row"><span class="k">Free heap</span><span class="v" id="free-heap">—</span></div>
    <div class="row"><span class="k">Uptime</span><span class="v" id="uptime">—</span></div>
    <div class="actions">
      <button type="button" class="btn" onclick="if(confirm('Reconfigure WiFi? Token kept.'))fetch('/api/reconfig',{method:'POST'}).then(_=>alert('Back to AP mode. Connect to SuperDMZ-Setup WiFi.'))">Reconfigure</button>
      <button type="button" class="btn danger" onclick="if(confirm('Reboot device?'))fetch('/api/reboot',{method:'POST'})">Reboot</button>
    </div>
  </div>
</div>

<footer>Auto-refresh 2 s · <a href="https://superdmz.com" style="color:#475569">superdmz.com</a> · See <code>/api/telemetry</code> for JSON</footer>

<script>
function fmt(n){if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';if(n<1073741824)return (n/1048576).toFixed(1)+' MB';return (n/1073741824).toFixed(1)+' GB';}
function fmtUp(s){var h=Math.floor(s/3600);var m=Math.floor((s%3600)/60);var x=s%60;return h+'h '+m+'m '+x+'s';}
function drawSpark(hist){var c=document.getElementById('spark');var w=c.width=c.offsetWidth*2;var h=c.height=120;var ctx=c.getContext('2d');ctx.clearRect(0,0,w,h);if(!hist.length)return;var mn=Math.min.apply(null,hist),mx=Math.max.apply(null,hist),rg=mx-mn||1;ctx.beginPath();ctx.lineWidth=3;ctx.strokeStyle='#60a5fa';for(var i=0;i<hist.length;i++){var x=(i/(hist.length-1))*w;var y=h-((hist[i]-mn)/rg)*h*0.85-h*0.075;if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);}ctx.stroke();}
function refresh(){
  fetch('/api/telemetry').then(r=>r.json()).then(d=>{
    document.getElementById('status-badge').textContent = d.online?'ONLINE':'OFFLINE';
    document.getElementById('status-badge').className   = 'badge '+(d.online?'online':'offline');
    document.getElementById('temp').innerHTML = d.temp_c.toFixed(1)+'<small>°C</small>';
    document.getElementById('humi').innerHTML = d.humi_pct.toFixed(0)+'<small>%</small>';
    document.getElementById('samples').textContent = d.samples;
    document.getElementById('public-url').textContent = d.public_url||'—';
    document.getElementById('bytes-in').textContent  = fmt(d.bytes_in);
    document.getElementById('bytes-out').textContent = fmt(d.bytes_out);
    document.getElementById('wifi-ssid').textContent = d.wifi_ssid;
    document.getElementById('wifi-ip').textContent   = d.wifi_ip;
    document.getElementById('rssi').textContent      = d.rssi+' dBm';
    document.getElementById('free-heap').textContent = fmt(d.free_heap);
    document.getElementById('uptime').textContent    = fmtUp(d.uptime);
    drawSpark(d.temp_history);
  });
}
refresh(); setInterval(refresh, 2000);
</script></body></html>
)HTML";

void handleDashRoot() { server.send_P(200, "text/html", DASH_HTML); }

void handleTelemetry() {
  DynamicJsonDocument doc(2048);
  doc["online"]      = tunnel.isOnline();
  doc["public_url"]  = tunnel.publicUrl();
  doc["bytes_in"]    = tunnel.bytesIn();
  doc["bytes_out"]   = tunnel.bytesOut();
  doc["wifi_ssid"]   = WiFi.SSID();
  doc["wifi_ip"]     = WiFi.localIP().toString();
  doc["rssi"]        = WiFi.RSSI();
  doc["free_heap"]   = ESP.getFreeHeap();
  doc["uptime"]      = (millis() - bootMillis) / 1000;
  doc["temp_c"]      = readTempC();
  doc["humi_pct"]    = readHumidity();
  doc["samples"]     = histHead;
  JsonArray arr = doc.createNestedArray("temp_history");
  for (uint8_t i = 0; i < TELEMETRY_HISTORY; i++) {
    uint8_t idx = (histHead + i) % TELEMETRY_HISTORY;
    if (tempHistory[idx] != 0) arr.add(tempHistory[idx]);
  }
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void handleReconfig() {
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  clearWiFiOnly();
  enterAPMode("user requested reconfig");
}

void handleReboot() {
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

// ─── OTA update via tunnel ───────────────────────────────────────────────────
void handleOTA() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
      Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("[ota] done: %u bytes\n", upload.totalSize);
    else Update.printError(Serial);
  }
}

void handleOTAFinish() {
  server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK, rebooting");
  delay(500);
  ESP.restart();
}

void enterSTAMode() {
  Serial.printf("[boot] connecting to WiFi '%s'\n", savedSSID.c_str());
  appState = STATE_STA;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) { delay(200); Serial.print('.'); }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[boot] WiFi timeout — back to AP mode");
    enterAPMode("WiFi credentials failed");
    return;
  }
  Serial.printf("\n[wifi] OK, IP = %s, RSSI = %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  server.close();
  server.on("/", HTTP_GET, handleDashRoot);
  server.on("/api/telemetry", HTTP_GET, handleTelemetry);
  server.on("/api/reconfig", HTTP_POST, handleReconfig);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/ota", HTTP_POST, handleOTAFinish, handleOTA);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();

  tunnel.onStatus([](bool online, const char* publicUrl) {
    Serial.printf("[tunnel] %s -> %s\n", online ? "ONLINE" : "OFFLINE", publicUrl);
  });
  tunnel.begin(savedToken.c_str(), 80);
}

// ─── GPIO0 button ────────────────────────────────────────────────────────────
void pollResetButton() {
  static bool last = false;
  bool pressed = digitalRead(RESET_PIN) == LOW;
  if (pressed && !last) resetHoldStart = millis();
  if (!pressed && last) resetHoldStart = 0;
  if (pressed && resetHoldStart > 0) {
    uint32_t held = millis() - resetHoldStart;
    if (held >= HOLD_FACTORY_MS) {
      Serial.println("[reset] FACTORY RESET (>=10 s)");
      factoryReset(); ESP.restart();
    } else if (held >= HOLD_RECONFIG_MS && appState == STATE_STA) {
      Serial.println("[reset] reconfig WiFi (>=3 s)");
      clearWiFiOnly(); enterAPMode("button reconfig"); resetHoldStart = 0;
    }
  }
  last = pressed;
}

// ─── Sensor ring buffer ──────────────────────────────────────────────────────
void sampleSensors() {
  if (millis() - lastSampleMs < 1000) return;
  lastSampleMs = millis();
  tempHistory[histHead] = readTempC();
  histHead = (histHead + 1) % TELEMETRY_HISTORY;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  bootMillis = millis();
  pinMode(RESET_PIN, INPUT_PULLUP);
  for (uint8_t i = 0; i < TELEMETRY_HISTORY; i++) tempHistory[i] = 0;

  loadConfig();
  Serial.printf("[boot] config: ssid='%s' token=%s\n",
                savedSSID.c_str(), savedToken.length() ? "set" : "missing");

  if (savedSSID.length() == 0 || savedToken.length() < 32) {
    enterAPMode("no saved config");
  } else {
    enterSTAMode();
  }
}

void loop() {
  pollResetButton();
  server.handleClient();
  if (appState == STATE_AP)  dnsServer.processNextRequest();
  if (appState == STATE_STA) { tunnel.loop(); sampleSensors(); }
}
