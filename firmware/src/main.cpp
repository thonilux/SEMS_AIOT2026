#include <Arduino.h>
#include "AppMode.h"
#include "PinMap.h"
#include "Version.h"

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kHeartbeatIntervalMs = 1000;
constexpr uint32_t kBootConfigHoldMs = 5000;
constexpr uint32_t kBootButtonSampleMs = 25;

uint32_t lastHeartbeatMs = 0;
uint32_t heartbeatCount = 0;
AppMode currentMode = AppMode::Normal;

bool isConfigButtonPressed() {
  const int rawState = digitalRead(PinMap::kConfigButton);
  return PinMap::kConfigButtonActiveLow ? rawState == LOW : rawState == HIGH;
}

void printBootBanner() {
  Serial.println();
  Serial.println("========================================");
  Serial.println(FW_NAME);
  Serial.print("Firmware: ");
  Serial.println(FW_VERSION);
  Serial.println("Target: ESP32-WROOM / esp32dev");
  Serial.println("Project: PM1611 RS485 Reader");
  Serial.println("Status: boot baseline online");
  Serial.println("========================================");
  Serial.println();
}

AppMode decideBootMode() {
  Serial.print("Config button: GPIO");
  Serial.print(PinMap::kConfigButton);
  Serial.println(" active-low");
  Serial.print("Hold window: ");
  Serial.print(kBootConfigHoldMs);
  Serial.println(" ms");

  const uint32_t startMs = millis();
  uint32_t nextProgressMs = startMs + 1000;

  while (millis() - startMs < kBootConfigHoldMs) {
    if (!isConfigButtonPressed()) {
      Serial.println("Config button not held -> NORMAL_MODE");
      return AppMode::Normal;
    }

    const uint32_t nowMs = millis();
    if (nowMs >= nextProgressMs) {
      Serial.print("Config button held for ");
      Serial.print(nowMs - startMs);
      Serial.println(" ms");
      nextProgressMs += 1000;
    }

    delay(kBootButtonSampleMs);
  }

  Serial.println("Config button hold confirmed -> CONFIG_MODE");
  return AppMode::Config;
}

void printChipInfo() {
  Serial.print("Chip revision: ");
  Serial.println(ESP.getChipRevision());

  Serial.print("CPU frequency: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz");

  Serial.print("Flash size: ");
  Serial.print(ESP.getFlashChipSize() / (1024 * 1024));
  Serial.println(" MB");

  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
}

void printModeInfo() {
  Serial.print("Selected mode: ");
  Serial.println(appModeToString(currentMode));
}
}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(500);

  pinMode(PinMap::kConfigButton, INPUT_PULLUP);

  printBootBanner();
  printChipInfo();
  currentMode = decideBootMode();
  printModeInfo();
}

void loop() {
  const uint32_t nowMs = millis();

  if (nowMs - lastHeartbeatMs >= kHeartbeatIntervalMs) {
    lastHeartbeatMs = nowMs;
    heartbeatCount++;

    Serial.print("heartbeat=");
    Serial.print(heartbeatCount);
    Serial.print(" uptime_ms=");
    Serial.print(nowMs);
    Serial.print(" free_heap=");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" mode=");
    Serial.println(appModeToString(currentMode));
  }
}
