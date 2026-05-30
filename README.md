# ⚡ PM1611 RS485 Reader

ESP32-based RS485 Modbus energy monitoring gateway inspired by the PM1611Q-WD smart energy meter.

This firmware is intended to reproduce the main user experience of the PM1611Q-WD while using an external Modbus RTU energy meter as the measurement source.

## 🎯 Project Goal

Build a modular ESP32 firmware using PlatformIO and the Arduino framework.

The device should provide:

- WiFi setup AP mode
- WiFi client mode
- Web dashboard and configuration pages
- MQTT publish and subscribe
- Relay ON/OFF control
- Configurable current limit protection
- Voltage, current, power, frequency, power factor, and energy monitoring
- Energy history
- RTC/NTP time synchronization
- User/admin login
- Web firmware update
- LCD display abstraction
- LED status system
- RS485 Modbus meter abstraction

The final firmware must not depend on ESPHome. PlatformIO is used because this project needs full control over tasks, Modbus polling, relay safety, web authentication, OTA, MQTT compatibility, and long-term firmware structure.

## 🔧 Configuration Pages

As of 2026-05-30 (implemented by Claude, AI Assistant):

Web UI now supports configuring all major device settings through intuitive configuration pages:

| Page       | Settings                                      | Stored In                  | Status  |
| ---------- | --------------------------------------------- | -------------------------- | ------- |
| Device     | Name, Hostname, Timezone, CO2 Factor          | NVS namespace `device`     | ✅ Live |
| Network    | WiFi SSID, Password                           | NVS namespace `network`    | ✅ Live |
| MQTT       | Broker, Credentials, Topics, Publish Interval | NVS namespace `mqtt`       | ✅ Live |
| Modbus     | Baudrate, Slave ID, Register Profile          | NVS namespace `modbus`     | ✅ Live |
| Protection | Current Limit, Trip Delay, Reset Mode         | NVS namespace `protection` | ✅ Live |
| Display    | LCD Brightness, Rotation Interval             | NVS namespace `display`    | ✅ Live |
| History    | Retention Days, Flush Interval                | NVS namespace `history`    | ✅ Live |
| System     | NTP Servers, Debug Level                      | NVS namespace `system`     | ✅ Live |

Each configuration page provides form-based input with validation and automatic NVS persistence. Changes take effect after page save, with reboot required only for critical settings (WiFi, MQTT, Modbus).

**ConfigManager Architecture**: New `ConfigManager` class provides unified NVS access across all config categories with sensible defaults on first boot. See `firmware/include/ConfigManager.h` and `docs/architecture/config-model.md` for details.

## Current Firmware Status

Working firmware checkpoints already implemented and tested on the ESP32-WROOM board:

| Area                | Status                                                                      |
| ------------------- | --------------------------------------------------------------------------- |
| PlatformIO baseline | Builds and uploads on `esp32dev`                                            |
| Serial diagnostics  | Boot banner, chip info, heartbeat, free heap, WiFi state                    |
| Config button       | Hold `GPIO32` to `GND` for 5 seconds to enter `CONFIG_MODE`                 |
| Builtin LED         | `GPIO2` turns ON in `CONFIG_MODE`                                           |
| Config AP           | Broadcasts `PM1611-SETUP-{last6mac}` with password `PM123456`               |
| Setup Web UI        | Available at `http://192.168.4.1/` in Config Mode                           |
| WiFi scan           | Web UI can scan nearby WiFi networks                                        |
| WiFi save           | Web UI saves SSID/password to NVS using `Preferences`                       |
| WiFi client         | On reboot, firmware reads NVS, connects as STA, and auto-reconnects         |
| WiFi recovery       | If STA fails or drops, firmware can fall back to a setup AP again           |
| Normal Mode Web UI  | Same lightweight UI is served at `http://<STA_IP>/` after WiFi connects     |
| RTC / NTP           | After WiFi connects, firmware syncs NTP and exposes PM1611-style `rtc` text |
| Home dashboard      | Shows status plus RAM, firmware slot, and WiFi signal progress bars         |

Current verified board identity:

```text
Chip: ESP32-D0WD-V3
MAC:  38:18:2b:83:d1:b4
AP:   PM1611-SETUP-83D1B4
```

Current HTTP endpoints:

