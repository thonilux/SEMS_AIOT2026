# PM1611 RS485 Reader Project Progress Report

Prepared from the repo documentation and Git history up to `2026-06-02`.

## 1. Executive Summary

This project started as a firmware and architecture exercise to recreate the user experience of the PM1611Q-WD smart energy meter, but with an ESP32-WROOM reading data from an external RS485 Modbus energy meter.

The current firmware is no longer a skeleton. It now boots reliably, exposes a config AP, persists WiFi and device settings in NVS, serves a lightweight Web UI in both AP and STA modes, syncs NTP-backed time, reads a Schneider EM6400 / PM2xxx bootstrap profile, and already includes runtime wiring for MQTT, relay protection, LCD display, and energy history.

The biggest progress theme so far is this:

- first we built the control plane;
- then we stabilized configuration and recovery;
- then we proved the Modbus meter path;
- and now we are moving into full integration and field testing.

## 2. Project Target

The project goal, as described in the docs, is to build an ESP32-based RS485 Modbus energy monitoring gateway that mirrors the useful parts of the PM1611Q-WD experience without depending on ESPHome.

Core target features:

- WiFi AP setup mode and WiFi client mode
- Web dashboard and configuration pages
- MQTT publish and subscribe
- Relay ON/OFF control with current-limit protection
- Voltage, current, power, frequency, power factor, and energy monitoring
- Energy history
- RTC / NTP synchronization
- LCD display and LED status
- RS485 Modbus abstraction

## 3. What We Built First

The roadmap intentionally forced a disciplined order:

1. get the repo and tooling under control
2. make the ESP32 boot and flash cleanly
3. stabilize configuration and recovery
4. build WiFi setup and NVS persistence
5. add a usable Web UI
6. prove RS485 Modbus communication
7. wire in runtime services like MQTT, relay, display, and history

That sequencing kept the work from turning into a feature pile.

## 4. Timeline

### 2026-05-29

- Setup Web UI work began.
- WiFi scan in the setup portal was added.
- This established the first usable browser-based entry point on the ESP32.

### 2026-05-30

- WiFi credentials were persisted in NVS.
- Web UI started serving after STA connection.
- NTP-backed RTC baseline was added.
- Home dashboard status bars were introduced.
- Byte values were reformatted into human-readable units.
- The Web UI was split into Home and Network pages.
- Docs were updated to match the implementation.

Key commits:

- `1b3d46d` Persist WiFi credentials in NVS
- `586c26b` Serve web UI after WiFi connects
- `7ed064f` Add NTP-backed RTC baseline
- `d9f176b` Add Home status progress bars
- `8716651` Format byte values in human units
- `e1271d0` Split web UI into home and network pages
- `afa1a39` Update docs to match implemented config API and pages

### 2026-05-31

- Runtime config mode was added using the GPIO32 long-press gesture.
- The config AP was added with `PM1611-SETUP-{last6mac}` naming.
- The setup UI grew WiFi scan and save functions.
- NVS persistence expanded from WiFi into Device, MQTT, Modbus, Protection, Display, History, and System pages.
- WiFi recovery behavior was hardened.
- The main Web UI was experimented with in LittleFS, then rolled back because the string-rendered approach felt lighter on this board.
- A Schneider EM6400 / PM2xxx Modbus bootstrap profile was added from the manual and YAML references.
- A Meter page and RS485 polling proof were added.
- MQTT, relay, display, and history runtime services were activated.

Key commits:

- `ec98f98` Add config button boot mode decision
- `19f1428` Enter config mode from runtime button hold
- `327e312` Start config access point in config mode
- `9cc9390` Add setup web UI with WiFi scan
- `4d9fb11` Update docs with WiFi setup progress
- `376f0fe` Update docs for WiFi recovery flow
- `fab3971` fix wifi architecture
- `270a22a` Move main web UI to LittleFS
- `8162996` Show LittleFS usage in the dashboard
- `432f925` Revert "Move main web UI to LittleFS"
- `c2e2d18` Revert "Show LittleFS usage in the dashboard"
- `9aa0234` Refactor web UI render and document LittleFS rollback
- `ce450b9` Add Schneider Modbus bootstrap profile
- `3c9a3f3` Add Schneider meter bootstrap UI and RS485 poll
- `f1f9b96` Persist config pages across reboot
- `1109c7d` Activate MQTT relay display and history runtime

