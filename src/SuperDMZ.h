// SuperDMZ.h — biblioteca Arduino para ESP32
// Tunel reverso seguro: expoe o WebServer do ESP32 em https://<host>.dmzgate.com
//
// Uso minimo:
//   #include <WiFi.h>
//   #include <SuperDMZ.h>
//   SuperDMZ tunnel;
//   void setup() {
//     WiFi.begin("SSID", "PASS");
//     // ... wait WiFi.status() == WL_CONNECTED ...
//     server.begin();  // seu WebServer existente na porta 80
//     tunnel.begin("SEU_TOKEN_AQUI", 80);
//   }
//   void loop() { tunnel.loop(); }
//
// Limites: ESP32 vanilla aguenta ~3 conexoes TCP loopback simultaneas pelo
// limite de heap do mbedtls/WiFi stack. Com PSRAM o limite e maior.
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

  // Inicia o tunel. token = 48 hex chars (do painel). localPort = porta do seu
  // WebServer local (precisa bater com o local_port configurado no tunnel do
  // painel, senao a lib loga warning e o tunel fica inutil).
  // node opcional: hostname do node (ex "spo1.nodes.superdmz.com"). Se vazio,
  // a lib pergunta ao painel via /api/resolve-server.php.
  bool begin(const char* token, uint16_t localPort, const char* node = "");

  // Chame em loop(). Mantem WS vivo, processa requests, gerencia reconnect.
  void loop();

  // Callbacks opcionais (default: log via Serial).
  typedef void (*StatusCb)(bool online, const char* publicUrl);
  void onStatus(StatusCb cb) { _statusCb = cb; }

  bool isOnline() const { return _online; }
  const char* publicUrl() const { return _publicUrl.c_str(); }
  uint32_t bytesIn()  const { return _bytesIn; }
  uint32_t bytesOut() const { return _bytesOut; }

  // Forca uma reconexao manual (raro — o auto-reconnect ja cobre tudo).
  void reconnect();

 private:
  // ── Wire protocol (espelha tunnel.go do client Go) ─────────────────────────
  enum MsgType { MSG_HELLO, MSG_READY, MSG_ERROR, MSG_NEWCONN, MSG_CONNCLOSE,
                 MSG_DATA, MSG_PING, MSG_PONG, MSG_UNKNOWN };
  static MsgType parseType(const char* s);
  static const char* typeName(MsgType t);

  // Por-conexao: socket TCP local + bookkeeping.
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
  void pumpLocalToWs();   // chamado todo loop() — le TCP local e envia via WS
  void closeStream(const String& connId, bool notifyServer);

  // ── Envio ──────────────────────────────────────────────────────────────────
  bool sendEnvelope(MsgType t, const String& connId, const String& payloadJson);

  // ── Resolve node URL ───────────────────────────────────────────────────────
  String resolveNodeUrl();

  // ── State ──────────────────────────────────────────────────────────────────
  WebSocketsClient _ws;
  String _token;
  String _node;          // ex: spo1.nodes.superdmz.com
  uint16_t _localPort;   // porta do WebServer local do usuario
  bool _online;
  String _publicUrl;
  uint32_t _bytesIn;
  uint32_t _bytesOut;

  std::map<String, StreamState*> _streams;

  uint32_t _lastPingMs;
  StatusCb _statusCb;

  static SuperDMZ* _instance;  // singleton pra callback estatico do WS
};

#endif  // SUPERDMZ_H
