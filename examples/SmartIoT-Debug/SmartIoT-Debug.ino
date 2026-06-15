// SuperDMZ — SmartIoT_Debug v2
//
// Debug-friendly variant of SmartIoT. Differences from the published example:
//   - AP "SuperDMZ-Debug-XXXX" stays UP forever (in WIFI_AP_STA mode), even
//     after WiFi is configured. Devs can re-connect to it any time without
//     losing the running tunnel.
//   - 80-line ring-buffer log captured into memory. Same logf() output goes
//     to Serial AND the ring. Survives WiFi reconnects, board sleeps, etc.
//   - Live log viewer at http://192.168.4.1/log (auto-refresh every 2 s).
//     Also exposed via the public tunnel at https://<your-host>.dmzgate.com/log
//     once the tunnel is up.
//   - Network probes (DNS + TCP + TLS + ICMP-style TCP/53 to Google DNS +
//     Cloudflare DNS) panel; auto-runs every 120 s. Manual "Run probes now"
//     button still works.
//   - NTP time sync — date/time visible on the dashboard.
//   - OTA firmware update over HTTP: pick a .bin, click upload — board flashes
//     and reboots automatically.
//   - Node metadata (country flag + city) pulled from the SuperDMZ panel.
//
// AP credentials (change before deploying!): SSID "SuperDMZ-Debug-XXXX",
// password "12345678". The XXXX is derived from the chip's MAC so multiple
// devs in the same room don't collide.

#include <WiFi.h>
#include <WiFiUdp.h>     // raw UDP for real DNS probes (bypass lwIP cache)
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <SuperDMZ.h>
#include <stdarg.h>
#include <vector>

#define RESET_PIN          0
#define HOLD_RECONFIG_MS   3000
#define HOLD_FACTORY_MS    10000
#define AP_PASSWORD        "12345678"
#define LOG_MAX            80     // ring buffer depth
#define LOG_LINE_MAX       160    // chars per line
#define PROBE_INTERVAL_MS  120000 // auto-probe every 120 s
#define NODE_REFRESH_MS    300000 // refresh node metadata every 5 min
#define PANEL_HOST         "superdmz.com"
#define FW_VERSION         "2.0.10"  // firmware version — bump on every release

Preferences prefs;
WebServer   server(80);          // shared between AP and STA in WIFI_AP_STA mode
DNSServer   dnsServer;
SuperDMZ    tunnel;

String   savedSSID, savedPass, savedToken;
String   apSSID;
uint32_t bootMillis        = 0;
uint32_t resetHoldStart    = 0;
uint32_t lastSnapshotMs    = 0;
uint32_t lastProbeRunMs    = 0;
uint32_t lastNodeFetchMs   = 0;
uint32_t lastInetCheckMs   = 0;
bool     prevOnline        = false;
bool     ntpSynced         = false;
bool     inetGoogleCached  = false;   // updated in background by checkInternet()
bool     inetCfCached      = false;   // updated in background by checkInternet()
String   nodeJson          = "{}";    // cached panel response (node + tunnel public_url)

// Last 7 probe results (JSON-serialized snapshot)
String   lastProbeJson = "{\"ran\":false}";

// ── Network probe result type — declared at top so Arduino IDE's auto-generated
//    forward declarations for pDns()/pTcp()/runProbes() don't reference an
//    unknown type when inserted at the start of the translation unit.
struct ProbeRes { String name; bool ok; uint32_t ms; String detail; };

// ── Forward declarations (Arduino IDE 3.x / esp32 core 3.x quirk) ─────────────
void enterAPMode();
void tryConnectSTA();
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
void runProbes();
void fetchNodeInfo();
void syncNtp();
void checkInternet();
ProbeRes pDns(const char* host, const char* dnsServerStr = "8.8.8.8");
ProbeRes pTcp(const char* host, uint16_t port, uint32_t timeout);
ProbeRes pTls(const char* host, uint16_t port);

