# 🧠 ESP32 Hardware Capability Analysis

This document analyzes which ESP32 series is appropriate for the PM1611 RS485 Reader firmware.

The project is not just a simple Modbus reader. It is an appliance-style gateway with RS485 polling, relay protection, WiFi, MQTT, web UI, login/session handling, OTA updates, LCD output, LED states, configuration storage, and energy history. The selected ESP32 must have enough memory, flash, UART capability, and long-term availability for this workload.

## ⚙️ Firmware Workload Summary

Expected runtime features:

- RS485 Modbus RTU polling
- UART2 with TX enable control
- WiFi station mode
- WiFi access point setup mode
- MQTT publish and subscribe
- Embedded web UI
- JSON API
- Login/session handling
- OTA firmware update
- LittleFS web assets and history storage
- NVS configuration storage
- Relay protection logic
- LCD display task
- LED status task
- NTP time sync
- Optional future TLS MQTT or HTTPS-style security improvements

This means the board should be selected for margin, not just minimum boot success.

## ✅ Minimum Hardware Requirements

| Resource | Minimum | Recommended |
| --- | --- | --- |
| CPU | Dual-core preferred | Dual-core Xtensa or newer |
| RAM | 320 KB internal SRAM | 520 KB SRAM or more |
| Flash | 4 MB | 8 MB or 16 MB |
| PSRAM | Optional | Useful for larger web UI/TLS |
| UART | 2 hardware UARTs minimum | 3 hardware UARTs preferred |
| WiFi | Required | 2.4 GHz WiFi required |
| Bluetooth | Not required | Can be disabled |
| GPIO | 10+ usable pins | Avoid boot strapping pins for critical outputs |
| OTA | Required | Partition layout must support OTA |
| Filesystem | Required | LittleFS recommended |

## 🆚 ESP32 Series Comparison

### 🧱 Classic ESP32

Examples:

- ESP32-WROOM-32
- ESP32-WROOM-32E
- ESP32-WROVER
- ESP32-DevKitC
- NodeMCU-32S

Strengths:

- Mature Arduino and PlatformIO support.
- Dual-core CPU.
- Integrated WiFi.
- Multiple hardware UARTs.
- Many examples and libraries.
- Good enough for Modbus, MQTT, web UI, OTA, LCD, and relay control.
- Cheap and widely available.

Limitations:

- Internal RAM can become tight with large web UI, TLS, AsyncWebServer, MQTT, and JSON at the same time.
- 4 MB flash boards can be limiting once OTA and LittleFS are enabled.
- Some dev boards expose boot strapping pins in ways that can cause boot problems.

Verdict:

Classic ESP32 is the best starting point and likely the best production choice if using an 8 MB or 16 MB flash module.

Recommended classic modules:

```text
ESP32-WROOM-32E with 8 MB or 16 MB flash
ESP32-WROVER-E if PSRAM is desired
```

### 🧩 ESP32-WROVER

Strengths:

- Same classic ESP32 ecosystem.
- Adds PSRAM.
- Better margin for web UI buffers, JSON, TLS, and OTA.

Limitations:

- Slightly more expensive.
- Some PSRAM boards reserve GPIOs internally.

Verdict:

Recommended if the firmware will use a larger web UI, TLS MQTT, bigger JSON payloads, or more advanced local dashboards.

### 🔹 ESP32-S2

Strengths:

- Native USB on many variants.
- Good security feature set compared with older ESP32.
- Adequate for single-threaded IoT workloads.

Limitations:

- Single-core.
- No Bluetooth.
- Less ideal when running web UI, MQTT, Modbus polling, and protection logic together.

Verdict:

Not the first choice for this project. It can work, but the classic dual-core ESP32 or ESP32-S3 gives more comfortable task separation.

### 🚀 ESP32-S3

Strengths:

- Dual-core.
- Newer architecture than classic ESP32.
- Good Arduino and PlatformIO support.
- Native USB on many boards.
- PSRAM options are common.
- Better future margin for UI, TLS, and more complex firmware.

Limitations:

- Some older libraries were originally written for classic ESP32 and may need validation.
- Pin mapping and board definitions vary more between modules.
- Slightly more expensive than basic ESP32-WROOM boards.

Verdict:

Best high-margin choice. Recommended when starting a new product-style design, especially if using a board/module with 8 MB or 16 MB flash and PSRAM.

Recommended modules:

```text
ESP32-S3-WROOM-1 with 8 MB or 16 MB flash
ESP32-S3-WROOM-1-N8R8 for 8 MB flash + 8 MB PSRAM
ESP32-S3-WROOM-1-N16R8 for 16 MB flash + 8 MB PSRAM
```

### 🪙 ESP32-C3

Strengths:

- Low cost.
- RISC-V core.
- WiFi and BLE.
- Good for simple sensors.

Limitations:

- Single-core.
- Less RAM.
- Fewer GPIOs and UART resources than classic ESP32/S3.
- Tight for web UI + MQTT + OTA + Modbus + protection if the project grows.

Verdict:

Not recommended for this gateway unless the feature set is reduced significantly.

### 📡 ESP32-C6

Strengths:

- WiFi 6 and newer connectivity.
- RISC-V.
- Good future IoT direction.

Limitations:

- Single-core class device.
- More ecosystem validation needed compared with classic ESP32/S3.
- Not necessary for this RS485 gateway.

Verdict:

Interesting for future products, but not the best practical choice for this firmware today.

### 🧵 ESP32-H2

Strengths:

- Thread/Zigbee/BLE focused.

