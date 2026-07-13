---
name: firmware-version-bump
description: Bump the SEMS AIoT firmware version number. Use when the user asks to bump/increment the firmware version, cut a new firmware release, or asks "naikkan versi firmware" / "release versi baru".
---

# Firmware version bump

This project defines `FW_VERSION` in **two places** that must stay in sync:

- `firmware/platformio.ini` — `build_flags: -D FW_VERSION=\"X.Y.Z\"`
- `firmware/src/main.cpp` — `#define FW_VERSION "X.Y.Z"` (near top of file)

Note: `main.cpp`'s `#define` currently takes precedence/conflicts with the
`platformio.ini` build flag (duplicate definition — GCC will warn "redefined").
Until that's cleaned up, **both must be updated identically** or the build
flag define is effectively dead.

## Steps

1. Read the current version from `firmware/src/main.cpp` (`#define FW_VERSION`).
2. Determine the new version:
   - If the user gives an explicit version, use it.
   - Otherwise ask semver bump type (patch/minor/major) unless obvious from context (e.g. "bug fix" → patch, "fitur baru" → minor).
3. Update the version string in both:
   - `firmware/platformio.ini`
   - `firmware/src/main.cpp`
4. Grep for any other hardcoded version strings before finishing (e.g. `git grep -n "FW_VERSION\|v0\."` under `firmware/src` and `firmware/platformio.ini`) to make sure nothing was missed — exclude `pio-core/` (vendored SDK, not project code) and `.bak` files.
5. Show the diff to the user. Do not commit unless explicitly asked.
