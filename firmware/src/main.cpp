#include <Arduino.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <U8g2lib.h>
#include <cmath>
#include <time.h>
#include "AppMode.h"
#include "PinMap.h"
#include "Version.h"
#include "ConfigManager.h"

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kHeartbeatIntervalMs = 1000;
constexpr uint32_t kRuntimeConfigHoldMs = 5000;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kNtpSyncTimeoutMs = 10000;
constexpr char kConfigApSsidPrefix[] = "PM1611-SETUP";
constexpr char kConfigApPassword[] = "PM123456";
constexpr char kNetworkPrefsNamespace[] = "network";
constexpr char kWifiSsidKey[] = "wifi_ssid";
constexpr char kWifiPassKey[] = "wifi_pass";
constexpr char kNtpServer1[] = "pool.ntp.org";
constexpr char kNtpServer2[] = "time.google.com";
constexpr char kNtpTimezone[] = "WIB-7";

uint32_t lastHeartbeatMs = 0;
uint32_t heartbeatCount = 0;
uint32_t configButtonPressedSinceMs = 0;
uint32_t lastConfigButtonProgressSecond = 0;
AppMode currentMode = AppMode::Normal;
bool configApStarted = false;
bool configWebStarted = false;
bool rebootRequested = false;
uint32_t rebootAtMs = 0;
String savedWifiSsid;
String savedWifiPassword;
bool hasSavedWifi = false;
bool wifiConnecting = false;
uint32_t wifiConnectStartedMs = 0;
bool wifiConnected = false;
bool ntpSyncStarted = false;
bool fallbackApActive = false;
bool timeSynced = false;
time_t lastNtpSyncEpoch = 0;
DeviceConfig currentDeviceConfig;
MqttConfig currentMqttConfig;
ModbusConfig currentModbusConfig;
ProtectionConfig currentProtectionConfig;
DisplayConfig currentDisplayConfig;
HistoryConfig currentHistoryConfig;
SystemConfig currentSystemConfig;
bool currentModbusConfigLoaded = false;
bool modbusPortReady = false;
uint32_t lastModbusPollMs = 0;
bool mqttConnected = false;
bool mqttClientConfigured = false;
uint32_t mqttLastReconnectAttemptMs = 0;
uint32_t mqttLastPublishMs = 0;
WiFiClient mqttTransport;
WiFiClientSecure mqttSecureTransport;
PubSubClient mqttClient(mqttTransport);
bool relayRequestedState = false;
bool relayActualState = false;
bool relayLockedOut = false;
uint32_t relayTripUntilMs = 0;
uint32_t relayOvercurrentSinceMs = 0;
bool displayReady = false;
uint32_t displayLastUpdateMs = 0;
uint8_t displayPage = 0;
U8G2_ST7567_ENH_DG128064_F_4W_HW_SPI displaySt7567(U8G2_R0, PinMap::kDisplayCs, PinMap::kDisplayDc, PinMap::kDisplayReset);
U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI displaySsd1306(U8G2_R0, PinMap::kDisplayCs, PinMap::kDisplayDc, PinMap::kDisplayReset);

struct HistoryRuntime {
  bool loaded = false;
  bool dirty = false;
  uint32_t dayKey = 0;
  float baselineEnergy = NAN;
  float daily[7] = {0, 0, 0, 0, 0, 0, 0};
  uint32_t lastPersistMs = 0;
};

HistoryRuntime historyRuntime;

struct MeterSnapshot {
  bool online = false;
  bool valid = false;
  uint32_t lastPollMs = 0;
  uint32_t lastSuccessMs = 0;
  uint32_t lastErrorMs = 0;
  uint16_t lastErrorCode = 0;
  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float frequency = NAN;
  float pf = NAN;
  float energy = NAN;
};

MeterSnapshot meterSnapshot;
WebServer configServer(80);

void startConfigWebServer();
void enterConfigMode();
void startConfigAccessPoint(bool disconnectSta = true);

bool isConfigButtonPressed() {
  const int rawState = digitalRead(PinMap::kConfigButton);
  return PinMap::kConfigButtonActiveLow ? rawState == LOW : rawState == HIGH;
}

void setBuiltinLed(bool on) {
  const bool outputHigh = PinMap::kBuiltinLedActiveHigh ? on : !on;
  digitalWrite(PinMap::kBuiltinLed, outputHigh ? HIGH : LOW);
}


void updateStatusLed(uint32_t nowMs) {

  if (currentMode == AppMode::Config) {
    setBuiltinLed(true);
    return;
  }

  if (fallbackApActive || wifiConnecting) {
    const bool blink = ((nowMs / 500) % 2) == 0;
    setBuiltinLed(blink);
    return;
  }

  setBuiltinLed(false);
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

uint8_t clampPercent(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return static_cast<uint8_t>(value);
}

uint8_t percentUsed(uint32_t used, uint32_t total) {
  if (total == 0) {
    return 0;
  }
  return clampPercent(static_cast<int>((used * 100UL) / total));
}

String formatBytesHuman(uint64_t bytes) {
  const char* unit = "B";
  double value = static_cast<double>(bytes);

  if (bytes >= 1024ULL * 1024ULL) {
    unit = "MB";
    value = value / (1024.0 * 1024.0);
  } else if (bytes >= 1024ULL) {
    unit = "KB";
    value = value / 1024.0;
  }

  char buffer[24] = {};
  snprintf(buffer, sizeof(buffer), "%.1f %s", value, unit);
  return String(buffer);
}

uint32_t getHeapTotalBytes() {
  return ESP.getHeapSize();
}

uint32_t getHeapUsedBytes() {
  const uint32_t total = getHeapTotalBytes();
  const uint32_t free = ESP.getFreeHeap();
  return total > free ? total - free : 0;
}

uint8_t getHeapUsedPercent() {
  return percentUsed(getHeapUsedBytes(), getHeapTotalBytes());
}

uint32_t getSketchCapacityBytes() {
  return ESP.getSketchSize() + ESP.getFreeSketchSpace();
}

uint8_t getSketchUsedPercent() {
  return percentUsed(ESP.getSketchSize(), getSketchCapacityBytes());
}

int getWifiRssi() {
  return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127;
}

uint8_t getWifiQualityPercent() {
  if (WiFi.status() != WL_CONNECTED) {
    return 0;
  }

  const int rssi = WiFi.RSSI();
  if (rssi <= -100) {
    return 0;
  }
  if (rssi >= -50) {
    return 100;
  }
  return clampPercent(2 * (rssi + 100));
}

String formatUptime(uint32_t uptimeMs) {
  uint32_t totalSeconds = uptimeMs / 1000;
  const uint32_t days = totalSeconds / 86400;
  totalSeconds %= 86400;
  const uint32_t hours = totalSeconds / 3600;
  totalSeconds %= 3600;
  const uint32_t minutes = totalSeconds / 60;
  const uint32_t seconds = totalSeconds % 60;

  char buffer[32] = {};
  if (days > 0) {
    snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu",
             static_cast<unsigned long>(days),
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
  } else {
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
  }

  return String(buffer);
}

String formatDateTime(time_t epoch) {
  if (epoch <= 0) {
    return String("not_synced");
  }

  struct tm timeInfo {};
  if (!localtime_r(&epoch, &timeInfo)) {
    return String("not_synced");
  }

  char buffer[20] = {};
  strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeInfo);
  return String(buffer);
}

String getRtcString() {
  if (!timeSynced) {
    return String("not_synced");
  }

  return formatDateTime(time(nullptr));
}

String formatFloatValue(float value, uint8_t decimals, const char* unit = nullptr) {
  if (isnan(value)) {
    return String("N/A");
  }

  char buffer[32] = {};
  if (unit != nullptr && unit[0] != '\0') {
    snprintf(buffer, sizeof(buffer), "%.*f %s", decimals, value, unit);
  } else {
    snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  }
  return String(buffer);
}

uint8_t parseHexByteValue(const String& text, uint8_t fallback) {
  String trimmed = text;
  trimmed.trim();
  trimmed.toLowerCase();
  if (trimmed.startsWith("0x")) {
    trimmed.remove(0, 2);
  } else if (trimmed.startsWith("x")) {
    trimmed.remove(0, 1);
  }
  if (trimmed.length() == 0) {
    return fallback;
  }
  return static_cast<uint8_t>(strtoul(trimmed.c_str(), nullptr, 16));
}

String formatHexByte(uint8_t value) {
  char buffer[8] = {};
  snprintf(buffer, sizeof(buffer), "0x%02X", value);
  return String(buffer);
}

String getDeviceUid() {
  const uint64_t chipId = ESP.getEfuseMac();
  char suffix[7] = {};
  snprintf(suffix, sizeof(suffix), "%06X", static_cast<uint32_t>(chipId & 0xFFFFFFULL));
  return String("ESP32-RS485-") + suffix;
}

String getMqttBaseTopic() {
  if (currentMqttConfig.base_topic[0] == '\0') {
    return String("pm1611");
  }
  return String(currentMqttConfig.base_topic);
}

String getMqttClientId() {
  if (currentMqttConfig.client_id[0] == '\0') {
    return getDeviceUid();
  }
  return String(currentMqttConfig.client_id);
}

uint32_t currentDayKeyFromEpoch(time_t epoch) {
  if (epoch <= 0) {
    return 0;
  }

  struct tm timeInfo {};
  if (!localtime_r(&epoch, &timeInfo)) {
    return 0;
  }

  return static_cast<uint32_t>((timeInfo.tm_year + 1900) * 10000UL +
                               (timeInfo.tm_mon + 1) * 100UL +
                               timeInfo.tm_mday);
}

String formatDayKey(uint32_t dayKey) {
  if (dayKey == 0) {
    return String("unknown");
  }

  const uint16_t year = dayKey / 10000UL;
  const uint8_t month = (dayKey / 100UL) % 100;
  const uint8_t day = dayKey % 100;

  char buffer[16] = {};
  snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", year, month, day);
  return String(buffer);
}

float currentMeterEnergyDelta() {
  if (isnan(meterSnapshot.energy)) {
    return NAN;
  }

  return meterSnapshot.energy;
}

void setRelayOutput(bool on) {
  const bool outputHigh = PinMap::kRelayOutputActiveHigh ? on : !on;
  digitalWrite(PinMap::kRelayOutput, outputHigh ? HIGH : LOW);
  relayActualState = on;
}

void loadHistoryRuntime() {
  Preferences prefs;
  if (!prefs.begin("history_rt", true)) {
    return;
  }

  historyRuntime.dayKey = prefs.getUInt("day_key", 0);
  historyRuntime.baselineEnergy = prefs.getFloat("baseline", NAN);
  for (size_t i = 0; i < 7; i++) {
    char key[8] = {};
    snprintf(key, sizeof(key), "d%u", static_cast<unsigned>(i));
    historyRuntime.daily[i] = prefs.getFloat(key, 0.0f);
  }
  prefs.end();
  historyRuntime.loaded = true;
}

void saveHistoryRuntime() {
  Preferences prefs;
  if (!prefs.begin("history_rt", false)) {
    return;
  }

  prefs.putUInt("day_key", historyRuntime.dayKey);
  prefs.putFloat("baseline", historyRuntime.baselineEnergy);
  for (size_t i = 0; i < 7; i++) {
    char key[8] = {};
    snprintf(key, sizeof(key), "d%u", static_cast<unsigned>(i));
    prefs.putFloat(key, historyRuntime.daily[i]);
  }
  prefs.end();
  historyRuntime.dirty = false;
  historyRuntime.lastPersistMs = millis();
}

