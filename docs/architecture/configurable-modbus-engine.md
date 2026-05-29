# 🧩 Configurable Modbus Engine Architecture

The firmware must not be locked to one specific energy meter register map.

Instead, Modbus polling should be driven by user configuration from the Web UI. The device should support common Modbus RTU meters by letting the user define register packets, register addresses, data types, scaling, units, and output fields.

## 🎯 Goal

Build a universal RS485 Modbus data acquisition engine.

The firmware should allow the user to configure:

- Slave ID
- Baudrate
- Parity
- Stop bits
- Poll interval
- Timeout
- Retry count
- Register groups
- Register address
- Function code
- Data type
- Byte/word order
- Scale and offset
- Unit
- Target normalized field
- Display/MQTT formatting

No meter-specific register addresses should be hardcoded into the core firmware.

## 🧠 Core Idea

The firmware has two layers:

```text
Modbus Transport
  -> Reads raw register bytes

Configurable Register Map
  -> Decodes raw bytes into normalized meter fields
```

The user config defines how raw register data becomes application data.

```text
User Web UI config
  -> RegisterMapConfig
  -> ModbusPollPlan
  -> RawModbusResponse
  -> DecodedValue
  -> MeterData
  -> MQTT / Web / LCD / Protection
```

## 🏗️ Proposed Modules

```text
ModbusService
- Owns UART/RS485 polling lifecycle
- Executes poll plan
- Handles timeout and retry

Rs485Port
- Owns UART2
- Handles optional TX enable
- Supports auto-direction mode

RegisterMapManager
- Owns user-defined register map
- Validates map
- Builds efficient polling groups

RegisterDecoder
- Converts raw register bytes into typed values
- Applies endian, scale, and offset

MeterDataMapper
- Maps decoded values to normalized fields

MeterService
- Owns latest normalized data
- Marks values stale
- Exposes snapshots
```

## 🔌 RS485 Direction Modes

The previous prototype suggests hardware-derived `RE/DE` using 74HC04D logic.

Firmware should support:

| Mode | Description |
| --- | --- |
| `auto` | Hardware controls RS485 direction |
| `gpio_tx_enable` | Firmware controls transceiver DE/RE pin |

Config:

```json
{
  "direction_mode": "auto",
  "tx_enable_pin": null,
  "tx_enable_active_high": true
}
```

If `direction_mode = auto`, firmware must not toggle a direction GPIO.

## ⚙️ Modbus Port Config

Example:

```json
{
  "port": {
    "rx_pin": 16,
    "tx_pin": 17,
    "direction_mode": "auto",
    "tx_enable_pin": null,
    "baudrate": 19200,
    "parity": "none",
    "stop_bits": 1,
    "timeout_ms": 300,
    "retry_count": 2,
    "poll_interval_ms": 1000
  }
}
```

Supported parity:

```text
none
even
odd
```

Supported stop bits:

```text
1
2
```

## 📦 Register Packet Model

Registers should be grouped into packets to reduce bus overhead.

Example:

```json
{
  "packets": [
    {
      "id": "meter_realtime_1",
      "enabled": true,
      "slave_id": 1,
      "function_code": 3,
      "start_address": 0,
      "quantity": 20,
      "poll_every_ms": 1000
    }
  ]
}
```

Supported function codes:

| Function | Meaning |
| --- | --- |
| `3` | Read Holding Registers |
| `4` | Read Input Registers |

Future optional:

| Function | Meaning |
| --- | --- |
| `1` | Read Coils |
| `2` | Read Discrete Inputs |

For energy meters, function `3` and `4` are the priority.

## 🧮 Register Value Model

Each value references data inside a packet.

Example:

```json
{
  "values": [
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
  ]
}
```

The `address` is the Modbus register address of the value. It must be inside the packet range.

## 📊 Supported Data Types

Initial support:

| Type | Register Count | Description |
| --- | --- | --- |
| `uint16` | 1 | Unsigned 16-bit integer |
| `int16` | 1 | Signed 16-bit integer |
| `uint32` | 2 | Unsigned 32-bit integer |
| `int32` | 2 | Signed 32-bit integer |
| `float32` | 2 | IEEE-754 float |

Future support:

