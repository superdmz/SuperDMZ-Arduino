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
  // Optional node: node hostname (e.g. "<node>.nodes.superdmz.com"). If empty,
  // the lib asks the panel via /api/resolve-server.php.
  // targetHost: the host the lib dials for each incoming connection. Defaults to
  // loopback (expose THIS board's WebServer). A Gateway sets it to a LAN IP
  // (e.g. "192.168.0.20") to bridge to another machine on the internal network —
  // mirrors the Go client's target_host. Multiple SuperDMZ instances may run at
  // once (one per tunnel), each with its own token + targetHost.
  bool begin(const char* token, uint16_t localPort, const char* node = "",
             const char* targetHost = "127.0.0.1");

  // Call from loop(). Keeps the WS alive, processes requests, manages reconnect.
  void loop();

  // Optional callbacks (default: log via Serial).
  typedef void (*StatusCb)(bool online, const char* publicUrl);
  void onStatus(StatusCb cb) { _statusCb = cb; }

  // Optional structured log callback. Every internal step writes one line
  // through this callback, prefixed with a tag like "net", "ws", "envelope".
  // Sketches that maintain their own ring buffer (see SmartIoT-Debug) can
  // forward the lines so they show up in the live /log view.
  // The line passed in is short-lived — copy if you need to keep it.
  // Default is no callback; lines still go to Serial via Serial.println.
  typedef void (*LogCb)(const char* line);
  void onLog(LogCb cb) { _logCb = cb; }

  bool isOnline() const { return _online; }
  const char* publicUrl() const { return _publicUrl.c_str(); }
  // Relay node hostname this tunnel is connected to — but ONLY while the token
  // is authenticated (tunnel online); empty otherwise. So UIs show the REAL
  // node only once authenticated, never a guess or a not-yet-confirmed one.
  const char* nodeHost() const { return _online ? _nodeHost.c_str() : ""; }
  // Library version string compiled into the binary (e.g. "1.1.6"). Use this to
  // verify which lib version actually got built — not just what the IDE reports
  // as "installed".
  const char* version() const;
  // Node-lookup diagnostics (surfaced via /info in the SmartIoT-Debug example).
  uint32_t    resolveAttempts() const { return _resolveAttempts; }
  const char* lastResolveInfo() const { return _lastResolveInfo.c_str(); }
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
  String resolveNodeUrl();                     // one HTTPS lookup; "" on failure
  void   connectToUrl(const String& wsUrl);    // parse "wss://…" + (re)dial the WS

  // ── State ──────────────────────────────────────────────────────────────────
  WebSocketsClient _ws;
  String _token;
  String _node;          // optional pin, e.g. "<node>.nodes.superdmz.com"
  uint16_t _localPort;   // user's local WebServer port
  String   _targetHost = "127.0.0.1";  // host dialed per connection (loopback for
                                       // self; a LAN IP in Gateway mode)
  bool _online;
  String _publicUrl;
  String _nodeHost;      // relay node host resolved/used in begin()
  uint32_t _bytesIn;
  uint32_t _bytesOut;

  std::map<String, StreamState*> _streams;

  uint32_t _lastPingMs;
  StatusCb _statusCb;
  LogCb    _logCb = nullptr;
  bool     _resolved;        // true once a real node (not the fallback) resolved
  uint32_t _lastResolveMs;   // last resolve attempt — drives loop() re-resolve
  uint32_t _resolveAttempts; // total node lookups tried (diagnostics)
  String   _lastResolveInfo; // last lookup outcome string (diagnostics)

  // ── Internal structured logger ───────────────────────────────────────────────
  // Writes one line to Serial AND, if set, to the user's LogCb. Use this for
  // EVERY observable step inside the library so debugging never needs guesses
  // (the SmartIoT-Debug example mirrors these into its /log ring buffer).
  void lg(const char* tag, const char* fmt, ...);
};

#endif  // SUPERDMZ_H