String buildEnergyHistoryJson() {
  String body;
  body.reserve(128);
  body += F("[");
  for (size_t i = 0; i < 7; i++) {
    if (i > 0) {
      body += F(",");
    }
    body += F("\"");
    body += String(historyRuntime.daily[i], 3);
    body += F("\"");
  }
  body += F("]");
  return body;
}

void updateHistoryRuntime(uint32_t nowMs) {
  if (!currentHistoryConfig.enabled) {
    return;
  }

  if (!historyRuntime.loaded) {
    loadHistoryRuntime();
  }

  const float energy = currentMeterEnergyDelta();
  if (isnan(energy)) {
    return;
  }

  const uint32_t dayKey = currentDayKeyFromEpoch(time(nullptr));
  if (historyRuntime.dayKey == 0 || historyRuntime.baselineEnergy != historyRuntime.baselineEnergy) {
    historyRuntime.dayKey = dayKey;
    historyRuntime.baselineEnergy = energy;
    historyRuntime.daily[0] = 0.0f;
    historyRuntime.dirty = true;
  } else if (dayKey != 0 && dayKey != historyRuntime.dayKey) {
    const float completedDay = energy - historyRuntime.baselineEnergy;
    for (int i = 6; i > 0; --i) {
      historyRuntime.daily[i] = historyRuntime.daily[i - 1];
    }
    historyRuntime.daily[0] = completedDay > 0 ? completedDay : 0.0f;
    historyRuntime.dayKey = dayKey;
    historyRuntime.baselineEnergy = energy;
    historyRuntime.dirty = true;
  } else {
    const float currentDay = energy - historyRuntime.baselineEnergy;
    historyRuntime.daily[0] = currentDay > 0 ? currentDay : 0.0f;
    historyRuntime.dirty = true;
  }

  if (historyRuntime.dirty && nowMs - historyRuntime.lastPersistMs >= 30000UL) {
    saveHistoryRuntime();
  }
}

String buildRelayStateText() {
  if (!currentProtectionConfig.relay_enabled) {
    return String("disabled");
  }
  if (relayLockedOut) {
    return String("locked");
  }
  return relayActualState ? String("on") : String("off");
}

bool canTurnRelayOn() {
  if (!currentProtectionConfig.relay_enabled) {
    return false;
  }
  if (relayLockedOut && millis() < relayTripUntilMs) {
    return false;
  }
  return true;
}

void requestRelayState(bool on, const char* reason) {
  if (!currentProtectionConfig.relay_enabled) {
    setRelayOutput(false);
    relayRequestedState = false;
    Serial.println("Relay request ignored: relay disabled");
    return;
  }

  if (on && !canTurnRelayOn()) {
    Serial.print("Relay ON blocked: ");
    Serial.println(reason);
    return;
  }

  relayRequestedState = on;
  setRelayOutput(on);
  Serial.print("Relay state set: ");
  Serial.print(on ? "ON" : "OFF");
  Serial.print(" reason=");
  Serial.println(reason);
}

void tripRelay(const char* reason, uint32_t nowMs) {
  relayLockedOut = true;
  relayTripUntilMs = nowMs + static_cast<uint32_t>(currentProtectionConfig.auto_retry_delay_sec) * 1000UL;
  relayOvercurrentSinceMs = 0;
  relayRequestedState = false;
  setRelayOutput(false);
  Serial.print("Relay tripped: ");
  Serial.println(reason);
}

void updateProtectionRuntime(uint32_t nowMs) {
  if (!currentProtectionConfig.relay_enabled) {
    if (relayActualState) {
      setRelayOutput(false);
    }
    relayRequestedState = false;
    relayLockedOut = false;
    relayTripUntilMs = 0;
    relayOvercurrentSinceMs = 0;
    return;
  }

  if (relayLockedOut) {
    if (currentProtectionConfig.auto_retry_enabled && nowMs >= relayTripUntilMs) {
      const bool safeCurrent = isnan(meterSnapshot.current) || meterSnapshot.current <= currentProtectionConfig.current_limit_a;
      const bool meterOk = meterSnapshot.online;
      if (safeCurrent && meterOk) {
        relayLockedOut = false;
        if (relayRequestedState) {
          setRelayOutput(true);
        }
        Serial.println("Relay auto-retry unlocked");
      }
    }
    if (relayLockedOut) {
      setRelayOutput(false);
      return;
    }
  }

  const bool meterStale = meterSnapshot.lastSuccessMs > 0 && nowMs - meterSnapshot.lastSuccessMs > currentProtectionConfig.trip_delay_ms;
  if (currentProtectionConfig.trip_on_meter_stale && !meterSnapshot.online && meterStale) {
    tripRelay("meter stale", nowMs);
    return;
  }

  if (relayRequestedState) {
    const bool currentValid = !isnan(meterSnapshot.current);
    if (currentValid && meterSnapshot.current > currentProtectionConfig.current_limit_a) {
      if (relayOvercurrentSinceMs == 0) {
        relayOvercurrentSinceMs = nowMs;
      } else if (nowMs - relayOvercurrentSinceMs >= currentProtectionConfig.trip_delay_ms) {
        tripRelay("overcurrent", nowMs);
        return;
      }
    } else {
      relayOvercurrentSinceMs = 0;
    }
  } else {
    relayOvercurrentSinceMs = 0;
  }

  setRelayOutput(relayRequestedState);
}

String mqttCommandTopic() {
  return getMqttBaseTopic() + "/cmd";
}

String mqttStateTopic() {
  return getMqttBaseTopic() + "/state";
}

String mqttTelemetryTopic() {
  return getMqttBaseTopic() + "/telemetry";
}

void publishMqttState();

void mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
  String incomingTopic = String(topic ? topic : "");
  String message;
  message.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }
  message.trim();
  message.toLowerCase();

  Serial.print("MQTT message: ");
  Serial.print(incomingTopic);
  Serial.print(" -> ");
  Serial.println(message);

  if (message.indexOf("set_relay") >= 0 || message.indexOf("\"action\":\"set_relay\"") >= 0) {
    requestRelayState(true, "mqtt");
    publishMqttState();
    return;
  }
  if (message.indexOf("reset_relay") >= 0 || message.indexOf("\"action\":\"reset_relay\"") >= 0) {
    requestRelayState(false, "mqtt");
    publishMqttState();
    return;
  }
}

void configureMqttClient() {
  if (currentMqttConfig.port == 8883) {
    mqttSecureTransport.setInsecure();
    mqttClient.setClient(mqttSecureTransport);
  } else {
    mqttClient.setClient(mqttTransport);
  }
  mqttClient.setServer(currentMqttConfig.host, currentMqttConfig.port);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(30);
  mqttClient.setCallback(mqttMessageCallback);
  mqttClientConfigured = true;
}

bool connectMqtt(uint32_t nowMs) {
  if (!currentMqttConfig.enabled || WiFi.status() != WL_CONNECTED || currentMqttConfig.host[0] == '\0') {
    mqttConnected = false;
    return false;
  }

  if (!mqttClientConfigured) {
    configureMqttClient();
  }

  if (mqttClient.connected()) {
    mqttConnected = true;
    return true;
  }

  if (nowMs - mqttLastReconnectAttemptMs < 5000UL) {
    return false;
  }
  mqttLastReconnectAttemptMs = nowMs;

  const String clientId = getMqttClientId();
  const String willTopic = mqttStateTopic();
  const String willPayload = F("{\"connected\":false}");

  bool ok = false;
  if (currentMqttConfig.username[0] != '\0' || currentMqttConfig.password[0] != '\0') {
    ok = mqttClient.connect(clientId.c_str(),
                            currentMqttConfig.username,
                            currentMqttConfig.password,
                            willTopic.c_str(),
                            0,
                            true,
                            willPayload.c_str());
  } else {
    ok = mqttClient.connect(clientId.c_str(),
                            willTopic.c_str(),
                            0,
                            true,
                            willPayload.c_str());
  }

  if (!ok) {
    mqttConnected = false;
    Serial.print("MQTT connect failed, rc=");
    Serial.println(mqttClient.state());
    return false;
  }

  mqttClient.subscribe(mqttCommandTopic().c_str());
  mqttClient.subscribe((getMqttBaseTopic() + "/relay/set").c_str());
  mqttConnected = true;
  Serial.println("MQTT connected");
  publishMqttState();
  return true;
}

String buildMqttPayload() {
  String body;
  body.reserve(512);
  body += F("{\"uid\":\"");
  body += getDeviceUid();
  body += F("\",\"rtc\":\"");
  body += getRtcString();
  body += F("\",\"relay_state\":\"");
  body += relayActualState ? F("1") : F("0");
  body += F("\",\"meter_data\":{");
  body += F("\"voltage\":[\"");
  body += String(meterSnapshot.voltage, 2);
  body += F("\",\"V\"],\"current\":[\"");
  body += String(meterSnapshot.current, 2);
  body += F("\",\"A\"],\"power\":[\"");
  body += String(meterSnapshot.power, 2);
  body += F("\",\"kW\"],\"frequency\":[\"");
  body += String(meterSnapshot.frequency, 2);
  body += F("\",\"Hz\"],\"pf\":[\"");
  body += String(meterSnapshot.pf, 2);
  body += F("\",\"\"],\"energy\":[\"");
  body += String(meterSnapshot.energy, 3);
  body += F("\",\"kWh\"],\"co2\":[\"");
  const float co2 = meterSnapshot.valid ? (meterSnapshot.energy * currentDeviceConfig.co2_factor_kg_per_kwh) : 0.0f;
  body += String(co2, 3);
  body += F("\",\"kg\"]},\"energy_history\":");
  body += buildEnergyHistoryJson();
  body += F("}");
  return body;
}

void publishMqttState() {
  if (!mqttClient.connected()) {
    return;
  }

  const String payload = buildMqttPayload();
  mqttClient.publish(mqttStateTopic().c_str(), payload.c_str(), true);
  mqttClient.publish(mqttTelemetryTopic().c_str(), payload.c_str(), false);
}

bool testMqttBrokerConnection(String& errorOut) {
  if (WiFi.status() != WL_CONNECTED) {
    errorOut = "wifi_not_connected";
    return false;
  }
  if (!currentMqttConfig.enabled) {
    errorOut = "mqtt_disabled";
    return false;
  }
  if (currentMqttConfig.host[0] == '\0') {
    errorOut = "missing_host";
    return false;
  }

  if (!mqttClientConfigured) {
    configureMqttClient();
  } else {
    mqttClient.setServer(currentMqttConfig.host, currentMqttConfig.port);
  }

  const String clientId = getMqttClientId();
  const String willTopic = mqttStateTopic();
  const String willPayload = F("{\"connected\":false}");

  bool ok = false;
  if (currentMqttConfig.username[0] != '\0' || currentMqttConfig.password[0] != '\0') {
    ok = mqttClient.connect(clientId.c_str(),
                            currentMqttConfig.username,
                            currentMqttConfig.password,
                            willTopic.c_str(),
                            0,
                            true,
                            willPayload.c_str());
  } else {
    ok = mqttClient.connect(clientId.c_str(),
                            willTopic.c_str(),
                            0,
                            true,
                            willPayload.c_str());
  }

  if (!ok) {
    errorOut = String("rc_") + String(mqttClient.state());
    mqttConnected = false;
    return false;
  }

  mqttConnected = true;
  mqttClient.disconnect();
  mqttConnected = false;
  errorOut = "ok";
  return true;
}

