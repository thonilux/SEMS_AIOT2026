# ⚡ PM1611 Feature Adaptation Notes

Source reviewed:

```text
D:\enerma\bms\2026\Energy Meter Manual Guide.pdf
Manual: PM1611Q-WD Energy Meter User Manual
Pages: 27
```

This document maps PM1611Q-WD features from the manual into the ESP32 RS485 Modbus gateway design.

The goal is not to clone the internal metering hardware. The goal is to adapt the user experience and external behavior while using external RS485 Modbus meters as the measurement source.

## 🧠 High-Level Feature Summary

The manual confirms that PM1611Q-WD includes:

- WiFi Access Point mode for initial setup.
- WiFi Client mode for local network operation.
- Browser-based Web UI.
- Admin and user login roles.
- Device status page.
- Network configuration page.
- Device configuration page.
- MQTT configuration page.
- Metering/control page.
- Administration page.
- Firmware update action.
- Reboot action.
- Logout action.
- LCD display with multiple status/data screens.
- Activity, network, alarm, and energy pulse LEDs.
- MQTT publish for telemetry.
- MQTT subscribe for relay control.
- Relay ON/OFF control.
- Configurable current limit from 1 A to 16 A.
- Metering data: voltage, current, power, frequency, power factor, energy, CO2.
- Energy history, apparently 7-day style history.
- RTC/NTP time display and synchronization status.

## 📍 Manual Feature References

| Feature | Manual Pages | Adaptation Decision |
| --- | --- | --- |
| General device purpose | 5 | Preserve as ESP32 RS485 energy gateway |
| WiFi AP setup | 6, 10, 11 | Implement config mode AP |
| AP SSID/password/IP | 10, 11 | Use PM-style SSID, but safer password policy |
| Admin/User login | 11, 13, 19 | Implement role-based Web UI |
| Web UI pages | 13-18 | Adapt page structure |
| Device status info | 13 | Implement status API/page |
| Meter data | 14, 23, 25 | Use decoded Modbus values |
| Energy history | 14, 15, 23, 25 | Implement 7-day buckets |
| Network page | 15 | Implement WiFi DHCP/static settings |
| MQTT config | 16, 20-22 | Implement MQTT settings |
| NTP config | 16 | Implement NTP server/time offset |
| Relay control | 17, 23, 25-27 | Implement local/web/MQTT relay service |
| Current limit | 17 | Implement 1 A to 16 A protection |
| Admin page | 17-18 | Implement credential management |
| Firmware update | 18 | Implement OTA update page |
| Reboot/logout | 18 | Implement admin controls |
| LCD screens | 8-9 | Implement display page rotation |
| LEDs | 7 | Implement LED status abstraction |
| MQTT publish payload | 23, 25 | Preserve compatible JSON format |
| MQTT commands | 25-27 | Preserve `set_relay` and `reset_relay` |

## 🔧 Hardware Feature Adaptation

Original PM1611 hardware:

- Integrated energy measurement.
- Input/output power terminals.
- Internal relay.
- LCD.
- LEDs.
- WiFi.

ESP32 RS485 gateway adaptation:

| PM1611 Hardware Role | ESP32 RS485 Gateway Replacement |
| --- | --- |
| Internal metering circuit | External RS485 Modbus meter |
| Internal register/measurement engine | Configurable Modbus register engine |
| Internal relay | External relay module or relay output circuit |
| LCD | ST7567 128x64 SPI LCD |
| WiFi | ESP32 WiFi AP/STA |
| Device UID | ESP32 MAC-derived UID |
| Energy pulse LED | Optional derived LED behavior |

## 📡 WiFi Setup Mode

Manual behavior:

- User activates Access Point mode from the device.
- User connects to the meter AP SSID.
- User opens the setup IP in a browser.
- User logs in as admin.
- User configures local WiFi.
- User saves and reboots.
- Device reconnects as WiFi client.

Adapted firmware behavior:

