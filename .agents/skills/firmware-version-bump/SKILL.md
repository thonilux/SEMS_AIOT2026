---
name: firmware-version-bump
description: Bump firmware version in the pm1611-rs485-reader project. Updates version string in main.cpp or version header, creates git tag, and updates changelog.
---

# Firmware Version Bump Skill

## Overview
This skill automates version bumping for the `pm1611-rs485-reader` ESP32 firmware following semantic versioning (SemVer).

## Version Format
```
MAJOR.MINOR.PATCH
  ↑      ↑     ↑
  │      │     └── Bug fixes, small tweaks
  │      └──────── New features, register additions
  └─────────────── Breaking changes, hardware rev
```

## Where Version Is Defined

Check `main.cpp` or a version header:
```cpp
// In main.cpp
#define FIRMWARE_VERSION "1.2.3"
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 2
#define FIRMWARE_VERSION_PATCH 3
```

Or in a dedicated header:
```cpp
// version.h
#pragma once
#define VERSION_MAJOR 1
#define VERSION_MINOR 2
#define VERSION_PATCH 3
#define VERSION_STRING "1.2.3"
```

## Version Bump Steps

### 1. Find Current Version
```powershell
Select-String -Path "d:\enerma\bms\2026\pm1611-rs485-reader\firmware\src\main.cpp" -Pattern "FIRMWARE_VERSION|VERSION_STRING"
```

### 2. Update Version in Code
Edit `main.cpp` to increment the appropriate version number.

### 3. Update Changelog
Append entry to `CHANGELOG.md` or `docs/changelog.md`:
```markdown
## v1.2.4 - 2026-07-05
### Fixed
- Fixed CRC calculation for multi-register reads
- Improved RS485 timing for PM1611

### Changed
- Increased retry count from 3 to 5
```

### 4. Git Commit & Tag
```powershell
git -C d:\enerma\bms\2026\pm1611-rs485-reader add -A
git -C d:\enerma\bms\2026\pm1611-rs485-reader commit -m "chore: bump firmware to v1.2.4"
git -C d:\enerma\bms\2026\pm1611-rs485-reader tag -a "v1.2.4" -m "Firmware v1.2.4"
```

### 5. Push with Tags
```powershell
git -C d:\enerma\bms\2026\pm1611-rs485-reader push origin main --tags
```

## Version in Firmware Output
Make sure firmware prints version on boot:
```cpp
void setup() {
    Serial.begin(115200);
    Serial.printf("PM1611 RS485 Reader v%s\n", FIRMWARE_VERSION);
    Serial.printf("Build: %s %s\n", __DATE__, __TIME__);
}
```

## Bump Decision Guide

| Change Type | What to Bump | Example |
|---|---|---|
| Fix RS485 timeout bug | PATCH | 1.2.3 → 1.2.4 |
| Add new PM1611 registers | MINOR | 1.2.3 → 1.3.0 |
| Change hardware pin mapping | MINOR | 1.2.3 → 1.3.0 |
| Rewrite for new hardware rev | MAJOR | 1.2.3 → 2.0.0 |
| Add MQTT/cloud feature | MINOR | 1.2.3 → 1.3.0 |

## Git Tag Conventions
```
v1.2.3         → stable release
v1.2.3-beta.1  → beta
v1.2.3-rc.1    → release candidate
```

## Check Existing Tags
```powershell
git -C d:\enerma\bms\2026\pm1611-rs485-reader tag --sort=-version:refname | head -5
```