| Method | Path             | Purpose                              |
| ------ | ---------------- | ------------------------------------ |
| `GET`  | `/`              | Home/status UI                       |
| `GET`  | `/network`       | Network scan and WiFi credential UI  |
| `GET`  | `/api/status`    | Firmware, mode, AP, STA, heap status |
| `GET`  | `/api/wifi/scan` | Scan nearby WiFi networks            |
| `POST` | `/api/wifi/save` | Save `ssid` and `password` to NVS    |
| `POST` | `/api/reboot`    | Reboot after config save             |

Current limitations:

- No login/session protection yet.
- No static IP configuration yet.
- No explicit WiFi test-before-save endpoint yet.
- No dedicated WiFi verify endpoint yet; current validation happens through connect + timeout + fallback AP behavior.
- No RS485/Modbus polling yet.
- No hardware RTC backup across power loss yet; time is restored through NTP after WiFi connects.
- No MQTT, relay protection, LCD, OTA, or production auth yet.

## 🧰 Target Platform

```text
MCU:        ESP32
Framework: Arduino
Build:     PlatformIO
IDE:       VS Code on Windows
Protocol:  Modbus RTU over RS485
Baudrate:  19200 default
```

Typical RS485 pins:

```text
RXD2:      GPIO16
TXD2:      GPIO17
TX_ENABLE: GPIO0
```

Important hardware note: GPIO0 is an ESP32 boot strapping pin. It can be used for RS485 TX enable during development, but a different GPIO is safer for production hardware if the RS485 circuit can affect boot mode.

## 🧩 PM1611 Feature Map

The original PM1611Q-WD behavior can be interpreted as these subsystems:

| Area      | Features                                                                |
| --------- | ----------------------------------------------------------------------- |
| Metering  | Voltage, current, power, frequency, PF, energy, CO2                     |
| Network   | WiFi AP setup, WiFi client, NTP                                         |
| Cloud/IoT | MQTT publish, MQTT subscribe, command handling                          |
| Control   | Relay ON/OFF, current limit, protection trip                            |
| Web UI    | Login, metering, device config, network config, MQTT config, admin, OTA |
| Local UI  | LCD display, status LEDs                                                |
| Storage   | Device config, credentials, history, relay/protection state             |
| Time      | RTC/NTP timestamp used in MQTT payloads and history                     |

## 🏗️ High-Level Architecture

```text
+------------------------------------------------------+
| Web UI / MQTT / LCD / LEDs                           |
+------------------------------------------------------+
| Application Services                                 |
| - Meter service                                      |
| - Relay service                                      |
| - Protection service                                 |
| - MQTT service                                       |
| - Config service                                     |
| - Time service                                       |
| - History service                                    |
| - Auth service                                       |
| - OTA service                                        |
+------------------------------------------------------+
| Domain Models                                        |
| - MeterData                                          |
| - RelayState                                         |
| - DeviceConfig                                       |
| - NetworkConfig                                      |
| - MqttConfig                                         |
| - ModbusConfig                                       |
| - ProtectionConfig                                   |
| - SystemStatus                                       |
+------------------------------------------------------+
| Hardware Abstraction                                 |
| - RS485 port                                         |
| - Modbus manager                                     |
| - Meter profile decoder                              |
| - Relay driver                                       |
| - LCD driver                                         |
| - LED driver                                         |
+------------------------------------------------------+
| ESP32 / Arduino / FreeRTOS / PlatformIO              |
+------------------------------------------------------+
```

## 📦 Main Firmware Modules

| Module              | Responsibility                                    |
| ------------------- | ------------------------------------------------- |
| `App`               | Boot order, service startup, supervision          |
| `ConfigManager`     | Persistent configuration, defaults, migration     |
| `NetworkManager`    | WiFi AP/STA state machine                         |
| `MqttManager`       | MQTT connect, publish, subscribe, command routing |
| `WebServerManager`  | Web pages and JSON APIs                           |
| `AuthManager`       | Login, roles, sessions                            |
| `OtaManager`        | Web firmware update                               |
| `MeterService`      | Owns normalized meter data                        |
| `ModbusManager`     | Modbus RTU polling and error handling             |
| `Rs485Port`         | UART2 and TX enable control                       |
| `MeterProfile`      | Register map decoding and scaling                 |
| `RelayService`      | Relay state machine and output driver             |
| `ProtectionService` | Current limit, trip, lockout, reset policy        |
| `HistoryService`    | Daily energy history                              |
| `TimeService`       | NTP, RTC fallback, formatted time                 |
| `DisplayManager`    | LCD abstraction                                   |
| `LedManager`        | LED status patterns                               |
| `DeviceIdentity`    | UID, hostname, MQTT identity                      |