// ── WiFi disconnect reason code -> readable text ──────────────────────────────
const char* wifiReasonStr(uint8_t r) {
  switch (r) {
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE";
    case 3:   return "AUTH_LEAVE";
    case 4:   return "ASSOC_EXPIRE";
    case 5:   return "ASSOC_TOOMANY (AP full)";
    case 6:   return "NOT_AUTHED (AP forgot us)";
    case 7:   return "NOT_ASSOCED (AP forgot us)";
    case 8:   return "ASSOC_LEAVE (explicit deauth)";
    case 9:   return "ASSOC_NOT_AUTHED";
    case 10:  return "DISASSOC_PWRCAP_BAD";
    case 11:  return "DISASSOC_SUPCHAN_BAD";
    case 13:  return "IE_INVALID";
    case 14:  return "MIC_FAILURE";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT (wrong password?)";
    case 16:  return "GROUP_KEY_UPDATE_TIMEOUT";
    case 17:  return "IE_IN_4WAY_DIFFERS";
    case 18:  return "GROUP_CIPHER_INVALID";
    case 19:  return "PAIRWISE_CIPHER_INVALID";
    case 20:  return "AKMP_INVALID";
    case 23:  return "802_1X_AUTH_FAILED";
    case 24:  return "CIPHER_SUITE_REJECTED";
    case 200: return "BEACON_TIMEOUT (signal too weak / range)";
    case 201: return "NO_AP_FOUND (SSID disappeared)";
    case 202: return "AUTH_FAIL (wrong password)";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default:  return "?";
  }
}

// ─── Ring-buffer log ──────────────────────────────────────────────────────────
struct LogEntry {
  uint32_t ms;
  char     text[LOG_LINE_MAX];
};
LogEntry logBuf[LOG_MAX];
size_t   logHead = 0;
bool     logWrapped = false;

void logf(const char* fmt, ...) {
  char tmp[LOG_LINE_MAX];
  va_list a;
  va_start(a, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, a);
  va_end(a);

  // Serial echo
  Serial.print('[');
  Serial.print(millis() / 1000);
  Serial.print("s] ");
  Serial.println(tmp);

  // Ring buffer (single-threaded — only loop() calls this; no mutex needed)
  logBuf[logHead].ms = millis();
  strlcpy(logBuf[logHead].text, tmp, sizeof(logBuf[logHead].text));
  logHead = (logHead + 1) % LOG_MAX;
  if (logHead == 0) logWrapped = true;
}

String renderLogText() {
  String out;
  out.reserve(8 * 1024);
  size_t start = logWrapped ? logHead : 0;
  size_t count = logWrapped ? LOG_MAX : logHead;
  for (size_t i = 0; i < count; i++) {
    size_t idx = (start + i) % LOG_MAX;
    char buf[24];
    snprintf(buf, sizeof(buf), "[%6lus] ", logBuf[idx].ms / 1000UL);
    out += buf;
    out += logBuf[idx].text;
    out += '\n';
  }
  return out;
}

// ─── NVS ──────────────────────────────────────────────────────────────────────
void loadConfig() {
  prefs.begin("superdmz", true);
  savedSSID  = prefs.getString("ssid",  "");
  savedPass  = prefs.getString("pass",  "");
  savedToken = prefs.getString("token", "");
  prefs.end();
}
void saveConfig(const String& s, const String& p, const String& t) {
  prefs.begin("superdmz", false);
  prefs.putString("ssid",  s);
  prefs.putString("pass",  p);
  prefs.putString("token", t);
  prefs.end();
}
void saveTokenOnly(const String& t) {
  prefs.begin("superdmz", false);
  prefs.putString("token", t);
  prefs.end();
}
void clearWiFi()   { prefs.begin("superdmz", false); prefs.remove("ssid"); prefs.remove("pass"); prefs.end(); }
void factoryReset(){ prefs.begin("superdmz", false); prefs.clear(); prefs.end(); }

// ─── WiFi events → log ──────────────────────────────────────────────────────
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:        logf("[wifi-evt] STA_START"); break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      logf("[wifi-evt] STA_CONNECTED ssid='%s' channel=%d",
           reinterpret_cast<const char*>(info.wifi_sta_connected.ssid),
           info.wifi_sta_connected.channel);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      logf("[wifi-evt] STA_GOT_IP ip=%s gw=%s rssi=%d",
           WiFi.localIP().toString().c_str(),
           WiFi.gatewayIP().toString().c_str(),
           WiFi.RSSI());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      logf("[wifi-evt] STA_DISCONNECTED reason=%u (%s)",
           info.wifi_sta_disconnected.reason,
           wifiReasonStr(info.wifi_sta_disconnected.reason));
      break;
    case ARDUINO_EVENT_WIFI_AP_START:         logf("[wifi-evt] AP_START"); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      logf("[wifi-evt] AP client connected (mac=%02x:%02x:%02x:%02x:%02x:%02x)",
           info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
           info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
           info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      logf("[wifi-evt] AP client disconnected");
      break;
    default: break;  // many events, ignore the rest
  }
}

// ─── HTTP handlers ────────────────────────────────────────────────────────────
#include "debug_html.h"   // dashboard HTML (kept out of .ino; see file header)

