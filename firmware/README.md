# Firmware

PlatformIO firmware will live here.

Planned layout:

```text
firmware/
├── platformio.ini
├── include/   Public project headers
├── src/       Firmware source
├── lib/       Local reusable libraries
├── data/      LittleFS web UI and static assets
└── test/      PlatformIO tests
```

The active firmware target is ESP32 using the Arduino framework.

