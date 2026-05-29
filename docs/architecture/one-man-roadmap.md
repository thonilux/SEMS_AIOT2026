# 🚀 One-Man Army Development Roadmap

This roadmap is designed for a solo developer building the PM1611 RS485 Reader firmware step by step, without burning out halfway through the boss fight.

The goal is not to build every feature at once. The goal is to create small, testable increments that can be committed, pushed, and physically verified on hardware.

## 🧠 Development Principles

Use these rules throughout the project:

- Build one vertical slice at a time.
- Commit after each working checkpoint.
- Prefer boring, stable libraries over clever custom code.
- Keep hardware risk visible in documentation.
- Make every milestone testable without guessing.
- Do not add a web UI before the metering core is stable.
- Do not add MQTT relay control before local relay protection is stable.
- Do not add OTA until basic boot, config, and network behavior are predictable.
- Keep each feature small enough to debug from serial logs.

## 🧍 Solo Developer Constraints

A one-person project must control scope aggressively.

Main risks:

- Too many features before hardware validation.
- Unknown Modbus register map.
- LCD pinout uncertainty.
- Relay safety bugs.
- Flash/RAM pressure from web UI and JSON.
- Debugging WiFi, MQTT, Modbus, and web UI at the same time.
- Losing time to polish before the core device works.

Countermeasures:

- Use staged milestones.
- Add only one subsystem per milestone.
- Keep all default configs simple.
- Log every subsystem state.
- Make relay protection local and testable early.
- Use GitHub commits as recovery points.

## 🗺️ Roadmap Overview

Current status:

| Area | Status | Notes |
| --- | --- | --- |
| Repository structure | Done | Professional folder clustering is in place |
| GitHub remote | Done | Repo pushed to `thonilux/pm1611-rs485-reader` |
| Laptop tooling | Done | Git, Python, PlatformIO, npm/npx, VS Code checked |
| Storage cleanup | Done | C drive and dev caches cleaned |
| ESP32 board detection | Done | ESP32-D0WD-V3 on COM3 CH340 |
| Firmware boot baseline | Done | Built, flashed, serial heartbeat verified |
| ESP32 hardware analysis | Done | `docs/hardware/esp32.md` |
| LCD hardware analysis | Done | `docs/hardware/lcd.md` |
| Prototype schematic review | Done | `docs/hardware/prototype-schematic.md` |
| Prototype pin map | Done | `docs/hardware/pin-map.md` |
| PM1611 manual feature map | Done | `docs/architecture/pm1611-feature-adaptation.md` |
| Configurable Modbus design | Done | `docs/architecture/configurable-modbus-engine.md` |
| Config Mode design | Done | `docs/architecture/config-mode.md` |
| Config Mode API draft | Done | `docs/api/config-mode-api.md` |
| Modbus Config API draft | Done | `docs/api/modbus-config-api.md` |
| Config button firmware | Done | GPIO32 active-low boot hold selects `CONFIG_MODE` / `NORMAL_MODE` |
| WiFi AP Config Mode | Next | Start AP after button mode decision works |
| Web UI setup portal | Not started | Add after AP is working |
| RS485 Modbus proof | Not started | Use configurable static map first |

Current next milestone:

```text
Implement WiFi AP Config Mode.

Input:
- Selected mode from boot decision

Output:
- If CONFIG_MODE, start AP `PM1611-SETUP-{last6mac}`
- Print AP SSID and IP
- Keep NORMAL_MODE unchanged for now
```

```text
Phase 0: Repository and hardware decisions       [mostly done]
Phase 1: PlatformIO firmware baseline           [done]
Phase 2: Serial diagnostics and config foundation [in progress]
Phase 3: RS485 Modbus proof                     [not started]
Phase 4: Meter data model                       [not started]
Phase 5: Relay and protection                   [not started]
Phase 6: WiFi and setup mode                    [designed, not implemented]
Phase 7: MQTT compatibility                     [designed, not implemented]
Phase 8: Time and energy history                [not started]
Phase 9: LCD and LEDs                           [hardware analyzed, not implemented]
Phase 10: Web UI                                [API drafted, not implemented]
Phase 11: OTA and administration                [not started]
Phase 12: Hardening and release                 [not started]
```

## ✅ Phase 0: Repository And Hardware Decisions

Goal: Make the project ready for disciplined development.

Tasks:

- Finalize repository folder structure.
- Document ESP32 target choice.
- Document LCD type and risk.
- Document RS485 pins.
- Document relay pins.
- Document power supply assumptions.
- Confirm external Modbus meter model.
- Collect meter register map.