// Stream the dashboard HTML in 1 KiB chunks instead of one big send_P().
// Reason: through the SuperDMZ tunnel the WebServer writes to a loopback
// socket whose buffer is ~5 KiB. A single 18 KiB send_P() fills it, then
// blocks for ~10 s while waiting for the buffer to drain — but the drainer
// (tunnel.loop()'s pumpLocalToWs) only runs from loop(), which is itself
// stuck inside server.handleClient(). Deadlock. Eventually a timeout cuts
// the response in the middle and the browser receives partial HTML, so
// refresh() is never defined and the dashboard renders blank.
// Workaround: yield to tunnel.loop() between chunks so it can drain.
void handleRoot() {
  logf("[http] GET / from %s", server.client().remoteIP().toString().c_str());
  const size_t total = strlen_P(DEBUG_HTML);
  server.setContentLength(total);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", "");
  static const size_t CHUNK = 1024;
  for (size_t off = 0; off < total; off += CHUNK) {
    size_t n = (total - off) < CHUNK ? (total - off) : CHUNK;
    char* tmp = (char*)malloc(n + 1);
    if (!tmp) break;
    memcpy_P(tmp, DEBUG_HTML + off, n);
    tmp[n] = '\0';
    server.sendContent(tmp);
    free(tmp);
    tunnel.loop();   // drain the loopback socket so the next sendContent has room
    delay(1);
  }
}
void handleLogText() { logf("[http] GET /log"); server.send(200, "text/plain; charset=utf-8", renderLogText()); }
void handleLogJson() {
  DynamicJsonDocument doc(16 * 1024);
  JsonArray arr = doc.createNestedArray("lines");
  size_t start = logWrapped ? logHead : 0;
  size_t count = logWrapped ? LOG_MAX : logHead;
  for (size_t i = 0; i < count; i++) {
    size_t idx = (start + i) % LOG_MAX;
    char head[24];
    snprintf(head, sizeof(head), "[%6lus] ", logBuf[idx].ms / 1000UL);
    arr.add(String(head) + logBuf[idx].text);
  }
  String body; serializeJson(doc, body);
  server.send(200, "application/json", body);
}
void handleScan() {
  int n = WiFi.scanNetworks(false, true);
  logf("[scan] found %d networks", n);
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.createNestedObject();
    o["ssid"]   = WiFi.SSID(i);
    o["rssi"]   = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  String body; serializeJson(doc, body);
  server.send(200, "application/json", body);
  WiFi.scanDelete();
}
void handleSaveWifi() {
  String s = server.arg("ssid"), p = server.arg("pass");
  if (s.length() == 0) { server.send(400, "text/plain", "missing ssid"); return; }
  prefs.begin("superdmz", false);
  prefs.putString("ssid", s); prefs.putString("pass", p);
  prefs.end();
  savedSSID = s; savedPass = p;
  logf("[save] wifi ssid='%s'", s.c_str());
  server.send(200, "text/plain", "ok");
  delay(200);
  WiFi.disconnect(false, false);
  tryConnectSTA();
}
void handleSaveToken() {
  String t = server.arg("token"); t.trim(); t.toLowerCase();
  if (t.length() < 32 || t.length() > 64) { server.send(400, "text/plain", "token must be 32-64 hex"); return; }
  for (size_t i = 0; i < t.length(); i++) {
    char c = t[i];
    if (!((c>='0'&&c<='9') || (c>='a'&&c<='f'))) { server.send(400, "text/plain", "non-hex char in token"); return; }
  }
  saveTokenOnly(t);
  savedToken = t;
  logf("[save] token %.8s... (len=%u)", t.c_str(), t.length());
  server.send(200, "text/plain", "ok");
  delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    tunnel.begin(savedToken.c_str(), 80);
  }
}
void handleApiStatus() {
  DynamicJsonDocument doc(2048);
  doc["online"]      = tunnel.isOnline();
  doc["public_url"]  = tunnel.publicUrl();
  doc["bytes_in"]    = tunnel.bytesIn();
  doc["bytes_out"]   = tunnel.bytesOut();
  doc["wifi_ssid"]   = WiFi.SSID();
  doc["wifi_ip"]     = WiFi.localIP().toString();
  doc["wifi_status"] = WiFi.status();   // 3 = WL_CONNECTED
  doc["rssi"]        = WiFi.RSSI();
  doc["mac"]         = WiFi.macAddress();
  doc["chip"]        = ESP.getChipModel();
  doc["fw_version"]  = FW_VERSION;
  doc["free_heap"]   = ESP.getFreeHeap();
  doc["uptime"]      = (millis() - bootMillis) / 1000;
  doc["ntp_ok"]      = ntpSynced;
  doc["has_token"]   = savedToken.length() >= 32;
  if (doc["has_token"].as<bool>()) {
    doc["token_preview"] = savedToken.substring(0, 8) + "…" + savedToken.substring(savedToken.length()-4);
  }
  // Internet reachability flags are refreshed in background by checkInternet()
  // every 30 s (see loop()). Reading them here is non-blocking — critical for
  // tunnel-mode latency: a blocking probe inside the request handler would
  // freeze server.handleClient() and tunnel.loop() for up to 1.6 s and
  // dashboard polls (every 2 s) would never return through the WS tunnel.
  if (WiFi.status() == WL_CONNECTED) {
    doc["inet_google"] = inetGoogleCached;
    doc["inet_cf"]     = inetCfCached;
  } else {
    doc["inet_google"] = false;
    doc["inet_cf"]     = false;
  }
  // NTP date string (UTC)
  if (ntpSynced) {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &t);
    doc["date_utc"] = buf;
  }
  // Inject cached node info ONLY when authenticated (tunnel online) — never show
  // a node before the token has been accepted by the relay.
  if (tunnel.isOnline() && nodeJson.length() > 2) {
    DynamicJsonDocument nd(1024);
    if (deserializeJson(nd, nodeJson) == DeserializationError::Ok && nd["node"].is<JsonObject>()) {
      doc["node"] = nd["node"];
    }
  }
  String body; serializeJson(doc, body);
  server.send(200, "application/json", body);
}
// Plain-text diagnostics dump. Opening /info confirms you flashed THIS sketch
// (the route only exists here) and shows the LIB version compiled in
// (tunnel.version() — won't even build against the old lib), the firmware
// version, the exact build timestamp, and the node the lib actually resolved to.
void handleInfo() {
  logf("[http] GET /info");
  String s; s.reserve(900);
  s += "SuperDMZ ESP32 - /info\n----------------------\n";
  s += "fw_version    : " FW_VERSION "\n";
  s += "lib_version   : " + String(tunnel.version()) + "\n";
  s += "build         : " __DATE__ " " __TIME__ "\n";
  s += "chip          : " + String(ESP.getChipModel())
     + " (" + String((int)ESP.getChipCores()) + " cores, "
     + String((unsigned)ESP.getCpuFreqMHz()) + " MHz)\n";
  s += "sdk           : " + String(ESP.getSdkVersion()) + "\n";
  s += "free_heap     : " + String((unsigned)ESP.getFreeHeap()) + "\n";
  s += "uptime_s      : " + String((unsigned)((millis() - bootMillis) / 1000)) + "\n";
  s += "wifi_ssid     : " + WiFi.SSID() + "\n";
  s += "wifi_ip       : " + WiFi.localIP().toString() + "\n";
  s += "wifi_rssi     : " + String((int)WiFi.RSSI()) + "\n";
  s += "mac           : " + WiFi.macAddress() + "\n";
  s += "token         : " + (savedToken.length() >= 8 ? savedToken.substring(0, 8) + "..." : String("(none)")) + "\n";
  s += "tunnel_online : " + String(tunnel.isOnline() ? "yes" : "no") + "\n";
  { String nh = tunnel.nodeHost(); s += "node_resolved : " + (nh.length() ? nh : String("-")) + "\n"; }
  s += "resolve_tries : " + String((unsigned)tunnel.resolveAttempts()) + "\n";
  s += "resolve_last  : " + String(tunnel.lastResolveInfo()) + "\n";
  s += "public_url    : " + String(tunnel.publicUrl()) + "\n";
  server.send(200, "text/plain; charset=utf-8", s);
}
void handleReboot()  { logf("[cmd] reboot requested"); server.send(200, "application/json", "{\"ok\":true}"); delay(400); ESP.restart(); }
void handleFactory() { logf("[cmd] factory reset requested"); server.send(200, "application/json", "{\"ok\":true}"); delay(400); factoryReset(); ESP.restart(); }

