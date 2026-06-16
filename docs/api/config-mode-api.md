# 📨 Config Mode API Draft

This document defines the HTTP API used by the current Config Mode Web UI and the longer-term target API shape.

The firmware currently uses a small Arduino `WebServer` implementation with lightweight endpoints. A larger structured API can replace or extend these routes later.

## 🎯 API Goals

The Config Mode API should allow the Web UI to:

- Show setup dashboard information.
- Scan WiFi networks.
- Save WiFi configuration.
- Save device metadata.
- Save MQTT configuration.
- Save NTP/time configuration.
- Save Modbus register map.
- Validate configuration.
- Trigger reboot.
- Report current mode and setup progress.

## 🔐 Authentication

Initial development:

```text
Basic form login
Session cookie
Admin-only config changes
```

Future production:

```text
salted password hashes
session timeout
CSRF token
first boot password change
```

## 📍 Endpoint Overview

Current implemented endpoints:

| Method | Path                   | Purpose                               | Auth     |
| ------ | ---------------------- | ------------------------------------- | -------- |
| `GET`  | `/`                    | Home/status page                      | none yet |
| `GET`  | `/network`             | Network scan and WiFi credential page | none yet |
| `GET`  | `/device`              | Device config page                    | none yet |
| `GET`  | `/mqtt`                | MQTT config page                      | none yet |
| `GET`  | `/modbus`              | Modbus config page                    | none yet |
| `GET`  | `/protection`          | Protection config page                | none yet |
| `GET`  | `/display`             | Display config page                   | none yet |
| `GET`  | `/history`             | History config page                   | none yet |
| `GET`  | `/system`              | System config page                    | none yet |
| `GET`  | `/api/status`          | Firmware, mode, AP, STA, heap status  | none yet |
| `GET`  | `/api/wifi/scan`       | Scan nearby WiFi networks             | none yet |
| `POST` | `/api/wifi/save`       | Save `ssid` and `password` to NVS     | none yet |
| `POST` | `/api/device/save`     | Save device metadata                  | none yet |
| `POST` | `/api/mqtt/save`       | Save MQTT config                      | none yet |
| `POST` | `/api/modbus/save`     | Save Modbus config                    | none yet |
| `POST` | `/api/protection/save` | Save protection config                | none yet |
| `POST` | `/api/display/save`    | Save display config                   | none yet |
| `POST` | `/api/history/save`    | Save history config                   | none yet |
| `POST` | `/api/system/save`     | Save system/NTP config                | none yet |
| `POST` | `/api/reboot`          | Reboot after config save              | none yet |

Current status:

- Routes are available in Config Mode through `http://192.168.4.1/`.
- The same routes are available in Normal Mode through `http://<STA_IP>/` after WiFi connects.
- If STA connection fails or drops later, firmware can recover by retrying STA and/or starting a fallback AP.
- The firmware now exposes full config pages and save APIs for Device, MQTT, Modbus, Protection, Display, History, and System settings.
- Authentication is not implemented yet.
- Static IP configuration is not implemented yet.

Future target endpoint overview:

| Method | Path                       | Purpose                   | Role  |
| ------ | -------------------------- | ------------------------- | ----- |
| `GET`  | `/api/setup/status`        | Read setup/system status  | user  |
| `GET`  | `/api/setup/scan-wifi`     | Scan WiFi networks        | admin |
| `GET`  | `/api/setup/config`        | Read sanitized config     | admin |
| `PUT`  | `/api/setup/config`        | Replace full setup config | admin |
| `PUT`  | `/api/setup/wifi`          | Save WiFi config          | admin |
| `PUT`  | `/api/setup/device`        | Save device info          | admin |
| `PUT`  | `/api/setup/mqtt`          | Save MQTT config          | admin |
| `PUT`  | `/api/setup/time`          | Save NTP/time config      | admin |
| `PUT`  | `/api/setup/protection`    | Save protection config    | admin |
| `POST` | `/api/setup/validate`      | Validate pending config   | admin |
| `POST` | `/api/setup/reboot`        | Save/flush and reboot     | admin |
| `POST` | `/api/setup/factory-reset` | Factory reset, future     | admin |

Modbus-specific endpoints are defined in:

```text
docs/api/modbus-config-api.md
```

## 📊 Setup Status Response

Current implemented `GET /api/status` response:

```json
{
  "firmware": "0.1.0-dev",
  "mode": "NORMAL_MODE",
  "ap_ssid": "SEMS-SETUP-83D1B4",
  "ap_ip": "192.168.4.1",
  "saved_wifi_ssid": "OfficeWiFi",
  "sta_status": "connected",
  "sta_ip": "192.168.43.120",
  "rtc": "30/05/2026 14:10:00",
  "time_synced": true,
  "last_ntp_sync": "30/05/2026 14:09:55",
  "mac_suffix": "83D1B4",
  "free_heap": 247336,
  "heap_total": 307000,
  "heap_used": 59664,
  "heap_used_percent": 19,
  "sketch_size": 821061,
  "sketch_capacity": 1310720,
  "sketch_used_percent": 62,
  "wifi_rssi": -45,
  "wifi_quality_percent": 100,
  "uptime_ms": 16934,
  "uptime_text": "00:00:16"
}
```

Future expanded response:

