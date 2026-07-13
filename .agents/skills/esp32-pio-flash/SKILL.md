---
name: esp32-pio-flash
description: Flash ESP32 firmware using PlatformIO. Handles COM port detection, build, upload, and post-flash verification for the pm1611-rs485-reader project.
---

# ESP32 PlatformIO Flash Skill

## Overview
This skill helps you build and flash the ESP32 firmware for the `pm1611-rs485-reader` project using PlatformIO CLI (`pio`).

## Workflow

### 1. Check Available COM Ports
Before flashing, detect which COM port the ESP32 is connected to:
```powershell
pio device list
```
Or via PowerShell:
```powershell
Get-WmiObject Win32_PnPEntity | Where-Object { $_.Name -match "COM\d+" } | Select-Object Name
```

### 2. Build Firmware
```powershell
pio run -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware
```

### 3. Upload Firmware
```powershell
pio run -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware --target upload
```

Upload to specific port:
```powershell
pio run -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware --target upload --upload-port COM3
```

### 4. Build + Upload (Single Command)
```powershell
pio run -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware -t upload
```

## Common Issues & Fixes

| Problem | Solution |
|---|---|
| `No device found on COMX` | Check USB cable, try another port, install CH340/CP2102 driver |
| `Upload failed: could not open port` | Close serial monitor first, check Device Manager |
| `Error: Invalid value for "--upload-port"` | Use `pio device list` to find correct port |
| `RAM/Flash overflow` | Check `platformio.ini` build flags, reduce log verbosity |
| `esptool.py not found` | Run `pio pkg install` to reinstall toolchain |

## PlatformIO Environment (pm1611-rs485-reader)

The project uses these key settings in `platformio.ini`:
- Board: ESP32 (check `platformio.ini` for exact board name)
- Framework: Arduino
- Monitor speed: typically 115200 baud

## After Flash
Monitor output immediately after flash:
```powershell
pio device monitor -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware --baud 115200
```

## Tips
- Always close serial monitor before uploading
- Hold BOOT button on ESP32 if auto-reset fails
- Use `pio run --verbose -t upload` for detailed upload logs
