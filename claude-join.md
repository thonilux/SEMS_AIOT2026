# Claude Join — Project Understanding

This file is my working reference for the PM1611 RS485 Reader project. I write here what I learned from reading all the code and docs, so future sessions can pick up cold.

---

## What This Project Is

An ESP32 firmware that reproduces the **Schneider PM1611Q-WD** smart energy meter experience, but uses an **external Modbus RTU meter** (Schneider EM6400 / PM2xxx family) as the actual measurement source.

It is **not** an ESPHome project. Full control over tasks, Modbus polling, relay safety, and web auth required a standalone PlatformIO + Arduino build.

The target is: **RS485 meter → ESP32 → local relay protection → MQTT telemetry → browser config UI**.

---

## Hardware

| Item | Value |
|------|-------|
| MCU | ESP32-D0WD-V3 rev 3.1 |
| Board ID | `esp32dev` |
| Flash | 4 MB |
| Upload port | COM4 (was COM3 in early docs) |
| MAC | `38:18:2b:83:d1:b4` |
| AP name | `PM1611-SETUP-83D1B4` |

### Pin Map (locked in `PinMap.h`)

| Signal | GPIO | Notes |
|--------|------|-------|
| Config button | 32 | Active-low, hold 5 s → Config mode |
| Builtin LED | 2 | Active-high |
| RS485 RX | 16 | UART2 |
| RS485 TX | 17 | UART2 |
| Relay output | 25 | Active-LOW (inverted) |
| LCD CS | 26 | SPI |
| LCD DC | 21 | SPI |
| LCD Reset | 22 | SPI |

**No TX-enable GPIO** is wired — the prototype uses `74HC04D` logic so RS485 direction is hardware-automatic. The firmware does not toggle a direction pin.

---

## Firmware Structure

```
firmware/
  platformio.ini      board=esp32dev, COM4, upload_speed=921600
  include/
    AppMode.h         enum AppMode { Normal, Config }
    PinMap.h          all GPIO constants
    Version.h         FW_NAME, FW_VERSION = "0.1.0-dev"
    ConfigManager.h   7 config structs + static load/save API
  src/
    main.cpp          2524 lines — entire application
    ConfigManager.cpp NVS persistence for all config categories
    WebUiPages.inc    HTML/CSS/JS helpers (#included into main.cpp)
```

Everything lives in `main.cpp` inside an anonymous namespace. There are no separate `.cpp` service files yet — `ModbusManager`, `RelayService`, `ProtectionService`, etc. are all flat functions in that one file.

### Libraries (lib_deps)
- `knolleary/PubSubClient ^2.8` — MQTT
- `olikraus/U8g2 ^2.35.30` — LCD driver

---

## Config System

`ConfigManager` is a static-method class with load/save for 7 NVS namespaces:

| Struct | NVS namespace | Key fields |
|--------|--------------|------------|
| `DeviceConfig` | `device` | device_name, hostname, timezone, co2_factor |
| `MqttConfig` | `mqtt` | host, port, username, password, client_id, base_topic, publish_interval_sec, enabled |
| `ModbusConfig` | `modbus` | baudrate, slave_id, parity, stop_bits, poll_interval_ms, timeout_ms, retry_count, meter_profile |
| `ProtectionConfig` | `protection` | relay_enabled, current_limit_a, trip_delay_ms, reset_mode, auto_retry_enabled, auto_retry_delay_sec, trip_on_meter_stale |
| `DisplayConfig` | `display` | enabled, type (0=ST7567/1=SSD1306), i2c_address, rotation_interval_sec, brightness |
| `HistoryConfig` | `history` | enabled, days_retained, flush_interval_sec |
| `SystemConfig` | `system` | ntp_server1, ntp_server2, debug_enabled |

`saveWithRecovery()` in ConfigManager.cpp clears the namespace and retries if the first write fails — guards against NVS corruption.

WiFi credentials use a separate legacy `Preferences` namespace (`network` / `wifi_ssid`, `wifi_pass`) — NOT through ConfigManager. This is intentional and separate from the 7 config structs.

---

## Boot Sequence

