# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-06-12

### Added
- **New example: `SmartIoT`** — full production template combining the
  ProvisioningPortal flow with telemetry, a live dashboard, and OTA over
  the tunnel. Includes a synthetic temperature/humidity sensor, a vanilla-JS
  sparkline of temperature history, `/api/telemetry` JSON endpoint, and OTA
  via POST to `/api/ota`.

### Changed
- **`ProvisioningPortal` rewritten as production-grade.** Now includes:
  - Captive portal **WiFi network scan** with dropdown of SSIDs sorted by
    signal strength (no more typing the SSID by hand).
  - **In-place AP → STA transition** after saving config — no reboot needed.
  - **Live status dashboard** served via the tunnel with three cards
    (SuperDMZ / WiFi / System) auto-refreshing every 5 s.
  - **GPIO0 button** with two hold durations: 3 s reconfigures WiFi while
    keeping the token, 10 s factory-resets everything.
  - AP SSID now derived from the chip MAC (`SuperDMZ-Setup-XXXX`) so
    multiple devices don't collide in the same room.

### Notes
- This is a feature release. The library API (`SuperDMZ` class) is
  unchanged — existing code using v1.0.x keeps working without changes.
- `HelloWorld` and `WebServerBridge` are deliberately kept minimal for
  pedagogy; production users should base their work on `SmartIoT`.

## [1.0.2] - 2026-06-12

### Changed
- All in-code comments (`//`, doc headers, captive portal HTML strings) and
  example placeholders are now in English. Same rule as Serial output: code
  that ships globally must be readable globally, regardless of where the
  maintainer happens to be from.
- `ProvisioningPortal` captive portal labels translated to English
  ("Configure SuperDMZ", "WiFi password", "Save and reboot", etc.).
- Sketch placeholder strings (`YOUR_WIFI_SSID`, `PASTE_YOUR_TOKEN_HERE`)
  are now in English so the examples copy-paste cleanly anywhere.

### Notes
- No behavioural change. Existing v1.0.1 builds keep working unchanged —
  this is purely a documentation/i18n cleanup. Upgrade is recommended for
  cosmetic reasons only.

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
