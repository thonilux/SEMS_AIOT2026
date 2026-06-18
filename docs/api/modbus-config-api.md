# 📨 Modbus Configuration API Draft

This document defines the future Web API and JSON config model for user-configurable Modbus register maps.

The initial firmware may use a static config first. The Web API should follow this structure later.

## 🎯 API Goals

The Web UI should allow users to configure any compatible Modbus RTU energy meter without recompiling firmware.

The API should support:

- Read current Modbus config.
- Update port settings.
- Add/edit/delete register packets.
- Add/edit/delete register values.
- Test read one packet.
- Test decode one value.
- Import/export full register map.
- Validate config before saving.

## 🔌 Modbus Config Object

Top-level structure:

```json
{
  "version": 1,
  "port": {},
  "packets": [],
  "values": []
}
```

## ⚙️ Port Config

```json
{
  "rx_pin": 16,
  "tx_pin": 17,
  "direction_mode": "auto",
  "tx_enable_pin": null,
  "tx_enable_active_high": true,
  "baudrate": 19200,
  "parity": "none",
  "stop_bits": 1,
  "timeout_ms": 300,
  "retry_count": 2,
  "poll_interval_ms": 1000
}
```

## 📦 Packet Config

```json
{
  "id": "meter_realtime_1",
  "enabled": true,
  "slave_id": 1,
  "function_code": 3,
  "start_address": 0,
  "quantity": 20,
  "poll_every_ms": 1000
}
```

## 📊 Value Config

```json
{
  "id": "voltage",
  "enabled": true,
  "packet_id": "meter_realtime_1",
  "address": 0,
  "name": "Voltage",
  "field": "voltage",
  "unit": "V",
  "data_type": "float32",
  "word_order": "abcd",
  "byte_order": "big",
  "scale": 1.0,
  "offset": 0.0,
  "decimals": 1,
  "stale_after_ms": 5000
}
```

## 🌐 Future HTTP Endpoints

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/modbus/config` | Read full config |
| `PUT` | `/api/modbus/config` | Replace full config |
| `POST` | `/api/modbus/validate` | Validate config without saving |
| `POST` | `/api/modbus/test-packet` | Read one packet now |
| `POST` | `/api/modbus/test-value` | Decode one configured value |
| `POST` | `/api/modbus/import` | Import register map |
| `GET` | `/api/modbus/export` | Export register map |
| `GET` | `/api/modbus/status` | Runtime packet/value status |

## 🧪 Test Packet Request

```json
{
  "slave_id": 1,
  "function_code": 3,
  "start_address": 0,
  "quantity": 2
}
```

Example response:

```json
{
  "ok": true,
  "response_time_ms": 42,
  "raw_hex": "01030443B68000B1C2",
  "registers": [
    "0x43B6",
    "0x8000"
  ]
}
```

## 🧮 Test Value Request

```json
{
  "packet": {
    "slave_id": 1,
    "function_code": 3,
    "start_address": 0,
    "quantity": 2
  },
  "value": {
    "address": 0,
    "data_type": "float32",
    "word_order": "abcd",
    "byte_order": "big",
    "scale": 1.0,
    "offset": 0.0
  }
}
```

Example response:

```json
{
  "ok": true,
  "raw_hex": "43B68000",
  "raw_value": 365.0,
  "decoded_value": 365.0,
  "formatted_value": "365.0",
  "unit": "V"
}
```

## 🚦 Runtime Status Response

```json
{
  "online": true,
  "last_success_ms": 123456,
  "packets": [
    {
      "id": "meter_realtime_1",
      "online": true,
      "last_success_ms": 123456,
      "response_time_ms": 42,
      "error_count": 0,
      "last_error": ""
    }
  ],
  "values": [
    {
      "id": "voltage",
      "valid": true,
      "stale": false,
      "value": 230.5,
      "formatted": "230.5",
      "unit": "V",
      "last_update_ms": 123456
    }
  ]
}
```

## 🧯 Validation Errors

Example:

```json
{
  "ok": false,
  "errors": [
    {
      "path": "values[0].address",
      "message": "Value address is outside the selected packet range"
    }
  ]
}
```

## 🔐 Security

Modbus config endpoints should require admin login.

Read-only status endpoints may be visible to normal users.

Recommended access:

| Endpoint | Role |
| --- | --- |
| `GET /api/modbus/status` | user |
| `GET /api/modbus/config` | admin |
| `PUT /api/modbus/config` | admin |
| Test/import/export endpoints | admin |

## 💾 Storage

Recommended storage:

```text
LittleFS: full Modbus JSON config
NVS: active config version/hash and small flags
```

Reason:

- Register maps are structured and may grow.
- JSON is easy to export/import.
- Avoid stuffing large arrays into NVS.