```
setup()
  ├─ Serial.begin(115200)
  ├─ GPIO init (button, LED, relay OFF)
  ├─ Load all configs from NVS
  ├─ loadFeatureRuntime() — relay state reset, history load
  ├─ resetMeterSnapshot()
  └─ If saved WiFi → connectToSavedWifi(), wifiConnecting=true
     Else → enterConfigMode() → start AP + web server

loop() (cooperative, no RTOS tasks)
  ├─ updateStatusLed()
  ├─ handleWiFiLifecycle()   — STA connect/reconnect, fallback AP, NTP start
  ├─ handleConfigButton()    — 5-second hold detect
  ├─ pollModbusMeter()       — UART2 RTU read, update meterSnapshot
  ├─ updateProtectionRuntime() — overcurrent trip, lockout, auto-retry
  ├─ updateMqttRuntime()     — connect, loop, publish on interval
  ├─ updateDisplayRuntime()  — LCD page rotation
  ├─ configServer.handleClient()
  └─ heartbeat every 1 s (Serial print)
```

**No FreeRTOS tasks** — everything runs cooperative in `loop()`. This is intentional for simplicity.

---

## Modbus Implementation

Direct UART2 bit-banged Modbus RTU. No external library.

Key function: `modbusReadHoldingRegisters()` in `main.cpp:1003`
- Sends FC03 request manually
- Reads back header + payload + CRC
- Verifies CRC with `modbusCrc16()`
- Returns raw bytes

Float decode: `decodeFloat32BigEndian()` — register pairs decoded as IEEE 754 big-endian.

`readSchneiderFloat()` wraps the above for 2-register (32-bit) float reads.

`pollModbusMeter()` reads these **Schneider EM6400 / PM2xxx** registers each poll interval:

| Field | Zero-based register | Unit |
|-------|-------------------|------|
| Voltage A-N | 3027 | V |
| Current A | 2999 | A |
| Active Power A | 3053 | kW |
| Frequency | 3109 | Hz |
| Active Energy | 2675 | kWh |

Power factor register (3077) is in the doc but **not currently polled** — `meterSnapshot.pf` stays NAN.

Modbus only polls in `AppMode::Normal` and only when WiFi is connected. This is a coupling to note — meter will not poll if WiFi is not up.

---

## Relay & Protection

State tracked in plain booleans/timestamps (no state machine struct):

```cpp
bool relayRequestedState   // what was asked
bool relayActualState      // what GPIO reflects
bool relayLockedOut        // trip lockout active
uint32_t relayTripUntilMs  // auto-retry expiry
uint32_t relayOvercurrentSinceMs
```

`updateProtectionRuntime()` runs every loop:
1. If relay disabled → force OFF, clear lockout
2. If locked out → check auto-retry timer; if expired and current safe → unlock
3. If meter stale AND `trip_on_meter_stale` → `tripRelay()`
4. If current > limit for `trip_delay_ms` → `tripRelay()`
5. Otherwise → set relay to requested state

Relay GPIO is **active-LOW** (`kRelayOutputActiveHigh = false`), so writing `HIGH` turns relay OFF.

---

## MQTT

Topics derived from `base_topic` (default: `pm1611`):
- Publish state: `pm1611/state` (retained)
- Publish telemetry: `pm1611/telemetry` (not retained)
- Subscribe commands: `pm1611/cmd` and `pm1611/relay/set`

Payload is PM1611-compatible JSON built by `buildMqttPayload()`. Includes uid, rtc, relay_state, meter_data (V/I/P/Hz/PF/kWh/CO2), energy_history[7].

Command parsing is string-based (`indexOf`), not JSON-parsed — looks for `set_relay` or `reset_relay` substrings.

Port 8883 → uses `WiFiClientSecure` with `setInsecure()` (no cert validation). All other ports use plain `WiFiClient`.

MQTT reconnect backoff: 5-second minimum between attempts.

---

## Web UI

Served by `WebServer` on port 80. String-rendered HTML — LittleFS was tried and rolled back.

All page HTML is built inline with `F()` macro strings. The shared header/footer and the three base pages (Home, Meter, Network) are in `WebUiPages.inc`; config pages are in `main.cpp` directly.

**All routes:**

| Method | Path | Handler |
|--------|------|---------|
| GET | `/` | buildHomePage |
| GET | `/meter` | buildMeterPage |
| GET | `/network` | buildNetworkPage |
| GET | `/device` | buildDeviceConfigPage |
| GET | `/mqtt` | buildMqttConfigPage |
| GET | `/modbus` | buildModbusConfigPage |
| GET | `/protection` | buildProtectionConfigPage |
| GET | `/display` | buildDisplayConfigPage |
| GET | `/history` | buildHistoryConfigPage |
| GET | `/system` | buildSystemConfigPage |
| GET | `/nvs` | buildNvsPreviewPage |
| GET | `/api/status` | full JSON status dump |
| GET | `/api/meter/status` | meter snapshot JSON |
| GET | `/api/wifi/scan` | WiFi scan results |
| GET | `/api/relay/state` | relay status JSON |
| POST | `/api/wifi/save` | save SSID/password to NVS |
| POST | `/api/device/save` | save DeviceConfig |
| POST | `/api/mqtt/save` | save MqttConfig |
| POST | `/api/mqtt/test` | live broker connection test |
| POST | `/api/modbus/save` | save ModbusConfig |
| POST | `/api/protection/save` | save ProtectionConfig |
| POST | `/api/display/save` | save DisplayConfig |
| POST | `/api/history/save` | save HistoryConfig |
| POST | `/api/system/save` | save SystemConfig |
| POST | `/api/relay/set` | relay ON/OFF via web |
| POST | `/api/reboot` | reboot after 800 ms |

