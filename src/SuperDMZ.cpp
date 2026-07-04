// SuperDMZ.cpp — implementacao da lib ESP32

#include "SuperDMZ.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>

#define SUPERDMZ_VERSION    "1.2.0"
#define SUPERDMZ_PANEL_API  "https://superdmz.com/api"
#define SUPERDMZ_WS_PATH    "/ws/tunnel"
// NOTE: there is intentionally NO hardcoded default/fallback node. The node is
// ALWAYS discovered from the panel for the given token; until that succeeds the
// library does not dial anything (see begin()/loop()).
#define SUPERDMZ_RESOLVE_TRIES 3
#define SUPERDMZ_RERESOLVE_MS  10000  // while offline, re-resolve the node this often
#define SUPERDMZ_PING_MS    20000
// Sized for large dashboard HTML over the tunnel. Smaller than this leaves
// the loopback TCP buffer perpetually full while WebServer.send_P() blocks
// trying to write, and the request gets cut after ~10 s. 4 KiB drains the
// buffer in one pass for most responses.
#define SUPERDMZ_BUF_SIZE   4096

// ─── Structured logger ────────────────────────────────────────────────────────
// Writes ONE line per call to Serial AND (if set) to the user-provided
// callback, prefixed with "[SuperDMZ:<tag>] ". Use this instead of bare
// Serial.print for every observable step in the library — that way the
// SmartIoT-Debug example (or any other consumer) can mirror these lines into
// a ring buffer that the dashboard exposes at /log, and debugging never
// requires guessing again.
void SuperDMZ::lg(const char* tag, const char* fmt, ...) {
  char body[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  char line[256];
  snprintf(line, sizeof(line), "[SuperDMZ:%s] %s", tag, body);
  Serial.println(line);
  if (_logCb) _logCb(line);
}

// ─── ctor/dtor ────────────────────────────────────────────────────────────────
SuperDMZ::SuperDMZ()
  : _localPort(0), _online(false), _bytesIn(0), _bytesOut(0),
    _lastPingMs(0), _statusCb(nullptr), _resolved(false), _lastResolveMs(0),
    _resolveAttempts(0) {
}

SuperDMZ::~SuperDMZ() {
  for (auto& kv : _streams) {
    if (kv.second) {
      kv.second->client.stop();
      delete kv.second;
    }
  }
  _streams.clear();
}

// ─── Tipos do envelope ────────────────────────────────────────────────────────
SuperDMZ::MsgType SuperDMZ::parseType(const char* s) {
  if (!s) return MSG_UNKNOWN;
  if (!strcmp(s, "hello"))      return MSG_HELLO;
  if (!strcmp(s, "ready"))      return MSG_READY;
  if (!strcmp(s, "error"))      return MSG_ERROR;
  if (!strcmp(s, "new_conn"))   return MSG_NEWCONN;
  if (!strcmp(s, "conn_close")) return MSG_CONNCLOSE;
  if (!strcmp(s, "data"))       return MSG_DATA;
  if (!strcmp(s, "ping"))       return MSG_PING;
  if (!strcmp(s, "pong"))       return MSG_PONG;
  return MSG_UNKNOWN;
}

const char* SuperDMZ::typeName(MsgType t) {
  switch (t) {
    case MSG_HELLO:     return "hello";
    case MSG_READY:     return "ready";
    case MSG_ERROR:     return "error";
    case MSG_NEWCONN:   return "new_conn";
    case MSG_CONNCLOSE: return "conn_close";
    case MSG_DATA:      return "data";
    case MSG_PING:      return "ping";
    case MSG_PONG:      return "pong";
    default:            return "?";
  }
}

// ─── Library version ──────────────────────────────────────────────────────────
const char* SuperDMZ::version() const { return SUPERDMZ_VERSION; }

// ─── Resolve node URL via the panel ───────────────────────────────────────────
String SuperDMZ::resolveNodeUrl() {
  if (_node.length() > 0) {
    return String("wss://") + _node + SUPERDMZ_WS_PATH;
  }
  // One HTTPS lookup against the panel. Returns the "wss://node/ws/tunnel" URL,
  // or "" on failure — callers retry (begin() a few times at startup, loop()
  // periodically while offline). MUST use an explicit WiFiClientSecure:
  // HTTPClient::begin(url) without one is unreliable on ESP32 (the TLS handshake
  // intermittently fails). setInsecure() is fine here — this lookup only returns
  // routing info; the WSS tunnel to the node is the actual trust boundary.
  _resolveAttempts++;
  lg("resolve", "attempt #%u — POST https://superdmz.com/api/resolve-server.php",
     (unsigned)_resolveAttempts);

  // DNS check first so we know whether a future failure is name resolution
  // or the TCP/TLS handshake. ESP32 lwIP caches per name so a hit is "0 ms".
  IPAddress panelIp;
  uint32_t dnsStart = millis();
  if (WiFi.hostByName("superdmz.com", panelIp)) {
    lg("resolve", "DNS superdmz.com → %s (%lums)",
       panelIp.toString().c_str(), (unsigned long)(millis() - dnsStart));
  } else {
    lg("resolve", "DNS superdmz.com FAILED (%lums)",
       (unsigned long)(millis() - dnsStart));
    _lastResolveInfo = "DNS failed";
    return String("");
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(6000);
  String wsUrl;
  String url = String(SUPERDMZ_PANEL_API) + "/resolve-server.php";
  uint32_t connStart = millis();
  if (!http.begin(client, url)) {
    lg("resolve", "http.begin() FAILED");
    _lastResolveInfo = "http.begin() failed";
    return String("");
  }
  // POST instead of GET: token MUST NOT appear in a query string (proxy logs).
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("X-Tunnel-Token", _token);
  http.addHeader("User-Agent", String("SuperDMZ-Arduino/") + SUPERDMZ_VERSION);
  http.addHeader("Accept", "application/json");
  String postBody = String("token=") + _token;
  lg("resolve", "POST body=%u bytes, headers set", (unsigned)postBody.length());
  int code = http.POST(postBody);
  uint32_t connMs = millis() - connStart;
  lg("resolve", "HTTPS POST done in %lums → HTTP %d", (unsigned long)connMs, code);
  if (code == 200) {
    // Read the body MANUALLY. Both http.getString() AND deserializeJson(doc,
    // http.getStream()) intermittently return empty on ESP32 core 3.x + HTTPS
    // — the headers come in, status is 200, but the body bytes are still
    // arriving in another TCP segment. We loop on stream->available() until
    // we either hit Content-Length or a short read timeout.
    WiFiClient* stream = http.getStreamPtr();
    int contentLength = http.getSize();
    lg("resolve", "Content-Length=%d (-1 = chunked/unknown)", contentLength);
    String body;
    body.reserve(contentLength > 0 ? contentLength + 1 : 512);
    uint32_t startMs = millis();
    while (millis() - startMs < 3000) {
      while (stream && stream->available()) {
        body += (char) stream->read();
        if (contentLength > 0 && (int)body.length() >= contentLength) break;
      }
      if (contentLength > 0 && (int)body.length() >= contentLength) break;
      if (!http.connected() && (!stream || stream->available() == 0)) break;
      delay(1);
    }
    lg("resolve", "read body: %u bytes in %lums", (unsigned)body.length(),
       (unsigned long)(millis() - startMs));
    if (body.length() > 0) {
      String preview = body.length() <= 80 ? body : body.substring(0, 80) + "...";
      lg("resolve", "body[0..80]=%s", preview.c_str());
    }
    // Parse explicitly from char* + length to avoid any String overload magic
    // that was making doc["ws_url"] come back null on a perfectly valid body.
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, body.c_str(), body.length());
    lg("resolve", "JSON parse: %s (doc.size=%u)", err.c_str(), (unsigned)doc.size());
    if (err == DeserializationError::Ok) {
      // Use .as<const char*>() with explicit String construction — works even
      // when operator|<const char*>() returns null inside the lib (still
      // diagnosing why on arduino-esp32 core 3.x + ArduinoJson 6.x).
      String wstr = doc["ws_url"].as<const char*>() ? String(doc["ws_url"].as<const char*>()) : String();
      if (wstr.length() > 0) {
        wsUrl = wstr;
        String tname = doc["tunnel_name"].as<const char*>() ? String(doc["tunnel_name"].as<const char*>()) : String("?");
        lg("resolve", "ws_url=%s tunnel_name=%s", wstr.c_str(), tname.c_str());
      } else {
        // Walk the JSON object so we know what keys ACTUALLY arrived.
        JsonObjectConst obj = doc.as<JsonObjectConst>();
        int n = 0;
        for (JsonPairConst kv : obj) {
          lg("resolve", "  doc[%d] key='%s' type=%s", n,
             kv.key().c_str(),
             kv.value().is<const char*>() ? "str"
               : kv.value().is<int>() ? "int"
               : kv.value().is<JsonObject>() ? "obj" : "?");
          n++;
        }
        lg("resolve", "ws_url key missing or empty (iterated %d keys)", n);
      }
    }
    _lastResolveInfo = wsUrl.length()
      ? (String("OK ") + wsUrl)
      : (String("HTTP 200 size=") + body.length()
         + " err=" + err.c_str()
         + " doc.size=" + doc.size());
  } else {
    _lastResolveInfo = String("HTTP ") + code;
  }
  http.end();
  if (wsUrl.length() == 0) lg("resolve", "FAIL — %s", _lastResolveInfo.c_str());
  return wsUrl;
}

// ─── begin / loop ─────────────────────────────────────────────────────────────
bool SuperDMZ::begin(const char* token, uint16_t localPort, const char* node, const char* targetHost) {
  if (!token || strlen(token) < 16) {
    lg("begin", "ERROR: invalid token");
    return false;
  }
  if (localPort == 0) {
    lg("begin", "ERROR: localPort=0; pass your WebServer port (e.g. 80)");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    lg("begin", "WARN: WiFi not connected; will wait before dialing");
  }
  _token = token;
  _localPort = localPort;
  _node = node ? node : "";
  _targetHost = (targetHost && *targetHost) ? targetHost : "127.0.0.1";

  // Discover the node from the panel (a few quick attempts). On a weak link
  // DNS/TLS often isn't ready in the first seconds after getting an IP, so this
  // may fail — in that case we do NOT dial anything (there is no hardcoded
  // fallback). loop() keeps trying until the panel answers, then dials the real
  // node. The tunnel is therefore never sent to the wrong node.
  String wsUrl;
  for (int i = 0; i < SUPERDMZ_RESOLVE_TRIES && wsUrl.length() == 0; i++) {
    wsUrl = resolveNodeUrl();
    if (wsUrl.length() == 0 && i + 1 < SUPERDMZ_RESOLVE_TRIES) delay(600);
  }
  _resolved = (wsUrl.length() > 0);

  // Per-instance dispatch via a capturing lambda — lets several SuperDMZ
  // instances coexist (a Gateway runs one per assigned tunnel). No singleton.
  _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    this->wsEvent(type, payload, length);
  });
  if (_resolved) {
    connectToUrl(wsUrl);
  } else {
    lg("begin", "node not resolved yet — not dialing; will keep trying in loop()");
  }
  _lastPingMs = millis();
  _lastResolveMs = millis();
  return true;
}

