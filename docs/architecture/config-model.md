# Configuration Model

**Author**: Claude (AI Assistant) @ 2026-05-30  
**Phase**: 10 (Web Dashboard and Config API)  
**Status**: Complete

---

## Overview

The PM1611 firmware stores all configuration in ESP32 NVS (Non-Volatile Storage) using isolated namespaces. Each configuration category has a dedicated NVS namespace and corresponding web UI page for user-friendly configuration.

Configuration is loaded on boot with sensible defaults. Changes made through the web UI are persisted to NVS immediately and survive device reboot.

---

## Architecture

### ConfigManager Class

**Location**: `firmware/include/ConfigManager.h` and `firmware/src/ConfigManager.cpp`

The `ConfigManager` class provides:

- 7 config categories with load/save methods
- Struct-based configuration models
- Automatic default values on first boot
- Unified NVS access patterns
- Type-safe getters and setters

**Usage**:

```cpp
#include "ConfigManager.h"

// Load device config (with defaults if empty)
DeviceConfig cfg = ConfigManager::loadDeviceConfig();

// Modify
strcpy(cfg.device_name, "My Device");

// Save back to NVS
bool ok = ConfigManager::saveDeviceConfig(cfg);
```

---

## Configuration Categories

### 1. Device Configuration

**NVS Namespace**: `device`  
**Web Page**: `/device`  
**API Endpoint**: `/api/device/save`

Configuration for device identity and timezone:

| Key           | Type    | Size     | Default         | Purpose                                                               |
| ------------- | ------- | -------- | --------------- | --------------------------------------------------------------------- |
| `device_name` | string  | 64 bytes | "PM1611-Device" | User-friendly device name (displayed on dashboard)                    |
| `hostname`    | string  | 64 bytes | "pm1611"        | mDNS hostname for local network discovery (`hostname.local`)          |
| `timezone`    | string  | 32 bytes | "WIB-7"         | POSIX timezone string for NTP time conversion (e.g., "UTC0", "WIB-7") |
| `co2_factor`  | uint8_t | 1 byte   | 0               | CO2 emission factor in kg per kWh (for energy reporting)              |

**Example payload**:

```json
{
  "device_name": "Living Room Meter",
  "hostname": "pm1611-living-room",
  "timezone": "UTC+7",
  "co2_factor_kg_per_kwh": 0
}
```

---

### 2. MQTT Configuration

**NVS Namespace**: `mqtt`  
**Web Page**: `/mqtt`  
**API Endpoint**: `/api/mqtt/save`

Configuration for MQTT broker connectivity:

| Key                    | Type     | Size     | Default     | Purpose                                                        |
| ---------------------- | -------- | -------- | ----------- | -------------------------------------------------------------- |
| `host`                 | string   | 64 bytes | "localhost" | MQTT broker hostname or IP address                             |
| `port`                 | uint16_t | 2 bytes  | 1883        | MQTT broker port (1883 for plaintext, 8883 for TLS)            |
| `username`             | string   | 64 bytes | ""          | MQTT broker username                                           |
| `password`             | string   | 64 bytes | ""          | MQTT broker password                                           |
| `client_id`            | string   | 64 bytes | "pm1611"    | MQTT client ID (must be unique on broker)                      |
| `base_topic`           | string   | 64 bytes | "pm1611"    | Topic prefix for publish/subscribe (e.g., `pm1611/{uid}/data`) |
| `publish_interval_sec` | uint16_t | 2 bytes  | 5           | Interval between telemetry publishes (seconds)                 |
| `enabled`              | bool     | 1 byte   | false       | Enable MQTT connectivity                                       |

**Example payload**:

```json
{
  "mqtt_enabled": true,
  "mqtt_host": "mosquitto.local",
  "mqtt_port": 1883,
  "mqtt_username": "enerma",
  "mqtt_password": "secret",
  "mqtt_client_id": "pm1611-001",
  "mqtt_base_topic": "pm1611",
  "mqtt_publish_interval_sec": 5
}
```

---

### 3. Modbus Configuration

**NVS Namespace**: `modbus`  
**Web Page**: `/modbus`  
**API Endpoint**: `/api/modbus/save`

Configuration for RS485/Modbus communication with external meter:

| Key                | Type     | Size    | Default | Purpose                                             |
| ------------------ | -------- | ------- | ------- | --------------------------------------------------- |
| `baudrate`         | uint32_t | 4 bytes | 19200   | RS485 baud rate (9600, 19200, or 38400)             |
| `slave_id`         | uint8_t  | 1 byte  | 1       | Modbus slave device address (1-247)                 |
| `parity`           | uint8_t  | 1 byte  | 0       | Parity: 0=EVEN, 1=ODD, 2=NONE                       |
| `stop_bits`        | uint8_t  | 1 byte  | 1       | Stop bits: 1 or 2                                   |
| `poll_interval_ms` | uint16_t | 2 bytes | 1000    | How often to poll meter registers (milliseconds)    |
| `timeout_ms`       | uint16_t | 2 bytes | 1000    | Modbus response timeout before retry (milliseconds) |
| `retry_count`      | uint8_t  | 1 byte  | 3       | Failed poll attempts before marking meter offline   |
| `meter_profile`    | uint8_t  | 1 byte  | 0       | Register map profile: 0=Schneider EM6400 / PM2xxx, 1=Generic float32 |

