# 📡 Config Mode Architecture

Config Mode is the setup and recovery mode for the ESP32 RS485 Modbus energy gateway.

It adapts the PM1611Q-WD manual behavior:

```text
Activate AP mode
Connect to device SSID
Open browser
Login as admin
Configure WiFi/network
Save
Reboot
Device connects as WiFi client
```

## 🎯 Goals

Config Mode must allow a user to commission the device without serial access.

It should support:

- WiFi Access Point broadcast.
- Web UI setup portal.
- Device information display.
- WiFi scan and setup.
- DHCP/static IP setup.
- Configurable Modbus register map setup.
- MQTT setup.
- NTP/time setup.
- Relay/protection setup.
- Save and reboot flow.
- Recovery if WiFi configuration fails.

## 🧠 Operating Modes

The firmware should have these top-level modes:

```text
BOOT
CONFIG_MODE
NORMAL_MODE
SAFE_MODE
OTA_MODE
FACTORY_RESET
```

Initial implementation should focus on:

```text
BOOT
CONFIG_MODE
NORMAL_MODE
```

## 🔘 Config Button Behavior

Recommended button:

```text
CONFIG_BUTTON_PIN = GPIO32
Button logic: active low
Internal pullup: enabled
```

Why GPIO32:

- Available from previous prototype debug header.
- Not a boot strapping pin.
- Supports digital input.
- Safer than GPIO0/GPIO2/GPIO15.

Button actions:

| Action | Duration | Behavior |
| --- | --- | --- |
| Hold during boot | 5 seconds | Force Config Mode |
| Long press during Normal Mode | 3 seconds | Request Config Mode reboot |
| Hold during boot | 10 seconds | Factory reset, future feature |
| Short press | < 1 second | Reserved, no action initially |

Implementation note:

During boot, do not block forever waiting for the button. Sample for a bounded window.

Recommended boot sampling:

```text
sample button for 5 seconds
if held continuously -> force config mode
else continue boot
```

## 🚦 Boot Decision Tree

```text
BOOT
  -> init serial
  -> init pin map
  -> sample config button
  -> load config
  -> validate config
  -> decide mode
```

Mode decision:

```text
if config_button_held:
    CONFIG_MODE
else if force_config_flag:
    CONFIG_MODE
else if wifi_config_missing:
    CONFIG_MODE
else if config_invalid:
    CONFIG_MODE
else:
    NORMAL_MODE
```

Normal mode WiFi failure fallback:

```text
NORMAL_MODE
  -> WiFi connect
  -> retry N times
  -> if failed and fallback_ap_enabled:
       CONFIG_MODE
```

Recommended retries:

```text
wifi_connect_attempts = 5
retry_delay_ms = 3000
total wait = about 15 seconds
```

## 📶 Access Point Behavior

Config Mode starts a WiFi AP.

Recommended AP identity:

```text
SSID: PM1611-SETUP-{last6mac}
IP:   192.168.4.1
Host: pm1611-{last6mac}.local, later if mDNS enabled
```

Example:

```text
PM1611-SETUP-83D1B4
```

Development password:

```text
PM123456
```

Production password options:

```text
Printed setup key
Generated from MAC + secret
User-set local setup password
```

Security note:

The PM1611 manual uses simple defaults. This project can start with development defaults, but production firmware should force credential change.

## 🌐 Config Web UI Pages

Minimum Config Mode pages:

```text
/login
/setup
/setup/wifi
/setup/device
/setup/modbus
/setup/mqtt
/setup/time
/setup/protection
/setup/summary
/setup/reboot
```

Initial MVP pages:

```text
/login
/setup
/setup/wifi
/setup/reboot
```

Then add:

```text
/setup/modbus
/setup/mqtt
/setup/protection
```

## 🧾 Setup Dashboard Information

The Config Mode setup dashboard should display:

```text
UID
MAC address
Firmware version
Chip model
Flash size
Free heap
Boot reason
Current mode
AP SSID
AP IP
Configured WiFi SSID
WiFi connection status
Modbus port config
Modbus runtime status
MQTT config status
Relay state
Protection state
Time sync state
```

Do not display stored WiFi or MQTT passwords.

## 📡 WiFi Configuration Flow

User flow:

```text
1. User holds config button during boot.
2. Device starts AP.
3. User connects to AP.
4. User opens 192.168.4.1.
5. User logs in as admin.
6. User scans WiFi networks.
7. User selects SSID.
8. User enters password.
9. User chooses DHCP or static IP.
10. User clicks Save.
11. Device validates config.
12. Device writes config.
13. UI shows "saved, reboot required".
14. User clicks Reboot.
15. Device reboots.
16. Device disables AP and connects as WiFi STA.
```

Optional later:

```text
Test WiFi before save
Apply without reboot
AP+STA mode during test
```

Initial design should use save + reboot because it is simpler and more reliable.

## 💾 Save And Reboot Contract

When user clicks Save:

```text
validate config
write pending config
mark config dirty
return success
```

When user clicks Reboot:

```text
flush storage
delay 500 ms
restart ESP32
```

On next boot:

```text
load config
if valid WiFi config exists:
    NORMAL_MODE
else:
    CONFIG_MODE
```