// Parse a "wss://host[:port]/path" URL and (re)dial the WebSocket to it.
void SuperDMZ::connectToUrl(const String& wsUrl) {
  String host;
  uint16_t port = 443;
  String path = SUPERDMZ_WS_PATH;
  int hostStart = wsUrl.indexOf("://") + 3;
  int pathStart = wsUrl.indexOf('/', hostStart);
  if (pathStart > 0) {
    host = wsUrl.substring(hostStart, pathStart);
    path = wsUrl.substring(pathStart);
  } else {
    host = wsUrl.substring(hostStart);
  }
  int colon = host.indexOf(':');
  if (colon > 0) {
    port = host.substring(colon + 1).toInt();
    host = host.substring(0, colon);
  }
  _nodeHost = host;   // exposed via nodeHost() for diagnostics
  lg("ws", "connecting wss://%s:%u%s ...", host.c_str(), port, path.c_str());
  _ws.beginSSL(host.c_str(), port, path.c_str());
  _ws.setReconnectInterval(5000);
  _ws.enableHeartbeat(SUPERDMZ_PING_MS, 60000, 2);   // WS-level ping 20s, pong wait 60s
}

void SuperDMZ::loop() {
  _ws.loop();
  pumpLocalToWs();

  // Until the node is discovered, keep asking the panel. We never dial a
  // fallback, so no WS is running yet — the lookup has the TLS stack entirely to
  // itself (this also fixes the case where a constantly-reconnecting WS starved
  // the lookup on RAM-tight chips / weak links). Once the panel answers, dial
  // the real node — once. After that we stop (the WS auto-reconnects to it).
  if (!_resolved && millis() - _lastResolveMs > SUPERDMZ_RERESOLVE_MS) {
    _lastResolveMs = millis();
    String u = resolveNodeUrl();
    if (u.length() > 0) {
      _resolved = true;
      lg("loop", "node resolved — dialing it");
      connectToUrl(u);
    }
  }
}