// ─── OTA upload ──────────────────────────────────────────────────────────────
void handleOtaFinish() {
  bool ok = !Update.hasError();
  String msg = ok ? "ok" : String("err:") + Update.errorString();
  server.send(ok ? 200 : 500, "text/plain", msg);
  logf("[ota] finish ok=%d msg=%s", ok ? 1 : 0, msg.c_str());
  if (ok) { delay(800); ESP.restart(); }
}
void handleOtaUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    logf("[ota] start filename='%s' size=%u", upload.filename.c_str(), upload.totalSize);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      logf("[ota] Update.begin FAILED: %s", Update.errorString());
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      logf("[ota] write FAILED: %s", Update.errorString());
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      logf("[ota] upload complete bytes=%u", upload.totalSize);
    } else {
      logf("[ota] Update.end FAILED: %s", Update.errorString());
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    logf("[ota] upload ABORTED");
  }
}

// ─── Network probes ──────────────────────────────────────────────────────────
// DNS probe via raw UDP query — bypasses the lwIP DNS cache entirely so the
// measured time reflects an actual round-trip to the upstream resolver. The
// previous version called WiFi.hostByName() which returns from cache in 0 ms
// after the first lookup and never actually exercises the network.
//
// Builds a minimal DNS query packet (12-byte header + QNAME labels + QTYPE=A +
// QCLASS=IN), sends it to dnsServer:53 and parses the first A record from the
// reply.
ProbeRes pDns(const char* host, const char* dnsServerStr) {
  ProbeRes r;
  r.name = String("DNS ") + host + " @" + dnsServerStr;
  IPAddress dnsServer;
  if (!dnsServer.fromString(dnsServerStr)) {
    r.ok = false; r.ms = 0; r.detail = "bad dns server"; return r;
  }

  // ── Build the question packet ────────────────────────────────────────
  uint8_t pkt[256];
  size_t  pktLen = 0;
  // Header — 12 bytes
  uint16_t txid = (uint16_t)(millis() & 0xFFFF);
  pkt[pktLen++] = (txid >> 8) & 0xFF;
  pkt[pktLen++] = txid & 0xFF;
  pkt[pktLen++] = 0x01; pkt[pktLen++] = 0x00;   // flags: standard query, recursion desired
  pkt[pktLen++] = 0x00; pkt[pktLen++] = 0x01;   // qd_count = 1
  pkt[pktLen++] = 0x00; pkt[pktLen++] = 0x00;   // an_count
  pkt[pktLen++] = 0x00; pkt[pktLen++] = 0x00;   // ns_count
  pkt[pktLen++] = 0x00; pkt[pktLen++] = 0x00;   // ar_count
  // QNAME — length-prefixed labels, null-terminated
  const char* p = host;
  while (*p && pktLen < sizeof(pkt) - 6) {
    const char* dot = strchr(p, '.');
    size_t labelLen = dot ? (size_t)(dot - p) : strlen(p);
    if (labelLen == 0 || labelLen > 63) { r.ok = false; r.ms = 0; r.detail = "bad host"; return r; }
    pkt[pktLen++] = (uint8_t)labelLen;
    memcpy(pkt + pktLen, p, labelLen);
    pktLen += labelLen;
    p = dot ? (dot + 1) : (p + labelLen);
  }
  pkt[pktLen++] = 0x00;                            // root label
  pkt[pktLen++] = 0x00; pkt[pktLen++] = 0x01;      // qtype A
  pkt[pktLen++] = 0x00; pkt[pktLen++] = 0x01;      // qclass IN

  // ── Send + receive ────────────────────────────────────────────────────
  WiFiUDP udp;
  if (!udp.begin(0)) { r.ok = false; r.ms = 0; r.detail = "udp.begin fail"; return r; }
  uint32_t t0 = millis();
  udp.beginPacket(dnsServer, 53);
  udp.write(pkt, pktLen);
  udp.endPacket();
  while (millis() - t0 < 2500) {
    int n = udp.parsePacket();
    if (n > 0) {
      uint8_t buf[512];
      int len = udp.read(buf, sizeof(buf));
      udp.stop();
      r.ms = millis() - t0;
      if (len < 12) { r.ok = false; r.detail = "short reply"; return r; }
      if (buf[0] != ((txid >> 8) & 0xFF) || buf[1] != (txid & 0xFF)) {
        r.ok = false; r.detail = "txid mismatch"; return r;
      }
      uint8_t rcode = buf[3] & 0x0F;
      uint16_t anCount = (buf[6] << 8) | buf[7];
      if (rcode != 0 || anCount == 0) {
        r.ok = false;
        r.detail = String("rcode=") + rcode + " an=" + anCount;
        return r;
      }
      // Skip question section (we know its length)
      size_t pos = 12;
      while (pos < (size_t)len && buf[pos] != 0) {
        pos += 1 + buf[pos];
      }
      pos += 1 + 4;   // null label + qtype + qclass
      // Walk the answers, return the first A.
      for (int i = 0; i < anCount && pos + 12 < (size_t)len; i++) {
        // skip the name (pointer or labels)
        if ((buf[pos] & 0xC0) == 0xC0) pos += 2;
        else { while (pos < (size_t)len && buf[pos] != 0) pos += 1 + buf[pos]; pos += 1; }
        uint16_t type = (buf[pos] << 8) | buf[pos+1]; pos += 2;
        pos += 2;          // class
        pos += 4;          // ttl
        uint16_t rdlen = (buf[pos] << 8) | buf[pos+1]; pos += 2;
        if (type == 1 && rdlen == 4 && pos + 4 <= (size_t)len) {
          IPAddress ip(buf[pos], buf[pos+1], buf[pos+2], buf[pos+3]);
          r.ok = true; r.detail = ip.toString(); return r;
        }
        pos += rdlen;
      }
      r.ok = false; r.detail = "no A record in reply"; return r;
    }
    delay(2);
  }
  udp.stop();
  r.ms = millis() - t0; r.ok = false; r.detail = "timeout"; return r;
}
ProbeRes pTcp(const char* host, uint16_t port, uint32_t timeout) {
  ProbeRes r; r.name = String("TCP ") + host + ":" + port;
  WiFiClient c; uint32_t t0 = millis();
  r.ok = c.connect(host, port, timeout);
  r.ms = millis() - t0;
  if (r.ok) { r.detail = "connected"; c.stop(); } else r.detail = "no connection";
  return r;
}
ProbeRes pTls(const char* host, uint16_t port) {
  ProbeRes r; r.name = String("TLS ") + host + ":" + port;
  WiFiClientSecure c;
  c.setInsecure();
  c.setHandshakeTimeout(15);   // seconds
  uint32_t t0 = millis();
  r.ok = c.connect(host, port);
  r.ms = millis() - t0;
  if (r.ok) {
    c.print("");
    r.detail = String("handshake OK, free_heap=") + ESP.getFreeHeap();
  } else {
    r.detail = String("handshake failed after ") + r.ms + "ms";
  }
  c.stop();
  return r;
}