```text
Normal Mode
  -> config button long press
  -> Config Mode
  -> AP starts
  -> Web UI available at 192.168.4.1
  -> user configures WiFi/Modbus/MQTT
  -> save
  -> reboot
  -> AP disabled
  -> STA connects to configured WiFi
```

Current implemented behavior:

```text
Normal Mode
  -> loads WiFi credentials from NVS
  -> connects to saved WiFi as STA when credentials exist
  -> syncs RTC from NTP after WiFi connects
  -> starts lightweight Web UI at http://<STA_IP>/ after STA connects
  -> auto-reconnects if STA drops later
  -> can start a fallback AP if STA times out

Runtime config entry
  -> hold GPIO32 to GND for 5 seconds
  -> builtin LED GPIO2 turns ON
  -> AP starts as PM1611-SETUP-{last6mac}
  -> setup Web UI is available at http://192.168.4.1/
  -> user can scan nearby WiFi
  -> user can save SSID/password to NVS
  -> user can reboot from Web UI
```

Implementation note:

- The current UI is string-rendered.
- Page rendering helpers are split into `firmware/src/WebUiPages.inc` so `main.cpp` stays focused on firmware flow.
- A LittleFS-backed UI was tested during the refactor and then rolled back because the string-render path was simpler and felt lighter on this ESP32 board.
- The adapted Web UI now includes a Meter page for Schneider EM6400 / PM2xxx bootstrap status and live register proof-of-life.

Recommended AP defaults:

```text
SSID: PM1611-SETUP-{last6mac}
IP:   192.168.4.1
```

Manual default password is simple. For our firmware, use a safer approach:

```text
Development: PM123456
Production: unique per device or printed setup key
```

## 🌐 Web UI Feature Map

PM1611 Admin pages:

```text
Status
Network
Configuration
Metering
Administration
Update
Reboot
Logout
```

Adapted Web UI:

```text
/login
/dashboard
/status
/network
/device
/mqtt
/ntp
/modbus
/metering
/relay
/history
/admin
/update
/logout
```

## 👤 Login Roles

Manual confirms:

```text
Admin: full access
User: limited access
```

Adapted role model:

| Role | Access |
| --- | --- |
| Admin | All pages, config, relay, OTA, reboot |
| User | Status, meter data, history, limited account edit |

Default credentials should only be used for development. Production should force credential change.

## 📊 Status Page Adaptation

Manual status page includes:

- Serial number.
- Hardware version.
- Firmware version.
- MAC address.
- Device name.
- Device location.
- Uptime.
- RTC.
- Last NTP sync.
- WiFi SSID.
- WiFi signal.
- Operation status.
- Load switch condition.

Adapted status model:

```text
uid
hardware_id
firmware_version
mac_address
device_name
location_name
longitude
latitude
uptime
rtc
last_ntp_sync
wifi_ssid
wifi_rssi
operation_mode
relay_state
modbus_status
mqtt_status
free_heap
flash_size
reset_reason
```

## 📈 Meter Data Adaptation

Manual meter data includes:

```text
Voltage
Current
Power
Frequency
Energy
Power Factor
CO2
```

Adapted source:

```text
Configurable Modbus register map
```

If a value is not configured:

```text
Web UI: show N/A
MQTT compatibility mode: publish empty/default value if required
Protection: do not rely on missing current value
```

## 🗓️ Energy History

Manual shows energy history for 7 days.

Adapted implementation:

```text
daily_energy_buckets[7]
today_start_energy_kwh
last_rollover_date
```

Source:

```text
total_energy_kwh from Modbus meter
```

Daily usage:

```text
today_kwh = current_total_energy_kwh - today_start_energy_kwh
```

## 🧲 Relay And Current Limit

Manual behavior:

- Relay can be controlled ON/OFF from Web UI.
- Relay can be controlled from MQTT subscribe commands.
- Current limit adjustable from 1 A to 16 A.

Adapted behavior:

```text
RelayService
ProtectionService
Current limit: 1 A to 16 A
Trip on overcurrent
Manual reset after trip
Optional stale-meter trip
```

