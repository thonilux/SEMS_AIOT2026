# 🧷 Prototype Pin Map

This pin map is based on the previous prototype schematic and the current ESP32-WROOM board detected during firmware flashing.

Current detected board:

```text
Chip:  ESP32-D0WD-V3 revision v3.1
Port:  COM3
USB:   CH340
Flash: 4 MB
MAC:   38:18:2b:83:d1:b4
```

## ✅ Current Firmware Target

```text
PlatformIO board: esp32dev
Framework:        Arduino
Serial monitor:   115200
Upload port:      COM3
```

## 🔄 RS485 / Modbus UART

Recommended firmware default:

| Function | ESP32 GPIO | Notes |
| --- | --- | --- |
| RS485 RX | GPIO16 | ESP32 UART2 RX |
| RS485 TX | GPIO17 | ESP32 UART2 TX |
| RS485 TX Enable | Not fixed | Depends on hardware direction mode |

The previous prototype schematic appears to derive `RE/DE` through `74HC04D` logic.

Therefore support two firmware modes:

| Direction Mode | Meaning |
| --- | --- |
| `auto` | Hardware handles MAX485 direction |
| `gpio_tx_enable` | Firmware toggles a dedicated direction GPIO |

Initial firmware config should use:

```text
rs485.direction_mode = auto
```

If a future board uses direct direction control, use a non-strapping GPIO such as:

```text
GPIO25
GPIO26
GPIO27
GPIO32
GPIO33
```

Avoid using `GPIO0` for RS485 direction in production because it is a boot strapping pin.

## 🧲 Relay Header From Prototype

Observed relay header:

| Header Pin | Signal |
| --- | --- |
| 1 | +5 V |
| 2 | GND |
| 3 | GPIO13 |
| 4 | GPIO14 |
| 5 | GPIO15 |
| 6 | GPIO2 |

Recommended firmware decision:

```text
Do not finalize relay GPIO until the actual relay module wiring is confirmed.
```

Risk:

- `GPIO2` is a strapping pin.
- `GPIO15` is a strapping pin.
- External relay boards may pull inputs during boot.

Safer future relay GPIO candidates:

```text
GPIO25
GPIO26
GPIO27
GPIO32
GPIO33
```

## 🖥️ LCD Pin Plan

The selected LCD is an ST7567 128x64 SPI LCD.

The previous prototype did not show this exact SPI LCD connector. It did show an I2C header:

| Header Pin | Signal |
| --- | --- |
| 1 | GND |
| 2 | +3.3 V |
| 3 | GPIO21 |
| 4 | GPIO22 |

For the ST7567 SPI LCD, recommended firmware pin plan:

| LCD Signal | ESP32 GPIO | Notes |
| --- | --- | --- |
| SCL / CLK | GPIO18 | Can share SPI clock with W5500 if Ethernet is used |
| SDA / MOSI | GPIO23 | Can share SPI MOSI with W5500 if Ethernet is used |
| CS | GPIO25 | Dedicated LCD chip select, avoids W5500 CS |
| DC / A0 | GPIO21 | Data/command |
| RST | GPIO22 | LCD reset |
| VDD | 3.3 V | Logic supply |
| GND | GND | Ground |

If W5500 Ethernet is not used, the SPI bus remains simple.

If W5500 Ethernet is used, LCD and W5500 may share:

```text
SCLK: GPIO18
MOSI: GPIO23
MISO: GPIO19, W5500 only
```

Each device must have its own CS.

## 📡 W5500 Mini Prototype Mapping

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

- `GPIO5` is a strapping pin.
- Use pullups/pulldowns carefully.
- W5500 is optional for this firmware because WiFi is the primary network path.

## 🔌 Debug UART

| Function | ESP32 GPIO |
| --- | --- |
| U0TXD | GPIO1 |
| U0RXD | GPIO3 |

Use this for boot logs and serial diagnostics.

Do not use this UART for Modbus RS485.

## 🧪 Debug Header Candidates

Observed debug GPIOs:

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
- GPIO4 is a strapping pin.
- GPIO25, GPIO32, and GPIO33 are useful spare outputs.

## 🚦 LED Plan

The previous prototype includes at least a power/status LED. Final LED GPIOs are not fully locked.

Suggested firmware LED assignments if pins are free:

| LED | Suggested GPIO | Notes |
| --- | --- | --- |
| WiFi LED | GPIO32 | Safe general GPIO |
| MQTT LED | GPIO33 | Safe general GPIO |
| Relay LED | GPIO14 | Available but check relay header use |
| Fault LED | GPIO13 | Available but check relay header use |

This is provisional.

## ⚠️ Pins To Avoid For Critical Outputs

Avoid these for relay enable, RS485 enable, or anything that can be externally pulled at boot:

```text
GPIO0
GPIO2
GPIO4
GPIO5
GPIO12
GPIO15
```

Input-only pins:

```text
GPIO34
GPIO35
GPIO36
GPIO39
```

## 🎯 Current Firmware Constants Recommendation

For the next RS485 UART proof:

```text
RS485_RX_PIN = 16
RS485_TX_PIN = 17
RS485_DIRECTION_MODE = auto
RS485_BAUDRATE = 19200
```

Do not implement relay control until the actual relay board wiring and polarity are confirmed.