void runProbes() {
  if (WiFi.status() != WL_CONNECTED) {
    lastProbeJson = "{\"ran\":true,\"error\":\"WiFi STA not connected — probes need internet\"}";
    logf("[probe] aborted — WiFi STA not connected");
    return;
  }
  logf("[probe] running probes...");
  // Always-on reachability checks (no token needed).
  std::vector<ProbeRes> tests;
  tests.push_back(pTcp("8.8.8.8", 53, 3000));               // Google DNS reachable
  tests.push_back(pTcp("1.1.1.1", 53, 3000));               // Cloudflare DNS reachable
  tests.push_back(pDns("superdmz.com", "8.8.8.8"));         // real DNS round-trip
  tests.push_back(pDns("superdmz.com", "1.1.1.1"));         // cross-check via CF
  tests.push_back(pTcp("superdmz.com", 443, 5000));
  // Probe the relay node THIS token resolved to — not a hardcoded one. The lib
  // fills nodeHost() in begin(); it stays empty when no token is configured
  // yet, in which case we skip the node probes entirely.
  String node = tunnel.nodeHost();
  if (node.length() > 0) {
    tests.push_back(pDns(node.c_str(), "8.8.8.8"));
    tests.push_back(pDns(node.c_str(), "1.1.1.1"));
    tests.push_back(pTcp(node.c_str(), 443, 5000));
    tests.push_back(pTls(node.c_str(), 443));
  } else {
    logf("[probe] no token/node resolved yet - skipping node probes");
  }
  DynamicJsonDocument doc(2048);
  doc["ran"] = true;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["wifi_ip"]   = WiFi.localIP().toString();
  doc["node"]      = node;   // relay node probed ("" if none resolved yet)
  JsonArray arr = doc.createNestedArray("tests");
  for (size_t i = 0; i < tests.size(); i++) {
    JsonObject o = arr.createNestedObject();
    o["name"]   = tests[i].name;
    o["ok"]     = tests[i].ok;
    o["ms"]     = tests[i].ms;
    o["detail"] = tests[i].detail;
    logf("[probe] %-32s %-4s %4lums  %s",
         tests[i].name.c_str(),
         tests[i].ok ? "OK" : "FAIL",
         (unsigned long)tests[i].ms,
         tests[i].detail.c_str());
  }
  String body; serializeJson(doc, body);
  lastProbeJson = body;
  lastProbeRunMs = millis();
  logf("[probe] done");
}

