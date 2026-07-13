---
name: platformio-build
description: Build the pm1611-rs485-reader PlatformIO project. Handles compilation, dependency management, memory usage analysis, and common build error resolution for ESP32 firmware.
---

# PlatformIO Build Skill

## Overview
This skill covers building the `pm1611-rs485-reader` PlatformIO project, analyzing build output, managing libraries, and resolving common compilation errors.

## Project Path
```
d:\enerma\bms\2026\pm1611-rs485-reader\firmware\
```

## Basic Build Commands

### Build Only
```powershell
pio run -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware
```

### Build Specific Environment
```powershell
pio run -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware -e esp32dev
```

### Clean Build
```powershell
pio run -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware --target clean
pio run -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware
```

### Verbose Build (for debugging errors)
```powershell
pio run -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware --verbose
```

## Library Management

### List Installed Libraries
```powershell
pio pkg list -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware
```

### Install Missing Libraries
```powershell
pio pkg install -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware
```

### Install Specific Library
```powershell
pio pkg install -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware --library "ArduinoModbus"
pio pkg install -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware --library "ModbusMaster"
```

### Update Libraries
```powershell
pio pkg update -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware
```

## Memory Analysis

After build, check RAM/Flash usage:
```
RAM:   [===       ]  32.1% (used 10512 bytes from 327680 bytes)
Flash: [=====     ]  48.3% (used 633820 bytes from 1310720 bytes)
```

### If RAM is too high
- Reduce Serial buffer sizes
- Use `PROGMEM` for constant strings
- Reduce log verbosity / disable debug logs in production
- Use `F()` macro for string literals: `Serial.println(F("hello"))`

### If Flash is too high
- Remove unused libraries
- Disable unused features via `#define`
- Enable compiler optimization in `platformio.ini`:
```ini
build_flags = -Os
```

## platformio.ini Reference

Typical config for this project:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600
lib_deps =
    4-20ma/ModbusMaster @ ^2.0.1
    ; or your specific RS485/Modbus library
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DMONITOR_SPEED=115200
```

## Common Build Errors & Fixes

| Error | Cause | Fix |
|---|---|---|
| `'ModbusMaster' was not declared` | Library not installed | `pio pkg install --library "ModbusMaster"` |
| `multiple definition of 'setup'` | Duplicate .cpp file | Remove duplicate, check src_filter |
| `region 'dram0_0_seg' overflowed` | RAM overflow | Reduce buffers, use PROGMEM |
| `undefined reference to 'Serial2'` | Wrong UART | Use Serial1 or Serial2 consistently |
| `error: 'pinMode' was not declared` | Missing Arduino.h include | Add `#include <Arduino.h>` |
| `No such file or directory: 'esp_system.h'` | Wrong SDK version | Check platform version in platformio.ini |
| Toolchain download fails | Network issue | `pio pkg install --no-save` or use VPN |

## Build Output Location
```
d:\enerma\bms\2026\pm1611-rs485-reader\firmware\.pio\build\esp32dev\
├── firmware.bin     ← Flash this file
├── firmware.elf     ← For debugging
└── partitions.bin   ← Partition table
```

## Pre-build Checklist
- [ ] `platformio.ini` has correct board and environment
- [ ] All library dependencies listed under `lib_deps`
- [ ] No syntax errors in `main.cpp` (check with IDE)
- [ ] Pin definitions match actual hardware
- [ ] Baud rates consistent between firmware and hardware

## Tips
- Run `pio check` for static code analysis
- Use `pio run -t compiledb` to generate `compile_commands.json` for IDE integration
- Check `.pio/libdeps/` if library seems installed but not found