## 🔗 Module Dependency Diagram

```text
                 +----------------+
                 |      App       |
                 +--------+-------+
                          |
    +---------------------+----------------------+
    |                     |                      |
+---v---+          +------v------+        +------v------+
|Config |          |  Network    |        |    Time     |
+---+---+          +------+------+        +------+------+
    |                     |                      |
    |              +------v------+               |
    |              | Web Server  |               |
    |              +------+------+               |
    |                     |                      |
    |              +------v------+               |
    |              | AuthManager |               |
    |              +-------------+               |
    |                                            |
+---v-------------+       +----------------------+------+
| MqttManager     |<----->|      MeterService           |
+---+-------------+       +----------+------------------+
    |                                |
    |                         +------v------+
    |                         | ModbusMgr   |
    |                         +------+------+
    |                                |
    |                         +------v------+
    |                         | RS485 UART  |
    |                         +-------------+
    |
+---v-------------+
| RelayService    |<------+
+---+-------------+       |
    |                     |
+---v-------------+       |
| ProtectionSvc   |<------+
+-----------------+

+-----------------+
| HistoryService  |<---- MeterService + TimeService
+-----------------+

+-----------------+
| DisplayManager  |<---- MeterService + Network + MQTT + Relay
+-----------------+

+-----------------+
| LedManager      |<---- SystemStatus
+-----------------+
```

## ⏱️ Runtime Task Plan

| Task            | Period                    | Responsibility                                |
| --------------- | ------------------------- | --------------------------------------------- |
| App supervision | 1s                        | Feed watchdog, check service health           |
| Modbus poll     | 1s default                | Read external meter registers                 |
| Protection      | 250ms-1s                  | Evaluate current limit and stale meter safety |
| MQTT            | 5s-60s publish interval   | Publish telemetry, receive commands           |
| Web server      | Event driven              | Dashboard, config, OTA, login                 |
| History         | 1min tick, daily rollover | Track daily energy buckets                    |
| UI              | 500ms-2s                  | LCD page rotation and LED state               |
| NTP             | Startup, then 6h-24h      | Maintain wall-clock time                      |

## 🔄 State Machines

### Boot

```text
BOOT
  -> LOAD_CONFIG
  -> INIT_HARDWARE
  -> INIT_NETWORK
  -> START_SERVICES
  -> RUNNING
```

Failure paths:

```text
LOAD_CONFIG_FAILED -> FACTORY_DEFAULTS
WIFI_FAILED        -> AP_SETUP_MODE
MODBUS_FAILED      -> RUNNING_DEGRADED
OTA_PENDING        -> OTA_RECOVERY_CHECK
```

### WiFi

```text
NO_CONFIG -> AP_MODE
STA_CONNECTING -> STA_CONNECTED -> GOT_IP
STA_CONNECTING -> STA_FAILED -> RETRY_WAIT -> AP_FALLBACK
```

### MQTT

```text
DISABLED
CONFIGURED
CONNECTING
CONNECTED
SUBSCRIBED
PUBLISHING
ERROR
RETRY_WAIT
```

### Relay

```text
OFF -> ON_REQUESTED -> ON
ON -> OFF_REQUESTED -> OFF
ON -> PROTECTION_TRIP -> LOCKOUT
LOCKOUT -> MANUAL_RESET -> OFF
```

### Modbus Polling

```text
IDLE
  -> REQUEST_REGISTER_GROUP
  -> WAIT_RESPONSE
  -> DECODE_RESPONSE
  -> UPDATE_SNAPSHOT
  -> IDLE
```

Error path:

```text
WAIT_RESPONSE -> TIMEOUT -> RETRY -> METER_OFFLINE
```

## 🌊 Data Flow

```text
External Modbus Meter
        |
        v
RS485 UART / Transceiver
        |
        v
ModbusManager
        |
        v
Meter Profile Decoder
        |
        v
Normalized MeterData
        |
        +------------------+
        |                  |
        v                  v
ProtectionService     HistoryService
        |                  |
        v                  v
RelayService          Energy History
        |
        v
Relay Output

Normalized MeterData
        |
        +---------> MQTT Payload Builder
        +---------> Web API
        +---------> LCD Display
        +---------> LED Status
```

## 📊 Normalized Meter Data Model

The rest of the firmware should not depend on raw Modbus registers.

Conceptual data model:

