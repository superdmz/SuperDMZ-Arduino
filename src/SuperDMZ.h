// SuperDMZ.h — Arduino library for ESP32
// Secure reverse tunnel: exposes the ESP32 WebServer at https://<host>.dmzgate.com
//
// Minimal usage:
//   #include <WiFi.h>
//   #include <SuperDMZ.h>
//   SuperDMZ tunnel;
//   void setup() {
//     WiFi.begin("SSID", "PASS");
//     // ... wait for WiFi.status() == WL_CONNECTED ...
//     server.begin();  // your existing WebServer on port 80
//     tunnel.begin("YOUR_TOKEN_HERE", 80);
//   }
//   void loop() { tunnel.loop(); }
//
// Limits: vanilla ESP32 handles ~3 concurrent loopback TCP connections,
// bounded by mbedtls/WiFi stack heap. With PSRAM the limit is higher.
//
// (c) 2026 SuperDMZ — MIT License

#ifndef SUPERDMZ_H
#define SUPERDMZ_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <map>

class SuperDMZ {
 public:
  SuperDMZ();
  ~SuperDMZ();

  // Starts the tunnel. token = 48 hex chars (from the panel). localPort = your
  // local WebServer port (must match the local_port configured in the panel,
  // otherwise the lib logs a warning and no requests reach you).
  // Optional node: node hostname (e.g. "spo1.nodes.superdmz.com"). If empty,
  // the lib asks the panel via /api/resolve-server.php.
  bool begin(const char* token, uint16_t localPort, const char* node = "");

  // Call from loop(). Keeps the WS alive, processes requests, manages reconnect.
  void loop();

  // Optional callbacks (default: log via Serial).
  typedef void (*StatusCb)(bool online, const char* publicUrl);
  void onStatus(StatusCb cb) { _statusCb = cb; }

  bool isOnline() const { return _online; }
  const char* publicUrl() const { return _publicUrl.c_str(); }
  uint32_t bytesIn()  const { return _bytesIn; }
  uint32_t bytesOut() const { return _bytesOut; }

  // Force a manual reconnect (rare — auto-reconnect already covers it).
  void reconnect();

 private:
  // ── Wire protocol (mirrors tunnel.go from the Go client) ──────────────────
  enum MsgType { MSG_HELLO, MSG_READY, MSG_ERROR, MSG_NEWCONN, MSG_CONNCLOSE,
                 MSG_DATA, MSG_PING, MSG_PONG, MSG_UNKNOWN };
  static MsgType parseType(const char* s);
  static const char* typeName(MsgType t);

  // Per-connection: local TCP socket + bookkeeping.
  struct StreamState {
    WiFiClient client;
    bool active = false;
    uint32_t lastByteMs = 0;
  };

  // ── WebSocket callbacks ────────────────────────────────────────────────────
  void wsEvent(WStype_t type, uint8_t* payload, size_t length);
  static void wsEventStatic(WStype_t type, uint8_t* payload, size_t length);
  void handleEnvelope(uint8_t* payload, size_t length);

  // ── Per-conn handling ──────────────────────────────────────────────────────
  void handleNewConn(const String& connId, uint16_t localPort);
  void handleData(const String& connId, const String& b64);
  void handleConnClose(const String& connId);
  void pumpLocalToWs();   // called every loop() — drains local TCP, sends via WS
  void closeStream(const String& connId, bool notifyServer);

  // ── Send ───────────────────────────────────────────────────────────────────
  bool sendEnvelope(MsgType t, const String& connId, const String& payloadJson);

  // ── Resolve node URL ───────────────────────────────────────────────────────
  String resolveNodeUrl();

  // ── State ──────────────────────────────────────────────────────────────────
  WebSocketsClient _ws;
  String _token;
  String _node;          // e.g. spo1.nodes.superdmz.com
  uint16_t _localPort;   // user's local WebServer port
  bool _online;
  String _publicUrl;
  uint32_t _bytesIn;
  uint32_t _bytesOut;

  std::map<String, StreamState*> _streams;

  uint32_t _lastPingMs;
  StatusCb _statusCb;

  static SuperDMZ* _instance;  // singleton for the static WS callback
};

#endif  // SUPERDMZ_H
