# ⚡ SEMS AIoT

ESP32-based RS485/Modbus energy monitoring gateway — reads up to 4 power meters over a shared RS485 bus, controls a 4-channel relay board, and publishes readings to an MQTT broker on a precise RTC/NTP-anchored schedule.

Current release: **v1.0.0 "Koper Demo v1"** — see [`docs/journal/`](docs/journal/) for the full development history and [`docs/manual_book.md`](docs/manual_book.md) for the end-user manual (Indonesian).

## 🎯 What It Does

- Reads **up to 4 Modbus RTU power meters** on one RS485 bus (round-robin polling), supporting:
  - Schneider PM2xxx / EM6400 family (FP32 register values)
  - Renata AX9L, 1-phase and 3-phase (INT32 register values)
- Controls a **4-channel relay board** — web toggle, MQTT command, overcurrent trip with optional auto-retry.
- Publishes to **MQTT** on a fixed topic shape (`<base>/elc_data/<suffix>`, `<base>/elc_wh/<suffix>`), with **per-meter configurable publish intervals** (separate real-time vs. energy-accumulator schedules), scheduled off the device's RTC/NTP clock rather than uptime.
- Dual network path: **Ethernet (W5500)** as priority route, **WiFi** as automatic backup — switches instantly if the cable drops.
- **Config Mode** (WiFi AP `SEMS-SETUP-XXXX`) for on-site setup without needing the production network; falls back into it automatically if WiFi never connects.
- Small **OLED status display** (I2C SSD1306) with a single physical touch button for navigation; auto-returns to the dashboard and auto-blanks the panel after idle timeouts.
- **Modbus write-register (FC06)** support for safe, non-communication parameters: CT/PT ratio (Renata AX9L, Schneider PM2120) and Digital Output switches (Renata AX9L DO1/DO2) — with read-back verification, and without touching the meter's own communication settings.
- Web-based **OTA firmware update**.

## 🌐 Web UI

Self-contained HTML/CSS/JS served directly by the ESP32 (no filesystem dependency). Pages:

| Page | Path | Notes |
| --- | --- | --- |
| Home | `/` | Quick links; Config Mode entry button |
| Network | `/network` | WiFi scan/save, saved networks, LAN/AP status |
| MQTT | `/mqtt` | Broker connection settings |
| Modbus | `/modbus` | Up to 4 meter slots, RS485 bus config, CT/PT ratio & DO switches, per-meter MQTT intervals |
| Relay | `/relay` | 4-channel ON/OFF, trip status, protection settings |
| System | `/system` | Device status, timezone, OTA link, reboot — **Config Mode only** |
| OTA | `/update` | Firmware upload — **Config Mode only** |

Most configuration is locked to **Config Mode** to keep the device stable while running unattended in Normal Mode; System and OTA are hidden from navigation (and blocked if opened directly) outside Config Mode.

## 🧰 Target Platform

```text
MCU:        ESP32 (ESP32-D0WD-V3)
Framework:  Arduino (arduino-esp32 v3.x via pioarduino)
Build:      PlatformIO
Protocol:   Modbus RTU over RS485 (single shared bus, configurable baud/parity)
Network:    W5500 Ethernet (priority) + WiFi STA (backup) + WiFi AP (Config Mode)
Display:    SSD1306 128x64 OLED over I2C
```

## 📁 Repository Structure

```text
pm1611-rs485-reader/
├── README.md
├── firmware/
│   ├── platformio.ini
│   └── src/main.cpp        # single-file firmware (all logic + embedded web UI)
├── docs/
│   ├── manual_book.md       # end-user manual (Indonesian)
│   ├── journal/              # dated development session logs
│   ├── modbus-profiles/      # register map notes per meter family
│   └── webui/, oled/         # screenshots used in the manual
└── tools/                    # developer/debug utilities
```

## 🚀 Build & Flash

```bash
cd firmware
pio run -e esp32dev            # build
pio run -e esp32dev -t upload  # flash over USB
```

Over-the-air update (device must be in Config Mode):

```bash
curl -F "image=@.pio/build/esp32dev/firmware.bin" http://<device-ip>/update
```

## 📖 Documentation

- [`docs/manual_book.md`](docs/manual_book.md) — full end-user manual: physical layout, OLED navigation, Config Mode setup flow, web UI walkthrough, MQTT schedule behavior.
- [`docs/journal/`](docs/journal/) — chronological development log, including hardware debugging sessions and design decisions.
- [`docs/modbus-profiles/`](docs/modbus-profiles/) — register map references for supported meter families.