```text
MeterData
- voltage_v
- current_a
- power_w
- frequency_hz
- power_factor
- energy_kwh
- co2_kg
- timestamp
- valid
- stale
- source_status
```

For three-phase meters, keep phase data internally:

```text
MeterPhaseData
- voltage_an
- voltage_bn
- voltage_cn
- current_a
- current_b
- current_c
- power_a
- power_b
- power_c
```

The PM1611-compatible payload can publish total or selected values.

## 📨 MQTT Payload Compatibility

Publish PM1611-compatible telemetry to:

```text
pm1611/{uid}/data
```

Recommended additional topics:

```text
pm1611/{uid}/telemetry
pm1611/{uid}/status
pm1611/{uid}/relay
pm1611/{uid}/availability
pm1611/{uid}/event
```

Subscribe to:

```text
pm1611/{uid}/command
pm1611/all/command
```

Expected publish payload:

```json
{
  "uid": "ESP32-RS485-001",
  "rtc": "29/05/2026 10:30:00",
  "relay_state": "1",
  "meter_data": {
    "voltage": ["230.5", "V"],
    "current": ["10.5", "A"],
    "power": ["1420", "W"],
    "frequency": ["50.0", "Hz"],
    "pf": ["0.98", ""],
    "energy": ["15.23", "kWh"],
    "co2": ["10.36", "kg"]
  },
  "energy_history": ["1.2", "1.4", "0.9", "2.0", "1.8", "1.1", "0.7"]
}
```

Relay ON command:

```json
{
  "uid": "ESP32-RS485-001",
  "action": "set_relay"
}
```

Relay OFF command:

```json
{
  "uid": "ESP32-RS485-001",
  "action": "reset_relay"
}
```

Command validation rules:

- Reject malformed JSON.
- Reject commands with the wrong UID.
- Reject relay ON if protection lockout is active.
- Publish a command result event.

## ⚙️ Configuration Model

```text
DeviceConfig
- uid
- device_name
- location
- timezone
- co2_factor_kg_per_kwh

NetworkConfig
- wifi_ssid
- wifi_password
- ap_ssid
- ap_password
- fallback_ap_enabled
- static_ip_enabled
- ip
- gateway
- subnet
- dns

MqttConfig
- enabled
- host
- port
- username
- password
- client_id
- base_topic
- publish_interval_sec
- command_enabled
- retain_status
- tls_enabled

ModbusConfig
- slave_id
- baudrate
- parity
- stop_bits
- rx_pin
- tx_pin
- tx_enable_pin
- poll_interval_ms
- timeout_ms
- retry_count
- meter_profile

ProtectionConfig
- relay_enabled
- current_limit_a
- trip_delay_ms
- reset_mode
- auto_retry_enabled
- auto_retry_delay_sec
- trip_on_meter_stale
- power_on_restore_mode

AuthConfig
- admin_username
- admin_password_hash
- user_username
- user_password_hash
- session_timeout_sec

DisplayConfig
- enabled
- type
- i2c_address
- rotation_interval_sec
- brightness

HistoryConfig
- bucket_mode
- days_retained
- flash_flush_interval_sec
```

## 💾 Storage Strategy

Use ESP32 storage in layers:

| Storage           | Use                                                |
| ----------------- | -------------------------------------------------- |
| NVS / Preferences | Small config values, credentials, current settings |
| LittleFS          | Web assets, history files, config backup, logs     |
| RTC memory        | Last relay state, boot counter, OTA marker         |

Flash write rules:

- Save config only when changed.
- Debounce config writes.
- Do not write energy samples every second.
- Buffer history and flush periodically.
- Prefer daily history writes for long flash life.

## 🔐 Security Plan

Minimum security architecture:

- Admin login required.
- User/admin roles.
- Passwords stored as salted hashes.
- Session cookie with random token.
- Session timeout.
- CSRF token for state-changing web requests.
- MQTT username/password support.
- OTA restricted to admin.
- Config export must redact secrets.
- AP setup mode must use WPA2 password.
- First boot should force unique/default credential change.

## 🛡️ Relay Protection Plan

Protection inputs:

- Current total
- Configured current limit
- Meter data validity
- Modbus stale duration
- Relay state
- Trip delay
- Reset mode

Trip conditions:

```text
current > current_limit for trip_delay_ms
meter data stale and trip_on_meter_stale enabled
modbus offline while relay is ON
relay driver fault
```

Trip result:

