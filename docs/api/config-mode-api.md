# 📨 Config Mode API Draft

This document defines the future HTTP API used by the Config Mode Web UI.

Initial firmware can start with static HTML and simple endpoints. This API is the target shape for a maintainable implementation.

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

| Method | Path | Purpose | Role |
| --- | --- | --- | --- |
| `GET` | `/api/setup/status` | Read setup/system status | user |
| `GET` | `/api/setup/scan-wifi` | Scan WiFi networks | admin |
| `GET` | `/api/setup/config` | Read sanitized config | admin |
| `PUT` | `/api/setup/config` | Replace full setup config | admin |
| `PUT` | `/api/setup/wifi` | Save WiFi config | admin |
| `PUT` | `/api/setup/device` | Save device info | admin |
| `PUT` | `/api/setup/mqtt` | Save MQTT config | admin |
| `PUT` | `/api/setup/time` | Save NTP/time config | admin |
| `PUT` | `/api/setup/protection` | Save protection config | admin |
| `POST` | `/api/setup/validate` | Validate pending config | admin |
| `POST` | `/api/setup/reboot` | Save/flush and reboot | admin |
| `POST` | `/api/setup/factory-reset` | Factory reset, future | admin |

Modbus-specific endpoints are defined in:

```text
docs/api/modbus-config-api.md
```

## 📊 Setup Status Response

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
    "ssid": "PM1611-SETUP-83D1B4",
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

Implement in this order:

1. `GET /api/setup/status`
2. `GET /api/setup/scan-wifi`
3. `PUT /api/setup/wifi`
4. `POST /api/setup/reboot`
5. `GET /api/setup/config`
6. `PUT /api/setup/config`

Do not implement every endpoint at once.

## ✅ First Implementation Target

Before HTTP API, implement serial-only mode decision:

```text
CONFIG_BUTTON_PIN GPIO32
Hold at boot -> CONFIG_MODE
No hold -> NORMAL_MODE
Serial print selected mode
```

Then add AP and `GET /api/setup/status`.