void updateMqttRuntime(uint32_t nowMs) {
  if (!currentMqttConfig.enabled) {
    mqttConnected = false;
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    mqttConnected = false;
    return;
  }

  if (!mqttClientConfigured) {
    configureMqttClient();
  } else {
    mqttClient.setServer(currentMqttConfig.host, currentMqttConfig.port);
  }

  if (!mqttClient.connected()) {
    connectMqtt(nowMs);
  } else {
    mqttClient.loop();
    mqttConnected = true;
  }

  if (mqttClient.connected() && nowMs - mqttLastPublishMs >= static_cast<uint32_t>(currentMqttConfig.publish_interval_sec) * 1000UL) {
    mqttLastPublishMs = nowMs;
    publishMqttState();
  }
}

U8G2* getActiveDisplayDriver() {
  if (currentDisplayConfig.type == 1) {
    return &displaySsd1306;
  }
  return &displaySt7567;
}

void initDisplayRuntime() {
  if (!currentDisplayConfig.enabled) {
    displayReady = false;
    return;
  }

  U8G2* driver = getActiveDisplayDriver();
  if (driver == nullptr) {
    displayReady = false;
    return;
  }

  driver->begin();
  driver->setContrast(currentDisplayConfig.brightness);
  displayReady = true;
  Serial.println("LCD display ready");
}

void drawDisplayPage(U8G2* driver, uint8_t pageIndex) {
  driver->clearBuffer();
  driver->setFont(u8g2_font_6x12_tf);

  switch (pageIndex % 4) {
    case 0:
      driver->drawStr(0, 12, "PM1611 RS485");
      driver->drawStr(0, 28, formatFloatValue(meterSnapshot.voltage, 1, "V").c_str());
      driver->drawStr(0, 42, formatFloatValue(meterSnapshot.current, 1, "A").c_str());
      driver->drawStr(0, 56, formatFloatValue(meterSnapshot.power, 1, "kW").c_str());
      break;
    case 1:
      driver->drawStr(0, 12, "Energy / Power");
      driver->drawStr(0, 28, formatFloatValue(meterSnapshot.energy, 3, "kWh").c_str());
      driver->drawStr(0, 42, formatFloatValue(meterSnapshot.frequency, 1, "Hz").c_str());
      driver->drawStr(0, 56, formatFloatValue(meterSnapshot.pf, 2, "PF").c_str());
      break;
    case 2:
      driver->drawStr(0, 12, "Network / MQTT");
      driver->drawStr(0, 28, wifiConnected ? "STA connected" : "STA offline");
      driver->drawStr(0, 42, mqttConnected ? "MQTT connected" : "MQTT offline");
      driver->drawStr(0, 56, getMqttBaseTopic().c_str());
      break;
    default:
      driver->drawStr(0, 12, "Relay / Fault");
      driver->drawStr(0, 28, buildRelayStateText().c_str());
      driver->drawStr(0, 42, relayLockedOut ? "lockout active" : "ready");
      driver->drawStr(0, 56, meterSnapshot.online ? "meter ok" : "meter off");
      break;
  }

  driver->sendBuffer();
}

void updateDisplayRuntime(uint32_t nowMs) {
  if (!currentDisplayConfig.enabled) {
    return;
  }

  if (!displayReady) {
    initDisplayRuntime();
    if (!displayReady) {
      return;
    }
  }

  const uint32_t intervalMs = static_cast<uint32_t>(currentDisplayConfig.rotation_interval_sec) * 1000UL;
  if (intervalMs > 0 && nowMs - displayLastUpdateMs < intervalMs) {
    return;
  }

  displayLastUpdateMs = nowMs;
  displayPage = (displayPage + 1) % 4;
  U8G2* driver = getActiveDisplayDriver();
  if (driver != nullptr) {
    drawDisplayPage(driver, displayPage);
  }
}

void loadFeatureRuntime() {
  loadHistoryRuntime();
  setRelayOutput(false);
  relayRequestedState = false;
  relayActualState = false;
  relayLockedOut = false;
  relayTripUntilMs = 0;
  relayOvercurrentSinceMs = 0;
}

void resetMeterSnapshot() {
  meterSnapshot.online = false;
  meterSnapshot.valid = false;
  meterSnapshot.voltage = NAN;
  meterSnapshot.current = NAN;
  meterSnapshot.power = NAN;
  meterSnapshot.frequency = NAN;
  meterSnapshot.pf = NAN;
  meterSnapshot.energy = NAN;
}

uint16_t modbusCrc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

uint32_t serialConfigFromModbus(const ModbusConfig& cfg) {
  if (cfg.parity == 0 && cfg.stop_bits == 1) return SERIAL_8E1;
  if (cfg.parity == 0 && cfg.stop_bits == 2) return SERIAL_8E2;
  if (cfg.parity == 1 && cfg.stop_bits == 1) return SERIAL_8O1;
  if (cfg.parity == 1 && cfg.stop_bits == 2) return SERIAL_8O2;
  if (cfg.parity == 2 && cfg.stop_bits == 2) return SERIAL_8N2;
  return SERIAL_8N1;
}

void applyModbusPortConfig(const ModbusConfig& cfg) {
  const bool needsInit = !modbusPortReady ||
                         currentModbusConfig.baudrate != cfg.baudrate ||
                         currentModbusConfig.parity != cfg.parity ||
                         currentModbusConfig.stop_bits != cfg.stop_bits;
  if (!needsInit) {
    Serial2.setTimeout(cfg.timeout_ms);
    currentModbusConfig = cfg;
    return;
  }

  Serial2.end();
  delay(20);
  Serial2.begin(cfg.baudrate, serialConfigFromModbus(cfg), PinMap::kRs485Rx, PinMap::kRs485Tx);
  currentModbusConfig = cfg;
  currentModbusConfigLoaded = true;
  modbusPortReady = true;

  Serial.print("Modbus UART2 ready: baud=");
  Serial.print(cfg.baudrate);
  Serial.print(" slave=");
  Serial.print(cfg.slave_id);
  Serial.print(" parity=");
  Serial.print(cfg.parity);
  Serial.print(" stop_bits=");
  Serial.println(cfg.stop_bits);
  Serial2.setTimeout(cfg.timeout_ms);
  currentModbusConfig = cfg;
}

bool modbusReadHoldingRegisters(uint8_t slaveId, uint16_t startRegister, uint16_t quantity, uint8_t* response, size_t responseLen, uint16_t& exceptionCode, uint32_t timeoutMs) {
  exceptionCode = 0;
  if (quantity == 0 || quantity > 64 || response == nullptr || responseLen < quantity * 2) {
    return false;
  }

  uint8_t request[8];
  request[0] = slaveId;
  request[1] = 0x03;
  request[2] = static_cast<uint8_t>(startRegister >> 8);
  request[3] = static_cast<uint8_t>(startRegister & 0xFF);
  request[4] = static_cast<uint8_t>(quantity >> 8);
  request[5] = static_cast<uint8_t>(quantity & 0xFF);
  const uint16_t crc = modbusCrc16(request, 6);
  request[6] = static_cast<uint8_t>(crc & 0xFF);
  request[7] = static_cast<uint8_t>(crc >> 8);

  while (Serial2.available() > 0) {
    Serial2.read();
  }

  Serial2.write(request, sizeof(request));
  Serial2.flush();

  const uint32_t deadline = millis() + timeoutMs;
  while (Serial2.available() < 3 && millis() < deadline) {
    delay(1);
  }
  if (Serial2.available() < 3) {
    return false;
  }

  uint8_t header[3];
  if (Serial2.readBytes(header, sizeof(header)) != sizeof(header)) {
    return false;
  }

  if (header[0] != slaveId) {
    return false;
  }

  if (header[1] & 0x80) {
    while (Serial2.available() < 2 && millis() < deadline) {
      delay(1);
    }
    if (Serial2.available() < 2) {
      return false;
    }
    uint8_t exceptionCrc[2];
    if (Serial2.readBytes(exceptionCrc, sizeof(exceptionCrc)) != sizeof(exceptionCrc)) {
      return false;
    }
    exceptionCode = header[2];
    return false;
  }

  const uint8_t byteCount = header[2];
  if (byteCount != quantity * 2 || byteCount > responseLen) {
    return false;
  }

  while (Serial2.available() < byteCount + 2 && millis() < deadline) {
    delay(1);
  }
  if (Serial2.available() < byteCount + 2) {
    return false;
  }

  if (Serial2.readBytes(response, byteCount) != byteCount) {
    return false;
  }

  uint8_t crcBytes[2];
  if (Serial2.readBytes(crcBytes, sizeof(crcBytes)) != sizeof(crcBytes)) {
    return false;
  }

  uint8_t frame[3 + 64];
  frame[0] = header[0];
  frame[1] = header[1];
  frame[2] = header[2];
  memcpy(frame + 3, response, byteCount);
  const uint16_t expectedCrc = modbusCrc16(frame, 3 + byteCount);
  const uint16_t receivedCrc = static_cast<uint16_t>(crcBytes[0]) | (static_cast<uint16_t>(crcBytes[1]) << 8);
  return expectedCrc == receivedCrc;
}