```text
relay OFF
state = LOCKOUT
trip reason saved
MQTT event published
web status updated
LED fault pattern active
LCD fault page shown
```

Recommended default:

- Manual reset required after overcurrent.
- Trip on stale meter data enabled.
- Relay stays OFF after reboot if previous state was trip/lockout.

## 🗂️ Repository Structure

```text
pm1611-rs485-reader/
├── .github/
│   ├── ISSUE_TEMPLATE/
│   └── workflows/
├── README.md
├── docs/
│   ├── architecture/
│   ├── hardware/
│   │   ├── esp32.md
│   │   └── lcd.md
│   ├── api/
│   └── operations/
├── firmware/
│   ├── platformio.ini
│   ├── include/
│   ├── src/
│   ├── lib/
│   ├── data/
│   └── test/
├── hardware/
├── scripts/
└── tools/
```

Folder purpose:

| Folder               | Purpose                                                              |
| -------------------- | -------------------------------------------------------------------- |
| `.github/`           | GitHub workflows, issue templates, and repository automation         |
| `docs/architecture/` | Firmware architecture, state machines, roadmap, and design decisions |
| `docs/hardware/`     | Hardware selection and integration notes                             |
| `docs/api/`          | MQTT, web API, payload, and configuration contracts                  |
| `docs/operations/`   | Flashing, deployment, recovery, and production procedures            |
| `firmware/`          | PlatformIO ESP32 firmware project                                    |
| `firmware/data/`     | Reserved for optional LittleFS assets and future static files       |
| `hardware/`          | Schematics, PCB files, enclosure drawings, and wiring diagrams       |
| `scripts/`           | Repeatable build, flash, release, and maintenance scripts            |
| `tools/`             | Developer utilities, validators, and generators                      |

Current hardware notes:

- [ESP32 Hardware Capability Analysis](docs/hardware/esp32.md)
- [LCD Hardware Analysis](docs/hardware/lcd.md)

Development planning:

- [One-Man Army Development Roadmap](docs/architecture/one-man-roadmap.md)

## 🛣️ Step-by-Step Roadmap

### Step 0: Project Baseline

Goal: Prepare the PlatformIO project structure.

Tasks:

- Confirm ESP32 board type.
- Confirm `platformio.ini` environment.
- Create folder structure.
- Add README and architecture docs.
- Add empty service skeletons only after architecture is accepted.

Exit criteria:

- Project opens cleanly in VS Code.
- PlatformIO can identify the board environment.

### Step 1: Hardware Pin Map

Goal: Lock hardware assumptions before firmware logic.

Tasks:

- Confirm RS485 RX/TX/TX enable pins.
- Confirm relay GPIO and active polarity.
- Confirm LED GPIOs.
- Confirm LCD type and bus.
- Confirm button inputs if present.
- Decide whether GPIO0 remains TX enable.

Exit criteria:

- Hardware pin table exists.
- Risky boot pins are documented.

### Step 2: Core Boot Skeleton

Goal: Create a clean application shell.

Tasks:

- Implement `App`.
- Implement basic logging.
- Implement `SystemStatus`.
- Add boot state machine.
- Add service initialization order.

Exit criteria:

- ESP32 boots.
- Serial log shows boot stages.
- No blocking initialization loops.

### Step 3: Configuration System

Goal: Store and validate device settings.

Tasks:

- Add `ConfigManager`.
- Add versioned config model.
- Add defaults.
- Add validation.
- Add NVS persistence.

Exit criteria:

- Config loads on boot.
- Missing config creates defaults.
- Invalid config falls back safely.

### Step 4: RS485 and Modbus Foundation

Goal: Communicate with the external meter.

Tasks:

- Configure UART2.
- Control RS485 TX enable.
- Implement Modbus timeout/retry policy.
- Read known test registers.
- Add meter offline detection.

Exit criteria:

- Firmware can read at least one register from the meter.
- Timeout does not block the rest of the firmware.

### Step 5: Meter Profile and Normalized Data

Goal: Decode Modbus data into firmware-owned units.

Tasks:

- Add `MeterProfile` interface.
- Add first concrete register map.
- Decode voltage, current, power, frequency, PF, energy.
- Apply scaling and endian handling.
- Produce `MeterData`.

Exit criteria:

- Serial log or debug endpoint shows correct normalized values.
- Stale/invalid values are clearly marked.

### Step 6: Relay Control

Goal: Control relay safely.

Tasks:

- Add `RelayService`.
- Add relay state machine.
- Add boot restore policy.
- Add manual ON/OFF API internally.

