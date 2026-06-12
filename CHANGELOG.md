# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] - 2026-06-11

### Changed
- All `Serial.*` and internal log messages are now in English by convention
  (devtool/log output should be parseable globally, no matter the user's UI
  language).
- Compatibility table no longer lists ESP8266 — focus is ESP32 family.
- `library.properties` description rewritten in English for the Arduino
  Library Manager listing.

### Fixed
- `examples/WebServerBridge` and `examples/ProvisioningPortal` had
  inconsistent Serial log style — both now match HelloWorld's pattern
  (`[wifi] connecting...` / `[wifi] OK, IP = ...`).

## [1.0.0] - 2026-06-11

### Added
- Initial release.
- WSS client mirroring the SuperDMZ Go client wire protocol (envelopes
  `hello/ready/error/new_conn/conn_close/data/ping/pong`).
- TCP multiplex over a single WebSocket — each incoming request opens a
  loopback connection to your WebServer port.
- Heartbeat (20 s ping / 60 s pong-wait) plus auto-reconnect.
- Examples:
  - `HelloWorld` — bare `WebServer.h` page.
  - `WebServerBridge` — `ESPAsyncWebServer` with JSON + GPIO control.
  - `ProvisioningPortal` — captive portal saving WiFi + token in NVS.
- Supported chips: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, Arduino
  Nano ESP32.