Config pages are gated by `requireConfigMode()` — only shown when in Config mode or AP is active. MQTT page has no gate (accessible in Normal mode too).

---

## Energy History

7-day daily energy buckets stored in NVS namespace `history_rt`.

`HistoryRuntime` struct tracks:
- `dayKey` — YYYYMMDD integer
- `baselineEnergy` — meter kWh at start of today
- `daily[7]` — today's delta at [0], rolling back

Persisted every 30 seconds when dirty. Midnight rollover shifts array right, updates baseline.

---

## LCD

Two U8g2 driver objects are always instantiated (`ST7567` and `SSD1306` SPI). `getActiveDisplayDriver()` returns the one matching `DisplayConfig.type`.

4 rotating pages: Voltage/Current/Power → Energy/Frequency/PF → Network/MQTT status → Relay/Fault status.

Display uses SPI (hardware), not I2C, despite `i2c_address` field in config (leftover from I2C header in prototype, mostly unused for SPI types).

---

## Current Project Status (as of REPORT.md 2026-06-02)

| Area | Status |
|------|--------|
| WiFi AP + STA + recovery | Done |
| NVS config persistence | Done |
| Web UI + all config pages | Done |
| NTP / RTC | Done |
| Modbus RTU polling (Schneider bootstrap) | Done (live meter not yet verified) |
| MQTT publish + subscribe | Done |
| Relay + protection logic | Done |
| Energy history | Done |
| LCD display | Done (code present, hardware untested) |
| Authentication / login | Not started |
| OTA firmware update | Not started |
| Watchdog / hardening | Not started |

The roadmap says **Phase 3 (RS485 Modbus proof) is in progress** — code is there, waiting for live hardware verification with a real connected meter.

---

## Key Design Decisions / Things to Know

1. **Single-file architecture** — all logic is in `main.cpp`. No separate service files. The roadmap planned modular classes but everything is flat functions inside an anonymous namespace. This is intentional for now.

2. **Modbus only polls when WiFi is connected** (`pollModbusMeter()` line 1116 checks `WiFi.status() != WL_CONNECTED` and resets snapshot if not). This was probably unintentional — a bug or oversight.

3. **No TX-enable pin** — hardware auto-direction via 74HC04D. No GPIO needed in firmware for RS485 DE/RE.

4. **Relay is active-LOW** — easy to get wrong when adding relay control code.

5. **WiFi credentials are NOT in ConfigManager** — they use a separate `Preferences` namespace (`"network"`) with different keys (`wifi_ssid`, `wifi_pass`). The `NetworkConfig` struct in README is planned but not yet implemented in code.

6. **Power factor not polled** — register 3077 (PF A) exists in the Schneider profile doc but `pollModbusMeter()` does not read it. `meterSnapshot.pf` stays NAN.

7. **No authentication** — all web routes are unprotected. This is acknowledged as a known limitation.

8. **LittleFS was tried and reverted** — current UI is 100% string-rendered. There is no LittleFS dependency active. The `data/` folder is reserved but empty.

9. **GitHub remote**: `thonilux/pm1611-rs485-reader` (from roadmap). Current branch: `feature/littlefs-webui` (HEAD per REPORT.md).

10. **Timezone default**: `WIB-7` — Western Indonesian Time (UTC+7), which matches `iotlab@ft.uns.ac.id` — Universitas Negeri Surabaya, Indonesia.

---

## What's Cleanly Next

Per roadmap the immediate next hardware step is:
1. Wire a real Schneider EM6400/PM2xxx to the RS485 port
2. Verify `pollModbusMeter()` returns real values (currently code is correct but unverified on live hardware)
3. After that: authentication (Phase 11) and OTA

Then the big gaps are:
- Auth/sessions (entire `AuthManager` is unwritten)
- OTA upload page
- Watchdog
- Power factor poll (quick fix)
- Decouple Modbus polling from WiFi state (potential fix)