```json
{
  "mode": "config",
  "uid": "ESP32-RS485-83D1B4",
  "mac": "38:18:2B:83:D1:B4",
  "firmware": "0.1.0-dev",
  "chip": "ESP32-D0WD-V3",
  "flash_mb": 4,
  "free_heap": 350968,
  "uptime_ms": 12000,
  "ap": {
    "enabled": true,
    "ssid": "SEMS-SETUP-83D1B4",
    "ip": "192.168.4.1",
    "clients": 1
  },
  "wifi": {
    "configured": false,
    "ssid": "",
    "connected": false,
    "rssi": null,
    "ip": ""
  },
  "modbus": {
    "configured": false,
    "online": false,
    "last_error": ""
  },
  "mqtt": {
    "configured": false,
    "enabled": false,
    "connected": false
  },
  "config": {
    "dirty": false,
    "valid": true,
    "reboot_required": false
  }
}
```

## 📡 WiFi Scan Response

Current implemented `GET /api/wifi/scan` response:

```json
{
  "count": 2,
  "networks": [
    {
      "ssid": "OfficeWiFi",
      "rssi": -54,
      "channel": 6,
      "encryption": "secured"
    },
    {
      "ssid": "Guest",
      "rssi": -72,
      "channel": 11,
      "encryption": "open"
    }
  ]
}
```

Future expanded response:

```json
{
  "networks": [
    {
      "ssid": "OfficeWiFi",
      "rssi": -54,
      "secure": true,
      "channel": 6
    },
    {
      "ssid": "LabRouter",
      "rssi": -70,
      "secure": true,
      "channel": 11
    }
  ]
}
```

Do not run scans too frequently. WiFi scan can interrupt AP responsiveness.

Recommended:

```text
scan cooldown: 10 seconds
```

## 🛜 Save WiFi Config Request

Current implemented `POST /api/wifi/save` request:

```text
Content-Type: application/x-www-form-urlencoded

ssid=OfficeWiFi&password=secret-password
```

Current implemented response:

```json
{
  "ok": true,
  "ssid": "OfficeWiFi",
  "reboot_required": true
}
```

Storage:

```text
NVS namespace: network
wifi_ssid: saved SSID
wifi_pass: saved password
```

Future JSON request:

```json
{
  "ssid": "OfficeWiFi",
  "password": "secret-password",
  "mode": "dhcp",
  "static": {
    "ip": "",
    "gateway": "",
    "subnet": "",
    "dns1": "",
    "dns2": ""
  },
  "fallback_ap_enabled": true
}
```

Static IP example:

```json
{
  "ssid": "OfficeWiFi",
  "password": "secret-password",
  "mode": "static",
  "static": {
    "ip": "192.168.1.50",
    "gateway": "192.168.1.1",
    "subnet": "255.255.255.0",
    "dns1": "8.8.8.8",
    "dns2": "1.1.1.1"
  },
  "fallback_ap_enabled": true
}
```

Response:

```json
{
  "ok": true,
  "reboot_required": true,
  "message": "WiFi config saved. Reboot required."
}
```

## 🧾 Sanitized Config Response

Passwords must not be returned.

```json
{
  "device": {
    "name": "PM1611 RS485 Reader",
    "location": "Lab",
    "longitude": "",
    "latitude": ""
  },
  "wifi": {
    "ssid": "OfficeWiFi",
    "password_set": true,
    "mode": "dhcp",
    "fallback_ap_enabled": true
  },
  "mqtt": {
    "enabled": false,
    "host": "",
    "port": 1883,
    "username": "",
    "password_set": false,
    "publish_topic": "",
    "subscribe_topic": "",
    "publish_interval_sec": 10,
    "secure": false
  }
}
```

## 🧪 Validation Response

```json
{
  "ok": false,
  "errors": [
    {
      "path": "wifi.ssid",
      "message": "SSID is required"
    },
    {
      "path": "wifi.static.ip",
      "message": "Invalid static IP address"
    }
  ],
  "warnings": [
    {
      "path": "mqtt.secure",
      "message": "TLS certificates are not configured"
    }
  ]
}
```

## 🔄 Reboot Request

Current implemented `POST /api/reboot` response:

```json
{
  "ok": true,
  "rebooting": true
}
```

Current firmware behavior:

```text
send response
delay about 800 ms
ESP.restart()
```

Future request:

```json
{
  "reason": "config_saved"
}
```

Response:

```json
{
  "ok": true,
  "message": "Device will reboot in 1 second"
}
```

Firmware behavior:

```text
send response
flush config
delay 1000 ms
ESP.restart()
```

## 🧯 Error Format

All setup APIs should use a consistent error format:

```json
{
  "ok": false,
  "error": {
    "code": "validation_failed",
    "message": "Config validation failed"
  }
}
```

Recommended error codes:

```text
unauthorized
forbidden
validation_failed
storage_error
wifi_scan_busy
invalid_mode
reboot_pending
internal_error
```

## 🧠 MVP API Order

Implemented MVP order:

1. `GET /`
2. `GET /api/status`
3. `GET /api/wifi/scan`
4. `POST /api/wifi/save`
5. `POST /api/reboot`

Future implementation order:

1. `GET /api/setup/status`
2. `GET /api/setup/scan-wifi`
3. `PUT /api/setup/wifi`
4. `POST /api/setup/reboot`
5. `GET /api/setup/config`
6. `PUT /api/setup/config`

Do not implement every endpoint at once.

## ✅ First Implementation Target

Completed first implementation target:

```text
CONFIG_BUTTON_PIN GPIO32
Runtime 5 second hold -> CONFIG_MODE
Config AP starts
Web UI starts on AP IP
WiFi credentials save to NVS
STA connects on reboot
Web UI starts on STA IP
```

Next API targets:

```text
WiFi test/connect endpoint
clear WiFi credentials endpoint
auth/session protection
static IP settings
```
