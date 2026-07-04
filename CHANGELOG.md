# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0] - 2026-06-25

### Added — Gateway support (target_host + multiple tunnels)
- `begin()` gained an optional 4th argument `targetHost` (default `"127.0.0.1"`).
  Each incoming connection is dialed to `targetHost:localPort` instead of always
  loopback — so a Gateway can bridge to ANOTHER machine on the LAN
  (e.g. `tunnel.begin(token, 80, "", "192.168.0.20")`). Mirrors the Go client's
  `target_host`.
- **Multiple `SuperDMZ` instances may now run at once** (one per tunnel). The WS
  callback is dispatched through a per-instance capturing lambda instead of a
  static singleton, so N tunnels with N tokens/targets coexist. Heap-bound on
  vanilla ESP32 (~2-3 TLS tunnels); higher with PSRAM.

### Changed
- `handleNewConn()` dials `_targetHost` (was hardcoded `127.0.0.1`); the failure
  log now prints the actual target.
- Removed the `_instance`/`wsEventStatic` singleton plumbing.

## [1.1.14] - 2026-06-15

### Fixed (the actually-shippable one)
- `doc["ws_url"] | nullptr` was returning null even when `JSON parse: Ok`
  reported success and the payload contained `ws_url`. Root-caused to an
  interaction between `deserializeJson(doc, String)` and the Arduino-ESP32
  core 3.x heap layout. Switched to the explicit `deserializeJson(doc,
  body.c_str(), body.length())` overload + `.as<const char*>()` accessor.
- When ws_url is missing now, the lib iterates the JSON object and logs each
  key + its type. Future bugs of this class never get to "guess the JSON
  shape" again.

## [1.1.13] - 2026-06-15

### Added — structured logger
- New `tunnel.onLog(LogCb)` registers a callback that receives every
  internal step of the library as a single line prefixed with
  `[SuperDMZ:<tag>]`. The `SmartIoT-Debug` example wires it into its ring
  buffer, so the live `/log` view now reveals the entire dial sequence:
  DNS round-trip, HTTPS POST, Content-Length, body bytes, JSON parse
  result, WS connect/disconnect, hello/ready handshake, ONLINE event.
- Tags used internally: `begin`, `resolve`, `loop`, `ws`, `ready`,
  `envelope`, `newconn`. Each step has a dedicated tag — debugging the
  tunnel is now a matter of reading `/log`, not guessing.

## [1.1.12] - 2026-06-15

### Fixed
- Read the resolve-server.php response body manually with a 3 s timeout
  loop instead of `http.getString()` or `deserializeJson(doc,
  http.getStream())`. Both returned empty body on a perfectly valid
  HTTP 200 response on arduino-esp32 core 3.x + HTTPS — the response
  headers arrived before the body, both helpers checked once for a stream
  byte and gave up. The new loop polls `available()` and accumulates
  until Content-Length is satisfied or the timeout fires.

### Changed (server-side — outside this repo, but relevant)
- `/api/resolve-server.php` now emits an explicit `Content-Length` so PHP
  never falls back to `Transfer-Encoding: chunked`. The ESP32 stream API
  does not decode chunked when you read via `getStreamPtr()`, which was
  the root cause behind several "HTTP 200 / json err=InvalidInput" cycles
  during the 1.1.10–1.1.13 series.

## [1.1.11] - 2026-06-15

### Changed (security)
- The token is now sent **only** via POST form body **and** an
  `X-Tunnel-Token` header — never as a query string parameter. URL query
  strings end up in every reverse-proxy and CDN access log on the way; a
  POST body and a header do not.
- The companion server (`/api/resolve-server.php`) was switched to
  POST-only at the same time. The Windows/Mac/Linux Go client will get
  the same change in its next release.

## [1.1.10] - 2026-06-15

### Fixed (probing)
- Sized the resolve-server response JSON arena from `<256>` to `<512>`.
  The response carries `ws_url + tunnel_name + public_url` and was right
  at the edge of the original 256-byte arena — silently failing with
  `NoMemory` on arduino-esp32 core 3.x.

## [1.1.9] - 2026-06-15

### Changed (behavioral)
- **Removed the hardcoded fallback node entirely.** There is no
  `SUPERDMZ_DEFAULT_NODE` anymore. The node is ALWAYS discovered from the panel
  for the token; until the lookup succeeds the library dials nothing. Previously
  a failed lookup fell back to `spo1`, which silently sent every non-`spo1`
  tunnel to the wrong relay (rejected as "token not registered").
- `begin()` no longer dials on lookup failure; `loop()` keeps re-resolving until
  the panel answers, then dials the real node once. Because no fallback WS is
  running during retries, the lookup has the TLS stack to itself (fixes the
  weak-link / RAM-tight case where a reconnecting WS starved the lookup).
- **`nodeHost()` now returns the node only while the token is authenticated**
  (tunnel online); empty otherwise. So the dashboard/`/info` show the real node
  only once authenticated (a "-" placeholder before), never a guess. The
  `SmartIoT-Debug` `/api/status` only reports node metadata when online, and
  `/info` prints `-` until then. Example firmware → `2.0.7`.