Limitations:

- No WiFi.
- Not suitable as the main controller for this WiFi/MQTT gateway.

Verdict:

Not suitable.

## 🏆 Recommended ESP32 Choice

### ✅ Best Practical Choice

```text
ESP32-WROOM-32E, 8 MB or 16 MB flash
```

Why:

- Mature and stable.
- Dual-core.
- Enough UARTs.
- Excellent Arduino/PlatformIO support.
- Good availability.
- Low cost.
- Sufficient for the first complete firmware version.

Avoid 4 MB flash if possible because OTA plus LittleFS plus a web UI can become tight.

### 🚀 Best High-Margin Choice

```text
ESP32-S3-WROOM-1-N16R8
```

Why:

- Dual-core.
- 16 MB flash.
- 8 MB PSRAM.
- Comfortable for web UI, JSON, MQTT, TLS, OTA, and future features.
- Better long-term headroom.

This is the best choice if the hardware is still flexible and cost difference is acceptable.

### 🧪 Best Development Board Choice

For early firmware development:

```text
ESP32 DevKitC with ESP32-WROOM-32E
```

or:

```text
ESP32-S3 DevKitC-1 with PSRAM
```

Development board selection should expose enough safe GPIOs for:

- RS485 RX
- RS485 TX
- RS485 TX enable
- Relay output
- LCD I2C SDA
- LCD I2C SCL
- WiFi LED
- MQTT LED
- Relay LED
- Fault LED
- Optional button input

## 💾 Flash Size Recommendation

### ⚠️ 4 MB Flash

Possible, but constrained.

Risks:

- OTA partition size may be small.
- LittleFS space may be limited.
- Large web assets may not fit comfortably.
- Future features may require repartitioning.

Use only for early tests or reduced firmware.

### ✅ 8 MB Flash

Recommended minimum.

Allows:

- OTA partitions
- LittleFS web assets
- History storage
- Reasonable firmware growth

### 🏆 16 MB Flash

Best for product-style firmware.

Allows:

- Larger OTA partitions
- More web UI assets
- More logs/history
- Easier future expansion

## 🧠 PSRAM Recommendation

PSRAM is not mandatory for the first version, but it is useful.

Helpful when using:

- Async web server
- Larger HTML/CSS/JS assets
- More JSON documents
- TLS MQTT
- Complex dashboards
- Buffered logs
- Future data history features

Recommended if choosing ESP32-S3 or ESP32-WROVER.

## 🔌 UART And RS485 Considerations

The firmware should use a hardware UART for RS485.

Default development pins:

```text
RXD2:      GPIO16
TXD2:      GPIO17
TX_ENABLE: GPIO0
Baudrate:  19200
```

Recommended production adjustment:

```text
Avoid GPIO0 for TX_ENABLE if possible.
Use a non-strapping GPIO such as GPIO4, GPIO5, GPIO18, GPIO19, GPIO21, GPIO22, GPIO25, GPIO26, or GPIO27 depending on board layout.
```

GPIO0 risk:

- It affects ESP32 boot mode.
- If pulled low during reset, ESP32 may enter bootloader mode.
- RS485 transceiver circuits can accidentally influence the pin.

## 🧷 GPIO Planning

Avoid using strapping pins for critical outputs when possible.

Classic ESP32 strapping pins to treat carefully:

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

Typical safe planning:

| Function | Suggested GPIO |
| --- | --- |
| RS485 RX | GPIO16 |
| RS485 TX | GPIO17 |
| RS485 TX enable | GPIO4 or GPIO27 |
| Relay | GPIO25 or GPIO26 |
| I2C SDA | GPIO21 |
| I2C SCL | GPIO22 |
| WiFi LED | GPIO18 |
| MQTT LED | GPIO19 |
| Relay LED | GPIO23 |
| Fault LED | GPIO13 or GPIO14 |
| Button | GPIO32 or GPIO33 |

Final GPIO selection must match the real board schematic.

## 🧯 Memory Risk Areas

The most memory-sensitive features are:

- Web server
- JSON serialization
- OTA upload
- MQTT payload buffers
- TLS, if enabled
- Large LittleFS assets
- Modbus register maps if made dynamic

Memory control rules:

- Keep JSON documents sized explicitly.
- Avoid building huge strings repeatedly.
- Stream web assets from LittleFS.
- Avoid frequent dynamic allocation in polling loops.
- Keep MQTT payload generation centralized.
- Disable Bluetooth if unused.

## 🔄 OTA Partition Considerations

OTA requires enough flash for at least two app partitions.

Recommended partition strategy for 8 MB or 16 MB flash:

```text
nvs
otadata
app0
app1
littlefs
```

For 4 MB flash, OTA plus filesystem may force compromises.

## 🧰 PlatformIO Board Direction

Initial development can start with a common board definition such as:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
```

If using ESP32-S3:

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
```

The exact `board` value should be selected after confirming the physical module or development board.

## 🎯 Final Recommendation

Use one of these:

1. `ESP32-WROOM-32E` with 8 MB or 16 MB flash for the practical version.
2. `ESP32-S3-WROOM-1-N16R8` for the high-margin product version.
3. `ESP32-WROVER-E` if staying on classic ESP32 but wanting PSRAM.

Do not choose ESP32-C3/C6 for the full PM1611-style gateway unless the firmware scope is reduced.

For this project, the preferred target is:

```text
ESP32-S3-WROOM-1-N16R8
```

The fallback practical target is:

```text
ESP32-WROOM-32E with at least 8 MB flash
```