**Example payload**:

```json
{
  "modbus_baudrate": 19200,
  "modbus_slave_id": 1,
  "modbus_parity": 0,
  "modbus_stop_bits": 1,
  "modbus_poll_interval_ms": 1000,
  "modbus_timeout_ms": 1000,
  "modbus_retry_count": 3,
  "modbus_profile": 0
}
```

**Hardware Notes**:

- RS485 RX: GPIO16
- RS485 TX: GPIO17
- RS485 TX_ENABLE: GPIO0 (development only; use different GPIO in production)

---

### 4. Protection Configuration

**NVS Namespace**: `protection`  
**Web Page**: `/protection`  
**API Endpoint**: `/api/protection/save`

Configuration for relay overcurrent protection and safety:

| Key                    | Type     | Size    | Default | Purpose                                                            |
| ---------------------- | -------- | ------- | ------- | ------------------------------------------------------------------ |
| `relay_enabled`        | bool     | 1 byte  | false   | Enable relay output control                                        |
| `current_limit_a`      | uint8_t  | 1 byte  | 16      | Overcurrent threshold in Amps (1-63A)                              |
| `trip_delay_ms`        | uint32_t | 4 bytes | 1000    | Delay before tripping relay (milliseconds)                         |
| `reset_mode`           | uint8_t  | 1 byte  | 0       | Reset policy: 0=MANUAL (user must reset), 1=AUTO (automatic retry) |
| `auto_retry_enabled`   | bool     | 1 byte  | false   | Enable automatic retry after trip                                  |
| `auto_retry_delay_sec` | uint16_t | 2 bytes | 300     | Delay before automatic retry (seconds)                             |
| `trip_on_meter_stale`  | bool     | 1 byte  | true    | Trip relay if meter goes offline                                   |

**Example payload**:

```json
{
  "relay_enabled": true,
  "current_limit": 16,
  "trip_delay": 1000,
  "reset_mode": 0,
  "auto_retry_enabled": false,
  "auto_retry_delay": 300,
  "trip_on_stale": true
}
```

**Safety Rules**:

- Relay trips if current > `current_limit_a` for > `trip_delay_ms`
- Relay trips if meter offline and `trip_on_meter_stale` is true
- Relay cannot turn ON while in LOCKOUT state
- Manual reset required if `reset_mode` is MANUAL
- Auto-retry attempts activation after `auto_retry_delay_sec` if `auto_retry_enabled` is true

---

### 5. Display Configuration

**NVS Namespace**: `display`  
**Web Page**: `/display`  
**API Endpoint**: `/api/display/save`

Configuration for LCD display (if attached):

| Key                     | Type    | Size   | Default | Purpose                                      |
| ----------------------- | ------- | ------ | ------- | -------------------------------------------- |
| `enabled`               | bool    | 1 byte | false   | Enable LCD display output                    |
| `type`                  | uint8_t | 1 byte | 0       | Display controller type: 0=ST7567, 1=SSD1306 |
| `i2c_address`           | uint8_t | 1 byte | 0x3C    | I2C address (e.g., 0x3C, 0x3F)               |
| `rotation_interval_sec` | uint8_t | 1 byte | 5       | Interval to rotate display pages (seconds)   |
| `brightness`            | uint8_t | 1 byte | 200     | Display brightness (0-255)                   |

**Example payload**:

```json
{
  "display_enabled": true,
  "display_type": 0,
  "i2c_address": "0x3C",
  "rotation_interval": 5,
  "brightness": 200
}
```

---

### 6. History Configuration

**NVS Namespace**: `history`  
**Web Page**: `/history`  
**API Endpoint**: `/api/history/save`

Configuration for energy history tracking:

| Key                  | Type     | Size    | Default | Purpose                                      |
| -------------------- | -------- | ------- | ------- | -------------------------------------------- |
| `enabled`            | bool     | 1 byte  | true    | Enable daily energy history tracking         |
| `days_retained`      | uint8_t  | 1 byte  | 7       | Number of days to retain (1-31)              |
| `flush_interval_sec` | uint16_t | 2 bytes | 3600    | Interval to flush history to flash (seconds) |

**Example payload**:

```json
{
  "history_enabled": true,
  "days_retained": 7,
  "flush_interval": 3600
}
```

**Storage**: History is buffered in RAM and periodically saved to LittleFS to reduce flash wear.

---

### 7. System Configuration

**NVS Namespace**: `system`  
**Web Page**: `/system`  
**API Endpoint**: `/api/system/save`

System-level settings:

| Key             | Type   | Size     | Default           | Purpose                         |
| --------------- | ------ | -------- | ----------------- | ------------------------------- |
| `ntp_server1`   | string | 64 bytes | "pool.ntp.org"    | Primary NTP server              |
| `ntp_server2`   | string | 64 bytes | "time.google.com" | Secondary NTP server (fallback) |
| `debug_enabled` | bool   | 1 byte   | false             | Enable debug logging to serial  |