void handleApiProbe()    { runProbes(); server.send(200, "application/json", lastProbeJson); }
void handleApiProbeLast(){ server.send(200, "application/json", lastProbeJson); }

// ─── Internet reachability cache ─────────────────────────────────────────────
// Probes are blocking (~800 ms each) so they CANNOT run inside an HTTP request
// handler — that would freeze server.handleClient() + tunnel.loop() for ~1.6 s
// per request, and tunnel-mode polls (every 2 s) would never return through
// the WS proxy. We run them in loop() at a fixed cadence and cache the result.
void checkInternet() {
  if (WiFi.status() != WL_CONNECTED) {
    inetGoogleCached = false;
    inetCfCached     = false;
    return;
  }
  WiFiClient c1;
  inetGoogleCached = c1.connect("8.8.8.8", 53, 800);
  c1.stop();
  WiFiClient c2;
  inetCfCached     = c2.connect("1.1.1.1", 53, 800);
  c2.stop();
}

// ─── NTP ─────────────────────────────────────────────────────────────────────
void syncNtp() {
  if (WiFi.status() != WL_CONNECTED) return;
  logf("[ntp] sync start");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "a.ntp.br");
  // Block up to 8 s for first sync — fine, we're in setup or post-STA.
  time_t now = 0;
  uint32_t t0 = millis();
  while (now < 1700000000 && millis() - t0 < 8000) {
    delay(150);
    now = time(nullptr);
  }
  ntpSynced = (now >= 1700000000);
  if (ntpSynced) {
    struct tm t; gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &t);
    logf("[ntp] synced now=%s", buf);
  } else {
    logf("[ntp] sync FAILED after %lums", (unsigned long)(millis() - t0));
  }
}