float decodeFloat32BigEndian(const uint8_t* data) {
  const uint32_t bits = (static_cast<uint32_t>(data[0]) << 24) |
                        (static_cast<uint32_t>(data[1]) << 16) |
                        (static_cast<uint32_t>(data[2]) << 8) |
                        static_cast<uint32_t>(data[3]);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

bool readSchneiderFloat(uint8_t slaveId, uint16_t startRegister, float& value, uint32_t timeoutMs) {
  uint8_t buffer[4];
  uint16_t exceptionCode = 0;
  if (!modbusReadHoldingRegisters(slaveId, startRegister, 2, buffer, sizeof(buffer), exceptionCode, timeoutMs)) {
    return false;
  }

  value = decodeFloat32BigEndian(buffer);
  return true;
}

void pollModbusMeter(uint32_t nowMs) {
  const ModbusConfig cfg = currentModbusConfigLoaded ? currentModbusConfig : ConfigManager::loadModbusConfig();
  currentModbusConfig = cfg;
  currentModbusConfigLoaded = true;

  if (currentMode != AppMode::Normal || WiFi.status() != WL_CONNECTED) {
    resetMeterSnapshot();
    return;
  }

  applyModbusPortConfig(cfg);

  if (nowMs - lastModbusPollMs < cfg.poll_interval_ms) {
    return;
  }
  lastModbusPollMs = nowMs;

  meterSnapshot.lastPollMs = nowMs;
  meterSnapshot.online = false;
  meterSnapshot.valid = false;

  const uint8_t slaveId = cfg.slave_id == 0 ? 1 : cfg.slave_id;
  const uint16_t voltageReg = 3027;
  const uint16_t currentReg = 2999;
  const uint16_t powerReg = 3053;
  const uint16_t frequencyReg = 3109;
  const uint16_t energyReg = 2675;

  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float frequency = NAN;
  float energy = NAN;

  bool ok = true;
  ok = ok && readSchneiderFloat(slaveId, voltageReg, voltage, cfg.timeout_ms);
  ok = ok && readSchneiderFloat(slaveId, currentReg, current, cfg.timeout_ms);
  ok = ok && readSchneiderFloat(slaveId, powerReg, power, cfg.timeout_ms);
  ok = ok && readSchneiderFloat(slaveId, frequencyReg, frequency, cfg.timeout_ms);
  ok = ok && readSchneiderFloat(slaveId, energyReg, energy, cfg.timeout_ms);

  if (ok) {
    meterSnapshot.online = true;
    meterSnapshot.valid = true;
    meterSnapshot.lastSuccessMs = nowMs;
    meterSnapshot.voltage = voltage;
    meterSnapshot.current = current;
    meterSnapshot.power = power;
    meterSnapshot.frequency = frequency;
    meterSnapshot.energy = energy;
    Serial.print("Modbus meter ok: V=");
    Serial.print(voltage, 2);
    Serial.print(" I=");
    Serial.print(current, 2);
    Serial.print(" P=");
    Serial.print(power, 2);
    Serial.print(" F=");
    Serial.print(frequency, 2);
    Serial.print(" E=");
    Serial.println(energy, 3);
  } else {
    meterSnapshot.lastErrorMs = nowMs;
    meterSnapshot.lastErrorCode = 1;
    meterSnapshot.online = false;
    meterSnapshot.valid = false;
    Serial.println("Modbus meter read failed");
  }

  updateHistoryRuntime(nowMs);
}

void startNtpSync() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("NTP skipped: WiFi is not connected");
    return;
  }

  const char* timezone = currentDeviceConfig.timezone[0] != '\0' ? currentDeviceConfig.timezone : kNtpTimezone;
  const char* server1 = currentSystemConfig.ntp_server1[0] != '\0' ? currentSystemConfig.ntp_server1 : kNtpServer1;
  const char* server2 = currentSystemConfig.ntp_server2[0] != '\0' ? currentSystemConfig.ntp_server2 : kNtpServer2;

  Serial.print("NTP sync started: ");
  Serial.print(server1);
  Serial.print(", ");
  Serial.println(server2);

  configTzTime(timezone, server1, server2);
  ntpSyncStarted = true;
}

void restartNtpSync() {
  ntpSyncStarted = false;
  timeSynced = false;
  lastNtpSyncEpoch = 0;
  startNtpSync();
}

void checkNtpSync() {
  if (!ntpSyncStarted || timeSynced) {
    return;
  }

  struct tm timeInfo {};
  if (getLocalTime(&timeInfo, 0)) {
    if (timeInfo.tm_year > 120) {
      lastNtpSyncEpoch = time(nullptr);
      timeSynced = true;
      Serial.print("RTC synced: ");
      Serial.println(getRtcString());
    }
  }
}

const char* wifiStatusToString(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "ssid_not_available";
    case WL_SCAN_COMPLETED:
      return "scan_completed";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "unknown";
  }
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

String jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); i++) {
    const char c = value.charAt(i);
    if (c == '"' || c == '\\') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\n') {
      escaped += "\\n";
    } else if (c == '\r') {
      escaped += "\\r";
    } else if (c == '\t') {
      escaped += "\\t";
    } else {
      escaped += c;
    }
  }

  return escaped;
}

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); i++) {
    const char c = value.charAt(i);
    if (c == '&') {
      escaped += F("&amp;");
    } else if (c == '<') {
      escaped += F("&lt;");
    } else if (c == '>') {
      escaped += F("&gt;");
    } else if (c == '"') {
      escaped += F("&quot;");
    } else if (c == '\'') {
      escaped += F("&#39;");
    } else {
      escaped += c;
    }
  }

  return escaped;
}

void sendNoCacheHeader() {
  configServer.sendHeader("Cache-Control", "no-store");
}

void loadWifiConfig() {
  Preferences prefs;
  if (!prefs.begin(kNetworkPrefsNamespace, false)) {
    Serial.println("NVS network open failed");
    return;
  }

  savedWifiSsid = prefs.isKey(kWifiSsidKey) ? prefs.getString(kWifiSsidKey, "") : "";
  savedWifiPassword = prefs.isKey(kWifiPassKey) ? prefs.getString(kWifiPassKey, "") : "";
  prefs.end();

  hasSavedWifi = savedWifiSsid.length() > 0;
  Serial.print("Saved WiFi config: ");
  Serial.println(hasSavedWifi ? savedWifiSsid : "(none)");
}

bool saveWifiConfig(const String& ssid, const String& password) {
  Preferences prefs;
  if (!prefs.begin(kNetworkPrefsNamespace, false)) {
    Serial.println("NVS network write open failed");
    return false;
  }

  const size_t ssidBytes = prefs.putString(kWifiSsidKey, ssid);
  const size_t passBytes = prefs.putString(kWifiPassKey, password);
  prefs.end();

  if (ssidBytes == 0) {
    Serial.println("NVS WiFi SSID write failed");
    return false;
  }

  savedWifiSsid = ssid;
  savedWifiPassword = password;
  hasSavedWifi = true;

  Serial.print("Saved WiFi SSID to NVS: ");
  Serial.println(savedWifiSsid);
  Serial.print("Saved WiFi password length: ");
  Serial.println(passBytes > 0 ? savedWifiPassword.length() : 0);
  return true;
}

void connectToSavedWifi() {
  if (!hasSavedWifi) {
    Serial.println("WiFi STA skipped: no saved credentials");
    return;
  }

  Serial.print("WiFi STA connecting to: ");
  Serial.println(savedWifiSsid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedWifiSsid.c_str(), savedWifiPassword.c_str());
}

void handleWiFiLifecycle(uint32_t nowMs) {
  if (currentMode == AppMode::Config) {
    return;
  }

  if (wifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting = false;
      wifiConnected = true;
      Serial.println("WiFi STA connected");
      Serial.print("STA IP: ");
      Serial.println(WiFi.localIP());
      startConfigWebServer();
      startNtpSync();
      if (fallbackApActive) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        fallbackApActive = false;
        configApStarted = false;
        setBuiltinLed(false);
        Serial.println("STA connected, shutting down fallback AP.");
      }
    } else if (nowMs - wifiConnectStartedMs >= kWifiConnectTimeoutMs && !fallbackApActive) {
      Serial.print("WiFi STA connection timeout after ");
      Serial.print(kWifiConnectTimeoutMs);
      Serial.println(" ms. Starting fallback AP.");
      startConfigAccessPoint(false);
      fallbackApActive = true;
      // setBuiltinLed(true);
    }
  } else if (wifiConnected) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      Serial.println("WiFi STA disconnected! Attempting auto-reconnect...");
      wifiConnecting = true;
      wifiConnectStartedMs = nowMs;
      connectToSavedWifi();
    } else {
      checkNtpSync();
    }
  }
}

void printWebUiAddresses() {
  if (configApStarted) {
    Serial.print("Open AP Web UI: http://");
    Serial.print(WiFi.softAPIP());
    Serial.println("/");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Open STA Web UI: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
  }
}
#include "WebUiPages.inc"

// ============================================================================
// CONFIG PAGES - Device, MQTT, Modbus, Protection, Display, History, System
// ============================================================================
// Author: Claude (AI Assistant) @ 2026-05-30
// Provides web UI pages for configuring all device settings
// ============================================================================