Deliverables:

- `README.md`
- `docs/hardware/esp32.md`
- `docs/hardware/lcd.md`
- `docs/hardware/pin-map.md`
- `docs/hardware/prototype-schematic.md`
- `docs/architecture/pm1611-feature-adaptation.md`
- `docs/architecture/configurable-modbus-engine.md`
- `docs/architecture/config-mode.md`
- Future `docs/hardware/modbus-meter.md`

Exit criteria:

- Hardware assumptions are written down.
- Unknowns are explicitly listed.
- GitHub repo is clean.

Recommended commit:

```text
Document hardware baseline
```

## 🧱 Phase 1: PlatformIO Firmware Baseline

Goal: Make the ESP32 compile, flash, boot, and print logs.

Tasks:

- Create `firmware/platformio.ini`.
- Select initial ESP32 board.
- Create `firmware/src/main.cpp`.
- Add serial boot log.
- Add project version string.
- Add basic build flags.
- Verify upload from VS Code.

Do not add:

- WiFi
- MQTT
- Modbus
- Web server
- Relay logic

Exit criteria:

- Firmware compiles.
- Firmware flashes.
- Serial monitor shows boot banner.
- GitHub has first firmware commit.

Recommended commit:

```text
Add PlatformIO firmware baseline
```

## 🔎 Phase 2: Diagnostics And Config Foundation

Goal: Add a small foundation without external hardware complexity.

Tasks:

- Add simple logger helper.
- Add `SystemStatus` model.
- Add `ConfigManager` skeleton.
- Add NVS defaults.
- Add pin config constants.
- Print reset reason.
- Print chip info and flash size.

Exit criteria:

- Device boots and reports system info.
- Config defaults load.
- Reset reason is visible.
- No blocking loops.

Recommended commit:

```text
Add diagnostics and config foundation
```

## 🔌 Phase 3: RS485 Modbus Proof

Goal: Prove the ESP32 can communicate with the external meter.

Tasks:

- Wire RS485 transceiver.
- Implement UART2 initialization.
- Implement TX enable control.
- Send a simple Modbus read request.
- Decode response CRC.
- Read one known register.
- Add timeout and retry logs.

Do not add:

- Full register map
- MQTT
- Relay protection
- LCD

Exit criteria:

- One real meter register can be read.
- Failed reads timeout cleanly.
- Serial log identifies online/offline meter state.

Recommended commit:

```text
Add RS485 Modbus read proof
```

## 📊 Phase 4: Meter Data Model

Goal: Convert raw Modbus data into normalized engineering values.

Tasks:

- Add `MeterData` model.
- Add `MeterProfile` abstraction.
- Add first meter register profile.
- Decode voltage.
- Decode current.
- Decode power.
- Decode frequency.
- Decode power factor.
- Decode total energy.
- Mark stale/invalid values.

Exit criteria:

- Serial output shows normalized meter values.
- Data remains valid only within freshness timeout.
- Incorrect or missing meter data is obvious.

Recommended commit:

```text
Add normalized Modbus meter data
```

## 🛡️ Phase 5: Relay And Protection

Goal: Make relay safety work locally before network control exists.

Tasks:

- Add relay driver.
- Add relay state machine.
- Add current limit setting.
- Add overcurrent trip logic.
- Add trip delay.
- Add lockout.
- Add manual reset through serial command or temporary debug function.
- Add stale-meter trip option.

Do not add MQTT relay control yet.

Exit criteria:

- Relay ON/OFF works.
- Overcurrent turns relay OFF.
- Lockout prevents relay ON.
- Meter offline behavior is safe.

Recommended commit:

```text
Add local relay protection
```

## 📡 Phase 6: WiFi And Setup Mode

Goal: Add network connectivity after local metering and protection are stable.

Tasks:

- Add WiFi station mode.
- Add AP fallback mode.
- Add reconnect backoff.
- Generate hostname from device UID.
- Store WiFi config in NVS.
- Log WiFi state transitions.

Exit criteria:

- Device connects to configured WiFi.
- Device starts AP if WiFi config is missing.
- Metering and protection continue if WiFi fails.

Recommended commit:

```text
Add WiFi manager
```

## 📨 Phase 7: MQTT Compatibility

Goal: Publish PM1611-compatible payloads and receive relay commands.

Tasks:

- Add MQTT config model.
- Connect to broker.
- Publish availability.
- Publish PM1611-style JSON.
- Subscribe to command topic.
- Implement `set_relay`.
- Implement `reset_relay`.
- Enforce UID match.
- Enforce protection lockout.