### Validated
- Server-side resolve verified end-to-end for **all 14 live tunnels** (header
  and query forms, ESP32 User-Agent): every token returns its correct registered
  node. Also confirmed Cloudflare allows the `ESP32HTTPClient` UA (it blocks some
  scripted UAs like `Python-urllib`, but not the ESP32's).

## [1.1.8] - 2026-06-15

### Added
- Node-lookup diagnostics: `resolveAttempts()` and `lastResolveInfo()` (last
  outcome, e.g. `HTTP -1`, `HTTP 200 but no ws_url`, `OK wss://…`). The
  `SmartIoT-Debug` `/info` endpoint now shows `resolve_tries` / `resolve_last`
  so a failing node lookup can be diagnosed from the web UI without a Serial
  Monitor. Example firmware → `2.0.6`.

### Changed
- `loop()` now disconnects the WebSocket *before* each offline re-resolve. On
  RAM-tight chips (ESP32-C3), especially on a weak link, two concurrent TLS
  handshakes (the reconnecting WS + the HTTPS lookup) could starve each other
  and the lookup kept failing — leaving the tunnel stuck on the default node.

## [1.1.7] - 2026-06-15

### Fixed
- **A node lookup that failed at boot got stuck on the default node forever.**
  `begin()` resolved the node once, right after the STA got its IP — but on a
  weak link DNS/TLS to the panel often isn't ready in those first seconds, so
  the lookup fell back to the default node (`spo1`) and never retried. A tunnel
  homed on any other node (`usa1`/`eur1`/`asi1`) then stayed offline forever
  while that relay rejected the token. `resolveNodeUrl()` is now a single quick
  attempt that returns "" on failure, and `loop()` keeps re-resolving (every
  `SUPERDMZ_RERESOLVE_MS` = 10 s) while offline, re-pointing the WebSocket to
  the correct node the moment the panel answers. `nodeHost()` always reflects
  the node actually in use. Diagnosed with the 1.1.6 `/info` endpoint: the lib
  was correctly built yet `node_resolved` stayed `spo1`, while the sketch's
  later `fetchNodeInfo()` (same HTTPS path) succeeded a few seconds after boot —
  pinpointing a startup-timing race, not a TLS-method problem.
- `SmartIoT-Debug` example firmware bumped to `2.0.5` so a flashed build is
  always distinguishable on the dashboard / `/info`, even when only the linked
  library changed.

## [1.1.6] - 2026-06-15

### Added
- `SuperDMZ::version()` — returns the library version string compiled into the
  binary (e.g. `"1.1.6"`), so a sketch can show which lib version actually got
  built rather than what the IDE merely reports as "installed". The
  `SmartIoT-Debug` example uses it in a new `/info` endpoint: a plain-text dump
  of lib + firmware versions, build timestamp, resolved node, heap, WiFi and
  token. Example firmware bumped to `2.0.4`.

## [1.1.5] - 2026-06-15

### Fixed
- **Tunnels homed on any node other than the default (`spo1`) never came
  online** — the relay logged `token not registered on this server` and the
  client kept reconnecting. `resolveNodeUrl()` performed the HTTPS panel lookup
  with `HTTPClient::begin(url)` and no explicit `WiFiClientSecure`, which is
  unreliable on ESP32: the TLS handshake against the Cloudflare-fronted panel
  failed most of the time, so the lookup fell back to the hardcoded default
  node and the WSS `hello` carried a token that default relay had never been
  registered for. Tunnels that happened to be homed on the default node worked
  by coincidence (the fallback masked the bug); every tunnel on another node
  (`usa1`/`eur1`/`asi1`) was effectively unreachable. The lookup now uses
  `WiFiClientSecure` + `setInsecure()` + `begin(client, url)` (the same pattern
  the `SmartIoT-Debug` example already used for its panel calls) and retries up
  to 3 times before falling back. `setInsecure()` is acceptable here: the call
  only returns routing info, and the WSS tunnel to the node remains the trust
  boundary. Not a v1.1.4 regression — both prior versions used the unguarded
  `begin(url)`.
- Bumped `SUPERDMZ_VERSION` to `1.1.5` to match the package metadata; it had
  been left at `1.1.3` (the 1.1.4 release only bumped `library.properties` /
  `library.json`).

### Added
- `SuperDMZ::nodeHost()` — returns the relay node hostname the tunnel
  resolved/connected to (empty until `begin()` has run). Lets diagnostics probe
  the node actually in use instead of guessing a fixed one.

### Changed
- **`SmartIoT-Debug` connectivity probes now target the token's resolved node**
  (via `nodeHost()`) instead of the hardcoded `spo1.nodes.superdmz.com`, and
  skip the node DNS/TCP/TLS probes entirely when no token is configured yet.
  The probe JSON gained a `node` field; example firmware bumped to `2.0.3`. Its
  `fetchNodeInfo()` panel call was also moved to the `X-Tunnel-Token` header
  (the published copy still carried the token in the query string).

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