void SuperDMZ::reconnect() {
  _ws.disconnect();
}

// ─── WS event dispatcher ──────────────────────────────────────────────────────
void SuperDMZ::wsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      lg("ws", "DISCONNECTED");
      _online = false;
      if (_statusCb) _statusCb(false, _publicUrl.c_str());
      for (auto& kv : _streams) {
        if (kv.second) {
          kv.second->client.stop();
          delete kv.second;
        }
      }
      _streams.clear();
      break;

    case WStype_CONNECTED: {
      lg("ws", "CONNECTED — sending hello (token=%.8s... port=%u)",
         _token.c_str(), _localPort);
      // Hello carries platform + actual local config so a smart server can
      // self-correct a misconfigured tunnel (e.g. user picked HTTPS:443 in the
      // panel by accident; we'll keep telling them HTTP:<userPort> is the truth).
      String hello = String("{\"token\":\"") + _token + "\""
                   + ",\"version\":\""      + SUPERDMZ_VERSION + "\""
                   + ",\"platform\":\"arduino-esp32\""
                   + ",\"local_scheme\":\"http\""
                   + ",\"local_port\":"     + String(_localPort)
                   + "}";
      sendEnvelope(MSG_HELLO, "", hello);
      break;
    }

    case WStype_TEXT:
      handleEnvelope(payload, length);
      break;

    case WStype_ERROR:
      lg("ws", "ERROR: %.*s", (int)length, payload);
      break;

    default:
      break;
  }
}

