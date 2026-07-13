---
name: serial-monitor
description: Open and manage serial monitor for ESP32 pm1611-rs485-reader. Filter logs, capture PM1611 data output, and decode serial debug messages from firmware.
---

# Serial Monitor Skill

## Overview
This skill helps you open, filter, and interpret serial output from the ESP32 `pm1611-rs485-reader` firmware.

## Opening Serial Monitor

### Via PlatformIO CLI
```powershell
pio device monitor -d d:\enerma\bms\2026\pm1611-rs485-reader\firmware --baud 115200
```

With specific port:
```powershell
pio device monitor --port COM3 --baud 115200
```

With filters (raw hex + log):
```powershell
pio device monitor --port COM3 --baud 115200 --filter hexlify
```

### PlatformIO Monitor Filters
| Filter | Description |
|---|---|
| `default` | Standard text output |
| `hexlify` | Show hex + ASCII |
| `log2file` | Save output to file |
| `time` | Prefix each line with timestamp |
| `colorize` | Color-code output |

Add filters in `platformio.ini`:
```ini
[env:your_env]
monitor_speed = 115200
monitor_filters = time, colorize
```

## Capturing Output to File
```powershell
pio device monitor --port COM3 --baud 115200 | Tee-Object -FilePath "serial_log.txt"
```

## Reading Log from Existing File
```powershell
Get-Content serial_log.txt | Select-String -Pattern "PM1611|ERROR|WARN"
```

## Common PM1611 Log Patterns to Watch For

```
[INFO] PM1611: Voltage=220.5V Current=1.234A Power=271.8W
[INFO] PM1611: Energy=1234.56kWh PF=0.985 Freq=50.01Hz
[WARN] RS485: No response from slave 0x01 (attempt 1/3)
[ERROR] Modbus: CRC mismatch - expected 0xABCD got 0x1234
[DEBUG] TX: 01 03 00 00 00 06 C5 CB
[DEBUG] RX: 01 03 0C ...
```

## Filtering Specific Data
In PowerShell, filter while monitoring:
```powershell
pio device monitor --port COM3 --baud 115200 | Where-Object { $_ -match "PM1611|ERROR" }
```

## Decode Firmware Debug Levels
The `pm1611-rs485-reader` firmware likely uses these log levels:
- `[DEBUG]` — Raw Modbus frames, register values
- `[INFO]` — Parsed PM1611 measurements
- `[WARN]` — Retry attempts, communication warnings
- `[ERROR]` — CRC errors, timeout, hardware faults

## Troubleshooting Serial Monitor

| Problem | Fix |
|---|---|
| `Port is busy` | Close other serial terminals (Arduino IDE, Putty) |
| `Permission denied on COMX` | Run as Administrator or check device ownership |
| `Garbled output` | Wrong baud rate — try 9600, 115200 |
| `No output at all` | Check USB cable, press RESET on ESP32 |
| Monitor closes after flash | Add `monitor_rts = 0` and `monitor_dtr = 0` in platformio.ini |

## Tips
- Press `Ctrl+C` to exit PlatformIO monitor
- Use `--raw` flag to disable PlatformIO monitor UI for piping output
- Reset ESP32 with `EN` button if you miss boot messages