Exit criteria:

- Broker receives valid payload.
- Relay command works only when safe.
- Bad UID commands are rejected.
- MQTT disconnect does not break local protection.

Recommended commit:

```text
Add PM1611-compatible MQTT
```

## ⏱️ Phase 8: Time And Energy History

Goal: Add correct timestamps and 7-day energy history.

Tasks:

- Add NTP sync.
- Add timezone config.
- Format `rtc` as `DD/MM/YYYY HH:MM:SS`.
- Track daily energy from total kWh.
- Add 7-day history buckets.
- Persist history carefully.
- Detect meter energy reset.

Exit criteria:

- MQTT payload has correct `rtc`.
- `energy_history` survives reboot.
- No frequent flash writes.

Recommended commit:

```text
Add time sync and energy history
```

## 🖥️ Phase 9: LCD And LEDs

Goal: Add local status display after core data is stable.

Tasks:

- Add U8g2 dependency.
- Test ST7567 constructor.
- Add `DisplayManager`.
- Add compact display pages.
- Add `LedManager`.
- Show WiFi, MQTT, relay, meter, and fault states.
- Add contrast/rotation config if needed.

Exit criteria:

- LCD displays useful live data.
- Display does not block Modbus polling.
- LED patterns match system status.

Recommended commit:

```text
Add LCD and LED status UI
```

## 🌐 Phase 10: Web UI

Goal: Add browser dashboard and configuration after device behavior is stable.

Tasks:

- Add embedded web server.
- Add status API.
- Add meter API.
- Add relay API.
- Add dashboard page.
- Add network config page.
- Add MQTT config page.
- Add Modbus config page.
- Add protection config page.

Exit criteria:

- Browser can view live meter data.
- Browser can change config.
- Config persists after reboot.
- Web UI does not interfere with Modbus/protection.

Recommended commit:

```text
Add web dashboard and config API
```

## 🔐 Phase 11: Login, OTA, And Administration

Goal: Add appliance features after the core and web UI work.

Tasks:

- Add admin login.
- Add session handling.
- Protect config routes.
- Add OTA upload page.
- Add reboot button.
- Add factory reset.
- Add admin diagnostics page.

Exit criteria:

- Unauthenticated users cannot change settings.
- OTA update works from browser.
- Factory reset restores defaults.
- Admin page shows useful diagnostics.

Recommended commit:

```text
Add admin login and OTA update
```

## 🧪 Phase 12: Hardening And Release

Goal: Make the firmware reliable enough for real deployment.

Tasks:

- Add watchdog.
- Add long-run Modbus test.
- Test WiFi loss/recovery.
- Test MQTT loss/recovery.
- Test meter disconnect while relay is ON.
- Test power-cycle behavior.
- Test OTA failure behavior.
- Review flash write frequency.
- Review memory usage.
- Create release notes.

Exit criteria:

- Device runs for 24 hours without manual intervention.
- Relay protection still works during network failure.
- Recovery behavior is documented.
- Firmware version is tagged.

Recommended commit:

```text
Prepare first release candidate
```

Recommended tag:

```text
v0.1.0
```

## 📆 Weekly Solo Workflow

Use this rhythm:

```text
Day 1: Pick one milestone task
Day 2: Implement the smallest working version
Day 3: Test on hardware
Day 4: Fix and document findings
Day 5: Commit, push, and write next task notes
```

For short sessions:

```text
1. Pull latest repo
2. Check current milestone
3. Make one small change
4. Build
5. Test on device if possible
6. Commit only working checkpoints
```

## ✍️ Commit Style

Good solo commits:

```text
Add ESP32 boot diagnostics
Add RS485 UART driver
Decode voltage and current registers
Add relay trip lockout
Publish PM1611 MQTT payload
Document LCD pinout risk
```

Avoid vague commits:

```text
update
fix
test
misc
final
```

## 🎯 Definition Of Done

Each milestone is done only when:

- Code builds.
- Hardware behavior was tested or the limitation is documented.
- Serial logs are understandable.
- README/docs are updated if behavior changed.
- Git status is clean.
- Changes are pushed to GitHub.

## 🧊 Scope Control

Save these for later unless absolutely needed:

- TLS MQTT
- Fancy web dashboard animations
- Multi-meter support
- Cloud provisioning
- Mobile app
- Graph-heavy local history UI
- Remote log streaming
- Advanced user roles

First successful product target:

```text
RS485 meter -> ESP32 -> local protection -> MQTT payload -> basic web config
```

Everything else should build on that.
