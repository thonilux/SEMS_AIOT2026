#include <Arduino.h>
#include <WiFi.h>
#include "AppMode.h"
#include "PinMap.h"
#include "Version.h"

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kHeartbeatIntervalMs = 1000;
constexpr uint32_t kRuntimeConfigHoldMs = 5000;
constexpr char kConfigApSsidPrefix[] = "PM1611-SETUP";
constexpr char kConfigApPassword[] = "PM123456";

uint32_t lastHeartbeatMs = 0;
uint32_t heartbeatCount = 0;
uint32_t configButtonPressedSinceMs = 0;
uint32_t lastConfigButtonProgressSecond = 0;
AppMode currentMode = AppMode::Normal;
bool configApStarted = false;

bool isConfigButtonPressed() {
  const int rawState = digitalRead(PinMap::kConfigButton);
  return PinMap::kConfigButtonActiveLow ? rawState == LOW : rawState == HIGH;
}

void setBuiltinLed(bool on) {
  const bool outputHigh = PinMap::kBuiltinLedActiveHigh ? on : !on;
  digitalWrite(PinMap::kBuiltinLed, outputHigh ? HIGH : LOW);
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

void printButtonInfo() {
  Serial.print("Config button: GPIO");
  Serial.print(PinMap::kConfigButton);
  Serial.println(" active-low");
  Serial.print("Runtime hold window: ");
  Serial.print(kRuntimeConfigHoldMs);
  Serial.println(" ms");
  Serial.print("Builtin LED: GPIO");
  Serial.print(PinMap::kBuiltinLed);
  Serial.println(" active-high");
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

String getMacSuffix() {
  const uint64_t chipId = ESP.getEfuseMac();
  char suffix[7] = {};
  snprintf(suffix, sizeof(suffix), "%06X", static_cast<uint32_t>(chipId & 0xFFFFFF));
  return String(suffix);
}

String getConfigApSsid() {
  return String(kConfigApSsidPrefix) + "-" + getMacSuffix();
}

void startConfigAccessPoint() {
  if (configApStarted) {
    return;
  }

  const String ssid = getConfigApSsid();
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);

  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIp, gateway, subnet);

  const bool started = WiFi.softAP(ssid.c_str(), kConfigApPassword);
  if (!started) {
    Serial.println("Config AP start failed");
    return;
  }

  configApStarted = true;
  Serial.println("Config AP started");
  Serial.print("AP SSID: ");
  Serial.println(ssid);
  Serial.print("AP password: ");
  Serial.println(kConfigApPassword);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void enterConfigMode() {
  if (currentMode == AppMode::Config) {
    startConfigAccessPoint();
    return;
  }

  currentMode = AppMode::Config;
  setBuiltinLed(true);
  Serial.println("Config button hold confirmed -> CONFIG_MODE");
  printModeInfo();
  startConfigAccessPoint();
}

void handleConfigButton(uint32_t nowMs) {
  if (currentMode == AppMode::Config) {
    setBuiltinLed(true);
    startConfigAccessPoint();
    return;
  }

  if (!isConfigButtonPressed()) {
    configButtonPressedSinceMs = 0;
    lastConfigButtonProgressSecond = 0;
    setBuiltinLed(false);
    return;
  }

  if (configButtonPressedSinceMs == 0) {
    configButtonPressedSinceMs = nowMs;
    lastConfigButtonProgressSecond = 0;
    Serial.println("Config button pressed");
  }

  const uint32_t heldMs = nowMs - configButtonPressedSinceMs;
  const uint32_t heldSecond = heldMs / 1000;
  if (heldSecond > lastConfigButtonProgressSecond) {
    lastConfigButtonProgressSecond = heldSecond;
    Serial.print("Config button held for ");
    Serial.print(heldMs);
    Serial.println(" ms");
  }

  if (heldMs >= kRuntimeConfigHoldMs) {
    enterConfigMode();
  }
}
}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(500);

  pinMode(PinMap::kConfigButton, INPUT_PULLUP);
  pinMode(PinMap::kBuiltinLed, OUTPUT);
  setBuiltinLed(false);

  printBootBanner();
  printChipInfo();
  printButtonInfo();
  printModeInfo();
}

void loop() {
  const uint32_t nowMs = millis();

  handleConfigButton(nowMs);

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