## 🧱 Configuration Storage

Use two storage layers:

```text
NVS / Preferences:
- small settings
- boot flags
- WiFi credentials
- admin/user password hashes
- device UID

LittleFS:
- Web UI files
- Modbus register map JSON
- larger structured config
```

Initial MVP can store everything in NVS or a static struct while building the mode logic.

Final design:

```text
ConfigManager
  -> DeviceConfig
  -> NetworkConfig
  -> AuthConfig
  -> MqttConfig
  -> TimeConfig
  -> ProtectionConfig
  -> ModbusConfig
```

## 🔐 Login And Security

Config Mode should still require login.

Development defaults:

```text
Admin username: admin
Admin password: admin1234
User username:  user
User password:  user1234
```

Production rules:

```text
force admin password change
store salted password hash
session timeout
do not expose passwords in API responses
CSRF token for state-changing requests
```

Access model:

| Page/API | Admin | User |
| --- | --- | --- |
| View setup dashboard | yes | yes |
| Change WiFi | yes | no |
| Change Modbus config | yes | no |
| Change MQTT config | yes | no |
| Change relay/protection | yes | no |
| Reboot | yes | no |
| OTA | yes | no |

## 🛜 Captive Portal Behavior

Optional but recommended:

```text
DNS server redirects all domains to 192.168.4.1
HTTP requests redirect to /setup
```

Initial implementation can skip DNS captive portal and rely on:

```text
http://192.168.4.1
```

## 🔌 Config Mode And Services

In Config Mode:

| Service | Behavior |
| --- | --- |
| WiFi AP | ON |
| WiFi STA | OFF initially |
| Web UI | ON |
| DNS captive portal | Optional |
| MQTT | OFF |
| Modbus polling | Optional, read-only |
| Relay remote control | OFF |
| Relay protection | ON if relay hardware active |
| LCD | Show setup info |
| LEDs | Config pattern |

Recommended MVP:

```text
AP ON
Web UI ON
MQTT OFF
Modbus OFF or diagnostic only
Relay unchanged/off
```

## 🧲 Relay Safety In Config Mode

Config Mode should not unexpectedly turn loads ON.

Recommended:

```text
relay stays OFF by default in config mode
MQTT relay control disabled
Web relay control disabled until protection system is implemented
```

Later:

Admin can test relay manually only after acknowledging safety warning.

## 🖥️ LCD Behavior

LCD pages in Config Mode:

```text
Page 1:
PM1611 SETUP
SSID PM1611-SETUP-xxxx
IP 192.168.4.1

Page 2:
MAC/UID
Firmware version
Mode CONFIG

Page 3:
WiFi saved?
Modbus configured?
MQTT configured?
```

## 🚦 LED Behavior

Suggested patterns:

| State | Pattern |
| --- | --- |
| Boot | Fast blink activity LED |
| Config AP active | Slow blink network LED |
| Client connected to AP | Network LED double blink |
| Config saved | Activity LED solid 2 seconds |
| Rebooting | All LEDs blink twice |
| Fault | Alarm LED blink |

## 🧯 Recovery Rules

The device must avoid lockout.

Rules:

```text
If WiFi credentials are missing -> Config Mode
If WiFi fails repeatedly -> Config Mode fallback
If config file is corrupt -> Config Mode with defaults
If admin password is lost -> factory reset path later
If Modbus config is invalid -> allow Web UI correction
```

## 🧪 MVP Firmware Milestones

### Milestone 1: Button Mode Decision

```text
Add PinMap.h
Read GPIO32 button
Hold during boot -> print CONFIG_MODE
No hold -> print NORMAL_MODE
```

No WiFi yet.

### Milestone 2: AP Mode

```text
If CONFIG_MODE:
  start AP PM1611-SETUP-{last6mac}
  print AP IP
```

No Web UI yet.

### Milestone 3: Minimal Web Server

```text
serve setup page
show UID/MAC/FW/version/mode
```

### Milestone 4: WiFi Save Mock

```text
form: ssid/password
save to temporary config
print saved config
```

### Milestone 5: NVS Persistence

```text
save WiFi config to NVS
load on boot
normal/config mode decision based on config
```

### Milestone 6: Save And Reboot

```text
save config
show success
click reboot
ESP.restart()
connect to WiFi STA
```

## ✅ Recommended Constants

```text
CONFIG_BUTTON_PIN = 32
CONFIG_BUTTON_ACTIVE_LOW = true
BOOT_CONFIG_HOLD_MS = 5000
NORMAL_CONFIG_HOLD_MS = 3000
FACTORY_RESET_HOLD_MS = 10000
CONFIG_AP_IP = 192.168.4.1
CONFIG_AP_SSID_PREFIX = PM1611-SETUP
CONFIG_AP_PASSWORD_DEV = PM123456
WIFI_CONNECT_ATTEMPTS = 5
WIFI_CONNECT_RETRY_MS = 3000
```

## 🎯 Next Implementation Step

Implement Milestone 1 only:

```text
PinMap.h
Config button read
Boot mode decision
Serial output
```

This keeps the next firmware change small, testable, and reversible.