| Type | Register Count | Description |
| --- | --- | --- |
| `uint64` | 4 | Unsigned 64-bit integer |
| `int64` | 4 | Signed 64-bit integer |
| `float64` | 4 | Double precision float |
| `bitfield16` | 1 | Bit flags |

## 🔁 Endianness And Word Order

Many energy meters differ in register order.

For 32-bit values, support:

| Word Order | Meaning |
| --- | --- |
| `abcd` | Register high word first, normal byte order |
| `badc` | Byte-swapped inside each word |
| `cdab` | Word-swapped |
| `dcba` | Word and byte swapped |

For 16-bit values, support byte order:

```text
big
little
```

Most Modbus registers are big-endian per 16-bit register, but 32-bit word ordering varies by meter.

## 🧭 Normalized Meter Fields

Decoded values can map to known fields:

```text
voltage
current
power
frequency
power_factor
energy
co2
phase_a_voltage
phase_b_voltage
phase_c_voltage
phase_a_current
phase_b_current
phase_c_current
phase_a_power
phase_b_power
phase_c_power
custom_1
custom_2
custom_3
custom_4
```

The PM1611-compatible MQTT payload should use:

```text
voltage
current
power
frequency
power_factor
energy
co2
```

If a field is not configured, publish `"N/A"` or omit it based on MQTT compatibility mode.

## 🧪 Decode Formula

For every configured value:

```text
decoded_value = raw_value * scale + offset
```

Then:

```text
formatted_value = round(decoded_value, decimals)
```

Example:

```json
{
  "raw_value": 2305,
  "scale": 0.1,
  "offset": 0,
  "decoded_value": 230.5
}
```

## 🧯 Validation Rules

Config validation must reject:

- Duplicate packet IDs.
- Duplicate value IDs.
- Unsupported function codes.
- Register value outside packet range.
- Invalid slave ID.
- Invalid baudrate.
- Invalid data type.
- Invalid endian/word order.
- Negative poll interval.
- Missing required normalized field for MQTT compatibility mode.
- Too many packets.
- Too many values.

Recommended limits for ESP32:

```text
max_packets = 16
max_values = 64
max_registers_per_packet = 64
max_packet_response_bytes = 128
```

These limits can be increased later if memory remains healthy.

## 🚦 Runtime Status

Each packet should track:

```text
last_poll_ms
last_success_ms
last_error
error_count
success_count
online
response_time_ms
```

Each value should track:

```text
valid
stale
last_update_ms
last_value
last_raw
decode_error
```

## 🧠 Poll Plan Builder

The Web UI may let users create values one by one. Internally, the firmware should group adjacent values into packets where possible.

Two modes:

### Manual Packet Mode

Advanced users define packets and values.

Pros:

- Predictable
- Easy to debug
- Best for advanced Modbus users

### Auto Packet Mode

User defines values, firmware groups them into packets.

Pros:

- Easier UX
- Less manual work

Initial firmware should implement manual packet mode first.

Auto grouping can be added later.

## 🌐 Web UI Flow

Recommended Web UI pages:

```text
Modbus Port
Register Packets
Register Values
Live Test
Import/Export
```

Minimum viable Web UI workflow:

1. Configure port settings.
2. Add packet.
3. Add value inside packet.
4. Click test read.
5. See raw response and decoded value.
6. Save config.
7. Firmware starts polling automatically.

## 🧾 Import / Export

The register map should be exportable as JSON.

Use cases:

- Backup configuration.
- Share meter profile.
- Clone devices.
- Debug support.

Future:

```text
docs/modbus-profiles/
```

can store known-good profiles for common meters.

## 🧱 Firmware Implementation Order

Do not build the Web UI first.

Recommended implementation sequence:

1. Define config structs.
2. Define static example config in firmware.
3. Implement packet polling.
4. Implement register decoder.
5. Print decoded values to serial.
6. Persist config to JSON/NVS/LittleFS.
7. Add Web API.
8. Add Web UI.
9. Add MQTT payload mapping.

This keeps the core engine testable without browser complexity.

## ✅ First Firmware Milestone

The first configurable Modbus milestone should support:

```text
UART2 RX GPIO16
UART2 TX GPIO17
Direction mode auto
Baudrate 19200
Function code 3 or 4
One packet
One float32 or uint16 value
Serial log output
```

No relay, no MQTT, no Web UI yet.

Once this works, expand to multiple values and Web UI configuration.

