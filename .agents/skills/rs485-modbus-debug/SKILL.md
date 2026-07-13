---
name: rs485-modbus-debug
description: Debug RS485 Modbus RTU communication for PM1611 power meter. Includes frame parsing, CRC validation, register mapping, and common troubleshooting for ESP32-based RS485 readers.
---

# RS485 Modbus RTU Debug Skill

## Overview
This skill assists in debugging RS485 Modbus RTU communication between the ESP32 and PM1611 power meter in the `pm1611-rs485-reader` project.

## PM1611 Key Information
- **Protocol**: Modbus RTU over RS485
- **Default Baud Rate**: 9600 bps (check device config)
- **Data bits**: 8, Parity: None, Stop bits: 1 (8N1)
- **Default Slave Address**: 0x01

## Modbus RTU Frame Structure

### Request Frame (Master → PM1611)
```
| Slave Addr | Function Code | Start Reg (Hi) | Start Reg (Lo) | Qty (Hi) | Qty (Lo) | CRC Lo | CRC Hi |
|   1 byte   |    1 byte     |     1 byte     |     1 byte     |  1 byte  |  1 byte  | 1 byte | 1 byte |
```

### Example: Read Holding Registers (FC03)
Read 2 registers starting at 0x0000 from slave 0x01:
```
01 03 00 00 00 02 C4 0B
```

### Response Frame (PM1611 → Master)
```
| Slave Addr | Function Code | Byte Count | Data (N bytes) | CRC Lo | CRC Hi |
```

## CRC-16 Modbus Calculation
```cpp
uint16_t calculateCRC(uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
```

## Common PM1611 Registers

| Register | Description | Unit | Scale |
|---|---|---|---|
| 0x0000 | Voltage L1 | V | x0.1 |
| 0x0002 | Current L1 | A | x0.001 |
| 0x0004 | Active Power | W | x0.1 |
| 0x0006 | Reactive Power | VAR | x0.1 |
| 0x0008 | Power Factor | - | x0.001 |
| 0x000A | Frequency | Hz | x0.01 |
| 0x0100 | Total Active Energy | kWh | x0.01 |

> ⚠️ Verify register map against official PM1611 datasheet — addresses may vary by firmware version.

## Debugging Checklist

### Hardware
- [ ] RS485 A/B wires not swapped (A+ to A+, B- to B-)
- [ ] Termination resistor (120Ω) on long cables
- [ ] Common ground between ESP32 and PM1611
- [ ] MAX485/SP3485 DE/RE pins correctly controlled
- [ ] Power supply stable (RS485 transceiver needs 3.3V or 5V)

### Software
- [ ] Correct baud rate configured
- [ ] Slave address matches PM1611 DIP switch setting
- [ ] DE/RE pin toggled before TX and after TX
- [ ] Sufficient delay after sending before reading response (>10ms)
- [ ] Serial2 (or correct UART) used for RS485

## Common Errors

| Error | Likely Cause | Fix |
|---|---|---|
| No response from PM1611 | Wrong baud/address/wiring | Check DIP switches, swap A/B wires |
| CRC mismatch | Noise on line, wrong byte order | Check cable, add termination resistor |
| Garbage data | Baud rate mismatch | Match baud rate exactly |
| Timeout always | DE pin not released | Toggle DE/RE to RX mode after TX |
| Partial frame | Response timeout too short | Increase timeout to 200-500ms |

## Serial Log Interpretation
Enable debug output in firmware and look for:
```
TX: 01 03 00 00 00 02 C4 0B     <- Request sent
RX: 01 03 04 09 C4 00 00 xx xx <- Response received
    ↑     ↑  ↑           ↑ CRC
    Addr  FC ByteCount   Data
```

## RS485 Timing (ESP32)
```cpp
// Before sending
digitalWrite(DE_RE_PIN, HIGH);
delayMicroseconds(100);

// After sending (wait for last byte to transmit)
Serial2.flush();
delayMicroseconds(100);
digitalWrite(DE_RE_PIN, LOW);
```