// ─── Parser do envelope ───────────────────────────────────────────────────────
void SuperDMZ::handleEnvelope(uint8_t* payload, size_t length) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    lg("envelope", "invalid: %s", err.c_str());
    return;
  }
  const char* tStr = doc["t"] | "";
  MsgType t = parseType(tStr);
  String connId = doc["c"] | "";
  JsonVariant d = doc["d"];

  switch (t) {
    case MSG_READY: {
      const char* pubUrl = d["public_url"] | "";
      uint16_t serverPort = d["port"] | 0;
      _publicUrl = pubUrl;
      _online = true;
      if (serverPort != 0 && serverPort != _localPort) {
        // Panel and code disagree on local_port — library wins. Log as info,
        // not warning: the user passed the right value to begin(), and the
        // server has been told (via hello) about the actual config.
        lg("ready", "note: panel says local_port=%u, library uses %u (library wins)",
           serverPort, _localPort);
      }
      lg("ready", "ONLINE: %s → http://localhost:%u", pubUrl, _localPort);
      if (_statusCb) _statusCb(true, pubUrl);
      break;
    }

    case MSG_NEWCONN: {
      uint16_t lp = d["local_port"] | _localPort;
      handleNewConn(connId, lp);
      break;
    }

    case MSG_DATA: {
      const char* b64 = d.as<const char*>();
      if (b64) handleData(connId, String(b64));
      break;
    }

    case MSG_CONNCLOSE:
      handleConnClose(connId);
      break;

    case MSG_PING:
      sendEnvelope(MSG_PONG, "", "");
      break;

    case MSG_ERROR: {
      const char* msg = d.as<const char*>();
      lg("envelope", "server error: %s", msg ? msg : "?");
      break;
    }

    default:
      break;
  }
}