String buildDeviceConfigPage() {
  const DeviceConfig& cfg = currentDeviceConfig;
  String page = buildPageHeader("Device Configuration");
  page.reserve(4000);
  page += F("<section class=\"panel\"><h2>Device Settings</h2>");
  page += F("<label for=\"device_name\">Device Name</label><input id=\"device_name\" maxlength=\"64\" placeholder=\"Device name\" value=\"");
  page += htmlEscape(String(cfg.device_name));
  page += F("\"><label for=\"hostname\">Hostname</label><input id=\"hostname\" maxlength=\"64\" placeholder=\"Hostname\" value=\"");
  page += htmlEscape(String(cfg.hostname));
  page += F("\"><label for=\"timezone\">Timezone</label><input id=\"timezone\" maxlength=\"32\" placeholder=\"e.g., WIB-7\" value=\"");
  page += htmlEscape(String(cfg.timezone));
  page += F("\"><label for=\"co2_factor\">CO2 Factor (kg/kWh)</label><input id=\"co2_factor\" type=\"number\" min=\"0\" max=\"255\" value=\"");
  page += String(cfg.co2_factor_kg_per_kwh);
  page += F("\"><div class=\"actions\"><button onclick=\"saveDeviceConfig()\">Save Device Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section></main>");
  page += F("<script>async function saveDeviceConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("device_name:document.getElementById('device_name').value,");
  page += F("hostname:document.getElementById('hostname').value,");
  page += F("timezone:document.getElementById('timezone').value,");
  page += F("co2_factor:document.getElementById('co2_factor').value");
  page += F("});try{const r=await fetch('/api/device/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});");
  page += F("const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}catch(e){s.textContent='Save failed'}}</script>");
  page += buildPageFooter();
  return page;
}

String buildMqttConfigPage() {
  const MqttConfig& cfg = currentMqttConfig;
  String page = buildPageHeader("MQTT Configuration");
  page.reserve(5000);
  page += F("<section class=\"panel\"><h2>MQTT Broker Settings</h2>");
  page += F("<label><input type=\"checkbox\" id=\"mqtt_enabled\"");
  page += cfg.enabled ? F(" checked") : F("");
  page += F("> Enable MQTT</label>");
  page += F("<label for=\"mqtt_host\">Broker Host</label><input id=\"mqtt_host\" maxlength=\"64\" placeholder=\"localhost\" value=\"");
  page += htmlEscape(String(cfg.host));
  page += F("\"><label for=\"mqtt_port\">Broker Port</label><input id=\"mqtt_port\" type=\"number\" min=\"1\" max=\"65535\" value=\"");
  page += String(cfg.port);
  page += F("\"><label for=\"mqtt_username\">Username</label><input id=\"mqtt_username\" maxlength=\"64\" placeholder=\"username\" value=\"");
  page += htmlEscape(String(cfg.username));
  page += F("\"><label for=\"mqtt_password\">Password</label><input id=\"mqtt_password\" type=\"password\" maxlength=\"64\" value=\"");
  page += htmlEscape(String(cfg.password));
  page += F("\"><label for=\"mqtt_client_id\">Client ID</label><input id=\"mqtt_client_id\" maxlength=\"64\" placeholder=\"pm1611\" value=\"");
  page += htmlEscape(String(cfg.client_id));
  page += F("\"><label for=\"mqtt_base_topic\">Base Topic</label><input id=\"mqtt_base_topic\" maxlength=\"64\" placeholder=\"pm1611\" value=\"");
  page += htmlEscape(String(cfg.base_topic));
  page += F("\"><label for=\"mqtt_publish_interval\">Publish Interval (sec)</label><input id=\"mqtt_publish_interval\" type=\"number\" min=\"1\" max=\"3600\" value=\"");
  page += String(cfg.publish_interval_sec);
  page += F("\"><div class=\"actions\"><button onclick=\"saveMqttConfig()\">Save MQTT Config</button><button type=\"button\" onclick=\"testMqttBroker()\">Test Broker</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section></main>");
  page += F("<script>async function saveMqttConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("mqtt_enabled:document.getElementById('mqtt_enabled').checked?'1':'0',");
  page += F("mqtt_host:document.getElementById('mqtt_host').value,mqtt_port:document.getElementById('mqtt_port').value,");
  page += F("mqtt_username:document.getElementById('mqtt_username').value,mqtt_password:document.getElementById('mqtt_password').value,");
  page += F("mqtt_client_id:document.getElementById('mqtt_client_id').value,mqtt_base_topic:document.getElementById('mqtt_base_topic').value,");
  page += F("mqtt_publish_interval:document.getElementById('mqtt_publish_interval').value});try{const r=await fetch('/api/mqtt/save',{method:'POST',");
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}");
  page += F("catch(e){s.textContent='Save failed'}}async function testMqttBroker(){const s=document.getElementById('saveState');s.textContent='Testing broker...';try{const r=await fetch('/api/mqtt/test',{method:'POST'});const d=await r.json();s.textContent=d.ok?'Broker connection OK':('Broker test failed: '+(d.error||'unknown'))}catch(e){s.textContent='Broker test failed'}}</script>");
  page += buildPageFooter();
  return page;
}

String buildModbusConfigPage() {
  const ModbusConfig& cfg = currentModbusConfig;
  String page = buildPageHeader("Modbus Configuration");
  page.reserve(4500);
  page += F("<section class=\"panel\"><h2>Modbus RTU Settings</h2>");
  page += F("<label for=\"modbus_baudrate\">Baudrate</label><select id=\"modbus_baudrate\">");
  page += F("<option value=\"9600\"");
  page += cfg.baudrate == 9600 ? F(" selected") : F("");
  page += F(">9600</option><option value=\"19200\"");
  page += cfg.baudrate == 19200 ? F(" selected") : F("");
  page += F(">19200</option><option value=\"38400\"");
  page += cfg.baudrate == 38400 ? F(" selected") : F("");
  page += F(">38400</option></select>");
  page += F("<label for=\"modbus_slave_id\">Slave Device Address</label><input id=\"modbus_slave_id\" type=\"number\" min=\"1\" max=\"247\" value=\"");
  page += String(cfg.slave_id);
  page += F("\"><label for=\"modbus_parity\">Parity</label><select id=\"modbus_parity\">");
  page += F("<option value=\"0\"");
  page += cfg.parity == 0 ? F(" selected") : F("");
  page += F(">Even</option><option value=\"1\"");
  page += cfg.parity == 1 ? F(" selected") : F("");
  page += F(">Odd</option><option value=\"2\"");
  page += cfg.parity == 2 ? F(" selected") : F("");
  page += F(">None</option></select>");
  page += F("<label for=\"modbus_stop_bits\">Stop Bits</label><select id=\"modbus_stop_bits\">");
  page += F("<option value=\"1\"");
  page += cfg.stop_bits == 1 ? F(" selected") : F("");
  page += F(">1</option><option value=\"2\"");
  page += cfg.stop_bits == 2 ? F(" selected") : F("");
  page += F(">2</option></select>");
  page += F("<label for=\"modbus_poll_interval\">Poll Interval (ms)</label><input id=\"modbus_poll_interval\" type=\"number\" min=\"100\" max=\"10000\" value=\"");
  page += String(cfg.poll_interval_ms);
  page += F("\"><label for=\"modbus_timeout\">Timeout (ms)</label><input id=\"modbus_timeout\" type=\"number\" min=\"100\" max=\"5000\" value=\"");
  page += String(cfg.timeout_ms);
  page += F("\"><label for=\"modbus_retry_count\">Retry Count</label><input id=\"modbus_retry_count\" type=\"number\" min=\"0\" max=\"10\" value=\"");
  page += String(cfg.retry_count);
  page += F("\"><label for=\"modbus_profile\">Meter Profile</label><select id=\"modbus_profile\"><option value=\"0\"");
  page += cfg.meter_profile == 0 ? F(" selected") : F("");
  page += F(">Schneider EM6400 / PM2xxx</option><option value=\"1\"");
  page += cfg.meter_profile == 1 ? F(" selected") : F("");
  page += F(">Generic float32</option></select>");
  page += F("<div class=\"actions\"><button onclick=\"saveModbusConfig()\">Save Modbus Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section></main>");
  page += F("<script>async function saveModbusConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("modbus_baudrate:document.getElementById('modbus_baudrate').value,modbus_slave_id:document.getElementById('modbus_slave_id').value,");
  page += F("modbus_parity:document.getElementById('modbus_parity').value,modbus_stop_bits:document.getElementById('modbus_stop_bits').value,");
  page += F("modbus_poll_interval:document.getElementById('modbus_poll_interval').value,modbus_timeout:document.getElementById('modbus_timeout').value,");
  page += F("modbus_retry_count:document.getElementById('modbus_retry_count').value,modbus_profile:document.getElementById('modbus_profile').value});");
  page += F("try{const r=await fetch('/api/modbus/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});");
  page += F("const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}catch(e){s.textContent='Save failed'}}</script>");
  page += buildPageFooter();
  return page;
}

String buildProtectionConfigPage() {
  const ProtectionConfig& cfg = currentProtectionConfig;
  String page = buildPageHeader("Protection Configuration");
  page.reserve(4000);
  page += F("<section class=\"panel\"><h2>Relay Protection Settings</h2>");
  page += F("<label><input type=\"checkbox\" id=\"relay_enabled\"");
  page += cfg.relay_enabled ? F(" checked") : F("");
  page += F("> Enable Relay Output</label>");
  page += F("<label for=\"current_limit\">Current Limit (A)</label><input id=\"current_limit\" type=\"number\" min=\"1\" max=\"63\" value=\"");
  page += String(cfg.current_limit_a);
  page += F("\"><label for=\"trip_delay\">Trip Delay (ms)</label><input id=\"trip_delay\" type=\"number\" min=\"100\" max=\"10000\" value=\"");
  page += String(cfg.trip_delay_ms);
  page += F("\"><label for=\"reset_mode\">Reset Mode</label><select id=\"reset_mode\"><option value=\"0\"");
  page += cfg.reset_mode == 0 ? F(" selected") : F("");
  page += F(">Manual</option><option value=\"1\"");
  page += cfg.reset_mode == 1 ? F(" selected") : F("");
  page += F(">Auto</option></select>");
  page += F("<label><input type=\"checkbox\" id=\"auto_retry_enabled\"");
  page += cfg.auto_retry_enabled ? F(" checked") : F("");
  page += F("> Enable Auto-Retry</label>");
  page += F("<label for=\"auto_retry_delay\">Auto-Retry Delay (sec)</label><input id=\"auto_retry_delay\" type=\"number\" min=\"10\" max=\"3600\" value=\"");
  page += String(cfg.auto_retry_delay_sec);
  page += F("\"><label><input type=\"checkbox\" id=\"trip_on_stale\"");
  page += cfg.trip_on_meter_stale ? F(" checked") : F("");
  page += F("> Trip on Meter Stale</label>");
  page += F("<div class=\"actions\"><button onclick=\"saveProtectionConfig()\">Save Protection Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section></main>");
  page += F("<script>async function saveProtectionConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("relay_enabled:document.getElementById('relay_enabled').checked?'1':'0',current_limit:document.getElementById('current_limit').value,");
  page += F("trip_delay:document.getElementById('trip_delay').value,reset_mode:document.getElementById('reset_mode').value,");
  page += F("auto_retry_enabled:document.getElementById('auto_retry_enabled').checked?'1':'0',auto_retry_delay:document.getElementById('auto_retry_delay').value,");
  page += F("trip_on_stale:document.getElementById('trip_on_stale').checked?'1':'0'});try{const r=await fetch('/api/protection/save',{method:'POST',");
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':((d.detail||d.error)||'Save failed')}");
  page += F("catch(e){s.textContent='Save failed'}}</script>");
  page += buildPageFooter();
  return page;
}

String buildDisplayConfigPage() {
  const DisplayConfig& cfg = currentDisplayConfig;
  String page = buildPageHeader("Display Configuration");
  page.reserve(3500);
  page += F("<section class=\"panel\"><h2>LCD Display Settings</h2>");
  page += F("<label><input type=\"checkbox\" id=\"display_enabled\"");
  page += cfg.enabled ? F(" checked") : F("");
  page += F("> Enable LCD Display</label>");
  page += F("<label for=\"display_type\">Display Type</label><select id=\"display_type\"><option value=\"0\"");
  page += cfg.type == 0 ? F(" selected") : F("");
  page += F(">ST7567</option><option value=\"1\"");
  page += cfg.type == 1 ? F(" selected") : F("");
  page += F(">SSD1306</option></select>");
  page += F("<label for=\"i2c_address\">I2C Address (hex)</label><input id=\"i2c_address\" maxlength=\"4\" placeholder=\"0x3C\" value=\"");
  page += F("0x");
  page += String(cfg.i2c_address, HEX);
  page += F("\"><label for=\"rotation_interval\">Page Rotation (sec)</label><input id=\"rotation_interval\" type=\"number\" min=\"1\" max=\"60\" value=\"");
  page += String(cfg.rotation_interval_sec);
  page += F("\"><label for=\"brightness\">Brightness (0-255)</label><input id=\"brightness\" type=\"number\" min=\"0\" max=\"255\" value=\"");
  page += String(cfg.brightness);
  page += F("\"><div class=\"actions\"><button onclick=\"saveDisplayConfig()\">Save Display Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section></main>");
  page += F("<script>async function saveDisplayConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("display_enabled:document.getElementById('display_enabled').checked?'1':'0',display_type:document.getElementById('display_type').value,");
  page += F("i2c_address:document.getElementById('i2c_address').value,rotation_interval:document.getElementById('rotation_interval').value,");
  page += F("brightness:document.getElementById('brightness').value});try{const r=await fetch('/api/display/save',{method:'POST',");
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':((d.detail||d.error)||'Save failed')}");
  page += F("catch(e){s.textContent='Save failed'}}</script>");
  page += buildPageFooter();
  return page;
}

String buildHistoryConfigPage() {
  const HistoryConfig& cfg = currentHistoryConfig;
  String page = buildPageHeader("History Configuration");
  page.reserve(3000);
  page += F("<section class=\"panel\"><h2>Energy History Settings</h2>");
  page += F("<label><input type=\"checkbox\" id=\"history_enabled\"");
  page += cfg.enabled ? F(" checked") : F("");
  page += F("> Enable Energy History</label>");
  page += F("<label for=\"days_retained\">Days Retained</label><input id=\"days_retained\" type=\"number\" min=\"1\" max=\"31\" value=\"");
  page += String(cfg.days_retained);
  page += F("\"><label for=\"flush_interval\">Flush Interval (sec)</label><input id=\"flush_interval\" type=\"number\" min=\"60\" max=\"86400\" value=\"");
  page += String(cfg.flush_interval_sec);
  page += F("\"><div class=\"actions\"><button onclick=\"saveHistoryConfig()\">Save History Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section></main>");
  page += F("<script>async function saveHistoryConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("history_enabled:document.getElementById('history_enabled').checked?'1':'0',days_retained:document.getElementById('days_retained').value,");
  page += F("flush_interval:document.getElementById('flush_interval').value});try{const r=await fetch('/api/history/save',{method:'POST',");
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}");
  page += F("catch(e){s.textContent='Save failed'}}</script>");
  page += buildPageFooter();
  return page;
}

String buildSystemConfigPage() {
  const SystemConfig& cfg = currentSystemConfig;
  String page = buildPageHeader("System Configuration");
  page.reserve(3500);
  page += F("<section class=\"panel\"><h2>System Settings</h2>");
  page += F("<label for=\"ntp_server1\">NTP Server 1</label><input id=\"ntp_server1\" maxlength=\"64\" placeholder=\"pool.ntp.org\" value=\"");
  page += htmlEscape(String(cfg.ntp_server1));
  page += F("\"><label for=\"ntp_server2\">NTP Server 2</label><input id=\"ntp_server2\" maxlength=\"64\" placeholder=\"time.google.com\" value=\"");
  page += htmlEscape(String(cfg.ntp_server2));
  page += F("\"><label><input type=\"checkbox\" id=\"debug_enabled\"");
  page += cfg.debug_enabled ? F(" checked") : F("");
  page += F("> Enable Debug Logging</label>");
  page += F("<div class=\"actions\"><button onclick=\"saveSystemConfig()\">Save System Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section></main>");
  page += F("<script>async function saveSystemConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("ntp_server1:document.getElementById('ntp_server1').value,ntp_server2:document.getElementById('ntp_server2').value,");
  page += F("debug_enabled:document.getElementById('debug_enabled').checked?'1':'0'});try{const r=await fetch('/api/system/save',{method:'POST',");
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}");
  page += F("catch(e){s.textContent='Save failed'}}</script>");
  page += buildPageFooter();
  return page;
}

String buildNvsPreviewPage() {
  String page = buildPageHeader("NVS Preview");
  page.reserve(9000);
  page += F("<section class=\"panel\"><h2>NVS Snapshot</h2><div class=\"muted\">Read-only preview of the config currently loaded into runtime. Empty values mean the namespace has no stored value for that field.</div>");
  page += F("<div class=\"actions\"><button onclick=\"location.reload()\">Reload Preview</button></div></section>");

  page += F("<section class=\"panel\"><h2>Device</h2><div class=\"grid\">");
  page += F("<div>Device name</div><div>"); page += htmlEscape(String(currentDeviceConfig.device_name)); page += F("</div>");
  page += F("<div>Hostname</div><div>"); page += htmlEscape(String(currentDeviceConfig.hostname)); page += F("</div>");
  page += F("<div>Timezone</div><div>"); page += htmlEscape(String(currentDeviceConfig.timezone)); page += F("</div>");
  page += F("<div>CO2 factor</div><div>"); page += String(currentDeviceConfig.co2_factor_kg_per_kwh); page += F(" kg/kWh</div>");
  page += F("</div></section>");

  page += F("<section class=\"panel\"><h2>MQTT</h2><div class=\"grid\">");
  page += F("<div>Enabled</div><div>"); page += currentMqttConfig.enabled ? F("yes") : F("no"); page += F("</div>");
  page += F("<div>Host</div><div>"); page += htmlEscape(String(currentMqttConfig.host)); page += F("</div>");
  page += F("<div>Port</div><div>"); page += String(currentMqttConfig.port); page += F("</div>");
  page += F("<div>Username</div><div>"); page += strlen(currentMqttConfig.username) ? htmlEscape(String(currentMqttConfig.username)) : F("(empty)"); page += F("</div>");
  page += F("<div>Password</div><div>"); page += strlen(currentMqttConfig.password) ? F("set") : F("(empty)"); page += F("</div>");
  page += F("<div>Client ID</div><div>"); page += htmlEscape(String(currentMqttConfig.client_id)); page += F("</div>");
  page += F("<div>Base topic</div><div>"); page += htmlEscape(String(currentMqttConfig.base_topic)); page += F("</div>");
  page += F("<div>Publish interval</div><div>"); page += String(currentMqttConfig.publish_interval_sec); page += F(" sec</div>");
  page += F("</div></section>");

  page += F("<section class=\"panel\"><h2>Modbus</h2><div class=\"grid\">");
  page += F("<div>Baudrate</div><div>"); page += String(currentModbusConfig.baudrate); page += F("</div>");
  page += F("<div>Slave ID</div><div>"); page += String(currentModbusConfig.slave_id); page += F("</div>");
  page += F("<div>Parity</div><div>"); page += String(currentModbusConfig.parity); page += F("</div>");
  page += F("<div>Stop bits</div><div>"); page += String(currentModbusConfig.stop_bits); page += F("</div>");
  page += F("<div>Poll interval</div><div>"); page += String(currentModbusConfig.poll_interval_ms); page += F(" ms</div>");
  page += F("<div>Timeout</div><div>"); page += String(currentModbusConfig.timeout_ms); page += F(" ms</div>");
  page += F("<div>Retry count</div><div>"); page += String(currentModbusConfig.retry_count); page += F("</div>");
  page += F("<div>Meter profile</div><div>"); page += String(currentModbusConfig.meter_profile); page += F("</div>");
  page += F("</div></section>");

  page += F("<section class=\"panel\"><h2>Protection</h2><div class=\"grid\">");
  page += F("<div>Relay enabled</div><div>"); page += currentProtectionConfig.relay_enabled ? F("yes") : F("no"); page += F("</div>");
  page += F("<div>Current limit</div><div>"); page += String(currentProtectionConfig.current_limit_a); page += F(" A</div>");
  page += F("<div>Trip delay</div><div>"); page += String(currentProtectionConfig.trip_delay_ms); page += F(" ms</div>");
  page += F("<div>Reset mode</div><div>"); page += currentProtectionConfig.reset_mode == 0 ? F("manual") : F("auto"); page += F("</div>");
  page += F("<div>Auto-retry</div><div>"); page += currentProtectionConfig.auto_retry_enabled ? F("yes") : F("no"); page += F("</div>");
  page += F("<div>Auto-retry delay</div><div>"); page += String(currentProtectionConfig.auto_retry_delay_sec); page += F(" sec</div>");
  page += F("<div>Trip on stale meter</div><div>"); page += currentProtectionConfig.trip_on_meter_stale ? F("yes") : F("no"); page += F("</div>");
  page += F("</div></section>");

  page += F("<section class=\"panel\"><h2>Display</h2><div class=\"grid\">");
  page += F("<div>Enabled</div><div>"); page += currentDisplayConfig.enabled ? F("yes") : F("no"); page += F("</div>");
  page += F("<div>Type</div><div>"); page += currentDisplayConfig.type == 0 ? F("ST7567") : F("SSD1306"); page += F("</div>");
  page += F("<div>I2C address</div><div>"); page += formatHexByte(currentDisplayConfig.i2c_address); page += F("</div>");
  page += F("<div>Page rotation</div><div>"); page += String(currentDisplayConfig.rotation_interval_sec); page += F(" sec</div>");
  page += F("<div>Brightness</div><div>"); page += String(currentDisplayConfig.brightness); page += F("</div>");
  page += F("</div></section>");

  page += F("<section class=\"panel\"><h2>History</h2><div class=\"grid\">");
  page += F("<div>Enabled</div><div>"); page += currentHistoryConfig.enabled ? F("yes") : F("no"); page += F("</div>");
  page += F("<div>Days retained</div><div>"); page += String(currentHistoryConfig.days_retained); page += F("</div>");
  page += F("<div>Flush interval</div><div>"); page += String(currentHistoryConfig.flush_interval_sec); page += F(" sec</div>");
  page += F("</div></section>");

  page += F("<section class=\"panel\"><h2>System</h2><div class=\"grid\">");
  page += F("<div>NTP server 1</div><div>"); page += htmlEscape(String(currentSystemConfig.ntp_server1)); page += F("</div>");
  page += F("<div>NTP server 2</div><div>"); page += htmlEscape(String(currentSystemConfig.ntp_server2)); page += F("</div>");
  page += F("<div>Debug logging</div><div>"); page += currentSystemConfig.debug_enabled ? F("yes") : F("no"); page += F("</div>");
  page += F("</div></section>");

  page += buildPageFooter();
  return page;
}

// ============================================================================
// CONFIG PAGE HANDLERS
// ============================================================================
bool requireConfigMode() {
  if (currentMode == AppMode::Config || configApStarted) {
    return true;
  }

  configServer.sendHeader("Location", "/", true);
  configServer.send(302, "text/plain", "");
  return false;
}
void handleDeviceConfigPage() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildDeviceConfigPage());
}

void handleMqttConfigPage() {
  sendNoCacheHeader();
  configServer.send(200, "text/html", buildMqttConfigPage());
}

void handleModbusConfigPage() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildModbusConfigPage());
}

