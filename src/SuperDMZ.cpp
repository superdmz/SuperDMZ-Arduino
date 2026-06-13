// SuperDMZ.cpp — implementacao da lib ESP32

#include "SuperDMZ.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <mbedtls/base64.h>

#define SUPERDMZ_VERSION    "1.1.3"
#define SUPERDMZ_PANEL_API  "https://superdmz.com/api"
#define SUPERDMZ_DEFAULT_NODE "spo1.nodes.superdmz.com"
#define SUPERDMZ_WS_PATH    "/ws/tunnel"
#define SUPERDMZ_PING_MS    20000
// Sized for large dashboard HTML over the tunnel. Smaller than this leaves
// the loopback TCP buffer perpetually full while WebServer.send_P() blocks
// trying to write, and the request gets cut after ~10 s. 4 KiB drains the
// buffer in one pass for most responses.
#define SUPERDMZ_BUF_SIZE   4096

SuperDMZ* SuperDMZ::_instance = nullptr;

// ─── ctor/dtor ────────────────────────────────────────────────────────────────
SuperDMZ::SuperDMZ()
  : _localPort(0), _online(false), _bytesIn(0), _bytesOut(0),
    _lastPingMs(0), _statusCb(nullptr) {
  _instance = this;
}

SuperDMZ::~SuperDMZ() {
  for (auto& kv : _streams) {
    if (kv.second) {
      kv.second->client.stop();
      delete kv.second;
    }
  }
  _streams.clear();
  _instance = nullptr;
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

// ─── Resolve node URL via the panel ───────────────────────────────────────────
String SuperDMZ::resolveNodeUrl() {
  if (_node.length() > 0) {
    return String("wss://") + _node + SUPERDMZ_WS_PATH;
  }
  HTTPClient http;
  String url = String(SUPERDMZ_PANEL_API) + "/resolve-server.php?token=" + _token;
  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();
  String wsUrl;
  if (code == 200) {
    String body = http.getString();
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      const char* w = doc["ws_url"] | nullptr;
      if (w) wsUrl = w;
    }
  }
  http.end();
  if (wsUrl.length() == 0) {
    wsUrl = String("wss://") + SUPERDMZ_DEFAULT_NODE + SUPERDMZ_WS_PATH;
    Serial.printf("[SuperDMZ] resolve failed (code=%d), using default %s\n", code, wsUrl.c_str());
  }
  return wsUrl;
}

// ─── begin / loop ─────────────────────────────────────────────────────────────
bool SuperDMZ::begin(const char* token, uint16_t localPort, const char* node) {
  if (!token || strlen(token) < 16) {
    Serial.println("[SuperDMZ] ERROR: invalid token");
    return false;
  }
  if (localPort == 0) {
    Serial.println("[SuperDMZ] ERROR: localPort=0; pass your WebServer port (e.g. 80)");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SuperDMZ] WARN: WiFi not connected; will wait before dialing");
  }
  _token = token;
  _localPort = localPort;
  _node = node ? node : "";

  String wsUrl = resolveNodeUrl();
  // wsUrl tem formato "wss://<host>/ws/tunnel"
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
  Serial.printf("[SuperDMZ] connecting wss://%s:%u%s ...\n", host.c_str(), port, path.c_str());

  _ws.onEvent(SuperDMZ::wsEventStatic);
  _ws.beginSSL(host.c_str(), port, path.c_str());
  _ws.setReconnectInterval(5000);
  _ws.enableHeartbeat(SUPERDMZ_PING_MS, 60000, 2);   // WS-level ping 20s, pong wait 60s
  _lastPingMs = millis();
  return true;
}

void SuperDMZ::loop() {
  _ws.loop();
  pumpLocalToWs();
}

void SuperDMZ::reconnect() {
  _ws.disconnect();
}

// ─── WS event dispatcher ──────────────────────────────────────────────────────
void SuperDMZ::wsEventStatic(WStype_t type, uint8_t* payload, size_t length) {
  if (_instance) _instance->wsEvent(type, payload, length);
}

void SuperDMZ::wsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("[SuperDMZ] WS disconnected");
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
      Serial.println("[SuperDMZ] WS connected, sending hello");
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
      Serial.printf("[SuperDMZ] WS error: %.*s\n", (int)length, payload);
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
    Serial.printf("[SuperDMZ] invalid envelope: %s\n", err.c_str());
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
        Serial.printf("[SuperDMZ] note: panel says local_port=%u, library uses %u (library wins)\n",
                      serverPort, _localPort);
      }
      Serial.printf("[SuperDMZ] ONLINE: %s -> http://localhost:%u\n", pubUrl, _localPort);
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
      Serial.printf("[SuperDMZ] server error: %s\n", msg ? msg : "?");
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
  if (!s->client.connect(IPAddress(127, 0, 0, 1), _localPort, 2000)) {
    Serial.printf("[SuperDMZ][%s] failed to connect localhost:%u (is your WebServer up?)\n",
                  connId.c_str(), _localPort);
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
