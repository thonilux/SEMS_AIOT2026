# 🧾 Previous Prototype Schematic Notes

Reference schematic:

```text
Title: SCH_ESP32.DCin-Relay-pisah_2026-05-07
Revision: 1.0
MCU: ESP32-WROOM-32E (8 MB)
Source: Previous prototype schematic screenshot
```

This document captures what can be inferred from the previous prototype schematic. Treat this as an engineering note, not a final production schematic.

## 🧠 Main Findings

The prototype includes:

- ESP32-WROOM-32E module.
- 5 V input rail.
- AMS1117-3.3 regulator for 3.3 V rail.
- LM2596S-5.0 buck regulator section.
- MAX485 RS485 transceiver.
- 74HC04D inverter logic around UART/RS485 signals.
- Relay output header.
- W5500 Mini Ethernet module header.
- I2C header.
- UART header.
- Debug GPIO header.
- Boot and reset buttons.
- Power/status LED.
- RS485 surge/ESD protection components.

## 🔌 Power Architecture

Observed rails:

```text
+5V
+3.3V
VIN
GND
```

Sections:

- `AMS1117-3.3` generates 3.3 V from 5 V.
- `LM2596S-5.0` appears to generate 5 V from `VIN`.
- Relay header uses 5 V.
- ESP32 and logic use 3.3 V.

Notes:

- AMS1117 can run warm if the 5 V rail is high-current or poorly regulated.
- ESP32 current spikes need good local decoupling.
- Relay coil/load current should not share weak 3.3 V paths.

## 🧩 ESP32 Module

The schematic shows:

```text
Module: ESP32-WROOM-32E (8 MB)
```

Important pins exposed or used:

```text
GPIO0
GPIO2
GPIO4
GPIO5
GPIO12
GPIO13
GPIO14
GPIO15
GPIO16 / RXD
GPIO17 / TXD
GPIO18
GPIO19
GPIO21
GPIO22
GPIO23
GPIO25
GPIO26
GPIO27
GPIO32
GPIO33
GPIO34
GPIO35
U0RXD / GPIO3
U0TXD / GPIO1
```

Boot/reset:

- `GPIO0` is connected to BOOT circuit.
- `EN` is connected to reset circuit.

## 🔄 RS485 Section

The prototype uses:

```text
Transceiver: MAX485
Logic:       74HC04D inverter
Connector:   RS485 terminal/header
```

MAX485 signals shown:

```text
R
RE#
DE
D
A
B
```

Important inference:

The previous prototype does not appear to use a simple dedicated ESP32 `TX_ENABLE` GPIO directly into `DE/RE#`.

Instead, the schematic routes UART-related signals through a `74HC04D`, producing a signal labeled:

```text
RE/DE
```

This suggests an auto-direction or logic-derived RS485 enable circuit.

Firmware implication:

- If this exact circuit is used, firmware may not need to toggle `TX_ENABLE`.
- If the new PCB uses a standard MAX485 circuit, firmware should use a dedicated `TX_ENABLE` GPIO.
- The firmware architecture should support both modes:

```text
RS485 direction mode:
- auto
- gpio_tx_enable
```

## 🧷 RS485 Protection

The RS485 bus includes protection/biasing components:

- TVS/ESD diodes near A/B.
- Series or protection elements marked around `F3`, `F4`.
- Bias resistors.
- Termination resistor appears present, likely around `120R`.

Notes:

- Termination should be configurable by hardware jumper or BOM option in production.
- Biasing should exist only once per RS485 segment when multiple devices share the bus.

## 📡 W5500 Mini Header

The prototype includes a W5500 Mini Ethernet module header.

Observed mapping:

| W5500 Signal | ESP32 GPIO |
| --- | --- |
| MOSI | GPIO23 |
| SCLK | GPIO18 |
| CS | GPIO5 |
| INT | GPIO27 |
| RST | GPIO26 |
| MISO | GPIO19 |
| 3V3 | +3.3 V |
| GND | GND |

Notes:

- This uses the ESP32 VSPI-style pins.
- `GPIO5` is a strapping pin, so the W5500 CS pull state during boot matters.
- If Ethernet is not used, these pins can be reassigned to LCD SPI or other functions.

## 🖥️ I2C Header

Observed I2C header:

| Header Pin | Signal |
| --- | --- |
| 1 | GND |
| 2 | +3.3 V |
| 3 | GPIO21 |
| 4 | GPIO22 |

This is the common ESP32 I2C mapping:

```text
SDA: GPIO21
SCL: GPIO22
```

The current selected LCD is an ST7567 SPI LCD, not I2C. Therefore this header may be unused unless another I2C peripheral is added.

## 🔌 UART Header

Observed UART header:

| Header Pin | Signal |
| --- | --- |
| 1 | +3.3 V |
| 2 | U0RXD |
| 3 | U0TXD |
| 4 | GND |

This is likely the ESP32 programming/debug UART:

```text
U0RXD: GPIO3
U0TXD: GPIO1
```

Avoid using this for Modbus RS485 if USB serial debugging is needed.

## 🧲 Relay Header

Observed relay header:

| Header Pin | Signal |
| --- | --- |
| 1 | +5 V |
| 2 | GND |
| 3 | GPIO13 |
| 4 | GPIO14 |
| 5 | GPIO15 |
| 6 | GPIO2 |

Notes:

- `GPIO2` and `GPIO15` are ESP32 strapping pins.
- Using strapping pins for relay control can cause boot problems if the relay board pulls them during reset.
- Prefer `GPIO25`, `GPIO26`, `GPIO27`, `GPIO32`, or `GPIO33` for new relay-control designs if available.

## 🧪 Debug Header

The debug header appears to expose:

```text
GPIO34
GPIO35
GPIO25
GPIO4
GPIO33
GPIO32
```

Notes:

- GPIO34 and GPIO35 are input-only.
- GPIO4 is a strapping pin and should be handled carefully.
- GPIO25, GPIO32, and GPIO33 are useful general-purpose pins.

## ⚠️ Boot-Strapping Pin Risk

Treat these pins carefully:

```text
GPIO0
GPIO2
GPIO4
GPIO5
GPIO12
GPIO15
```

The prototype uses several of them on headers or peripherals.

Production firmware and PCB should avoid depending on external modules that pull these pins to unsafe boot states.

## 🎯 Firmware Consequences

The firmware should support:

```text
RS485 UART:
- TX: GPIO17
- RX: GPIO16
- Direction: auto or GPIO-controlled

Debug UART:
- U0TXD/GPIO1
- U0RXD/GPIO3

Optional Ethernet:
- W5500 SPI on GPIO18/19/23
- CS GPIO5
- INT GPIO27
- RST GPIO26

Optional I2C:
- SDA GPIO21
- SCL GPIO22
```

Relay GPIO should not be finalized until the actual relay board/input polarity is confirmed.