void handleProtectionConfigPage() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildProtectionConfigPage());
}

void handleDisplayConfigPage() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildDisplayConfigPage());
}

void handleHistoryConfigPage() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildHistoryConfigPage());
}

void handleSystemConfigPage() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildSystemConfigPage());
}

void handleNvsPreviewPage() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildNvsPreviewPage());
}

// ============================================================================
// CONFIG API HANDLERS
// ============================================================================
void handleDeviceConfigSaveApi() {
  DeviceConfig cfg = currentDeviceConfig;

  if (configServer.hasArg("device_name")) {
    String name = configServer.arg("device_name");
    strncpy(cfg.device_name, name.c_str(), sizeof(cfg.device_name) - 1);
  }
  if (configServer.hasArg("hostname")) {
    String hostname = configServer.arg("hostname");
    strncpy(cfg.hostname, hostname.c_str(), sizeof(cfg.hostname) - 1);
  }
  if (configServer.hasArg("timezone")) {
    String tz = configServer.arg("timezone");
    strncpy(cfg.timezone, tz.c_str(), sizeof(cfg.timezone) - 1);
  }
  if (configServer.hasArg("co2_factor")) {
    cfg.co2_factor_kg_per_kwh = configServer.arg("co2_factor").toInt();
  }

  bool ok = ConfigManager::saveDeviceConfig(cfg);
  if (ok) {
    currentDeviceConfig = cfg;
    restartNtpSync();
  }
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleMqttConfigSaveApi() {
  MqttConfig cfg = currentMqttConfig;

  if (configServer.hasArg("mqtt_host")) {
    String host = configServer.arg("mqtt_host");
    strncpy(cfg.host, host.c_str(), sizeof(cfg.host) - 1);
  }
  if (configServer.hasArg("mqtt_port")) {
    cfg.port = configServer.arg("mqtt_port").toInt();
  }
  if (configServer.hasArg("mqtt_username")) {
    String user = configServer.arg("mqtt_username");
    strncpy(cfg.username, user.c_str(), sizeof(cfg.username) - 1);
  }
  if (configServer.hasArg("mqtt_password")) {
    String pass = configServer.arg("mqtt_password");
    strncpy(cfg.password, pass.c_str(), sizeof(cfg.password) - 1);
  }
  if (configServer.hasArg("mqtt_client_id")) {
    String cid = configServer.arg("mqtt_client_id");
    strncpy(cfg.client_id, cid.c_str(), sizeof(cfg.client_id) - 1);
  }
  if (configServer.hasArg("mqtt_base_topic")) {
    String topic = configServer.arg("mqtt_base_topic");
    strncpy(cfg.base_topic, topic.c_str(), sizeof(cfg.base_topic) - 1);
  }
  if (configServer.hasArg("mqtt_publish_interval")) {
    cfg.publish_interval_sec = configServer.arg("mqtt_publish_interval").toInt();
  }
  cfg.enabled = configServer.hasArg("mqtt_enabled") && configServer.arg("mqtt_enabled") == "1";

  bool ok = ConfigManager::saveMqttConfig(cfg);
  if (ok) {
    currentMqttConfig = cfg;
    mqttClientConfigured = false;
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    mqttConnected = false;
  }
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleMqttTestApi() {
  String error = "unknown";
  const bool ok = testMqttBrokerConnection(error);
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true,\"result\":\"connected\"}");
  } else {
    String body = "{\"ok\":false,\"error\":\"";
    body += jsonEscape(error);
    body += F("\"}");
    configServer.send(200, "application/json", body);
  }
}