// ─── Per-conn ─────────────────────────────────────────────────────────────────
void SuperDMZ::handleNewConn(const String& connId, uint16_t /*serverHintPort*/) {
  // The lib ALWAYS uses the port the user passed to begin() — they know where
  // their WebServer is listening, the panel can't get this wrong. If the panel
  // is configured with a different local_port (e.g. someone created the tunnel
  // as HTTPS:443 by accident), we silently override it. The user shouldn't
  // have to keep two configs in sync.
  StreamState* s = new StreamState();
  // Dial the configured target: loopback for self-hosting, or a LAN IP in
  // Gateway mode. WiFiClient::connect resolves an IP string or a hostname.
  if (!s->client.connect(_targetHost.c_str(), _localPort, 2000)) {
    lg("newconn", "[%s] failed to connect %s:%u (is the target up / reachable?)",
       connId.c_str(), _targetHost.c_str(), _localPort);
    delete s;
    sendEnvelope(MSG_CONNCLOSE, connId, "");
    return;
  }
  s->active = true;
  s->lastByteMs = millis();
  _streams[connId] = s;
}

void SuperDMZ::handleData(const String& connId, const String& b64) {
  auto it = _streams.find(connId);
  if (it == _streams.end() || !it->second || !it->second->active) return;

  size_t outLen = 0;
  size_t inLen = b64.length();
  size_t maxOut = (inLen * 3) / 4 + 4;
  uint8_t* buf = (uint8_t*)malloc(maxOut);
  if (!buf) return;

  int rc = mbedtls_base64_decode(buf, maxOut, &outLen,
                                 (const uint8_t*)b64.c_str(), inLen);
  if (rc == 0 && outLen > 0) {
    _bytesIn += outLen;
    it->second->client.write(buf, outLen);
  }
  free(buf);
}

void SuperDMZ::handleConnClose(const String& connId) {
  closeStream(connId, false);
}

void SuperDMZ::closeStream(const String& connId, bool notifyServer) {
  auto it = _streams.find(connId);
  if (it == _streams.end()) return;
  if (it->second) {
    it->second->client.stop();
    delete it->second;
  }
  _streams.erase(it);
  if (notifyServer) sendEnvelope(MSG_CONNCLOSE, connId, "");
}

// ─── Bombeia bytes do TCP local de volta pro WS ───────────────────────────────
void SuperDMZ::pumpLocalToWs() {
  static uint8_t buf[SUPERDMZ_BUF_SIZE];
  std::vector<String> toClose;
  for (auto& kv : _streams) {
    StreamState* s = kv.second;
    if (!s || !s->active) continue;
    // Detecta socket fechado pelo lado local (WebServer terminou response).
    if (!s->client.connected() && s->client.available() == 0) {
      toClose.push_back(kv.first);
      continue;
    }
    while (s->client.available() > 0) {
      int n = s->client.read(buf, sizeof(buf));
      if (n <= 0) break;
      _bytesOut += n;
      s->lastByteMs = millis();
      // Encode base64
      size_t outLen = 0;
      size_t maxOut = ((n + 2) / 3) * 4 + 4;
      char* b64 = (char*)malloc(maxOut);
      if (!b64) break;
      if (mbedtls_base64_encode((uint8_t*)b64, maxOut, &outLen, buf, n) == 0) {
        b64[outLen] = '\0';
        String payload = String("\"") + b64 + "\"";   // JSON string literal
        sendEnvelope(MSG_DATA, kv.first, payload);
      }
      free(b64);
      // No early break: we MUST drain the local socket fully here so
      // WebServer.send_P() can keep writing instead of blocking on a full
      // TCP buffer (which used to cut large responses around ~10 KiB).
    }
  }
  for (auto& id : toClose) closeStream(id, true);
}

// ─── Send envelope ────────────────────────────────────────────────────────────
bool SuperDMZ::sendEnvelope(MsgType t, const String& connId, const String& payloadJson) {
  String env = "{\"t\":\"";
  env += typeName(t);
  env += "\"";
  if (connId.length() > 0) {
    env += ",\"c\":\"";
    env += connId;
    env += "\"";
  }
  if (payloadJson.length() > 0) {
    env += ",\"d\":";
    if (payloadJson.charAt(0) == '{' || payloadJson.charAt(0) == '"' ||
        payloadJson.charAt(0) == '[') {
      env += payloadJson;
    } else {
      env += "\"";
      env += payloadJson;
      env += "\"";
    }
  }
  env += "}";
  return _ws.sendTXT(env);
}