Exit criteria:

- Relay turns ON/OFF reliably.
- Relay state is tracked correctly.

### Step 7: Protection Logic

Goal: Add local safety behavior.

Tasks:

- Add `ProtectionService`.
- Add current limit 1A-16A.
- Add trip delay.
- Add lockout.
- Add manual reset.
- Add stale-meter trip option.

Exit criteria:

- Overcurrent trips relay.
- Relay cannot turn ON while locked out.
- Protection works without WiFi/MQTT.

### Step 8: WiFi Manager

Goal: Bring up network connectivity.

Tasks:

- Add STA connection mode.
- Add setup AP fallback.
- Add reconnect backoff.
- Add hostname from UID.

Exit criteria:

- Device connects to configured WiFi.
- Device starts AP if WiFi is not configured or fails.

### Step 9: MQTT Compatibility

Goal: Publish PM1611-compatible telemetry and receive commands.

Tasks:

- Add MQTT config.
- Connect to broker.
- Publish `pm1611/{uid}/data`.
- Subscribe to command topic.
- Implement `set_relay`.
- Implement `reset_relay`.

Exit criteria:

- Broker receives PM1611-style JSON.
- MQTT command can control relay under protection rules.

### Step 10: Time and NTP

Goal: Provide correct timestamps.

Tasks:

- Add timezone config.
- Sync NTP after WiFi connection.
- Add fallback time state.
- Format timestamp as `DD/MM/YYYY HH:MM:SS`.

Exit criteria:

- MQTT payload includes correct `rtc`.
- System marks whether time is synced.

### Step 11: Energy History

Goal: Match PM1611-style 7-day history.

Tasks:

- Track daily energy from total kWh.
- Store 7 buckets.
- Handle midnight rollover.
- Detect meter energy reset.
- Persist history safely.

Exit criteria:

- MQTT payload includes `energy_history`.
- History survives reboot.

### Step 12: Web UI Foundation

Goal: Configure and monitor from browser.

Tasks:

- Add embedded web server.
- Add JSON API.
- Add dashboard.
- Add login.
- Add protected routes.

Exit criteria:

- Browser shows meter data.
- Relay can be controlled from web UI.
- Unauthenticated users cannot access protected pages.

### Step 13: Configuration Web Pages

Goal: Make the device configurable without serial access.

Tasks:

- Device page.
- Network page.
- MQTT page.
- Modbus page.
- Protection page.
- Admin page.

Exit criteria:

- Settings can be changed and saved.
- Device can reboot and keep settings.

### Step 14: OTA Update

Goal: Update firmware from web UI.

Tasks:

- Add OTA upload page.
- Restrict to admin.
- Validate image.
- Reboot after success.
- Report update errors.

Exit criteria:

- Firmware can be updated from browser.
- Failed update does not erase running firmware.

### Step 15: LCD and LED UI

Goal: Add local appliance-style feedback.

Tasks:

- Add LCD abstraction.
- Add display pages.
- Add LED status patterns.
- Show WiFi, MQTT, relay, fault, and meter state.

Exit criteria:

- Device state is understandable without browser or MQTT.

### Step 16: Recovery and Hardening

Goal: Make firmware reliable.

Tasks:

- Add watchdog.
- Add reset reason reporting.
- Add MQTT reconnect testing.
- Add WiFi reconnect testing.
- Add Modbus offline recovery.
- Add config migration.
- Add factory reset.

Exit criteria:

- Device recovers from WiFi loss.
- Device recovers from MQTT loss.
- Device handles meter disconnect safely.
- Device can factory reset.

### Step 17: Production Testing

Goal: Validate as an appliance.

Tasks:

- 24h Modbus soak test.
- Relay trip/recovery test.
- Power cycle test.
- OTA success/failure test.
- Flash write review.
- Memory usage review.
- Web UI responsiveness test.

Exit criteria:

- Stable release candidate.
- Known limitations documented.

## 👉 Immediate Next Step

The next practical firmware step is to start the RS485 Modbus proof while keeping the network/config foundation stable:

1. Confirm RS485 A/B wiring and transceiver direction behavior.
2. Initialize UART2 with RX `GPIO16`, TX `GPIO17`, baud `19200`.
3. Add one Modbus RTU read request with timeout/retry logging.
4. Read one known register from the external meter.
5. Document the meter model and first working register map.

Before production use, also add login/session protection around the Web UI config routes.