// ─── Node metadata fetch from panel ──────────────────────────────────────────
void fetchNodeInfo() {
  if (WiFi.status() != WL_CONNECTED || savedToken.length() < 32) return;
  logf("[node] fetching panel info...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://") + PANEL_HOST + "/api/tunnel-status.php";
  http.setTimeout(6000);
  if (!http.begin(client, url)) {
    logf("[node] http.begin failed");
    return;
  }
  http.addHeader("X-Tunnel-Token", savedToken);   // token in the header, not the query string (keeps it out of access logs)
  int code = http.GET();
  if (code == 200) {
    nodeJson = http.getString();
    lastNodeFetchMs = millis();
    logf("[node] OK %u bytes", nodeJson.length());
  } else {
    logf("[node] HTTP %d", code);
  }
  http.end();
}

// ─── AP + STA cycle (AP stays UP forever) ──────────────────────────────────
void enterAPMode() {
  if (apSSID.length() == 0) {
    uint64_t mac = ESP.getEfuseMac();
    char suf[5]; snprintf(suf, sizeof(suf), "%04X", (uint16_t)(mac >> 32));
    apSSID = String("SuperDMZ-Debug-") + suf;
  }
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID.c_str(), AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  logf("[ap] up SSID='%s' pass='%s' ip=%s", apSSID.c_str(), AP_PASSWORD, ip.toString().c_str());
  dnsServer.start(53, "*", ip);
}

void tryConnectSTA() {
  if (savedSSID.length() == 0) { logf("[sta] no SSID configured, skipping STA"); return; }
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setAutoReconnect(true);
  logf("[sta] connecting to '%s' (sleep=off, tx=max) ...", savedSSID.c_str());
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(200); }
  if (WiFi.status() == WL_CONNECTED) {
    logf("[sta] connected. ip=%s rssi=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    syncNtp();
    if (savedToken.length() >= 32) {
      logf("[tunnel] begin(token=%.8s..., port=80)", savedToken.c_str());
      tunnel.onStatus([](bool on, const char* url) {
        logf("[tunnel-evt] %s url=%s in=%u out=%u",
             on ? "ONLINE" : "OFFLINE", url ? url : "(null)",
             tunnel.bytesIn(), tunnel.bytesOut());
      });
      // Mirror every internal step of the library into the ring buffer so the
      // /log endpoint shows DNS lookup, HTTPS POST, body read, JSON parse,
      // WS connect/disconnect, etc. — never debug blind again. Lines from the
      // lib are already prefixed with [SuperDMZ:<tag>] so they don't collide
      // with the sketch's own [tag] convention.
      tunnel.onLog([](const char* line) { logf("%s", line); });
      bool ok = tunnel.begin(savedToken.c_str(), 80);
      logf("[tunnel] begin() returned %s", ok ? "true" : "FALSE");
      fetchNodeInfo();
    } else {
      logf("[tunnel] no token yet — skipped tunnel.begin()");
    }
  } else {
    logf("[sta] connect TIMEOUT after 20 s (last status=%d). AP stays UP so you can fix it.",
         WiFi.status());
  }
}

// ─── Reset button ─────────────────────────────────────────────────────────────
void pollResetButton() {
  static bool last = false;
  bool pressed = digitalRead(RESET_PIN) == LOW;
  if (pressed && !last) resetHoldStart = millis();
  if (!pressed && last) resetHoldStart = 0;
  if (pressed && resetHoldStart > 0) {
    uint32_t held = millis() - resetHoldStart;
    if (held >= HOLD_FACTORY_MS) {
      logf("[btn] FACTORY RESET (held >= 10 s)"); delay(50); factoryReset(); ESP.restart();
    } else if (held >= HOLD_RECONFIG_MS) {
      logf("[btn] WiFi reconfig (held >= 3 s)");
      clearWiFi(); resetHoldStart = 0;
      WiFi.disconnect(false, false);
    }
  }
  last = pressed;
}