Important adaptation:

Original PM1611 measures current internally. Our firmware must only enforce current protection when the configured Modbus current field is valid and fresh.

## 📨 MQTT Publish Compatibility

Manual confirms publish payload contains:

- UID.
- RTC timestamp.
- Relay state.
- Meter data object.
- Voltage.
- Current.
- Power.
- Frequency.
- Power factor.
- Energy.
- CO2.
- Energy history.

Adapted payload should preserve:

```json
{
  "uid": "ESP32-RS485-83D1B4",
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
  "energy_history": [
    "1.2",
    "1.4",
    "0.9",
    "2.0",
    "1.8",
    "1.1",
    "0.7"
  ]
}
```

## 📨 MQTT Subscribe Commands

Manual confirms:

Relay ON:

```json
{
  "uid": "70B8F665A7E4",
  "action": "set_relay"
}
```

Relay OFF:

```json
{
  "uid": "70B8F665A7E4",
  "action": "reset_relay"
}
```

Adapted rules:

- UID must match this device.
- `set_relay` requests relay ON.
- `reset_relay` requests relay OFF.
- Protection lockout can reject relay ON.
- Device should publish updated relay status after command.

## 🖥️ LCD Adaptation

Manual LCD screens include:

- Welcome display.
- Before/after WiFi setup.
- Energy history.
- Relay status.
- Energy consumption.
- Important information screen.

Adapted LCD pages:

```text
Page 1: Welcome / boot
Page 2: WiFi/AP status
Page 3: Voltage/current
Page 4: Power/energy
Page 5: Frequency/PF
Page 6: Relay/current limit
Page 7: Energy history summary
Page 8: Fault/status
```

## 🚦 LED Adaptation

Manual LEDs:

- Activity LED.
- Network LED.
- Alarm LED.
- Energy pulse LED.

Adapted LED model:

| PM1611 LED | Adapted Behavior |
| --- | --- |
| Activity | Heartbeat/system alive |
| Network | WiFi/MQTT state |
| Alarm | Fault/protection trip |
| Energy Pulse | Optional pulse based on measured energy/power |

## 🔐 Administration

Manual administration includes:

- Admin credential update.
- User credential update.
- Firmware update.
- Reboot.
- Logout.

Adapted implementation:

```text
AuthManager
SessionManager
OtaManager
Admin API
Reboot API
Factory reset later
```

## 🧭 NTP / RTC

Manual confirms RTC and NTP sync fields in status/config pages.

Adapted implementation:

```text
TimeService
NTP server config
time offset config
last_ntp_sync
formatted rtc string
fallback uptime mode
```

Current implemented baseline:

```text
Timezone: WIB / UTC+7
NTP: pool.ntp.org, time.google.com
RTC format: DD/MM/YYYY HH:MM:SS
Source: ESP32 software clock after NTP sync
Exposed in serial heartbeat, Web UI, and /api/status
```

## ✅ Implementation Priority From Manual

Recommended order:

1. Config mode AP. Done.
2. Minimal Web UI and WiFi scan. Done.
3. WiFi setup, NVS persistence, reboot flow, STA Web UI, and WiFi recovery logic. Done.
4. Login shell and admin/user protection. Next security hardening item.
5. Status page with richer device info.
6. Configurable Modbus engine.
7. Meter data page.
8. MQTT config and publish.
9. Relay/protection.
10. Energy history.
11. LCD pages.
12. Admin user management.
13. OTA update.

## ⚠️ Important Differences From Original PM1611

| Original PM1611 | ESP32 RS485 Gateway |
| --- | --- |
| Internal measurement hardware | External Modbus meter |
| Fixed internal register source | User-configurable register map |
| Internal current protection source | Depends on valid Modbus current field |
| Built-in relay path | External relay output must be confirmed |
| Fixed LCD hardware | ST7567 LCD abstraction |

The firmware should clearly show when Modbus data is missing, stale, or invalid.