void handleModbusConfigSaveApi() {
  ModbusConfig cfg = currentModbusConfig;

  cfg.baudrate = configServer.hasArg("modbus_baudrate") ? configServer.arg("modbus_baudrate").toInt() : 19200;
  cfg.slave_id = configServer.hasArg("modbus_slave_id") ? configServer.arg("modbus_slave_id").toInt() : 1;
  cfg.parity = configServer.hasArg("modbus_parity") ? configServer.arg("modbus_parity").toInt() : 0;
  cfg.stop_bits = configServer.hasArg("modbus_stop_bits") ? configServer.arg("modbus_stop_bits").toInt() : 1;
  cfg.poll_interval_ms = configServer.hasArg("modbus_poll_interval") ? configServer.arg("modbus_poll_interval").toInt() : 1000;
  cfg.timeout_ms = configServer.hasArg("modbus_timeout") ? configServer.arg("modbus_timeout").toInt() : 1000;
  cfg.retry_count = configServer.hasArg("modbus_retry_count") ? configServer.arg("modbus_retry_count").toInt() : 3;
  cfg.meter_profile = configServer.hasArg("modbus_profile") ? configServer.arg("modbus_profile").toInt() : 0;

  bool ok = ConfigManager::saveModbusConfig(cfg);
  if (ok) {
    currentModbusConfig = cfg;
    currentModbusConfigLoaded = true;
    modbusPortReady = false;
  }
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleProtectionConfigSaveApi() {
  ProtectionConfig cfg = currentProtectionConfig;

  cfg.relay_enabled = configServer.hasArg("relay_enabled") && configServer.arg("relay_enabled") == "1";
  cfg.current_limit_a = configServer.hasArg("current_limit") ? configServer.arg("current_limit").toInt() : 16;
  cfg.trip_delay_ms = configServer.hasArg("trip_delay") ? configServer.arg("trip_delay").toInt() : 1000;
  cfg.reset_mode = configServer.hasArg("reset_mode") ? configServer.arg("reset_mode").toInt() : 0;
  cfg.auto_retry_enabled = configServer.hasArg("auto_retry_enabled") && configServer.arg("auto_retry_enabled") == "1";
  cfg.auto_retry_delay_sec = configServer.hasArg("auto_retry_delay") ? configServer.arg("auto_retry_delay").toInt() : 300;
  cfg.trip_on_meter_stale = configServer.hasArg("trip_on_stale") && configServer.arg("trip_on_stale") == "1";

  Serial.print("Protection save request: relay_enabled=");
  Serial.print(cfg.relay_enabled ? "1" : "0");
  Serial.print(" current_limit=");
  Serial.print(cfg.current_limit_a);
  Serial.print(" trip_delay=");
  Serial.print(cfg.trip_delay_ms);
  Serial.print(" reset_mode=");
  Serial.print(cfg.reset_mode);
  Serial.print(" auto_retry_enabled=");
  Serial.print(cfg.auto_retry_enabled ? "1" : "0");
  Serial.print(" auto_retry_delay=");
  Serial.print(cfg.auto_retry_delay_sec);
  Serial.print(" trip_on_stale=");
  Serial.println(cfg.trip_on_meter_stale ? "1" : "0");

  bool ok = ConfigManager::saveProtectionConfig(cfg);
  if (ok) {
    currentProtectionConfig = cfg;
    if (!currentProtectionConfig.relay_enabled) {
      requestRelayState(false, "protection_disabled");
    }
  }
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    Serial.println("Protection save failed in ConfigManager");
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleDisplayConfigSaveApi() {
  DisplayConfig cfg = currentDisplayConfig;

  cfg.enabled = configServer.hasArg("display_enabled") && configServer.arg("display_enabled") == "1";
  cfg.type = configServer.hasArg("display_type") ? configServer.arg("display_type").toInt() : 0;
  cfg.i2c_address = configServer.hasArg("i2c_address") ? parseHexByteValue(configServer.arg("i2c_address"), 0x3C) : 0x3C;
  cfg.rotation_interval_sec = configServer.hasArg("rotation_interval") ? configServer.arg("rotation_interval").toInt() : 5;
  cfg.brightness = configServer.hasArg("brightness") ? configServer.arg("brightness").toInt() : 200;

  Serial.print("Display save request: enabled=");
  Serial.print(cfg.enabled ? "1" : "0");
  Serial.print(" type=");
  Serial.print(cfg.type);
  Serial.print(" i2c=");
  Serial.print(formatHexByte(cfg.i2c_address));
  Serial.print(" rotation=");
  Serial.print(cfg.rotation_interval_sec);
  Serial.print(" brightness=");
  Serial.println(cfg.brightness);

  bool ok = ConfigManager::saveDisplayConfig(cfg);
  if (ok) {
    currentDisplayConfig = cfg;
    displayReady = false;
  }
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    Serial.println("Display save failed in ConfigManager");
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleHistoryConfigSaveApi() {
  HistoryConfig cfg = currentHistoryConfig;

  cfg.enabled = configServer.hasArg("history_enabled") && configServer.arg("history_enabled") == "1";
  cfg.days_retained = configServer.hasArg("days_retained") ? configServer.arg("days_retained").toInt() : 7;
  cfg.flush_interval_sec = configServer.hasArg("flush_interval") ? configServer.arg("flush_interval").toInt() : 3600;

  bool ok = ConfigManager::saveHistoryConfig(cfg);
  if (ok) {
    currentHistoryConfig = cfg;
    historyRuntime.loaded = false;
  }
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleSystemConfigSaveApi() {
  SystemConfig cfg = currentSystemConfig;

  if (configServer.hasArg("ntp_server1")) {
    String ntp1 = configServer.arg("ntp_server1");
    strncpy(cfg.ntp_server1, ntp1.c_str(), sizeof(cfg.ntp_server1) - 1);
  }
  if (configServer.hasArg("ntp_server2")) {
    String ntp2 = configServer.arg("ntp_server2");
    strncpy(cfg.ntp_server2, ntp2.c_str(), sizeof(cfg.ntp_server2) - 1);
  }
  cfg.debug_enabled = configServer.hasArg("debug_enabled") && configServer.arg("debug_enabled") == "1";

  bool ok = ConfigManager::saveSystemConfig(cfg);
  if (ok) {
    currentSystemConfig = cfg;
    restartNtpSync();
  }
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleRelayStateApi() {
  String body;
  body.reserve(64);
  body += F("{\"ok\":true,\"relay_enabled\":");
  body += currentProtectionConfig.relay_enabled ? F("true") : F("false");
  body += F(",\"relay_state\":\"");
  body += relayActualState ? F("1") : F("0");
  body += F("\",\"relay_requested\":\"");
  body += relayRequestedState ? F("1") : F("0");
  body += F("\",\"relay_locked\":");
  body += relayLockedOut ? F("true") : F("false");
  body += F("}");
  sendNoCacheHeader();
  configServer.send(200, "application/json", body);
}

void handleRelaySetApi() {
  String action;
  if (configServer.hasArg("action")) {
    action = configServer.arg("action");
  } else if (configServer.hasArg("state")) {
    action = configServer.arg("state");
  }
  action.toLowerCase();

  if (action == "set" || action == "1" || action == "on" || action == "true") {
    requestRelayState(true, "web");
  } else if (action == "reset" || action == "0" || action == "off" || action == "false") {
    requestRelayState(false, "web");
  } else {
    Serial.print("Relay action invalid: ");
    Serial.println(action);
    sendNoCacheHeader();
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_action\"}");
    return;
  }

  handleRelayStateApi();
}

void handleSetupRoot() {
  sendNoCacheHeader();
  configServer.send(200, "text/html", buildHomePage());
}

void handleNetworkPage() {
  sendNoCacheHeader();
  configServer.send(200, "text/html", buildNetworkPage());
}

void handleStatusApi() {
  String body;
  body.reserve(1024);
  body += F("{\"firmware\":\"");
  body += FW_VERSION;
  body += F("\",\"mode\":\"");
  body += appModeToString(currentMode);
  body += F("\",\"ap_ssid\":\"");
  body += jsonEscape(getConfigApSsid());
  body += F("\",\"ap_ip\":\"");
  body += WiFi.softAPIP().toString();
  body += F("\",\"saved_wifi_ssid\":\"");
  body += jsonEscape(savedWifiSsid);
  body += F("\",\"sta_status\":\"");
  body += wifiStatusToString(WiFi.status());
  body += F("\",\"device_name\":\"");
  body += jsonEscape(currentDeviceConfig.device_name);
  body += F("\",\"hostname\":\"");
  body += jsonEscape(currentDeviceConfig.hostname);
  body += F("\",\"timezone\":\"");
  body += jsonEscape(currentDeviceConfig.timezone);
  body += F("\",\"mqtt_enabled\":");
  body += currentMqttConfig.enabled ? F("true") : F("false");
  body += F(",\"mqtt_host\":\"");
  body += jsonEscape(currentMqttConfig.host);
  body += F("\",\"mqtt_port\":");
  body += String(currentMqttConfig.port);
  body += F(",\"modbus_baudrate\":");
  body += String(currentModbusConfig.baudrate);
  body += F(",\"modbus_slave_id\":");
  body += String(currentModbusConfig.slave_id);
  body += F(",\"modbus_profile\":");
  body += String(currentModbusConfig.meter_profile);
  body += F(",\"relay_enabled\":");
  body += currentProtectionConfig.relay_enabled ? F("true") : F("false");
  body += F(",\"relay_state\":\"");
  body += relayActualState ? F("1") : F("0");
  body += F("\",\"relay_requested\":\"");
  body += relayRequestedState ? F("1") : F("0");
  body += F("\",\"relay_locked\":");
  body += relayLockedOut ? F("true") : F("false");
  body += F(",\"display_enabled\":");
  body += currentDisplayConfig.enabled ? F("true") : F("false");
  body += F(",\"history_enabled\":");
  body += currentHistoryConfig.enabled ? F("true") : F("false");
  body += F(",\"ntp_server1\":\"");
  body += jsonEscape(currentSystemConfig.ntp_server1);
  body += F("\",\"ntp_server2\":\"");
  body += jsonEscape(currentSystemConfig.ntp_server2);
  body += F("\",\"debug_enabled\":");
  body += currentSystemConfig.debug_enabled ? F("true") : F("false");
  body += F(",\"mqtt_connected\":");
  body += mqttConnected ? F("true") : F("false");
  body += F(",\"mqtt_state_topic\":\"");
  body += jsonEscape(mqttStateTopic());
  body += F("\",\"mqtt_telemetry_topic\":\"");
  body += jsonEscape(mqttTelemetryTopic());
  body += F("\",\"mqtt_base_topic\":\"");
  body += jsonEscape(getMqttBaseTopic());
  body += F("\",\"display_ready\":");
  body += displayReady ? F("true") : F("false");
  body += F(",\"display_page\":");
  body += String(displayPage);
  body += F(",\"history_day_key\":\"");
  body += formatDayKey(historyRuntime.dayKey);
  body += F("\",\"energy_history\":");
  body += buildEnergyHistoryJson();
  body += F(",\"sta_ip\":\"");
  body += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  body += F("\",\"rtc\":\"");
  body += jsonEscape(getRtcString());
  body += F("\",\"time_synced\":");
  body += timeSynced ? F("true") : F("false");
  body += F(",\"last_ntp_sync\":\"");
  body += timeSynced ? jsonEscape(formatDateTime(lastNtpSyncEpoch)) : "";
  body += F("\",\"mac_suffix\":\"");
  body += getMacSuffix();
  body += F("\",\"free_heap\":");
  body += String(ESP.getFreeHeap());
  body += F(",\"heap_total\":");
  body += String(getHeapTotalBytes());
  body += F(",\"heap_total_human\":\"");
  body += jsonEscape(formatBytesHuman(getHeapTotalBytes()));
  body += F("\"");
  body += F(",\"heap_used\":");
  body += String(getHeapUsedBytes());
  body += F(",\"heap_used_human\":\"");
  body += jsonEscape(formatBytesHuman(getHeapUsedBytes()));
  body += F("\"");
  body += F(",\"heap_used_percent\":");
  body += String(getHeapUsedPercent());
  body += F(",\"sketch_size\":");
  body += String(ESP.getSketchSize());
  body += F(",\"sketch_size_human\":\"");
  body += jsonEscape(formatBytesHuman(ESP.getSketchSize()));
  body += F("\"");
  body += F(",\"sketch_capacity\":");
  body += String(getSketchCapacityBytes());
  body += F(",\"sketch_capacity_human\":\"");
  body += jsonEscape(formatBytesHuman(getSketchCapacityBytes()));
  body += F("\"");
  body += F(",\"sketch_used_percent\":");
  body += String(getSketchUsedPercent());
  body += F(",\"wifi_rssi\":");
  body += String(getWifiRssi());
  body += F(",\"wifi_quality_percent\":");
  body += String(getWifiQualityPercent());
  body += F(",\"uptime_ms\":");
  body += String(millis());
  body += F(",\"uptime_text\":\"");
  body += formatUptime(millis());
  body += F("\"");
  body += F(",\"meter_online\":");
  body += meterSnapshot.online ? F("true") : F("false");
  body += F(",\"meter_valid\":");
  body += meterSnapshot.valid ? F("true") : F("false");
  body += F(",\"meter_voltage\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.voltage, 2, "V"));
  body += F("\",\"meter_current\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.current, 2, "A"));
  body += F("\",\"meter_power\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.power, 2, "kW"));
  body += F("\",\"meter_frequency\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.frequency, 2, "Hz"));
  body += F("\",\"meter_pf\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.pf, 2, ""));
  body += F("\",\"meter_energy\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.energy, 3, "kWh"));
  body += F("\",\"meter_last_poll_ms\":");
  body += String(meterSnapshot.lastPollMs);
  body += F(",\"meter_last_success_ms\":");
  body += String(meterSnapshot.lastSuccessMs);
  body += F(",\"relay_limit_a\":");
  body += String(currentProtectionConfig.current_limit_a);
  body += F(",\"relay_trip_delay_ms\":");
  body += String(currentProtectionConfig.trip_delay_ms);
  body += F("}");

  sendNoCacheHeader();
  configServer.send(200, "application/json", body);
}

void handleMeterPage() {
  sendNoCacheHeader();
  configServer.send(200, "text/html", buildMeterPage());
}

void handleMeterStatusApi() {
  String body;
  body.reserve(256);
  body += F("{\"online\":");
  body += meterSnapshot.online ? F("true") : F("false");
  body += F(",\"valid\":");
  body += meterSnapshot.valid ? F("true") : F("false");
  body += F(",\"voltage\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.voltage, 2, "V"));
  body += F("\",\"current\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.current, 2, "A"));
  body += F("\",\"power\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.power, 2, "kW"));
  body += F("\",\"frequency\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.frequency, 2, "Hz"));
  body += F("\",\"pf\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.pf, 2, ""));
  body += F("\",\"energy\":\"");
  body += jsonEscape(formatFloatValue(meterSnapshot.energy, 3, "kWh"));
  body += F("\",\"last_poll_ms\":");
  body += String(meterSnapshot.lastPollMs);
  body += F(",\"last_success_ms\":");
  body += String(meterSnapshot.lastSuccessMs);
  body += F("}");

  sendNoCacheHeader();
  configServer.send(200, "application/json", body);
}

void handleWifiScanApi() {
  Serial.println("WiFi scan requested from setup UI");
  const int count = WiFi.scanNetworks(false, true);
  String body;
  body.reserve(512 + (count > 0 ? count * 96 : 0));
  body += F("{\"count\":");
  body += String(count > 0 ? count : 0);
  body += F(",\"networks\":[");

  for (int i = 0; i < count; i++) {
    if (i > 0) {
      body += ',';
    }

    body += F("{\"ssid\":\"");
    body += jsonEscape(WiFi.SSID(i));
    body += F("\",\"rssi\":");
    body += String(WiFi.RSSI(i));
    body += F(",\"channel\":");
    body += String(WiFi.channel(i));
    body += F(",\"encryption\":\"");
    body += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? F("open") : F("secured");
    body += F("\"}");
  }

  body += F("]}");
  WiFi.scanDelete();

  sendNoCacheHeader();
  configServer.send(200, "application/json", body);
}

void handleWifiSaveApi() {
  if (!configServer.hasArg("ssid")) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}");
    return;
  }

  String ssid = configServer.arg("ssid");
  String password = configServer.arg("password");
  ssid.trim();

  if (ssid.length() == 0 || ssid.length() > 32) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_ssid\"}");
    return;
  }

  if (password.length() > 64) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_password\"}");
    return;
  }

  const bool saved = saveWifiConfig(ssid, password);
  if (!saved) {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"nvs_write_failed\"}");
    return;
  }

  String body;
  body.reserve(96);
  body += F("{\"ok\":true,\"ssid\":\"");
  body += jsonEscape(savedWifiSsid);
  body += F("\",\"reboot_required\":true}");
  sendNoCacheHeader();
  configServer.send(200, "application/json", body);
}

void handleRebootApi() {
  rebootRequested = true;
  rebootAtMs = millis() + 800;
  sendNoCacheHeader();
  configServer.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
  Serial.println("Reboot requested from setup UI");
}

void handleNotFound() {
  configServer.sendHeader("Location", "/", true);
  configServer.send(302, "text/plain", "");
}

void startConfigWebServer() {
  if (configWebStarted) {
    return;
  }

  configServer.on("/", HTTP_GET, handleSetupRoot);
  configServer.on("/meter", HTTP_GET, handleMeterPage);
  configServer.on("/network", HTTP_GET, handleNetworkPage);
  configServer.on("/device", HTTP_GET, handleDeviceConfigPage);
  configServer.on("/mqtt", HTTP_GET, handleMqttConfigPage);
  configServer.on("/modbus", HTTP_GET, handleModbusConfigPage);
  configServer.on("/protection", HTTP_GET, handleProtectionConfigPage);
  configServer.on("/display", HTTP_GET, handleDisplayConfigPage);
  configServer.on("/history", HTTP_GET, handleHistoryConfigPage);
  configServer.on("/system", HTTP_GET, handleSystemConfigPage);
  configServer.on("/nvs", HTTP_GET, handleNvsPreviewPage);
  configServer.on("/api/status", HTTP_GET, handleStatusApi);
  configServer.on("/api/meter/status", HTTP_GET, handleMeterStatusApi);
  configServer.on("/api/wifi/scan", HTTP_GET, handleWifiScanApi);
  configServer.on("/api/wifi/save", HTTP_POST, handleWifiSaveApi);
  configServer.on("/api/device/save", HTTP_POST, handleDeviceConfigSaveApi);
  configServer.on("/api/mqtt/save", HTTP_POST, handleMqttConfigSaveApi);
  configServer.on("/api/mqtt/test", HTTP_POST, handleMqttTestApi);
  configServer.on("/api/modbus/save", HTTP_POST, handleModbusConfigSaveApi);
  configServer.on("/api/protection/save", HTTP_POST, handleProtectionConfigSaveApi);
  configServer.on("/api/display/save", HTTP_POST, handleDisplayConfigSaveApi);
  configServer.on("/api/history/save", HTTP_POST, handleHistoryConfigSaveApi);
  configServer.on("/api/system/save", HTTP_POST, handleSystemConfigSaveApi);
  configServer.on("/api/relay/state", HTTP_GET, handleRelayStateApi);
  configServer.on("/api/relay/set", HTTP_POST, handleRelaySetApi);
  configServer.on("/api/reboot", HTTP_POST, handleRebootApi);
  configServer.onNotFound(handleNotFound);
  configServer.begin();

  configWebStarted = true;
  Serial.println("Web UI started");
  printWebUiAddresses();
}

void startConfigAccessPoint(bool disconnectSta) {
  if (configApStarted) {
    startConfigWebServer();
    return;
  }

  const String ssid = getConfigApSsid();
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);

  if (disconnectSta) {
      WiFi.disconnect(false, false);
      delay(100);
  }
  WiFi.mode(WIFI_AP_STA);
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
  startConfigWebServer();
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
    // setBuiltinLed(true);
    startConfigAccessPoint();
    return;
  }

  if (!isConfigButtonPressed()) {
    configButtonPressedSinceMs = 0;
    lastConfigButtonProgressSecond = 0;
    // setBuiltinLed(false);
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
  pinMode(PinMap::kRelayOutput, OUTPUT);
  setRelayOutput(false);
  // setBuiltinLed(false);

  printBootBanner();
  printChipInfo();
  printButtonInfo();
  currentDeviceConfig = ConfigManager::loadDeviceConfig();
  currentMqttConfig = ConfigManager::loadMqttConfig();
  loadWifiConfig();
  currentModbusConfig = ConfigManager::loadModbusConfig();
  currentProtectionConfig = ConfigManager::loadProtectionConfig();
  currentDisplayConfig = ConfigManager::loadDisplayConfig();
  currentHistoryConfig = ConfigManager::loadHistoryConfig();
  currentSystemConfig = ConfigManager::loadSystemConfig();
  currentModbusConfigLoaded = true;
  loadFeatureRuntime();
  resetMeterSnapshot();
  if (hasSavedWifi) {
    wifiConnecting = true;
    wifiConnectStartedMs = millis();
    connectToSavedWifi();
  } else {
    Serial.println("No saved WiFi configuration. Entering CONFIG_MODE.");
    enterConfigMode();
  }
  printModeInfo();
}

void loop() {
  const uint32_t nowMs = millis();
  updateStatusLed(nowMs);
  handleWiFiLifecycle(nowMs);
  handleConfigButton(nowMs);
  pollModbusMeter(nowMs);
  updateProtectionRuntime(nowMs);
  updateMqttRuntime(nowMs);
  updateDisplayRuntime(nowMs);
  if (configWebStarted) {
    configServer.handleClient();
  }

  if (rebootRequested && millis() >= rebootAtMs) {
    Serial.println("Restarting now");
    ESP.restart();
  }

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
    Serial.print(appModeToString(currentMode));
    Serial.print(" wifi=");
    Serial.print(wifiStatusToString(WiFi.status()));
    Serial.print(" rtc=");
    Serial.println(getRtcString());
  }
}