// ─── Setup / loop ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(600);
  bootMillis = millis();
  pinMode(RESET_PIN, INPUT_PULLUP);

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  SuperDMZ ESP32 — SmartIoT_Debug v" FW_VERSION);
  Serial.printf ("  chip=%s cores=%d cpu=%dMHz heap=%u sdk=%s\n",
                 ESP.getChipModel(), ESP.getChipCores(),
                 ESP.getCpuFreqMHz(), ESP.getFreeHeap(), ESP.getSdkVersion());
  Serial.println("==================================================");

  WiFi.onEvent(onWiFiEvent);
  loadConfig();
  logf("[boot] config: ssid='%s' token=%.8s... (len=%u)",
       savedSSID.c_str(), savedToken.c_str(), savedToken.length());

  enterAPMode();
  tryConnectSTA();

  server.on("/",             HTTP_GET,  handleRoot);
  server.on("/log",          HTTP_GET,  handleLogText);
  server.on("/log.json",     HTTP_GET,  handleLogJson);
  server.on("/scan",         HTTP_GET,  handleScan);
  server.on("/save/wifi",    HTTP_POST, handleSaveWifi);
  server.on("/save/token",   HTTP_POST, handleSaveToken);
  server.on("/api/status",   HTTP_GET,  handleApiStatus);
  server.on("/info",         HTTP_GET,  handleInfo);
  server.on("/api/probe",    HTTP_POST, handleApiProbe);
  server.on("/api/probe",    HTTP_GET,  handleApiProbeLast);
  server.on("/api/reboot",   HTTP_POST, handleReboot);
  server.on("/api/factory",  HTTP_POST, handleFactory);
  // OTA: two-handler form — body callback receives the multipart chunks,
  // finish callback runs after the upload is fully received.
  server.on("/ota", HTTP_POST, handleOtaFinish, handleOtaUpload);

  server.onNotFound([]() {
    logf("[http] 404 host='%s' path='%s'", server.hostHeader().c_str(), server.uri().c_str());
    if (server.hostHeader() == WiFi.softAPIP().toString()) {
      server.sendHeader("Location", "/");
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "not found");
    }
  });
  server.begin();
  logf("[http] WebServer started on port 80 (AP+STA)");
}

void loop() {
  pollResetButton();
  dnsServer.processNextRequest();
  server.handleClient();
  tunnel.loop();

  // tunnel state transitions
  bool cur = tunnel.isOnline();
  if (cur != prevOnline) {
    logf("[tunnel-state] %s -> %s (in=%u out=%u)",
         prevOnline ? "online" : "offline",
         cur ? "online" : "offline",
         tunnel.bytesIn(), tunnel.bytesOut());
    prevOnline = cur;
  }

  // Auto-run network probes every PROBE_INTERVAL_MS, once STA is up.
  if (WiFi.status() == WL_CONNECTED &&
      (lastProbeRunMs == 0 || millis() - lastProbeRunMs > PROBE_INTERVAL_MS)) {
    runProbes();
  }

  // Refresh cached internet-reachability flags every 30 s in background, so
  // /api/status never blocks (critical for tunnel-mode dashboard polls).
  if (lastInetCheckMs == 0 || millis() - lastInetCheckMs > 30000) {
    lastInetCheckMs = millis();
    checkInternet();
  }

  // Refresh node metadata every NODE_REFRESH_MS.
  if (WiFi.status() == WL_CONNECTED && savedToken.length() >= 32 &&
      (lastNodeFetchMs == 0 || millis() - lastNodeFetchMs > NODE_REFRESH_MS)) {
    fetchNodeInfo();
  }

  // Periodic snapshot every 15 s
  if (millis() - lastSnapshotMs > 15000) {
    lastSnapshotMs = millis();
    logf("[snap] wifi=%s rssi=%d ip=%s tunnel=%s in=%u out=%u heap=%u",
         WiFi.status() == WL_CONNECTED ? "OK" : "DOWN",
         WiFi.RSSI(),
         WiFi.localIP().toString().c_str(),
         cur ? "online" : "offline",
         tunnel.bytesIn(), tunnel.bytesOut(),
         ESP.getFreeHeap());
    if (WiFi.status() != WL_CONNECTED && savedSSID.length() > 0) {
      logf("[sta] WiFi dropped — auto-reconnect()");
      WiFi.reconnect();
    }
  }
}