## 5. Current Delivered Capabilities

The firmware currently supports:

- boot banner and serial diagnostics
- config mode entry via GPIO32 long press
- config AP and WiFi scan
- WiFi credential persistence in NVS
- STA connect and auto-reconnect
- fallback AP behavior after WiFi failure
- NTP-backed RTC string output
- Web UI in AP and STA mode
- Home, Meter, Network, Device, MQTT, Modbus, Protection, Display, History, System pages
- read-only NVS preview page
- Schneider bootstrap Modbus UI and polling
- runtime relay control
- runtime display rotation
- runtime history persistence
- runtime MQTT publishing and command subscription

## 6. Major Challenges And How They Were Handled

### 6.1 ESP32-WROOM resource pressure

The board is small enough that every feature matters. We learned quickly that UI style and library choice affect responsiveness and flash usage.

What we did:

- kept the Web UI lightweight
- split page rendering helpers out of `main.cpp`
- tried LittleFS static UI, then rolled it back
- watched RAM and flash usage during every build

### 6.2 LittleFS was cleaner, but not the best fit here

The LittleFS experiment was valuable, but the team found the string-rendered UI felt lighter and simpler on this exact hardware and workflow.

Result:

- LittleFS UI was documented as a trial
- the active UI stayed string-rendered
- page helpers were moved into `WebUiPages.inc` for readability

### 6.3 NVS key length limits

During save testing, several config pages failed because the original NVS keys were too long for the ESP32 `Preferences` layer.

Symptom:

- `KEY_TOO_LONG` on Protection and related pages

Fix:

- shortened the stored keys
- kept the runtime models intact
- added a `/nvs` preview page to show exactly what is loaded

### 6.4 Relay command parsing bug

The relay OFF path was accidentally matching the word `set` inside `reset`.

Fix:

- switched to exact action matching
- allowed `set`, `reset`, `on`, `off`, `true`, `false`, `1`, `0`

### 6.5 MQTT broker transport mismatch

HiveMQ Cloud uses TLS on port `8883`, but the initial MQTT path used plain TCP.

Fix:

- added `WiFiClientSecure`
- switched MQTT transport automatically when port is `8883`
- added a broker connection test button to the MQTT page

## 7. Current Architecture Snapshot

The current implementation has three major layers:

- **Boot and mode selection**: normal mode, config mode, AP fallback, WiFi recovery
- **Runtime services**: Modbus polling, MQTT, relay protection, display, history
- **Web control plane**: status, config pages, NVS preview, relay control, meter page

The current source structure is also cleaner than it was at the beginning:

- `main.cpp` still owns the firmware flow and API handlers
- `WebUiPages.inc` carries the page render helpers
- `ConfigManager` owns NVS persistence
- hardware constants live in `PinMap.h`

## 8. What Is Ready To Demo

The most presentation-ready points right now are:

1. boot and serial diagnostics
2. config AP and WiFi setup flow
3. NVS persistence and preview
4. Home dashboard status and health bars
5. Meter page and Schneider bootstrap proof
6. WiFi recovery behavior
7. MQTT connection test and relay command path

## 9. What Is Still In Progress

The remaining major milestone is still the one the roadmap calls out:

- full RS485 Modbus proof with a live meter connected and verified on real registers

After that, the next obvious layers are:

- full meter data model normalization
- richer MQTT telemetry payloads
- hardened login / access control
- LCD and LED behavior polish
- OTA / administration hardening

## 10. Recommended Presentation Closing

If you need a short verbal summary for the presentation, use this:

> We started from a clean repository and a hardware-first roadmap, then built the ESP32 control plane, WiFi recovery, NVS-backed configuration, and a lightweight dashboard. We proved the Schneider meter bootstrap path, activated relay, MQTT, display, and history runtime services, and documented the rollback of the LittleFS UI experiment when it proved heavier than the string-rendered path on this board. The remaining work is now focused on final RS485 meter verification and end-to-end telemetry hardening.

