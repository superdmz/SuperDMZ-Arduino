# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.4] - 2026-06-14

### Security
- **Tunnel token no longer travels in the URL query string.** `resolveNodeUrl()`
  now sends the token in the `X-Tunnel-Token` HTTP header instead of `?token=...`
  when calling `/api/resolve-server.php`, so it can no longer leak into nginx /
  proxy access logs. The `SmartIoT-Debug` example's `/api/tunnel-status.php` call
  was updated the same way. The WebSocket `hello` already carried the token in
  the message body (unchanged). Backward-compatible: the SuperDMZ panel accepts
  the token via header or query during the transition, so older nodes/clients
  keep working.

## [1.1.3] - 2026-06-13

### Fixed
- **Large dashboard HTML over the tunnel was cut around ~10 KiB** with `curl`
  reporting `end of response with N bytes missing`. Two changes in
  `pumpLocalToWs()`:
  - `SUPERDMZ_BUF_SIZE` bumped from 1024 to 4096 — drains the loopback TCP
    buffer in a single read.
  - Removed the early `break` when `n < sizeof(buf)`. We now drain the local
    socket fully on every call so `WebServer.send_P()` can keep writing.

  These fixes ALONE don't eliminate the deadlock between a blocking handler
  and `loop()`-based draining: sketches that serve >10 KiB via `send_P()`
  must still chunk and yield to `tunnel.loop()` between chunks. See the
  `handleRoot()` of the `SmartIoT-Debug` example for the canonical pattern.

### Examples reorganized
- **Removed** `WebServerBridge` and `SmartIoT` — they were subsets of
  `HelloWorld` and `SmartIoT-Debug` respectively. The matrix is now three
  examples with no overlap:
  - `HelloWorld` — quick start, hardcoded creds, `WebServer.h`.
  - `ProvisioningPortal` — production template with captive portal.
  - `SmartIoT-Debug` — superset of the previous `SmartIoT` with ring log,
    network probes, OTA upload from the dashboard, token renew, NTP, node
    region flag and city.
- Each example folder now ships a screenshot.

### Fixed (examples)
- `ProvisioningPortal` had a route-dispatch bug where switching from STA
  back to AP (via "Reconfigure WiFi") kept showing the status dashboard at
  `192.168.4.1/`. Cause: the Arduino-ESP32 WebServer keeps handlers in an
  append-only linked list — calling `server.on("/", ...)` a second time
  ADDS a handler, never REPLACES the first one. Fix: register the routes
  ONCE in `setup()` with a single dispatcher (`handleRootDispatch()`)
  that picks the right page based on `appState`.
- Same example: `server.begin()` was being called BEFORE `WiFi.mode()`,
  so the server bound to a not-yet-initialised network interface and
  silently dropped connections. Moved to AFTER `enterAPMode()`/`enterSTAMode()`.
- Same example: the `status-badge` HTML started hardcoded as `"ONLINE"`,
  so if the `/api/status` fetch failed (in AP mode the route doesn't even
  exist) the badge stayed green. Changed default to `"OFFLINE"`; JS only
  flips it to green when the JSON confirms the tunnel is up.

### v1.2.0 attempt (NOT released)
A FreeRTOS background-task refactor was prototyped to eliminate the
chunking workaround in handlers entirely — `loopInternal()` would run
in its own task with mutex-protected WS access, so a blocking
`server.send_P()` couldn't starve the pump. The build flashed cleanly
but the tunnel stayed silently offline (zero bytes in/out, no
`[SuperDMZ]` events in the relay log) — suspected race between the
synchronous `_ws.beginSSL()` in `begin()` and the first `_ws.loop()`
inside the task. Reverted to 1.1.3. When v1.2.0 returns,
`_ws.beginSSL()` needs to move INSIDE the task as its first action.

### Validated on
ESP32-C3 Super Mini, 4 MB flash, arduino-esp32 core 3.3.8 + Arduino IDE 2.x.

## [1.1.2] - 2026-06-12

### Fixed
- `ProvisioningPortal` and `SmartIoT` examples failed to compile on
  Arduino IDE 3.x with arduino-esp32 core 3.x due to `enterSTAMode()`
  being called from `handleSave()` before its definition. The IDE's
  automatic prototype generation didn't pick it up inside the lambda
  context. Added explicit forward declarations for `enterAPMode()` and
  `enterSTAMode()` at the top of both sketches.

### Compatibility note
- The `Multiple libraries were found for "WiFi.h"` message users see when
  compiling with the Arduino IDE is informational, not an error — the
  compiler correctly prefers the esp32-bundled WiFi over user-installed
  WiFiNINA/WiFiEspAT for ESP32 targets. Nothing to fix on our side.

## [1.1.1] - 2026-06-12

### Changed (defensive: panel and code can disagree, library is authoritative)
- The lib now **always** uses the `localPort` passed to `begin()` when forwarding
  requests to the local WebServer. The `local_port` field from the panel's
  tunnel configuration is treated as advisory only — if the user picked
  HTTPS:443 in the panel by mistake but their WebServer is on HTTP:80, the
  panel value is silently overridden. Rationale: the user knows where their
  WebServer is listening; the panel can't get this wrong from inside an MCU.
- Hello message now carries `platform: "arduino-esp32"`, `local_scheme: "http"`
  and `local_port: <userPort>` so a smart relay can self-correct a misconfigured
  tunnel record without the user having to touch the panel.
- The previous `WARN: panel configured tunnel for port X, but library is
  using Y` message is now an informational `note:` (and only fires when the
  panel actually disagrees) — it's not a user-actionable warning.

### Why this matters
- A user creating their first tunnel in the panel can pick any combination of
  `protocol/scheme/port`. With v1.1.0 a wrong choice produced a tunnel that
  appeared "online" in the panel (WebSocket handshakes worked) but never
  delivered any HTTP request to the device (TCP loopback was hitting the
  wrong port). v1.1.1 makes that scenario just work.

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