**Example payload**:

```json
{
  "ntp_server1": "pool.ntp.org",
  "ntp_server2": "time.google.com",
  "debug_enabled": false
}
```

---

## Load Behavior

When the firmware boots:

1. **Boot Sequence**:
   - ConfigManager loads each config category from NVS
   - If a namespace doesn't exist or is empty, defaults are applied
   - Defaults are **NOT** written back to NVS (lazy loading)
   - Device uses loaded values immediately

2. **First Boot**:
   - NVS namespaces are empty
   - All defaults are loaded into RAM
   - User can modify and save via web UI
   - After save, values persist in NVS

3. **Subsequent Boots**:
   - Saved values loaded from NVS
   - Device resumes with previous configuration

---

## Save Behavior

When a user clicks "Save" on a configuration page:

1. **Form Submission**:
   - JavaScript collects form input values
   - Sends POST to `/api/{category}/save`
   - Content-Type: `application/x-www-form-urlencoded`

2. **Server Validation**:
   - Web handler extracts form parameters
   - Applies basic length and type validation
   - Constructs config struct
   - Calls `ConfigManager::save{Category}Config()`

3. **NVS Persistence**:
   - Preferences API writes struct fields to NVS namespace
   - Returns `{"ok": true}` or `{"ok": false, "error": "..."}`

4. **User Feedback**:
   - Page displays save status
   - User optionally reboots device (required for network/MQTT/Modbus changes)

---

## API Reference

### GET /api/device, /api/mqtt, /api/modbus, /api/protection, /api/display, /api/history, /api/system

Returns current configuration as JSON.

**Example**: `curl http://192.168.4.1/api/device`

```json
{
  "device_name": "PM1611-Device",
  "hostname": "pm1611",
  "timezone": "WIB-7",
  "co2_factor_kg_per_kwh": 0
}
```

### POST /api/device/save, /api/mqtt/save, /api/modbus/save, /api/protection/save, /api/display/save, /api/history/save, /api/system/save

Saves configuration to NVS.

**Example**:

```bash
curl -X POST http://192.168.4.1/api/device/save \
  -d "device_name=MyMeter&hostname=my-pm1611&timezone=WIB-7&co2_factor=0"
```

**Response**:

```json
{ "ok": true }
```

---

## Validation Rules

### Device Config

- `device_name`: 1-64 characters
- `hostname`: 1-64 characters, valid mDNS name (alphanumeric + hyphen)
- `timezone`: valid POSIX TZ string
- `co2_factor`: 0-255

### MQTT Config

- `host`: 1-64 characters
- `port`: 1-65535
- `username`: 0-64 characters
- `password`: 0-64 characters
- `publish_interval_sec`: 1-3600

### Modbus Config

- `baudrate`: 9600, 19200, or 38400 only
- `slave_id`: 1-247
- `parity`: 0, 1, or 2 only
- `stop_bits`: 1 or 2 only
- `poll_interval_ms`: 100-10000
- `timeout_ms`: 100-5000
- `retry_count`: 0-10
- `meter_profile`: 0 = Schneider EM6400 / PM2xxx, 1 = Generic float32

### Protection Config

- `current_limit_a`: 1-63
- `trip_delay_ms`: 100-10000
- `reset_mode`: 0 or 1 only
- `auto_retry_delay_sec`: 10-3600

### Display Config

- `type`: 0 or 1 only
- `i2c_address`: valid hex (0x00-0xFF)
- `rotation_interval_sec`: 1-60
- `brightness`: 0-255

### History Config

- `days_retained`: 1-31
- `flush_interval_sec`: 60-86400

---

## Future Enhancements

1. **Config Versioning**
   - Add `uint8_t version` field to each config struct
   - Implement migration logic for schema changes
   - Document breaking changes in release notes

2. **Config Export/Import**
   - Export all config as JSON file
   - Import previously exported config
   - Backup/restore functionality (Phase 11)

3. **Config Validation**
   - Regex validation for email, URLs, hostnames
   - Network connectivity test before save
   - Configuration verification

4. **Role-Based Access**
   - Admin vs User permissions
   - Read-only config viewing
   - Audit log of config changes (Phase 11)

---

## Implementation Notes

### Memory Footprint

- All config structs fit in RAM (< 2KB total)
- NVS operations are non-blocking
- No flash wear concerns with typical usage

### Thread Safety

- Config read operations are lock-free
- Config save operations use Preferences API (internally synchronized)
- Safe to read config from any task

### Backward Compatibility

- WiFi config migrated from old format to ConfigManager
- Existing NVS data is preserved
- New config namespaces are independent

---

## Team Signature

**Implemented by**: Claude (AI Assistant)  
**Date**: 2026-05-30  
**Phase**: 10 (Web Dashboard and Config API)  
**Commit**: "Add Web UI configuration pages for Device, MQTT, Modbus, Protection, Display, History, System"
