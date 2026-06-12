# SuperDMZ para Arduino (ESP32)

Expõe o WebServer do seu ESP32 em uma URL pública `https://<seu-host>.dmzgate.com` — sem IP fixo, sem abrir porta no roteador, sem configurar DNS dinâmico.

A biblioteca abre uma conexão WSS (WebSocket TLS) outbound pra um node SuperDMZ, faz multiplexação de requests sobre essa conexão e encaminha tudo pro seu WebServer local. TLS é terminado no node (cert Let's Encrypt válido pra `*.dmzgate.com`).

## Compatibilidade

| Chip | Status |
|------|--------|
| ESP32 (ESP32-WROOM-32, WROVER) | ✅ Suportado |
| ESP32-S2 | ✅ Suportado |
| ESP32-S3 | ✅ Suportado |
| ESP32-C3 | ✅ Suportado |
| ESP32-C6 | ✅ Suportado |
| Arduino Nano ESP32 | ✅ Suportado (é um ESP32-S3 em formato Nano) |

Framework: **Arduino-ESP32 core 2.0.14+** ou **3.0+**. Testado com Arduino IDE 2.x e PlatformIO.

## Instalação

### Arduino IDE
1. `Sketch` → `Include Library` → `Manage Libraries…`
2. Procure por **SuperDMZ** → `Install`
3. Dependências (são instaladas no mesmo prompt): **WebSockets** by Markus Sattler e **ArduinoJson** by Benoit Blanchon.

Ou instalação manual:
1. Baixe o ZIP em [superdmz.com/download/SuperDMZ-Arduino-v1.0.0.zip](https://superdmz.com/download/SuperDMZ-Arduino-v1.0.0.zip)
2. `Sketch` → `Include Library` → `Add .ZIP Library…`

### PlatformIO
```ini
lib_deps =
    superdmz/SuperDMZ@^1.0.0
```

## Uso mínimo

```cpp
#include <WiFi.h>
#include <WebServer.h>
#include <SuperDMZ.h>

WebServer server(80);
SuperDMZ  tunnel;

void setup() {
  WiFi.begin("SUA_REDE", "SUA_SENHA");
  while (WiFi.status() != WL_CONNECTED) delay(200);

  server.on("/", [](){ server.send(200, "text/html", "Hello"); });
  server.begin();

  tunnel.begin("SEU_TOKEN_48_HEX", 80);   // 80 = porta do WebServer (= local_port do tunnel)
}

void loop() {
  server.handleClient();
  tunnel.loop();
}
```

## API

```cpp
bool begin(const char* token, uint16_t localPort, const char* node = "");
```
Inicia o tunnel. `token` é o de 48 hex chars do painel. `localPort` é a porta do seu WebServer **e precisa bater** com o `local_port` configurado no tunnel pelo painel. `node` é opcional — se omitido, a lib consulta o painel pra descobrir o melhor node.

```cpp
void loop();
```
Chame em todo `loop()`. Mantém WSS vivo, processa requests, gerencia reconnect, ping/pong.

```cpp
void onStatus(void (*cb)(bool online, const char* publicUrl));
```
Callback chamado quando o tunnel sobe/cai.

```cpp
bool isOnline();
const char* publicUrl();
uint32_t bytesIn();
uint32_t bytesOut();
```
Telemetria pra mostrar status no seu próprio dashboard / OLED.

```cpp
void reconnect();
```
Força reconexão. Raramente necessário — auto-reconnect já cobre quase tudo.

## Limites e considerações

- **Conexões concorrentes**: ESP32 vanilla (sem PSRAM) aguenta ~3 requests HTTP simultâneos. Com PSRAM, ~10. Acima disso o mbedtls começa a ficar sem heap.
- **WiFi precisa estar UP**: chame `tunnel.begin()` só depois que `WiFi.status() == WL_CONNECTED`. A lib detecta WiFi caindo e reconecta sozinha.
- **Cert chain TLS**: a lib confia em qualquer cert TLS do node (lado outbound). O TLS termina no node, não no ESP, então a CA do tunnel é pública (Let's Encrypt) mas a verificação é simplificada pra economizar heap.

## Exemplos incluídos

- **HelloWorld** — WebServer.h básico, página estática.
- **WebServerBridge** — ESPAsyncWebServer com endpoints JSON e controle de GPIO.
- **ProvisioningPortal** — captive portal de setup (WiFi + token salvos em NVS).

## Licença

MIT. Veja [LICENSE](LICENSE).

## Suporte

- Painel: [superdmz.com/login/](https://superdmz.com/login/)
- Manual completo: [superdmz.com/login/?page=installer#esp32-manual](https://superdmz.com/login/?page=installer#esp32-manual)
- Issues: [github.com/superdmz/SuperDMZ-Arduino/issues](https://github.com/superdmz/SuperDMZ-Arduino/issues)
