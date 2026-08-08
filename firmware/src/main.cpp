#include <Arduino.h>
#include <Update.h>
#include <Ethernet.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <Wire.h>
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
constexpr char kConfigApSsidPrefix[] = "SEMS-SETUP";
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
bool webConfigUnlocked = false;
bool configWebStarted = false;
// Send '0' over serial to silence periodic/routine logs (Modbus poll spam,
// heartbeat, ETH link chatter); send '1' to turn them back on. Defaults ON
// so nothing is hidden unless the user explicitly quiets it.
bool verboseLog = true;
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

// ===== /configmod UART test-mode (hidden Modbus scan/mapping tool) =====
// Serial2 is single-owner: while a test-mode scan session is active,
// production polling (pollModbusMeter) is paused entirely so the two never
// fight over the UART. An idle timeout auto-restores production settings if
// an operator forgets to click "Restore Normal" (or the tab/network drops).
bool modbusTestModeActive = false;
uint32_t modbusTestModeLastActivityMs = 0;
constexpr uint32_t kModbusTestModeIdleTimeoutMs = 5UL * 60UL * 1000UL;  // 5 minutes
ModbusConfig modbusTestModeConfig;  // last-applied test UART params, not persisted
bool modMapCacheDirty = true;       // set by /api/modmap/save so the Custom Mapping poll path picks up changes without reboot
ModbusMapConfig cachedModMapConfig;
bool modMapCacheLoaded = false;
bool mqttConnected = false;
bool mqttClientConfigured = false;
uint32_t mqttLastReconnectAttemptMs = 0;
uint32_t mqttLastPublishMs = 0;
uint32_t mqttLastAlignedEpochMinute = 0;  // last wall-clock minute the aligned scheduler fired on (guards double-fire)
// /mqtt "Push Test Data" diagnostic — a manual publish to <base>/test that
// the device also subscribes to, so a successful round-trip (publish ok +
// echo received back) proves both directions work, not just that connect()
// succeeded. Distinguishes "broker unreachable" from "connected but this
// client isn't allowed to subscribe" (the nocola_2 symptom this exists for).
bool mqttTestPublishOk = false;
uint32_t mqttTestPublishMs = 0;
String mqttTestPublishPayload;
uint32_t mqttTestEchoMs = 0;
String mqttTestEchoPayload;
uint32_t mqttTestSeq = 0;
WiFiClient mqttWifiTransport;
// mqttEthTransport declared below after EthernetClient is available
PubSubClient mqttWifiClient(mqttWifiTransport);
uint8_t relayState[4] = {0, 0, 0, 0};            // 0=OFF, 1=ON, 2=TRIP
uint8_t relayRequestedState[4] = {0, 0, 0, 0};   // 0=OFF, 1=ON
uint32_t relayTripUntilMs[4] = {0, 0, 0, 0};
uint32_t relayOvercurrentSinceMs[4] = {0, 0, 0, 0};
bool displayReady = false;
uint32_t displayLastUpdateMs = 0;
uint8_t displayPage = 0;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R2, /* reset= */ U8X8_PIN_NONE);

// OLED navigation state (upstream rebuild-2026 behaviour)
bool oledInMenu        = false;
uint8_t oledMenuCursor = 0;     // 0=Config/Normal Mode, 1=View Info, 2=Exit
bool oledInfoModeActive = false; // true while manually paging info screens

bool ethLinkUp = false;
bool ethConfigured = false;
EthernetClient mqttEthTransport;
PubSubClient mqttEthClient(mqttEthTransport);
PubSubClient* activeMqttClient = &mqttWifiClient;
uint32_t ethLastCheckMs = 0;

struct HistoryRuntime {
  bool loaded = false;
  bool dirty = false;
  uint32_t dayKey = 0;
  float baselineEnergy = NAN;
  float daily[7] = {0, 0, 0, 0, 0, 0, 0};
  uint32_t lastPersistMs = 0;
};

HistoryRuntime historyRuntime[3];

struct MeterSnapshot {
  bool online = false;
  bool valid = false;
  uint32_t lastPollMs = 0;
  uint32_t lastSuccessMs = 0;
  uint32_t lastErrorMs = 0;
  uint16_t lastErrorCode = 0;

  // Tegangan (V)
  float ua = NAN;
  float ub = NAN;
  float uc = NAN;
  float uab = NAN;
  float ubc = NAN;
  float uca = NAN;

  // Arus (A)
  float ia = NAN;
  float ib = NAN;
  float ic = NAN;

  // Daya Aktif (W)
  float pa = NAN;
  float pb = NAN;
  float pc = NAN;
  float p_total = NAN;

  // Daya Reaktif (var)
  float qa = NAN;
  float qb = NAN;
  float qc = NAN;
  float q_total = NAN;

  // Daya Nyata (VA)
  float sa = NAN;
  float sb = NAN;
  float sc = NAN;
  float s_total = NAN;

  // Power Factor
  float pf1 = NAN;
  float pf2 = NAN;
  float pf3 = NAN;
  float pf_avg = NAN;

  // Frekuensi & Energi
  float frequency = NAN;
  float kwh_total = NAN;
  float kvarh_total = NAN;
  float kwh_forward = NAN;
  float kwh_backward = NAN;
  float kvarh_forward = NAN;
  float kvarh_backward = NAN;

  // Variabel kompatibilitas lama
  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float pf = NAN;
  float energy = NAN;
};

MeterSnapshot meterSnapshots[3];
WebServer configServer(80);

void startConfigWebServer();
void enterConfigMode();
void startConfigAccessPoint(bool disconnectSta = true);
bool isPinUsedByRelay(uint8_t pin);
String mqttSwitchStateTopic(size_t index);
String getConfigApSsid();

bool isConfigButtonPressed() {
  const int rawState = digitalRead(PinMap::kConfigButton);
  return PinMap::kConfigButtonActiveLow ? rawState == LOW : rawState == HIGH;
}

void setBuiltinLed(bool on) {
  // GPIO2 doubles as the default Relay 1 pin on this board — never let the
  // status LED fight a relay output that's using the same physical pin.
  if (isPinUsedByRelay(PinMap::kBuiltinLed)) {
    return;
  }
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
  Serial.println("Project: SEMS AIoT");
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

String getDeviceUid() {
  const uint64_t chipId = ESP.getEfuseMac();
  char suffix[7] = {};
  snprintf(suffix, sizeof(suffix), "%06X", static_cast<uint32_t>(chipId & 0xFFFFFFULL));
  return String("ESP32-RS485-") + suffix;
}

String getMqttBaseTopic() {
  String base = String(currentMqttConfig.base_topic);
  base.trim();
  if (base.length() == 0 || base == "sems" || base == "semsiot") {
    base = "trofis/enms";
  }
  while (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }
  String devName = String(currentDeviceConfig.device_name);
  devName.trim();
  if (devName.length() == 0) {
    devName = "nocola";
  }
  while (devName.endsWith("/")) {
    devName.remove(devName.length() - 1);
  }
  return base + "/" + devName;
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

float currentMeterEnergyDelta(size_t index) {
  if (isnan(meterSnapshots[index].energy)) {
    return NAN;
  }

  return meterSnapshots[index].energy;
}

uint8_t relayPinFor(size_t index) {
  const uint8_t defPins[4] = {2, 15, 14, 13};
  if (index >= 4) return defPins[0];
  const uint8_t configured = currentProtectionConfig.relay_pin[index];
  return configured == 0 ? defPins[index] : configured;
}

bool isPinUsedByRelay(uint8_t pin) {
  for (size_t i = 0; i < 4; i++) {
    if (relayPinFor(i) == pin) return true;
  }
  return false;
}

void setRelayOutput(size_t index, uint8_t state) {
  if (index >= 4) return;
  const uint8_t pin = relayPinFor(index);

  const bool on = (state == 1);
  const bool outputHigh = PinMap::kRelayOutputActiveHigh ? on : !on;
  digitalWrite(pin, outputHigh ? HIGH : LOW);
  relayState[index] = state;
}

void loadHistoryRuntime(size_t index) {
  Preferences prefs;
  HistoryRuntime& hr = historyRuntime[index];
  hr.loaded = true;  // mark loaded regardless — NVS missing = fresh start
  char ns[16] = {};
  snprintf(ns, sizeof(ns), "history_rt%u", static_cast<unsigned>(index));
  if (!prefs.begin(ns, true)) {
    return;
  }

  hr.dayKey = prefs.getUInt("day_key", 0);
  hr.baselineEnergy = prefs.getFloat("baseline", NAN);
  for (size_t i = 0; i < 7; i++) {
    char key[8] = {};
    snprintf(key, sizeof(key), "d%u", static_cast<unsigned>(i));
    hr.daily[i] = prefs.getFloat(key, 0.0f);
  }
  prefs.end();
  hr.loaded = true;
}

void saveHistoryRuntime(size_t index) {
  Preferences prefs;
  HistoryRuntime& hr = historyRuntime[index];
  char ns[16] = {};
  snprintf(ns, sizeof(ns), "history_rt%u", static_cast<unsigned>(index));
  if (!prefs.begin(ns, false)) {
    return;
  }

  prefs.putUInt("day_key", hr.dayKey);
  prefs.putFloat("baseline", hr.baselineEnergy);
  for (size_t i = 0; i < 7; i++) {
    char key[8] = {};
    snprintf(key, sizeof(key), "d%u", static_cast<unsigned>(i));
    prefs.putFloat(key, hr.daily[i]);
  }
  prefs.end();
  hr.dirty = false;
  hr.lastPersistMs = millis();
}

String buildEnergyHistoryJson(size_t index) {
  String body;
  body.reserve(128);
  body += F("[");
  for (size_t i = 0; i < 7; i++) {
    if (i > 0) {
      body += F(",");
    }
    body += F("\"");
    body += String(historyRuntime[index].daily[i], 3);
    body += F("\"");
  }
  body += F("]");
  return body;
}

void updateHistoryRuntime(uint32_t nowMs) {
  if (!currentHistoryConfig.enabled) {
    return;
  }

  for (size_t m = 0; m < 3; m++) {
    HistoryRuntime& hr = historyRuntime[m];
    if (!hr.loaded) {
      loadHistoryRuntime(m);
    }

    const float energy = currentMeterEnergyDelta(m);
    if (isnan(energy)) {
      continue;
    }

    const uint32_t dayKey = currentDayKeyFromEpoch(time(nullptr));
    if (hr.dayKey == 0 || hr.baselineEnergy != hr.baselineEnergy) {
      hr.dayKey = dayKey;
      hr.baselineEnergy = energy;
      hr.daily[0] = 0.0f;
      hr.dirty = true;
    } else if (dayKey != 0 && dayKey != hr.dayKey) {
      const float completedDay = energy - hr.baselineEnergy;
      for (int i = 6; i > 0; --i) {
        hr.daily[i] = hr.daily[i - 1];
      }
      hr.daily[0] = completedDay > 0 ? completedDay : 0.0f;
      hr.dayKey = dayKey;
      hr.baselineEnergy = energy;
      hr.dirty = true;
    } else {
      const float currentDay = energy - hr.baselineEnergy;
      hr.daily[0] = currentDay > 0 ? currentDay : 0.0f;
      hr.dirty = true;
    }

    if (hr.dirty && nowMs - hr.lastPersistMs >= 30000UL) {
      saveHistoryRuntime(m);
    }
  }
}

String getFormattedTimestamp();

String buildRelayStateText(size_t index) {
  if (index >= 4) return String("invalid");
  if (!currentProtectionConfig.relay_enabled) {
    return String("disabled");
  }
  if (relayState[index] == 2) {
    return String("TRIP");
  }
  return (relayState[index] == 1) ? String("ON") : String("OFF");
}

String buildRelayStateText() {
  return buildRelayStateText(0);
}

void publishMqttControlState() {
  if (!activeMqttClient || !(*activeMqttClient).connected()) {
    return;
  }
  const String topic = getMqttBaseTopic() + "/control-state";
  String body;
  body.reserve(128);
  body += F("{\"timestamp\":\"");
  body += getFormattedTimestamp();
  body += F("\",\"r1\":"); body += String(relayState[0]);
  body += F(",\"r2\":"); body += String(relayState[1]);
  body += F(",\"r3\":"); body += String(relayState[2]);
  body += F(",\"r4\":"); body += String(relayState[3]);
  body += F("}");

  (*activeMqttClient).publish(topic.c_str(), body.c_str(), true);
  Serial.print("Published MQTT control-state: ");
  Serial.println(body);

  // Per-relay state as a single character: "0"=OFF, "1"=ON, "2"=TRIP.
  for (size_t i = 0; i < 4; i++) {
    const String switchStateTopic = mqttSwitchStateTopic(i);
    const String statePayload = String(relayState[i]);
    const bool pubOk = (*activeMqttClient).publish(switchStateTopic.c_str(), statePayload.c_str(), true);
    Serial.print("Published MQTT switch state: ");
    Serial.print(switchStateTopic);
    Serial.print(" = ");
    Serial.print(statePayload);
    Serial.println(pubOk ? " (ok)" : " (FAILED - broker rejected/write failed)");
  }
}

bool canTurnRelayOn(size_t index) {
  if (index >= 4) return false;
  if (!currentProtectionConfig.relay_enabled) {
    return false;
  }
  if (relayState[index] == 2 && millis() < relayTripUntilMs[index]) {
    return false;
  }
  return true;
}

void requestRelayState(size_t index, uint8_t state, const char* reason, bool notifyMqtt = true) {
  if (index >= 4) return;
  if (!currentProtectionConfig.relay_enabled) {
    setRelayOutput(index, 0);
    relayRequestedState[index] = 0;
    Serial.println("Relay request ignored: relay disabled");
    if (notifyMqtt) publishMqttControlState();
    return;
  }

  if (state == 1 && !canTurnRelayOn(index)) {
    Serial.print("Relay R");
    Serial.print(index + 1);
    Serial.print(" ON blocked: ");
    Serial.println(reason);
    return;
  }

  const uint8_t targetState = (state == 1) ? 1 : 0;
  relayRequestedState[index] = targetState;
  setRelayOutput(index, targetState);
  ConfigManager::saveRelayStates(relayRequestedState);

  Serial.print("Relay R");
  Serial.print(index + 1);
  Serial.print(" state set: ");
  Serial.print(targetState == 1 ? "ON" : "OFF");
  Serial.print(" reason=");
  Serial.println(reason);

  if (notifyMqtt) {
    publishMqttControlState();
  }
}

void tripRelay(size_t index, const char* reason, uint32_t nowMs) {
  if (index >= 4) return;
  relayTripUntilMs[index] = nowMs + static_cast<uint32_t>(currentProtectionConfig.auto_retry_delay_sec) * 1000UL;
  relayOvercurrentSinceMs[index] = 0;
  relayRequestedState[index] = 0;
  setRelayOutput(index, 2); // 2 = TRIP

  Serial.print("Relay R");
  Serial.print(index + 1);
  Serial.print(" TRIPPED: ");
  Serial.println(reason);

  publishMqttControlState();
}

float maxMeterCurrent() {
  float maxCurrent = NAN;
  for (size_t m = 0; m < 3; m++) {
    const float c = meterSnapshots[m].current;
    if (isnan(c)) continue;
    if (isnan(maxCurrent) || c > maxCurrent) {
      maxCurrent = c;
    }
  }
  return maxCurrent;
}

bool anyMeterOffline() {
  for (size_t m = 0; m < 3; m++) {
    if (!meterSnapshots[m].online) return true;
  }
  return false;
}

bool anyMeterStale(uint32_t nowMs, uint32_t staleAfterMs) {
  for (size_t m = 0; m < 3; m++) {
    const MeterSnapshot& s = meterSnapshots[m];
    if (!s.online && s.lastSuccessMs > 0 && nowMs - s.lastSuccessMs > staleAfterMs) {
      return true;
    }
  }
  return false;
}

void updateProtectionRuntime(uint32_t nowMs) {
  if (!currentProtectionConfig.relay_enabled) {
    for (size_t i = 0; i < 4; i++) {
      if (relayState[i] != 0) {
        setRelayOutput(i, 0);
      }
      relayRequestedState[i] = 0;
      relayTripUntilMs[i] = 0;
      relayOvercurrentSinceMs[i] = 0;
    }
    return;
  }

  // 1. Auto-retry for tripped relays
  for (size_t i = 0; i < 4; i++) {
    if (relayState[i] == 2) {
      if (currentProtectionConfig.auto_retry_enabled && nowMs >= relayTripUntilMs[i]) {
        const float cur = (i < 3) ? meterSnapshots[i].current : maxMeterCurrent();
        const bool safeCurrent = isnan(cur) || cur <= currentProtectionConfig.current_limit_a;
        const bool meterOk = (i < 3) ? meterSnapshots[i].online : !anyMeterOffline();
        if (safeCurrent && meterOk) {
          Serial.print("Relay R"); Serial.print(i + 1); Serial.println(" auto-retry unlocked");
          setRelayOutput(i, relayRequestedState[i]);
          publishMqttControlState();
        }
      }
    }
  }

  // 2. Stale meter protection (only if explicitly enabled and meter previously communicated)
  if (currentProtectionConfig.trip_on_meter_stale && anyMeterStale(nowMs, currentProtectionConfig.trip_delay_ms)) {
    for (size_t m = 0; m < 3; m++) {
      const MeterSnapshot& s = meterSnapshots[m];
      if (!s.online && s.lastSuccessMs > 0 && nowMs - s.lastSuccessMs > currentProtectionConfig.trip_delay_ms) {
        if (relayState[m] != 2) {
          tripRelay(m, "meter offline/stale", nowMs);
        }
      }
    }
    if (relayState[3] != 2) {
      tripRelay(3, "master meter offline/stale", nowMs);
    }
  }

  // 3. Overcurrent protection per meter
  for (size_t m = 0; m < 3; m++) {
    const float cur = meterSnapshots[m].current;
    if (!isnan(cur) && cur > currentProtectionConfig.current_limit_a) {
      if (relayOvercurrentSinceMs[m] == 0) {
        relayOvercurrentSinceMs[m] = nowMs;
      } else if (nowMs - relayOvercurrentSinceMs[m] >= currentProtectionConfig.trip_delay_ms) {
        if (relayState[m] != 2) {
          char reasonBuf[64];
          snprintf(reasonBuf, sizeof(reasonBuf), "overcurrent (%.2fA > %dA)", cur, currentProtectionConfig.current_limit_a);
          tripRelay(m, reasonBuf, nowMs);
        }
      }
    } else {
      relayOvercurrentSinceMs[m] = 0;
    }
  }

  // 4. Master relay overcurrent protection
  float maxCur = maxMeterCurrent();
  if (!isnan(maxCur) && maxCur > currentProtectionConfig.current_limit_a) {
    if (relayOvercurrentSinceMs[3] == 0) {
      relayOvercurrentSinceMs[3] = nowMs;
    } else if (nowMs - relayOvercurrentSinceMs[3] >= currentProtectionConfig.trip_delay_ms) {
      if (relayState[3] != 2) {
        char reasonBuf[64];
        snprintf(reasonBuf, sizeof(reasonBuf), "master overcurrent (%.2fA > %dA)", maxCur, currentProtectionConfig.current_limit_a);
        tripRelay(3, reasonBuf, nowMs);
      }
    }
  } else {
    relayOvercurrentSinceMs[3] = 0;
  }
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

// Per-relay boolean control topics, e.g. trofis/enms/nocola_1/control/slave_1.
// The slave_N suffix is configurable per relay (ProtectionConfig::relay_slave_id,
// default {1,2,3,4}) so it can be remapped independently of the Modbus meter
// slave IDs — e.g. relays mapped to slave_4/5/6 to match a site's numbering.
String mqttSwitchTopic(size_t index) {
  const uint8_t slaveId = (index < 4 && currentProtectionConfig.relay_slave_id[index] != 0)
                               ? currentProtectionConfig.relay_slave_id[index]
                               : static_cast<uint8_t>(index + 1);
  return getMqttBaseTopic() + "/control/slave_" + String(slaveId);
}

String mqttSwitchStateTopic(size_t index) {
  return mqttSwitchTopic(index) + "/state";
}

// Diagnostic loopback topic for the /mqtt "Push Test Data" button — see
// mqttTestPublishOk et al. above.
String mqttTestTopic() {
  return getMqttBaseTopic() + "/test";
}

// PubSubClient::state() codes, per the library's own docs — surfaced in the
// /mqtt diagnostics panel so a failure can be told apart (bad credentials vs.
// broker unreachable vs. duplicate client ID kicking the connection) without
// needing serial console access.
const __FlashStringHelper* mqttStateToString(int state) {
  switch (state) {
    case -4: return F("MQTT_CONNECTION_TIMEOUT");
    case -3: return F("MQTT_CONNECTION_LOST");
    case -2: return F("MQTT_CONNECT_FAILED (broker unreachable)");
    case -1: return F("MQTT_DISCONNECTED");
    case 0:  return F("MQTT_CONNECTED");
    case 1:  return F("MQTT_CONNECT_BAD_PROTOCOL");
    case 2:  return F("MQTT_CONNECT_BAD_CLIENT_ID (duplicate Client ID?)");
    case 3:  return F("MQTT_CONNECT_UNAVAILABLE");
    case 4:  return F("MQTT_CONNECT_BAD_CREDENTIALS");
    case 5:  return F("MQTT_CONNECT_UNAUTHORIZED (broker ACL denies this client)");
    default: return F("UNKNOWN");
  }
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

  Serial.print("MQTT message: ");
  Serial.print(incomingTopic);
  Serial.print(" -> ");
  Serial.println(message);

  if (incomingTopic == mqttTestTopic()) {
    mqttTestEchoPayload = message;
    mqttTestEchoMs = millis();
    return;
  }

  // Per-relay boolean switch topics: trofis/enms/<device>/control/slave_N,
  // payload is a bare "0" or "1". Matched by exact topic, not by payload
  // content, so it doesn't interact with the generic parsing below.
  for (size_t i = 0; i < 4; i++) {
    if (incomingTopic == mqttSwitchTopic(i)) {
      if (message == "1") {
        requestRelayState(i, 1, "mqtt_switch");
      } else if (message == "0") {
        requestRelayState(i, 0, "mqtt_switch");
      } else {
        Serial.print("MQTT switch topic ignored non-boolean payload: ");
        Serial.println(message);
      }
      return;
    }
  }

  String upperMsg = message;
  upperMsg.toUpperCase();

  // Parse R1=1, R1=0, R2=1, R2=0, R3=1, R3=0, R4=1, R4=0
  for (size_t r = 1; r <= 4; r++) {
    String tag1 = "R" + String(r) + "=1";
    String tag0 = "R" + String(r) + "=0";
    String json1 = "\"R" + String(r) + "\":1";
    String json0 = "\"R" + String(r) + "\":0";

    if (upperMsg.indexOf(tag1) >= 0 || upperMsg.indexOf(json1) >= 0) {
      requestRelayState(r - 1, 1, "mqtt");
      return;
    }
    if (upperMsg.indexOf(tag0) >= 0 || upperMsg.indexOf(json0) >= 0) {
      requestRelayState(r - 1, 0, "mqtt");
      return;
    }
  }

  // Parse legacy set_relay / reset_relay
  // Check RESET_RELAY first: "RESET_RELAY" contains "SET_RELAY" as a substring,
  // so checking SET_RELAY first would misfire on reset commands.
  if (upperMsg.indexOf("RESET_RELAY") >= 0 || upperMsg.indexOf("\"ACTION\":\"RESET_RELAY\"") >= 0) {
    for (size_t r = 0; r < 4; r++) requestRelayState(r, 0, "mqtt_legacy", false);
    publishMqttControlState();
    return;
  }
  if (upperMsg.indexOf("SET_RELAY") >= 0 || upperMsg.indexOf("\"ACTION\":\"SET_RELAY\"") >= 0) {
    for (size_t r = 0; r < 4; r++) requestRelayState(r, 1, "mqtt_legacy", false);
    publishMqttControlState();
    return;
  }
}

void configureMqttClient() {
  (*activeMqttClient).setServer(currentMqttConfig.host, currentMqttConfig.port);
  (*activeMqttClient).setBufferSize(1024);
  (*activeMqttClient).setKeepAlive(30);
  (*activeMqttClient).setCallback(mqttMessageCallback);
  mqttClientConfigured = true;
}

bool connectMqtt(uint32_t nowMs) {
  const bool networkAvailable = ethLinkUp || (WiFi.status() == WL_CONNECTED);
  if (!currentMqttConfig.enabled || !networkAvailable || currentMqttConfig.host[0] == '\0') {
    mqttConnected = false;
    return false;
  }

  // Select transport: Ethernet preferred, fallback WiFi
  PubSubClient* preferred = ethLinkUp ? &mqttEthClient : &mqttWifiClient;
  if (activeMqttClient != preferred) {
    if ((*activeMqttClient).connected()) (*activeMqttClient).disconnect();
    activeMqttClient = preferred;
    mqttClientConfigured = false;
  }

  if (!mqttClientConfigured) {
    configureMqttClient();
  }

  if ((*activeMqttClient).connected()) {
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
    ok = (*activeMqttClient).connect(clientId.c_str(),
                            currentMqttConfig.username,
                            currentMqttConfig.password,
                            willTopic.c_str(),
                            0,
                            true,
                            willPayload.c_str());
  } else {
    ok = (*activeMqttClient).connect(clientId.c_str(),
                            willTopic.c_str(),
                            0,
                            true,
                            willPayload.c_str());
  }

  if (!ok) {
    mqttConnected = false;
    Serial.print("MQTT connect failed, rc=");
    Serial.println((*activeMqttClient).state());
    return false;
  }

  const String cmdTopic = mqttCommandTopic();
  const String relaySetTopic = getMqttBaseTopic() + "/relay/set";
  const bool subCmdOk = (*activeMqttClient).subscribe(cmdTopic.c_str());
  const bool subRelayOk = (*activeMqttClient).subscribe(relaySetTopic.c_str());
  mqttConnected = true;
  Serial.println("MQTT connected");
  Serial.print("MQTT subscribed: ");
  Serial.print(cmdTopic);
  Serial.print(subCmdOk ? " (ok)" : " (FAILED)");
  Serial.print(", ");
  Serial.print(relaySetTopic);
  Serial.println(subRelayOk ? " (ok)" : " (FAILED)");

  for (size_t i = 0; i < 4; i++) {
    const String switchTopic = mqttSwitchTopic(i);
    const bool subSwitchOk = (*activeMqttClient).subscribe(switchTopic.c_str());
    Serial.print("MQTT subscribed: ");
    Serial.print(switchTopic);
    Serial.println(subSwitchOk ? " (ok)" : " (FAILED)");
  }

  const String testTopic = mqttTestTopic();
  const bool subTestOk = (*activeMqttClient).subscribe(testTopic.c_str());
  Serial.print("MQTT subscribed: ");
  Serial.print(testTopic);
  Serial.println(subTestOk ? " (ok)" : " (FAILED)");

  publishMqttState();
  publishMqttControlState();
  return true;
}

// Real wall-clock HH:MM:SS — no forced ":00". As long as NTP is synced,
// the minute always reflects the actual local time the payload was built,
// whether that's a scheduled tick or a manual "Push Data Now".
String getFormattedTimestamp() {
  if (!timeSynced) {
    return String("not_synced");
  }
  time_t nowEpoch = time(nullptr);
  struct tm timeInfo {};
  if (!localtime_r(&nowEpoch, &timeInfo)) {
    return String("not_synced");
  }
  char buffer[32] = {};
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
  return String(buffer);
}

String buildMqttEnergyPayload(const MeterSnapshot& snapshot, uint8_t slaveId) {
  String body;
  body.reserve(1024);
  body += F("{\"timestamp\":\"");
  body += getFormattedTimestamp();
  body += F("\",\"slave_id\":");
  body += String(slaveId);

  auto formatVal = [&snapshot](float val, int decimals) -> String {
    if (!snapshot.online || isnan(val)) return String("0.") + String("000").substring(0, decimals);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, val);
    return String(buf);
  };

  body += F(",\"voltage\":{");
  body += F("\"ua\":\""); body += formatVal(snapshot.ua, 1);
  body += F("\",\"ub\":\""); body += formatVal(snapshot.ub, 1);
  body += F("\",\"uc\":\""); body += formatVal(snapshot.uc, 1);
  body += F("\",\"uab\":\""); body += formatVal(snapshot.uab, 1);
  body += F("\",\"ubc\":\""); body += formatVal(snapshot.ubc, 1);
  body += F("\",\"uca\":\""); body += formatVal(snapshot.uca, 1);
  body += F("\"},\"current\":{");
  body += F("\"ia\":\""); body += formatVal(snapshot.ia, 3);
  body += F("\",\"ib\":\""); body += formatVal(snapshot.ib, 3);
  body += F("\",\"ic\":\""); body += formatVal(snapshot.ic, 3);
  body += F("\"},\"power\":{");
  body += F("\"active\":{");
  body += F("\"pa\":\""); body += formatVal(snapshot.pa, 1);
  body += F("\",\"pb\":\""); body += formatVal(snapshot.pb, 1);
  body += F("\",\"pc\":\""); body += formatVal(snapshot.pc, 1);
  body += F("\",\"total\":\""); body += formatVal(snapshot.p_total, 1);
  body += F("\"},\"reactive\":{");
  body += F("\"qa\":\""); body += formatVal(snapshot.qa, 1);
  body += F("\",\"qb\":\""); body += formatVal(snapshot.qb, 1);
  body += F("\",\"qc\":\""); body += formatVal(snapshot.qc, 1);
  body += F("\",\"total\":\""); body += formatVal(snapshot.q_total, 1);
  body += F("\"},\"apparent\":{");
  body += F("\"sa\":\""); body += formatVal(snapshot.sa, 1);
  body += F("\",\"sb\":\""); body += formatVal(snapshot.sb, 1);
  body += F("\",\"sc\":\""); body += formatVal(snapshot.sc, 1);
  body += F("\",\"total\":\""); body += formatVal(snapshot.s_total, 1);
  body += F("\"}},\"power_factor\":{");
  body += F("\"pf1\":\""); body += formatVal(snapshot.pf1, 3);
  body += F("\",\"pf2\":\""); body += formatVal(snapshot.pf2, 3);
  body += F("\",\"pf3\":\""); body += formatVal(snapshot.pf3, 3);
  body += F("\",\"avg\":\""); body += formatVal(snapshot.pf_avg, 3);
  body += F("\"},\"frequency\":\"");
  body += formatVal(snapshot.frequency, 2);
  body += F("\"}");
  return body;
}

String buildMqttKwhPayload(const MeterSnapshot& snapshot, uint8_t slaveId) {
  String body;
  body.reserve(256);
  float kwh = isnan(snapshot.kwh_total) ? 0.0f : snapshot.kwh_total;
  int32_t wh = static_cast<int32_t>(round(kwh * 1000.0f));

  body += F("{\"timestamp\":\"");
  body += getFormattedTimestamp();
  body += F("\",\"slave_id\":");
  body += String(slaveId);
  body += F(",\"kwh\":");
  body += String(kwh, 2);
  body += F(",\"wh\":");
  body += String(wh);
  body += F("}");
  return body;
}

void publishMqttState() {
  if (!(*activeMqttClient).connected()) {
    return;
  }

  // Publish birth/online message on base state topic
  const String statePayload = F("{\"connected\":true}");
  (*activeMqttClient).publish(mqttStateTopic().c_str(), statePayload.c_str(), true);

  // Publish telemetry payloads for each of the 3 meters
  for (size_t m = 0; m < 3; m++) {
    const uint8_t slaveId = currentModbusConfig.slave_id[m] == 0 ? (m + 1) : currentModbusConfig.slave_id[m];
    const String energyTopic = getMqttBaseTopic() + "/elc_data/slave_" + String(slaveId);
    const String kwhTopic = getMqttBaseTopic() + "/elc_wh/slave_" + String(slaveId);

    const String energyPayload = buildMqttEnergyPayload(meterSnapshots[m], slaveId);
    (*activeMqttClient).publish(energyTopic.c_str(), energyPayload.c_str(), false);

    const String kwhPayload = buildMqttKwhPayload(meterSnapshots[m], slaveId);
    (*activeMqttClient).publish(kwhTopic.c_str(), kwhPayload.c_str(), false);
  }
}

void updateMqttRuntime(uint32_t nowMs) {
  if (!currentMqttConfig.enabled) {
    mqttConnected = false;
    if ((*activeMqttClient).connected()) {
      (*activeMqttClient).disconnect();
    }
    return;
  }

  if (!ethLinkUp && WiFi.status() != WL_CONNECTED) {
    mqttConnected = false;
    return;
  }

  if (!mqttClientConfigured) {
    configureMqttClient();
  } else {
    (*activeMqttClient).setServer(currentMqttConfig.host, currentMqttConfig.port);
  }

  if (!(*activeMqttClient).connected()) {
    connectMqtt(nowMs);
  } else {
    (*activeMqttClient).loop();
    mqttConnected = true;
  }

  if ((*activeMqttClient).connected()) {
    const uint32_t intervalSec = static_cast<uint32_t>(currentMqttConfig.publish_interval_sec);
    bool dueToPublish = false;

    if (timeSynced && intervalSec >= 60) {
      // Wall-clock-aligned schedule: fire exactly on the minute marks that
      // are multiples of the configured interval (e.g. 5 min -> :00, :05,
      // :10, :15 ...), regardless of when the device booted or last
      // reconnected. mqttLastAlignedEpochMinute guards against firing more
      // than once inside the same minute (loop() runs far faster than 1Hz).
      const uint32_t intervalMin = intervalSec / 60;
      time_t nowEpoch = time(nullptr);
      struct tm ti{};
      if (intervalMin > 0 && localtime_r(&nowEpoch, &ti)) {
        const uint32_t epochMinute = static_cast<uint32_t>(nowEpoch / 60);
        if ((static_cast<uint32_t>(ti.tm_min) % intervalMin) == 0 &&
            epochMinute != mqttLastAlignedEpochMinute) {
          mqttLastAlignedEpochMinute = epochMinute;
          dueToPublish = true;
        }
      }
    } else if (intervalSec > 0) {
      // No time sync yet — fall back to the relative timer so telemetry
      // still flows; once NTP catches up the schedule snaps to wall-clock.
      dueToPublish = (nowMs - mqttLastPublishMs >= intervalSec * 1000UL);
    }

    if (dueToPublish) {
      mqttLastPublishMs = nowMs;
      publishMqttState();
    }
  }
}

void initDisplayRuntime() {
  if (!currentDisplayConfig.enabled) {
    displayReady = false;
    return;
  }

  display.begin();
  display.setContrast(currentDisplayConfig.brightness);
  displayReady = true;
  Serial.println("LCD display ready");
}

// One relay's status as a short "Rn:STATE" token, e.g. "R1:ON", "R3:TRIP".
String relayStatusToken(size_t index) {
  char token[10];
  snprintf(token, sizeof(token), "R%u:%s", static_cast<unsigned>(index + 1), buildRelayStateText(index).c_str());
  return String(token);
}

// Dedicated setup-mode screen: shown instead of the normal page rotation
// whenever the device is broadcasting its config AP, so the SSID/password/IP
// needed to connect are readable directly off the device.
void drawApInfoPage(U8G2* driver) {
  driver->clearBuffer();
  driver->setFont(u8g2_font_6x12_tf);

  driver->drawStr(0, 12, "WiFi Setup Mode");
  driver->drawStr(0, 28, getConfigApSsid().c_str());
  char passLine[24];
  snprintf(passLine, sizeof(passLine), "Pass: %s", kConfigApPassword);
  driver->drawStr(0, 42, passLine);
  driver->drawStr(0, 56, "192.168.4.1");

  driver->sendBuffer();
}

void drawDisplayPage(U8G2* driver, uint8_t pageIndex) {
  driver->clearBuffer();
  driver->setFont(u8g2_font_6x12_tf);

  const uint8_t page = pageIndex % 3;

  if (page == 0) {
    // Meter summary: device name + each meter's current kWh, or OFFLINE.
    const char* devName = currentDeviceConfig.device_name[0] != '\0' ? currentDeviceConfig.device_name : "SEMS AIoT";
    driver->drawStr(0, 12, devName);
    for (size_t m = 0; m < 3; m++) {
      const MeterSnapshot& snapshot = meterSnapshots[m];
      char line[24];
      if (snapshot.online) {
        snprintf(line, sizeof(line), "M%u : %.2f kWh", static_cast<unsigned>(m + 1), snapshot.energy);
      } else {
        snprintf(line, sizeof(line), "M%u : OFFLINE", static_cast<unsigned>(m + 1));
      }
      driver->drawStr(0, 28 + static_cast<int>(m) * 14, line);
    }
  } else if (page == 1) {
    // Network status: IP, WiFi, MQTT — one fact per line.
    driver->drawStr(0, 12, "NETWORK STATUS");
    String ipStr;
    if (ethLinkUp) {
      ipStr = "IP: " + Ethernet.localIP().toString();
    } else if (wifiConnected) {
      ipStr = "IP: " + WiFi.localIP().toString();
    } else if (configApStarted) {
      ipStr = "IP: 192.168.4.1 (AP)";
    } else {
      ipStr = "IP: -";
    }
    driver->drawStr(0, 28, ipStr.c_str());
    driver->drawStr(0, 42, wifiConnected ? "WiFi : Online" : "WiFi : Offline");
    driver->drawStr(0, 56, mqttConnected ? "MQTT : Online" : "MQTT : Offline");
  } else {
    // Relay status: all 4 channels + overall meter health.
    driver->drawStr(0, 12, "RELAY STATUS");
    const String row1 = relayStatusToken(0) + "  " + relayStatusToken(1);
    const String row2 = relayStatusToken(2) + "  " + relayStatusToken(3);
    driver->drawStr(0, 28, row1.c_str());
    driver->drawStr(0, 42, row2.c_str());
    driver->drawStr(0, 56, anyMeterOffline() ? "Meter: OFFLINE" : "Meter: OK");
  }

  driver->sendBuffer();
}

// Draw the 3-option Setting Menu overlay.
// Called from updateDisplayRuntime when oledInMenu==true.
static void drawSettingMenu(uint32_t nowMs) {
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);

  // Header
  display.setFont(u8g2_font_helvB08_tf);
  display.drawStr(0, 10, "SETTING MENU");
  display.drawHLine(0, 14, 128);
  display.setFont(u8g2_font_6x12_tf);

  // Option 0: Config/Normal Mode toggle
  const bool inCfg = (currentMode == AppMode::Config);
  if (oledMenuCursor == 0) display.drawStr(2, 27, "> 1. Boot Mode");
  else                     display.drawStr(10, 27, "1. Boot Mode");
  display.drawStr(98, 27, inCfg ? "[CFG]" : "[NRM]");

  // Option 1: View Info
  if (oledMenuCursor == 1) display.drawStr(2, 39, "> 2. View Info");
  else                     display.drawStr(10, 39, "2. View Info");

  // Option 2: Exit
  if (oledMenuCursor == 2) display.drawStr(2, 51, "> 3. Exit");
  else                     display.drawStr(10, 51, "3. Exit");

  // Bottom bar: hold-progress or hint
  display.drawHLine(0, 55, 128);
  display.setFont(u8g2_font_5x7_tf);
  const uint32_t pressDur = (configButtonPressedSinceMs > 0) ? (nowMs - configButtonPressedSinceMs) : 0;
  if (pressDur > 0) {
    // Fill progress bar proportional to 2s hold
    uint8_t bar = static_cast<uint8_t>(min(pressDur * 128 / 2000UL, 128UL));
    display.drawBox(0, 57, bar, 7);
  } else {
    display.drawStr(2, 63, "Tap:Next | Hold 2s:Select");
  }
  display.sendBuffer();
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

  // Config AP info screen takes priority over all navigation modes.
  if (configApStarted && !oledInMenu && !oledInfoModeActive) {
    if (nowMs - displayLastUpdateMs < 1000UL) return;
    displayLastUpdateMs = nowMs;
    drawApInfoPage(&display);
    return;
  }

  // Setting Menu: redraw every heartbeat tick or while button is held.
  if (oledInMenu) {
    if (nowMs - displayLastUpdateMs < 200UL) return;
    displayLastUpdateMs = nowMs;
    drawSettingMenu(nowMs);
    return;
  }

  // Info Mode (manual paging, pages 1-2): refresh every second.
  if (oledInfoModeActive) {
    if (nowMs - displayLastUpdateMs < 1000UL) return;
    displayLastUpdateMs = nowMs;
    drawDisplayPage(&display, displayPage);
    return;
  }

  // Default: Dashboard (page 0) — auto-refresh every second.
  if (displayPage != 0) {
    displayPage = 0;
  }
  if (nowMs - displayLastUpdateMs < 1000UL) return;
  displayLastUpdateMs = nowMs;
  drawDisplayPage(&display, 0);
}

void loadFeatureRuntime() {
  for (size_t m = 0; m < 3; m++) {
    loadHistoryRuntime(m);
  }
  ConfigManager::loadRelayStates(relayRequestedState);
  for (size_t r = 0; r < 4; r++) {
    uint8_t initSt = (currentProtectionConfig.relay_enabled && relayRequestedState[r] == 1) ? 1 : 0;
    setRelayOutput(r, initSt);
    relayTripUntilMs[r] = 0;
    relayOvercurrentSinceMs[r] = 0;
  }
}

void resetMeterSnapshot(MeterSnapshot& snapshot) {
  snapshot.online = false;
  snapshot.valid = false;
  snapshot.voltage = NAN;
  snapshot.current = NAN;
  snapshot.power = NAN;
  snapshot.frequency = NAN;
  snapshot.pf = NAN;
  snapshot.energy = NAN;
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
  Serial.print(" slaves=");
  Serial.print(cfg.slave_id[0]);
  Serial.print(",");
  Serial.print(cfg.slave_id[1]);
  Serial.print(",");
  Serial.print(cfg.slave_id[2]);
  Serial.print(" parity=");
  Serial.print(cfg.parity);
  Serial.print(" stop_bits=");
  Serial.println(cfg.stop_bits);
  Serial2.setTimeout(cfg.timeout_ms);
  currentModbusConfig = cfg;
}

// Temporarily reinitializes Serial2 with scan parameters from /configmod,
// without touching the persisted/active ModbusConfig used by production
// polling. pollModbusMeter() checks modbusTestModeActive and skips entirely
// while this is in effect, so the two never contend for Serial2.
void applyModbusTestModeConfig(uint32_t baudrate, uint8_t parity, uint8_t stopBits, uint16_t timeoutMs) {
  Serial2.end();
  delay(20);

  ModbusConfig tmp = currentModbusConfig;
  tmp.baudrate = baudrate;
  tmp.parity = parity;
  tmp.stop_bits = stopBits;
  tmp.timeout_ms = timeoutMs;

  Serial2.begin(baudrate, serialConfigFromModbus(tmp), PinMap::kRs485Rx, PinMap::kRs485Tx);
  Serial2.setTimeout(timeoutMs);

  modbusTestModeConfig = tmp;
  modbusTestModeActive = true;
  modbusTestModeLastActivityMs = millis();

  Serial.print("Modbus TEST mode applied: baud=");
  Serial.print(baudrate);
  Serial.print(" parity=");
  Serial.print(parity);
  Serial.print(" stop_bits=");
  Serial.println(stopBits);
}

// Ends the test-mode session and forces the next pollModbusMeter() call to
// fully re-init Serial2 back to the active production ModbusConfig (via
// applyModbusPortConfig's needsInit branch) — no duplicated init logic here.
void restoreModbusNormalMode() {
  if (!modbusTestModeActive) return;
  Serial2.end();
  delay(20);
  modbusPortReady = false;
  modbusTestModeActive = false;
  Serial.println("Modbus test mode: restored to production UART settings");
}

// Keeps the web UI and MQTT responsive during Modbus blocking waits: relay
// on/off requests must not be stuck behind a slow/timed-out RS485 read.
void serviceDuringModbusWait() {
  if (configWebStarted) {
    configServer.handleClient();
  }
  if ((*activeMqttClient).connected()) {
    (*activeMqttClient).loop();
  }
}

void delayServicing(uint32_t ms) {
  const uint32_t until = millis() + ms;
  do {
    serviceDuringModbusWait();
    delay(1);
  } while (millis() < until);
}

// Populated on a failed modbusReadRegisters() call (optional, /configmod
// diagnostics only) so "timeout_or_crc" can be told apart into its actual
// causes — no response at all vs. a different slave answering vs. a frame
// that arrived but failed CRC (wiring noise / parity mismatch).
struct ModbusReadDiag {
  bool anyBytesReceived = false;
  uint8_t respondingSlaveId = 0;
  bool slaveIdMismatch = false;
  bool crcMismatch = false;
  uint16_t bytesReceived = 0;
};

// Generalized register read shared by production polling and the /configmod
// scan tool. functionCode is 0x03 (Holding Register) or 0x04 (Input
// Register) — both share the exact same request/response frame shape
// ("byte count + N*2 register bytes"), only the function byte differs.
bool modbusReadRegisters(uint8_t functionCode, uint8_t slaveId, uint16_t startRegister, uint16_t quantity, uint8_t* response, size_t responseLen, uint16_t& exceptionCode, uint32_t timeoutMs, ModbusReadDiag* diag = nullptr) {
  exceptionCode = 0;
  if (quantity == 0 || quantity > 64 || response == nullptr || responseLen < quantity * 2) {
    return false;
  }

  uint8_t request[8];
  request[0] = slaveId;
  request[1] = functionCode;
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
    serviceDuringModbusWait();
    delay(1);
  }
  if (Serial2.available() < 3) {
    if (diag) {
      diag->bytesReceived = Serial2.available();
      diag->anyBytesReceived = diag->bytesReceived > 0;
    }
    return false;
  }

  uint8_t header[3];
  if (Serial2.readBytes(header, sizeof(header)) != sizeof(header)) {
    return false;
  }

  if (diag) {
    diag->anyBytesReceived = true;
    diag->respondingSlaveId = header[0];
  }

  if (header[0] != slaveId) {
    if (diag) diag->slaveIdMismatch = true;
    return false;
  }

  if (header[1] & 0x80) {
    while (Serial2.available() < 2 && millis() < deadline) {
      serviceDuringModbusWait();
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
    serviceDuringModbusWait();
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

  uint8_t frame[3 + 128];
  frame[0] = header[0];
  frame[1] = header[1];
  frame[2] = header[2];
  memcpy(frame + 3, response, byteCount);
  const uint16_t expectedCrc = modbusCrc16(frame, 3 + byteCount);
  const uint16_t receivedCrc = static_cast<uint16_t>(crcBytes[0]) | (static_cast<uint16_t>(crcBytes[1]) << 8);
  const bool crcOk = expectedCrc == receivedCrc;
  if (diag && !crcOk) diag->crcMismatch = true;
  return crcOk;
}

// Thin wrapper kept for the existing production call sites (pollOneModbusMeter,
// readSchneiderFloat) — signature unchanged, always function code 0x03.
bool modbusReadHoldingRegisters(uint8_t slaveId, uint16_t startRegister, uint16_t quantity, uint8_t* response, size_t responseLen, uint16_t& exceptionCode, uint32_t timeoutMs) {
  return modbusReadRegisters(0x03, slaveId, startRegister, quantity, response, responseLen, exceptionCode, timeoutMs);
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

// Datatype: 0=UInt16, 1=Int16, 2=UInt32, 3=Int32, 4=Float32.
// Register count needed to hold a given datatype.
uint16_t modbusDataTypeQuantity(uint8_t datatype) {
  return (datatype == 0 || datatype == 1) ? 1 : 2;
}

// Shared decode logic used by both the /configmod scan-read endpoint and the
// "Custom Mapping" meter profile poll path. buf must hold at least
// modbusDataTypeQuantity(datatype)*2 bytes, big-endian (matches the raw
// int32 decode already used by pollOneModbusMeter's readVal1/readVal2).
float decodeModbusValue(uint8_t datatype, const uint8_t* buf, float scale) {
  switch (datatype) {
    case 0: {  // UInt16
      const uint16_t raw = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
      return raw * scale;
    }
    case 1: {  // Int16
      const int16_t raw = static_cast<int16_t>((static_cast<uint16_t>(buf[0]) << 8) | buf[1]);
      return raw * scale;
    }
    case 2: {  // UInt32
      const uint32_t raw = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
                            (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
      return static_cast<float>(raw) * scale;
    }
    case 3: {  // Int32
      const int32_t raw = (static_cast<int32_t>(buf[0]) << 24) | (static_cast<int32_t>(buf[1]) << 16) |
                           (static_cast<int32_t>(buf[2]) << 8) | static_cast<int32_t>(buf[3]);
      return static_cast<float>(raw) * scale;
    }
    case 4:  // Float32
    default:
      return decodeFloat32BigEndian(buf) * scale;
  }
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

void ensureModMapCacheLoaded() {
  if (!modMapCacheLoaded || modMapCacheDirty) {
    cachedModMapConfig = ConfigManager::loadModbusMapConfig();
    modMapCacheLoaded = true;
    modMapCacheDirty = false;
  }
}

// Maps a saved mapping entry's field_key (the fixed dot-path set offered in
// /configmod's "Field Name" dropdown) onto the matching MeterSnapshot field.
// Custom/unrecognized keys are silently ignored here — they remain saved in
// the mapping for future use but have no effect on the MQTT payload, since
// MeterSnapshot only has these fixed fields.
void applyMapEntryToSnapshot(const char* fieldKey, float value, MeterSnapshot& snapshot) {
  if (strcmp(fieldKey, "voltage.ua") == 0) snapshot.ua = value;
  else if (strcmp(fieldKey, "voltage.ub") == 0) snapshot.ub = value;
  else if (strcmp(fieldKey, "voltage.uc") == 0) snapshot.uc = value;
  else if (strcmp(fieldKey, "voltage.uab") == 0) snapshot.uab = value;
  else if (strcmp(fieldKey, "voltage.ubc") == 0) snapshot.ubc = value;
  else if (strcmp(fieldKey, "voltage.uca") == 0) snapshot.uca = value;
  else if (strcmp(fieldKey, "current.ia") == 0) snapshot.ia = value;
  else if (strcmp(fieldKey, "current.ib") == 0) snapshot.ib = value;
  else if (strcmp(fieldKey, "current.ic") == 0) snapshot.ic = value;
  else if (strcmp(fieldKey, "power.active.pa") == 0) snapshot.pa = value;
  else if (strcmp(fieldKey, "power.active.pb") == 0) snapshot.pb = value;
  else if (strcmp(fieldKey, "power.active.pc") == 0) snapshot.pc = value;
  else if (strcmp(fieldKey, "power.active.total") == 0) snapshot.p_total = value;
  else if (strcmp(fieldKey, "power.reactive.qa") == 0) snapshot.qa = value;
  else if (strcmp(fieldKey, "power.reactive.qb") == 0) snapshot.qb = value;
  else if (strcmp(fieldKey, "power.reactive.qc") == 0) snapshot.qc = value;
  else if (strcmp(fieldKey, "power.reactive.total") == 0) snapshot.q_total = value;
  else if (strcmp(fieldKey, "power.apparent.sa") == 0) snapshot.sa = value;
  else if (strcmp(fieldKey, "power.apparent.sb") == 0) snapshot.sb = value;
  else if (strcmp(fieldKey, "power.apparent.sc") == 0) snapshot.sc = value;
  else if (strcmp(fieldKey, "power.apparent.total") == 0) snapshot.s_total = value;
  else if (strcmp(fieldKey, "power_factor.pf1") == 0) snapshot.pf1 = value;
  else if (strcmp(fieldKey, "power_factor.pf2") == 0) snapshot.pf2 = value;
  else if (strcmp(fieldKey, "power_factor.pf3") == 0) snapshot.pf3 = value;
  else if (strcmp(fieldKey, "power_factor.avg") == 0) snapshot.pf_avg = value;
  else if (strcmp(fieldKey, "frequency") == 0) snapshot.frequency = value;
  else if (strcmp(fieldKey, "energy.kwh_total") == 0) snapshot.kwh_total = value;
}

// Custom Mapping profile (meter_profile == 2): reads each saved mapping
// entry for this slave individually (one register round-trip per entry,
// rather than the two big batched block-reads used by the hardcoded Renatta
// AX9L profile) and fills MeterSnapshot from the decoded values, so the
// existing elc_data/elc_wh MQTT payloads are populated the same way as
// before — no separate topic needed.
void pollOneModbusMeterCustomMapping(MeterSnapshot& snapshot, uint8_t slaveId, uint32_t timeoutMs, uint32_t nowMs) {
  snapshot.lastPollMs = nowMs;
  snapshot.online = false;
  snapshot.valid = false;

  ensureModMapCacheLoaded();

  bool anyOk = false;
  bool anyEntryForSlave = false;
  uint8_t buf[4];

  for (uint8_t i = 0; i < cachedModMapConfig.count; i++) {
    const ModbusMapEntry& entry = cachedModMapConfig.entries[i];
    if (entry.slave_id != slaveId) continue;
    anyEntryForSlave = true;

    const uint16_t quantity = modbusDataTypeQuantity(entry.datatype);
    uint16_t exceptionCode = 0;
    const bool ok = modbusReadRegisters(entry.function, slaveId, entry.address, quantity, buf, sizeof(buf), exceptionCode, timeoutMs);
    if (ok) {
      const float value = decodeModbusValue(entry.datatype, buf, entry.scale);
      applyMapEntryToSnapshot(entry.field_key, value, snapshot);
      anyOk = true;
    }
    delayServicing(20);  // brief RS485 bus settling between individual reads
  }

  if (!anyEntryForSlave) {
    snapshot.lastErrorMs = nowMs;
    snapshot.lastErrorCode = 2;  // no mapping saved for this slave
    return;
  }

  if (anyOk) {
    snapshot.online = true;
    snapshot.valid = true;
    snapshot.lastSuccessMs = nowMs;
    snapshot.voltage = snapshot.ua;
    snapshot.current = snapshot.ia;
    snapshot.power = isnan(snapshot.p_total) ? NAN : snapshot.p_total / 1000.0f;
    snapshot.pf = snapshot.pf_avg;
    snapshot.energy = snapshot.kwh_total;
  } else {
    snapshot.lastErrorMs = nowMs;
    snapshot.lastErrorCode = 1;
  }
}

void pollOneModbusMeter(MeterSnapshot& snapshot, uint8_t slaveId, uint32_t timeoutMs, uint32_t nowMs) {
  if (currentModbusConfig.meter_profile == 2) {
    pollOneModbusMeterCustomMapping(snapshot, slaveId, timeoutMs, nowMs);
    return;
  }

  snapshot.lastPollMs = nowMs;
  snapshot.online = false;
  snapshot.valid = false;

  uint8_t buf1[68]; // 34 registers * 2 bytes = 68 bytes (0x4000 s/d 0x4021)
  uint8_t buf2[60]; // 30 registers * 2 bytes = 60 bytes (0x4022 s/d 0x403F)
  uint16_t exceptionCode = 0;

  // Block 1: 0x4000 count 34
  bool ok1 = modbusReadHoldingRegisters(slaveId, 0x4000, 34, buf1, sizeof(buf1), exceptionCode, timeoutMs);

  // Delay 100ms to allow RS485 bus settling (servicing web/MQTT while we wait)
  delayServicing(100);

  // Block 2: 0x4022 count 30
  bool ok2 = modbusReadHoldingRegisters(slaveId, 0x4022, 30, buf2, sizeof(buf2), exceptionCode, timeoutMs);

  if (ok1 && ok2) {
    snapshot.online = true;
    snapshot.valid = true;
    snapshot.lastSuccessMs = nowMs;

    auto readVal1 = [&](uint16_t reg, float multiplier) -> float {
      uint16_t offset = (reg - 0x4000) * 2;
      int32_t raw = (static_cast<int32_t>(buf1[offset]) << 24) |
                    (static_cast<int32_t>(buf1[offset + 1]) << 16) |
                    (static_cast<int32_t>(buf1[offset + 2]) << 8) |
                    static_cast<int32_t>(buf1[offset + 3]);
      return raw * multiplier;
    };

    auto readVal2 = [&](uint16_t reg, float multiplier) -> float {
      uint16_t offset = (reg - 0x4022) * 2;
      int32_t raw = (static_cast<int32_t>(buf2[offset]) << 24) |
                    (static_cast<int32_t>(buf2[offset + 1]) << 16) |
                    (static_cast<int32_t>(buf2[offset + 2]) << 8) |
                    static_cast<int32_t>(buf2[offset + 3]);
      return raw * multiplier;
    };

    // Blok 1 values (0x4000 to 0x4021)
    snapshot.ua = readVal1(0x4000, 0.1f);
    snapshot.ub = readVal1(0x4002, 0.1f);
    snapshot.uc = readVal1(0x4004, 0.1f);
    snapshot.uab = readVal1(0x4006, 0.1f);
    snapshot.ubc = readVal1(0x4008, 0.1f);
    snapshot.uca = readVal1(0x400A, 0.1f);
    snapshot.ia = readVal1(0x400C, 0.001f);
    snapshot.ib = readVal1(0x400E, 0.001f);
    snapshot.ic = readVal1(0x4010, 0.001f);
    snapshot.pa = readVal1(0x4012, 0.1f);
    snapshot.pb = readVal1(0x4014, 0.1f);
    snapshot.pc = readVal1(0x4016, 0.1f);
    snapshot.p_total = readVal1(0x4018, 0.1f);
    snapshot.qa = readVal1(0x401A, 0.1f);
    snapshot.qb = readVal1(0x401C, 0.1f);
    snapshot.qc = readVal1(0x401E, 0.1f);
    snapshot.q_total = readVal1(0x4020, 0.1f);

    // Blok 2 values (0x4022 to 0x403F)
    snapshot.sa = readVal2(0x4022, 0.1f);
    snapshot.sb = readVal2(0x4024, 0.1f);
    snapshot.sc = readVal2(0x4026, 0.1f);
    snapshot.s_total = readVal2(0x4028, 0.1f);
    snapshot.pf1 = readVal2(0x402A, 0.001f);
    snapshot.pf2 = readVal2(0x402C, 0.001f);
    snapshot.pf3 = readVal2(0x402E, 0.001f);
    snapshot.pf_avg = readVal2(0x4030, 0.001f);
    snapshot.frequency = readVal2(0x4032, 0.01f);
    snapshot.kwh_total = readVal2(0x4034, 0.01f);
    snapshot.kvarh_total = readVal2(0x4036, 0.01f);
    snapshot.kwh_forward = readVal2(0x4038, 0.01f);
    snapshot.kwh_backward = readVal2(0x403A, 0.01f);
    snapshot.kvarh_forward = readVal2(0x403C, 0.01f);
    snapshot.kvarh_backward = readVal2(0x403E, 0.01f);

    // Compatibility fields
    snapshot.voltage = snapshot.ua;
    snapshot.current = snapshot.ia;
    snapshot.power = snapshot.p_total / 1000.0f; // W -> kW
    snapshot.pf = snapshot.pf_avg;
    snapshot.energy = snapshot.kwh_total;

    if (verboseLog) {
      Serial.print("Modbus meter ok: slave=");
      Serial.print(slaveId);
      Serial.print(" V_A=");
      Serial.print(snapshot.voltage, 1);
      Serial.print(" I_A=");
      Serial.print(snapshot.current, 3);
      Serial.print(" P_tot=");
      Serial.print(snapshot.power, 3);
      Serial.print(" F=");
      Serial.print(snapshot.frequency, 2);
      Serial.print(" E=");
      Serial.println(snapshot.energy, 2);
    }
  } else {
    snapshot.lastErrorMs = nowMs;
    snapshot.lastErrorCode = 1;
    snapshot.online = false;
    snapshot.valid = false;
    if (verboseLog) {
      Serial.print("Modbus meter read failed: slave=");
      Serial.print(slaveId);
      Serial.print(" ok1=");
      Serial.print(ok1 ? "true" : "false");
      Serial.print(", ok2=");
      Serial.println(ok2 ? "true" : "false");
    }
  }
}

static uint8_t modbusPollStep = 0;
static uint32_t lastModbusStepMs = 0;

void pollModbusMeter(uint32_t nowMs) {
  const ModbusConfig cfg = currentModbusConfigLoaded ? currentModbusConfig : ConfigManager::loadModbusConfig();
  currentModbusConfig = cfg;
  currentModbusConfigLoaded = true;

  if (currentMode != AppMode::Normal) {
    for (size_t m = 0; m < 3; m++) {
      resetMeterSnapshot(meterSnapshots[m]);
    }
    modbusPollStep = 0;
    return;
  }

  if (modbusTestModeActive) {
    // A /configmod test-mode session owns Serial2 exclusively; skip
    // production polling entirely rather than corrupting in-flight reads
    // on both sides.
    modbusPollStep = 0;
    return;
  }

  applyModbusPortConfig(cfg);

  if (modbusPollStep == 0) {
    if (nowMs - lastModbusPollMs >= cfg.poll_interval_ms) {
      modbusPollStep = 1;
      lastModbusStepMs = nowMs;
    }
    return;
  }

  if (nowMs - lastModbusStepMs < 50) {
    return;
  }
  lastModbusStepMs = nowMs;

  size_t m = modbusPollStep - 1;
  if (m < 3) {
    const uint8_t slaveId = cfg.slave_id[m] == 0 ? static_cast<uint8_t>(m + 1) : cfg.slave_id[m];
    pollOneModbusMeter(meterSnapshots[m], slaveId, cfg.timeout_ms, nowMs);
    modbusPollStep++;
  }

  if (modbusPollStep > 3) {
    modbusPollStep = 0;
    lastModbusPollMs = nowMs;
    updateHistoryRuntime(nowMs);
  }
}

void startNtpSync() {
  // NTP needs *some* IP-capable link, WiFi STA or Ethernet — not WiFi
  // specifically. Devices running LAN-only (no WiFi STA) used to never
  // reach this far because the check below only looked at WiFi.status(),
  // so timeSynced stayed false forever and every payload read "not_synced".
  if (WiFi.status() != WL_CONNECTED && !ethLinkUp) {
    Serial.println("NTP skipped: no network link (WiFi/ETH) up yet");
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
  String baseTopicStr = String(cfg.base_topic);
  baseTopicStr.trim();
  if (baseTopicStr.length() == 0 || baseTopicStr == "sems" || baseTopicStr == "semsiot") {
    baseTopicStr = "trofis/enms";
  }
  page += F("\"><label for=\"mqtt_base_topic\">Base Topic Prefix</label><input id=\"mqtt_base_topic\" maxlength=\"64\" placeholder=\"trofis/enms\" value=\"");
  page += htmlEscape(baseTopicStr);
  page += F("\"><label for=\"mqtt_publish_interval\">Publish Interval (menit)</label><input id=\"mqtt_publish_interval\" type=\"number\" min=\"1\" max=\"60\" value=\"");
  page += String(cfg.publish_interval_sec / 60);
  page += F("\"><div class=\"actions\"><button onclick=\"saveMqttConfig()\">Save MQTT Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section>");
  
  page += F("<section class=\"panel\"><h2>MQTT Topics Preview & Format Notes</h2><div class=\"muted\">");
  page += F("<p style=\"background:#f8fafc;padding:10px;border-radius:6px;border:1px solid #e2e8f0;\">");
  page += F("<strong>Topic Formation Rule:</strong><br>");
  page += F("<code>[Base Topic Prefix] / [Device Name] / [elc_data | elc_wh] / slave_[slave_id]</code></p>");
  page += F("<p><strong>Active Prefix:</strong> <code>");
  page += htmlEscape(getMqttBaseTopic());
  page += F("</code></p><p><strong>Target Topics per Slave:</strong></p><ul>");
  for (size_t m = 0; m < 3; m++) {
    const uint8_t slaveId = currentModbusConfig.slave_id[m] == 0 ? static_cast<uint8_t>(m + 1) : currentModbusConfig.slave_id[m];
    page += F("<li><strong>Slave ");
    page += String(slaveId);
    page += F(" Energy Sesaat:</strong> <code>");
    page += htmlEscape(getMqttBaseTopic() + "/elc_data/slave_" + String(slaveId));
    page += F("</code></li><li><strong>Slave ");
    page += String(slaveId);
    page += F(" kWh Kumulatif:</strong> <code>");
    page += htmlEscape(getMqttBaseTopic() + "/elc_wh/slave_" + String(slaveId));
    page += F("</code></li>");
  }
  page += F("</ul></div></section>");

  page += F("<section class=\"panel\"><h2>Connection &amp; Push Data</h2>");
  page += F("<p class=\"muted\">Push Data Now publishes the real state/energy/kWh/control-state payloads to their actual production topics immediately, without waiting for the Publish Interval — the same publish the periodic loop does, just triggered on demand. It also re-arms the interval countdown from this moment.</p>");
  page += F("<p>Connected: <strong id=\"mt_connected\">checking...</strong> &nbsp; Broker RC: <span id=\"mt_rc\">-</span> &nbsp; Transport: <span id=\"mt_transport\">-</span></p>");
  page += F("<p class=\"muted\">Client ID: <code id=\"mt_clientid\">-</code></p>");
  page += F("<div class=\"actions\"><button onclick=\"pushMqttTest()\">Push Data Now</button></div>");
  page += F("<p id=\"mt_result\" class=\"muted\">Ready.</p>");
  page += F("<p class=\"muted\">Published topics appear below after a push. A separate loopback message is also sent to <code id=\"mt_topic\">-</code> (not one of your data topics) purely to verify this client can both publish AND receive — if it doesn't echo back within a few seconds, the broker is likely blocking this client's subscriptions even though publishing still works.</p>");
  page += F("<ul id=\"mt_topics_list\" class=\"muted\"></ul></section></main>");

  page += F("<script>async function saveMqttConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("mqtt_enabled:document.getElementById('mqtt_enabled').checked?'1':'0',");
  page += F("mqtt_host:document.getElementById('mqtt_host').value,mqtt_port:document.getElementById('mqtt_port').value,");
  page += F("mqtt_username:document.getElementById('mqtt_username').value,mqtt_password:document.getElementById('mqtt_password').value,");
  page += F("mqtt_client_id:document.getElementById('mqtt_client_id').value,mqtt_base_topic:document.getElementById('mqtt_base_topic').value,");
  page += F("mqtt_publish_interval:document.getElementById('mqtt_publish_interval').value});try{const r=await fetch('/api/mqtt/save',{method:'POST',");
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}");
  page += F("catch(e){s.textContent='Save failed'}}</script>");

  page += F(R"MQTTTESTJS(<script>
let mtPollTimer = null;

function mtApplyStatus(d) {
  document.getElementById('mt_connected').textContent = d.mqtt_connected ? 'YES' : 'NO';
  document.getElementById('mt_rc').textContent = d.rc + ' (' + d.rc_text + ')';
  document.getElementById('mt_transport').textContent = d.transport;
  document.getElementById('mt_clientid').textContent = d.client_id;
  document.getElementById('mt_topic').textContent = d.test_topic;
  return d;
}

async function refreshMqttTestStatus() {
  try {
    const r = await fetch('/api/mqtt/test_status', { cache: 'no-store' });
    return mtApplyStatus(await r.json());
  } catch (e) {
    document.getElementById('mt_connected').textContent = 'unknown';
    return null;
  }
}

// Pushes the real production topics immediately (server-side), then polls
// briefly for the separate loopback echo — that echo only arrives if the
// broker lets this client subscribe too, so it's checked independently of
// whether the real-topic publish itself succeeded.
async function pushMqttTest() {
  if (mtPollTimer) { clearInterval(mtPollTimer); mtPollTimer = null; }
  const r0 = document.getElementById('mt_result');
  const list = document.getElementById('mt_topics_list');
  r0.textContent = 'Publishing...';
  list.innerHTML = '';
  try {
    const pr = await fetch('/api/mqtt/test_publish', { method: 'POST' });
    const pd = await pr.json();
    if (!pd.ok) {
      r0.textContent = pd.error === 'not_connected' ? 'Not connected to broker — cannot push.' : ('Publish failed (rc ' + pd.rc + ').');
      refreshMqttTestStatus();
      return;
    }
    r0.textContent = 'Pushed now — published ' + pd.topics.length + ' topic(s), interval countdown restarted. Checking loopback echo...';
    list.innerHTML = pd.topics.map(t => '<li><code>' + t + '</code></li>').join('');
  } catch (e) { r0.textContent = 'Publish request failed.'; return; }

  let attempts = 0;
  mtPollTimer = setInterval(async () => {
    attempts++;
    const d = await refreshMqttTestStatus();
    if (d && d.round_trip_ok) {
      clearInterval(mtPollTimer); mtPollTimer = null;
      r0.textContent = 'Pushed and confirmed — this client can both publish and subscribe (loopback echo received ' + d.echo_ms_ago + ' ms after push).';
    } else if (attempts >= 8) {
      clearInterval(mtPollTimer); mtPollTimer = null;
      r0.textContent = 'Pushed successfully, but the loopback echo never came back — this client likely cannot subscribe on this broker (check Client ID collision or broker ACL), even though its publishes above did go through.';
    }
  }, 1000);
}

refreshMqttTestStatus();
setInterval(refreshMqttTestStatus, 5000);
</script>)MQTTTESTJS");
  page += buildPageFooter();
  return page;
}

String buildModbusConfigPage() {
  const ModbusConfig& cfg = currentModbusConfig;
  const ModbusMapConfig modMapCfg = ConfigManager::loadModbusMapConfig();  // just for its (short) name label below
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
  page += F("<label for=\"modbus_slave_id_1\">Slave 1 Address</label><input id=\"modbus_slave_id_1\" type=\"number\" min=\"1\" max=\"247\" value=\"");
  page += String(cfg.slave_id[0]);
  page += F("\"><label for=\"modbus_slave_id_2\">Slave 2 Address</label><input id=\"modbus_slave_id_2\" type=\"number\" min=\"1\" max=\"247\" value=\"");
  page += String(cfg.slave_id[1]);
  page += F("\"><label for=\"modbus_slave_id_3\">Slave 3 Address</label><input id=\"modbus_slave_id_3\" type=\"number\" min=\"1\" max=\"247\" value=\"");
  page += String(cfg.slave_id[2]);
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
  page += F(">Renatta AX9L</option><option value=\"1\"");
  page += cfg.meter_profile == 1 ? F(" selected") : F("");
  page += F(">Generic float32</option><option value=\"2\"");
  page += cfg.meter_profile == 2 ? F(" selected") : F("");
  page += F(">Custom Mapping");
  if (strlen(modMapCfg.name) > 0) {
    page += F(" (");
    page += htmlEscape(modMapCfg.name);
    page += F(")");
  }
  page += F("</option></select>");
  page += F("<div class=\"actions\"><button onclick=\"saveModbusConfig()\">Save Modbus Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section></main>");
  page += F("<script>async function saveModbusConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("modbus_baudrate:document.getElementById('modbus_baudrate').value,modbus_slave_id_1:document.getElementById('modbus_slave_id_1').value,");
  page += F("modbus_slave_id_2:document.getElementById('modbus_slave_id_2').value,modbus_slave_id_3:document.getElementById('modbus_slave_id_3').value,");
  page += F("modbus_parity:document.getElementById('modbus_parity').value,modbus_stop_bits:document.getElementById('modbus_stop_bits').value,");
  page += F("modbus_poll_interval:document.getElementById('modbus_poll_interval').value,modbus_timeout:document.getElementById('modbus_timeout').value,");
  page += F("modbus_retry_count:document.getElementById('modbus_retry_count').value,modbus_profile:document.getElementById('modbus_profile').value});");
  page += F("try{const r=await fetch('/api/modbus/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});");
  page += F("const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}catch(e){s.textContent='Save failed'}}</script>");
  page += buildPageFooter();
  return page;
}

// ============================================================================
// /configmod — hidden Modbus register scan & mapping tool
// ============================================================================
// Never linked from <nav> anywhere (see buildPageHeader in WebUiPages.inc) —
// reachable only by typing the URL directly. Still gated by
// requireConfigMode() (PIN-unlock flow), same as every other config page.
String buildModConfigPage() {
  String page = buildPageHeader("Modbus Register Mapping");
  page.reserve(9000);
  page += F("<section class=\"panel\"><h2>UART Test Mode</h2>");
  page += F("<p class=\"muted\">Applies temporarily for scanning only — does NOT change the active Modbus Configuration. Production meter polling is paused while a test session is active, and auto-restores after 5 minutes idle.</p>");
  page += F("<p>Status: <span id=\"testStatus\">checking...</span></p>");
  page += F("<label for=\"cm_baud\">Baudrate</label><select id=\"cm_baud\">");
  page += F("<option value=\"4800\">4800</option><option value=\"9600\" selected>9600</option>");
  page += F("<option value=\"19200\">19200</option><option value=\"38400\">38400</option>");
  page += F("<option value=\"57600\">57600</option><option value=\"115200\">115200</option></select>");
  page += F("<label for=\"cm_databits\">Data Bits</label><select id=\"cm_databits\"><option value=\"8\">8</option></select>");
  page += F("<label for=\"cm_parity\">Parity</label><select id=\"cm_parity\">");
  page += F("<option value=\"2\" selected>None</option><option value=\"0\">Even</option><option value=\"1\">Odd</option></select>");
  page += F("<label for=\"cm_stopbits\">Stop Bit</label><select id=\"cm_stopbits\"><option value=\"1\" selected>1</option><option value=\"2\">2</option></select>");
  page += F("<label for=\"cm_timeout\">Timeout (ms)</label><input id=\"cm_timeout\" type=\"number\" min=\"100\" max=\"10000\" value=\"1000\">");
  page += F("<div class=\"actions\"><button onclick=\"applyTestUart()\">Apply Test UART</button><button type=\"button\" onclick=\"restoreUart()\">Restore Normal</button></div>");
  page += F("<p id=\"uartMsg\" class=\"muted\">Ready.</p></section>");

  page += F("<section class=\"panel\"><h2>Read Registers</h2>");
  page += F("<p class=\"muted\">Modbus-Poll style block read: set Slave ID / Function / Address / Quantity and click Read — every register in the range is fetched in one request and listed below. Pick a Field Name per register (or select two adjacent registers and merge them into one 32-bit field) to add it to the Mapping Table.</p>");
  page += F("<label for=\"cm_slave\">Slave ID</label><input id=\"cm_slave\" type=\"number\" min=\"1\" max=\"247\" value=\"1\">");
  page += F("<label for=\"cm_function\">Function</label><select id=\"cm_function\" onchange=\"updatePlcPreview()\">");
  page += F("<option value=\"3\" selected>03 - Holding Register</option><option value=\"4\">04 - Input Register</option></select>");
  page += F("<label for=\"cm_address\">Address (decimal)</label><input id=\"cm_address\" type=\"number\" min=\"0\" max=\"65535\" value=\"0\" oninput=\"updatePlcPreview()\">");
  page += F("<p class=\"muted\">PLC Address: <span id=\"cm_plc\">400001</span></p>");
  page += F("<label for=\"cm_quantity\">Quantity</label><input id=\"cm_quantity\" type=\"number\" min=\"1\" max=\"32\" value=\"10\">");
  page += F("<div class=\"actions\"><button onclick=\"readBlock()\">Read</button></div>");
  page += F("<p id=\"readMsg\" class=\"muted\">Apply Test UART first, then click Read.</p>");
  page += F("<div style=\"overflow-x:auto\"><table style=\"width:100%;border-collapse:collapse\" id=\"blockTable\">");
  page += F("<thead><tr style=\"text-align:left;border-bottom:2px solid #cbd5e1\"><th style=\"padding:6px\"></th><th style=\"padding:6px\">PLC Address</th><th style=\"padding:6px\">UInt16</th><th style=\"padding:6px\">Int16</th><th style=\"padding:6px\">Field Name</th><th style=\"padding:6px\">Add as</th></tr></thead>");
  page += F("<tbody id=\"blockTableBody\"></tbody></table></div>");
  page += F("<div class=\"actions\"><label for=\"cm_mergetype\">Merge selected (2 adjacent) as</label><select id=\"cm_mergetype\"><option value=\"2\">UInt32</option><option value=\"3\" selected>Int32</option><option value=\"4\">Float32</option></select><button type=\"button\" onclick=\"mergeSelectedAsRow32()\">Merge Selected &rarr;</button></div>");
  page += F("<p id=\"mergeMsg\" class=\"muted\"></p></section>");

  page += F("<section class=\"panel\"><h2>Mapping Table</h2>");
  page += F("<label for=\"cm_mapname\">Mapping Name</label><input id=\"cm_mapname\" type=\"text\" maxlength=\"39\" placeholder=\"e.g. Panel A Custom\">");
  page += F("<p class=\"muted\"><span id=\"rowCount\">0</span>/24 entries used. \"Value\" is the raw decoded register; \"Data\" = Value x Faktor Pengali (matches your meter's datasheet unit, e.g. 0.1V, 0.001A).</p>");
  page += F("<div style=\"overflow-x:auto\"><table style=\"width:100%;border-collapse:collapse\" id=\"mapTable\">");
  page += F("<thead><tr style=\"text-align:left;border-bottom:2px solid #cbd5e1\"><th style=\"padding:6px\">PLC Address</th><th style=\"padding:6px\">Value</th><th style=\"padding:6px\">Field Name</th><th style=\"padding:6px\">Faktor Pengali</th><th style=\"padding:6px\">Data</th><th style=\"padding:6px\"></th></tr></thead>");
  page += F("<tbody id=\"mapTableBody\"></tbody></table></div>");
  page += F("<div class=\"actions\"><button onclick=\"saveMapping()\">Save Mapping</button></div>");
  page += F("<p id=\"saveMsg\" class=\"muted\">Ready.</p></section></main>");

  page += F(R"CFGMODJS(<script>
const FIELD_KEYS = [
  ["voltage.ua","Voltage A (ua)"],["voltage.ub","Voltage B (ub)"],["voltage.uc","Voltage C (uc)"],
  ["voltage.uab","Voltage AB (uab)"],["voltage.ubc","Voltage BC (ubc)"],["voltage.uca","Voltage CA (uca)"],
  ["current.ia","Current A (ia)"],["current.ib","Current B (ib)"],["current.ic","Current C (ic)"],
  ["power.active.pa","Active Power A"],["power.active.pb","Active Power B"],["power.active.pc","Active Power C"],["power.active.total","Active Power Total"],
  ["power.reactive.qa","Reactive Power A"],["power.reactive.qb","Reactive Power B"],["power.reactive.qc","Reactive Power C"],["power.reactive.total","Reactive Power Total"],
  ["power.apparent.sa","Apparent Power A"],["power.apparent.sb","Apparent Power B"],["power.apparent.sc","Apparent Power C"],["power.apparent.total","Apparent Power Total"],
  ["power_factor.pf1","Power Factor 1"],["power_factor.pf2","Power Factor 2"],["power_factor.pf3","Power Factor 3"],["power_factor.avg","Power Factor Avg"],
  ["frequency","Frequency"],["energy.kwh_total","Energy kWh Total"]
];
let mappingRows = [];
let nextRowId = 1;
let pollActive = false;
let pollTimer = null;

function plcAddress(fn, addr) {
  addr = parseInt(addr, 10);
  if (isNaN(addr)) return '';
  switch (String(fn)) {
    case '1': return String(1 + addr).padStart(5, '0');
    case '2': return String(10001 + addr);
    case '3': return String(40001 + addr);
    case '4': return String(30001 + addr);
    default: return '';
  }
}

function fmt(v) {
  return (v === null || v === undefined || isNaN(v)) ? '-' : Number(v).toFixed(4);
}

function updatePlcPreview() {
  const fn = document.getElementById('cm_function').value;
  const addr = document.getElementById('cm_address').value;
  document.getElementById('cm_plc').textContent = plcAddress(fn, addr) || '-';
}

function fieldKeySelectHtml(row) {
  let html = '<select onchange="onFieldSelectChange(' + row.id + ',this.value)">';
  html += '<option value="__custom__"' + (row.key === '__custom__' ? ' selected' : '') + '>Custom…</option>';
  for (const [k, label] of FIELD_KEYS) {
    html += '<option value="' + k + '"' + (row.key === k ? ' selected' : '') + '>' + label + '</option>';
  }
  html += '</select>';
  if (row.key === '__custom__') {
    html += '<br><input type="text" placeholder="custom.key" value="' + (row.customKey || '').replace(/"/g, '&quot;') + '" oninput="onCustomKeyInput(' + row.id + ',this.value)">';
  }
  return html;
}

// Full rebuild — only called when rows are added/removed/loaded, never on
// every poll tick, so an in-progress Faktor Pengali edit or Field Name pick
// never loses focus while continuous reading is running.
function renderTable() {
  const body = document.getElementById('mapTableBody');
  body.innerHTML = mappingRows.map(row => {
    const plc = plcAddress(row.fn, row.addr);
    const sub = 'slave ' + row.slave + ' · fn' + String(row.fn).padStart(2, '0') + ' · addr ' + row.addr;
    return '<tr style="border-bottom:1px solid #e5e7eb">' +
      '<td style="padding:6px">' + plc + '<br><span class="muted" style="font-size:11px">' + sub + '</span></td>' +
      '<td style="padding:6px" id="val_' + row.id + '">' + fmt(row.raw) + '</td>' +
      '<td style="padding:6px">' + fieldKeySelectHtml(row) + '</td>' +
      '<td style="padding:6px"><input type="number" step="any" value="' + row.factor + '" style="width:90px" oninput="onFactorInput(' + row.id + ',this.value)"></td>' +
      '<td style="padding:6px" id="data_' + row.id + '">' + fmt(row.data) + '</td>' +
      '<td style="padding:6px"><button type="button" onclick="removeRow(' + row.id + ')">Remove</button></td>' +
      '</tr>';
  }).join('');
  document.getElementById('rowCount').textContent = mappingRows.length;
}

// Lightweight update used every poll tick — only touches the Value/Data
// text cells directly, leaving selects/inputs (and their focus/cursor) alone.
function updatePollValues() {
  mappingRows.forEach(row => {
    const ve = document.getElementById('val_' + row.id);
    const de = document.getElementById('data_' + row.id);
    if (ve) ve.textContent = fmt(row.raw);
    if (de) de.textContent = fmt(row.data);
  });
}

function onFieldSelectChange(id, val) {
  const row = mappingRows.find(r => r.id === id);
  if (row) { row.key = val; renderTable(); }
}

function onCustomKeyInput(id, val) {
  const row = mappingRows.find(r => r.id === id);
  if (row) row.customKey = val;
}

function onFactorInput(id, val) {
  const row = mappingRows.find(r => r.id === id);
  if (!row) return;
  row.factor = parseFloat(val);
  if (isNaN(row.factor)) row.factor = 0;
  row.data = (row.raw === null || row.raw === undefined) ? null : row.raw * row.factor;
  updatePollValues();
}

function removeRow(id) {
  mappingRows = mappingRows.filter(r => r.id !== id);
  renderTable();
}

async function refreshStatus() {
  try {
    const r = await fetch('/api/modmap/status', { cache: 'no-store' });
    const d = await r.json();
    document.getElementById('testStatus').textContent = d.test_mode_active ? 'ACTIVE (production polling paused)' : 'INACTIVE (production polling normal)';
    return d.test_mode_active;
  } catch (e) {
    document.getElementById('testStatus').textContent = 'unknown';
    return false;
  }
}

async function applyTestUart() {
  const m = document.getElementById('uartMsg');
  m.textContent = 'Applying...';
  const body = new URLSearchParams({
    baudrate: document.getElementById('cm_baud').value,
    parity: document.getElementById('cm_parity').value,
    stop_bits: document.getElementById('cm_stopbits').value,
    timeout_ms: document.getElementById('cm_timeout').value
  });
  try {
    const r = await fetch('/api/modmap/uart/apply', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
    const d = await r.json();
    m.textContent = d.ok ? 'Test UART applied. Continuous reading started.' : 'Apply failed';
    if (d.ok) startPolling();
  } catch (e) { m.textContent = 'Apply failed'; }
  refreshStatus();
}

async function restoreUart() {
  const m = document.getElementById('uartMsg');
  m.textContent = 'Restoring...';
  stopPolling();
  try {
    const r = await fetch('/api/modmap/uart/restore', { method: 'POST' });
    const d = await r.json();
    m.textContent = d.ok ? 'Restored to production settings.' : 'Restore failed';
  } catch (e) { m.textContent = 'Restore failed'; }
  refreshStatus();
}

let blockRows = [];      // last block-read result: {addr, uint16, int16, selected}
let blockSlave = 1, blockFn = 3;

function addMappingRow(slave, fn, addr, type) {
  if (mappingRows.length >= 24) { document.getElementById('readMsg').textContent = 'Mapping table is full (24/24).'; return false; }
  mappingRows.push({ id: nextRowId++, slave, fn, addr, type, factor: 1, raw: null, data: null, key: '__custom__', customKey: '' });
  renderTable();
  document.getElementById('readMsg').textContent = pollActive ? 'Added — value fills in on the next read sweep.' : 'Added — apply Test UART to start reading.';
  return true;
}

function renderBlockTable() {
  const body = document.getElementById('blockTableBody');
  body.innerHTML = blockRows.map(row => {
    const plc = plcAddress(blockFn, row.addr);
    return '<tr style="border-bottom:1px solid #e5e7eb">' +
      '<td style="padding:6px"><input type="checkbox" ' + (row.selected ? 'checked' : '') + ' onchange="onBlockRowSelect(' + row.addr + ',this.checked)"></td>' +
      '<td style="padding:6px">' + plc + '<br><span class="muted" style="font-size:11px">addr ' + row.addr + '</span></td>' +
      '<td style="padding:6px">' + row.uint16 + '</td>' +
      '<td style="padding:6px">' + row.int16 + '</td>' +
      '<td style="padding:6px"><select onchange="onBlockFieldChange(' + row.addr + ',this.value)"><option value="__custom__"' + (row.key === '__custom__' ? ' selected' : '') + '>Custom…</option>' +
        FIELD_KEYS.map(([k, label]) => '<option value="' + k + '"' + (row.key === k ? ' selected' : '') + '>' + label + '</option>').join('') + '</select></td>' +
      '<td style="padding:6px"><select id="type_' + row.addr + '" style="width:80px"><option value="0">UInt16</option><option value="1">Int16</option></select>' +
        '<button type="button" onclick="addBlockRowAsSingle(' + row.addr + ')">Add</button></td>' +
      '</tr>';
  }).join('');
}

function onBlockRowSelect(addr, checked) {
  const row = blockRows.find(r => r.addr === addr);
  if (row) row.selected = checked;
}

function onBlockFieldChange(addr, val) {
  const row = blockRows.find(r => r.addr === addr);
  if (row) row.key = val;
}

// Adds a single register from the block-read table into the Mapping Table,
// as UInt16 or Int16 per that row's "Add as" selector (there's no way to
// change datatype after a row lands in the Mapping Table, so it must be
// chosen here).
function addBlockRowAsSingle(addr) {
  const row = blockRows.find(r => r.addr === addr);
  if (!row) return;
  const typeSel = document.getElementById('type_' + addr);
  const type = typeSel ? parseInt(typeSel.value, 10) : 0;
  if (addMappingRow(blockSlave, blockFn, addr, type)) {
    const mrow = mappingRows[mappingRows.length - 1];
    mrow.key = row.key;
    renderTable();
  }
}

function mergeSelectedAsRow32() {
  const msg = document.getElementById('mergeMsg');
  const selected = blockRows.filter(r => r.selected).sort((a, b) => a.addr - b.addr);
  if (selected.length !== 2 || selected[1].addr !== selected[0].addr + 1) {
    msg.textContent = 'Select exactly 2 adjacent registers to merge.';
    return;
  }
  const type = parseInt(document.getElementById('cm_mergetype').value, 10);
  if (addMappingRow(blockSlave, blockFn, selected[0].addr, type)) {
    const mrow = mappingRows[mappingRows.length - 1];
    mrow.key = selected[0].key !== '__custom__' ? selected[0].key : selected[1].key;
    renderTable();
    selected.forEach(r => r.selected = false);
    renderBlockTable();
    msg.textContent = 'Merged addr ' + selected[0].addr + '+' + selected[1].addr + ' into Mapping Table.';
  }
}

async function readBlock() {
  const msg = document.getElementById('readMsg');
  msg.textContent = 'Reading...';
  blockSlave = parseInt(document.getElementById('cm_slave').value, 10);
  blockFn = parseInt(document.getElementById('cm_function').value, 10);
  const addr = parseInt(document.getElementById('cm_address').value, 10);
  const qty = parseInt(document.getElementById('cm_quantity').value, 10);
  const body = new URLSearchParams({ slave_id: blockSlave, function: blockFn, address: addr, quantity: qty });
  try {
    const r = await fetch('/api/modmap/read_block', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
    const d = await r.json();
    if (d.ok) {
      blockRows = d.registers.map(reg => ({ addr: reg.addr, uint16: reg.uint16, int16: reg.int16, selected: false, key: '__custom__' }));
      renderBlockTable();
      msg.textContent = 'Read ' + blockRows.length + ' register(s). Pick a Field Name and Add, or select two adjacent rows to merge as 32-bit.';
    } else if (d.error === 'test_mode_not_active') {
      msg.textContent = 'Apply Test UART first, then click Read.';
    } else {
      msg.textContent = 'Read failed: ' + (d.detail || d.error || 'unknown') + (d.exception_code ? (' (exception ' + d.exception_code + ')') : '');
    }
  } catch (e) { msg.textContent = 'Read failed.'; }
}

// Continuously reads every row in the table, one register at a time (the
// ESP32's web server handles one request at a time, so reads are sequenced
// rather than fired in parallel), then waits briefly before the next sweep —
// runs until stopPolling() is called (Restore Normal) or the server reports
// the test-mode session ended (e.g. the 5-minute idle safety net fired).
async function pollLoop() {
  if (!pollActive) return;
  for (const row of mappingRows) {
    if (!pollActive) break;
    try {
      const body = new URLSearchParams({ slave_id: row.slave, function: row.fn, address: row.addr, datatype: row.type });
      const r = await fetch('/api/modmap/read', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
      const d = await r.json();
      if (d.ok) {
        row.raw = d.decoded;
        row.data = row.raw * row.factor;
      } else {
        row.raw = null;
        row.data = null;
        if (d.error === 'test_mode_not_active') {
          document.getElementById('uartMsg').textContent = 'Test mode ended (idle timeout or restored elsewhere).';
          stopPolling();
          refreshStatus();
          return;
        }
      }
    } catch (e) {
      row.raw = null;
      row.data = null;
    }
  }
  updatePollValues();
  if (pollActive) pollTimer = setTimeout(pollLoop, 400);
}

function startPolling() {
  if (pollActive) return;
  pollActive = true;
  pollLoop();
}

function stopPolling() {
  pollActive = false;
  if (pollTimer) { clearTimeout(pollTimer); pollTimer = null; }
}

async function saveMapping() {
  const m = document.getElementById('saveMsg');
  m.textContent = 'Saving...';
  const params = new URLSearchParams();
  params.set('map_name', document.getElementById('cm_mapname').value || '');
  params.set('count', mappingRows.length);
  mappingRows.forEach((row, i) => {
    const key = row.key === '__custom__' ? (row.customKey || 'custom') : row.key;
    params.set('f' + i + '_key', key);
    params.set('f' + i + '_slave', row.slave);
    params.set('f' + i + '_fn', row.fn);
    params.set('f' + i + '_addr', row.addr);
    params.set('f' + i + '_type', row.type);
    params.set('f' + i + '_scale', row.factor);
  });
  try {
    const r = await fetch('/api/modmap/save', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: params });
    const d = await r.json();
    m.textContent = d.ok ? ('Saved ' + d.saved + ' entries.') : ('Save failed: ' + (d.error || 'unknown'));
  } catch (e) { m.textContent = 'Save failed'; }
}

async function loadMapping() {
  try {
    const r = await fetch('/api/modmap/load', { cache: 'no-store' });
    const d = await r.json();
    if (d.ok) {
      document.getElementById('cm_mapname').value = d.name || '';
      if (Array.isArray(d.entries)) {
        mappingRows = d.entries.map(e => ({
          id: nextRowId++, slave: e.slave, fn: e.fn, addr: e.addr, type: e.type, factor: e.scale,
          raw: null, data: null, key: FIELD_KEYS.some(fk => fk[0] === e.key) ? e.key : '__custom__',
          customKey: FIELD_KEYS.some(fk => fk[0] === e.key) ? '' : e.key
        }));
        renderTable();
      }
    }
  } catch (e) {}
}

async function init() {
  updatePlcPreview();
  const active = await refreshStatus();
  await loadMapping();
  if (active) startPolling();  // reopening the page mid-session resumes live reading
}
init();
</script>)CFGMODJS");

  page += buildPageFooter();
  return page;
}

String buildProtectionConfigPage() {
  const ProtectionConfig& cfg = currentProtectionConfig;
  String page = buildPageHeader("Protection Configuration");
  page.reserve(6500);

  const uint8_t defPins[4] = {2, 15, 14, 13};

  // 1. Realtime 4-Channel Relay Control Panel
  page += F("<section class=\"panel\"><h2>⚡ Realtime Relay Control</h2>");
  page += F("<div style=\"display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;margin:14px 0\">");
  for (size_t r = 0; r < 4; r++) {
    uint8_t pin = (cfg.relay_pin[r] == 0) ? defPins[r] : cfg.relay_pin[r];
    page += F("<div style=\"background:#f8fafc;border:1px solid #e2e8f0;border-radius:8px;padding:12px;text-align:center\">");
    page += F("<div style=\"font-weight:700\">Relay "); page += String(r + 1); page += F("</div>");
    page += F("<div class=\"muted\" style=\"font-size:12px\">GPIO "); page += String(pin); page += F("</div>");
    page += F("<div id=\"st_badge_"); page += String(r + 1); page += F("\" class=\"tag\" style=\"margin:6px 0;display:inline-block\">OFF</div>");
    page += F("<div><label class=\"sw\"><input type=\"checkbox\" id=\"r_sw_"); page += String(r + 1);
    page += F("\" onchange=\"toggleRelay("); page += String(r + 1); page += F(",this.checked)\"><span class=\"sl\"></span></label></div>");
    page += F("</div>");
  }
  page += F("</div>");
  page += F("<div class=\"actions\"><button type=\"button\" onclick=\"toggleAllRelays(true)\">ALL ON</button><button type=\"button\" onclick=\"toggleAllRelays(false)\">ALL OFF</button></div>");
  page += F("</section>");

  // 2. Relay Settings & Pin Mapping Panel
  page += F("<section class=\"panel\"><h2>Relay Protection & Pin Mapping Settings</h2>");
  page += F("<label><input type=\"checkbox\" id=\"relay_enabled\"");
  page += cfg.relay_enabled ? F(" checked") : F("");
  page += F("> Enable Relay Output System</label>");

  const uint8_t availPins[] = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};

  for (size_t r = 0; r < 4; r++) {
    uint8_t curPin = (cfg.relay_pin[r] == 0) ? defPins[r] : cfg.relay_pin[r];
    page += F("<label for=\"relay_pin_");
    page += String(r + 1);
    page += F("\">Relay ");
    page += String(r + 1);
    page += F(" GPIO Pin</label><select id=\"relay_pin_");
    page += String(r + 1);
    page += F("\">");
    for (uint8_t p : availPins) {
      page += F("<option value=\"");
      page += String(p);
      page += F("\"");
      if (p == curPin) page += F(" selected");
      page += F(">GPIO ");
      page += String(p);
      page += F("</option>");
    }
    page += F("</select>");
  }

  page += F("<hr style=\"margin:14px 0;border:none;border-top:1px solid #e2e8f0\">");
  page += F("<div class=\"muted\" style=\"font-size:12px;margin-bottom:6px\">MQTT control topic per relay: &lt;base&gt;/control/slave_&lt;N&gt; — independent from the Modbus meter slave IDs.</div>");
  for (size_t r = 0; r < 4; r++) {
    const uint8_t curSid = (cfg.relay_slave_id[r] == 0) ? static_cast<uint8_t>(r + 1) : cfg.relay_slave_id[r];
    page += F("<label for=\"relay_slave_id_");
    page += String(r + 1);
    page += F("\">Relay ");
    page += String(r + 1);
    page += F(" MQTT slave_N</label><input id=\"relay_slave_id_");
    page += String(r + 1);
    page += F("\" type=\"number\" min=\"1\" max=\"247\" value=\"");
    page += String(curSid);
    page += F("\">");
  }

  page += F("<label for=\"current_limit\">Current Limit per Meter (A)</label><input id=\"current_limit\" type=\"number\" min=\"1\" max=\"63\" value=\"");
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
  page += F("<div class=\"actions\"><button onclick=\"saveProtectionConfig()\">Save Protection Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section>");

  // JavaScript for saving config, live fetching, and toggling relays
  page += F("<script>");
  page += F("let isToggling=false;");
  page += F("function applyRelayStates(d){if(!d||!d.ok)return;const st=[d.r1,d.r2,d.r3,d.r4];for(let i=1;i<=4;i++){const s=st[i-1];const sw=document.getElementById('r_sw_'+i);const bg=document.getElementById('st_badge_'+i);if(!sw||!bg)continue;if(s===1){sw.checked=true;bg.textContent='ON';bg.style.background='#d1fae5';bg.style.color='#065f46';}else if(s===2){sw.checked=false;bg.textContent='TRIP';bg.style.background='#fee2e2';bg.style.color='#991b1b';}else{sw.checked=false;bg.textContent='OFF';bg.style.background='#eef2f6';bg.style.color='#475467';}}}");
  page += F("async function fetchRelayStates(){if(isToggling)return;try{const r=await fetch('/api/relay/state',{cache:'no-store'});const d=await r.json();applyRelayStates(d);}catch(e){}}");
  page += F("async function toggleRelay(r,on){isToggling=true;const bg=document.getElementById('st_badge_'+r);if(bg){bg.textContent=on?'ON':'OFF';bg.style.background=on?'#d1fae5':'#eef2f6';bg.style.color=on?'#065f46':'#475467';}const act=on?'set':'reset';try{const res=await fetch('/api/relay/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'relay='+r+'&action='+act});const d=await res.json();applyRelayStates(d);}catch(e){fetchRelayStates();}finally{isToggling=false;}}");
  page += F("async function toggleAllRelays(on){isToggling=true;for(let i=1;i<=4;i++){const sw=document.getElementById('r_sw_'+i);const bg=document.getElementById('st_badge_'+i);if(sw)sw.checked=on;if(bg){bg.textContent=on?'ON':'OFF';bg.style.background=on?'#d1fae5':'#eef2f6';bg.style.color=on?'#065f46':'#475467';}}const act=on?'set':'reset';try{const res=await fetch('/api/relay/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'relay=0&action='+act});const d=await res.json();applyRelayStates(d);}catch(e){fetchRelayStates();}finally{isToggling=false;}}");
  page += F("setInterval(fetchRelayStates,3000);fetchRelayStates();");
  page += F("async function saveProtectionConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("relay_enabled:document.getElementById('relay_enabled').checked?'1':'0',");
  page += F("relay_pin_1:document.getElementById('relay_pin_1').value,relay_pin_2:document.getElementById('relay_pin_2').value,");
  page += F("relay_pin_3:document.getElementById('relay_pin_3').value,relay_pin_4:document.getElementById('relay_pin_4').value,");
  page += F("relay_slave_id_1:document.getElementById('relay_slave_id_1').value,relay_slave_id_2:document.getElementById('relay_slave_id_2').value,");
  page += F("relay_slave_id_3:document.getElementById('relay_slave_id_3').value,relay_slave_id_4:document.getElementById('relay_slave_id_4').value,");
  page += F("current_limit:document.getElementById('current_limit').value,");
  page += F("trip_delay:document.getElementById('trip_delay').value,reset_mode:document.getElementById('reset_mode').value,");
  page += F("auto_retry_enabled:document.getElementById('auto_retry_enabled').checked?'1':'0',auto_retry_delay:document.getElementById('auto_retry_delay').value,");
  page += F("trip_on_stale:document.getElementById('trip_on_stale').checked?'1':'0'});try{const r=await fetch('/api/protection/save',{method:'POST',");
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}");
  page += F("catch(e){s.textContent='Save failed'}}");
  page += F("</script>");
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
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}");
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
  page.reserve(5500);
  page += F("<section class=\"panel\"><h2>System Settings</h2>");
  page += F("<label for=\"ntp_server1\">NTP Server 1</label><input id=\"ntp_server1\" maxlength=\"64\" placeholder=\"pool.ntp.org\" value=\"");
  page += htmlEscape(String(cfg.ntp_server1));
  page += F("\"><label for=\"ntp_server2\">NTP Server 2</label><input id=\"ntp_server2\" maxlength=\"64\" placeholder=\"time.google.com\" value=\"");
  page += htmlEscape(String(cfg.ntp_server2));
  page += F("\"><label><input type=\"checkbox\" id=\"debug_enabled\"");
  page += cfg.debug_enabled ? F(" checked") : F("");
  page += F("> Enable Debug Logging</label>");
  page += F("<div class=\"actions\"><button onclick=\"saveSystemConfig()\">Save System Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section>");

  // Firmware Update (OTA) panel
  page += F("<section class=\"panel\"><h2>Firmware Update (OTA)</h2>");
  page += F("<label for=\"ota_file\">Select firmware (.bin) file</label>");
  page += F("<input type=\"file\" id=\"ota_file\" accept=\".bin\" style=\"margin-bottom:15px; display:block; width:100%; border:1px solid #cbd5e1; padding:8px; border-radius:4px; background:#f8fafc; color:#1e293b;\">");
  page += F("<div class=\"actions\"><button onclick=\"startOtaUpdate()\">Upload & Flash Firmware</button></div>");
  page += F("<div id=\"ota_progress_container\" style=\"display:none; margin-top:15px; background:#e2e8f0; border-radius:4px; overflow:hidden; height:20px;\">");
  page += F("<div id=\"ota_progress_bar\" style=\"background:#3b82f6; width:0%; height:100%; text-align:center; color:#fff; font-size:12px; line-height:20px; transition:width 0.2s;\">0%</div>");
  page += F("</div><p id=\"otaState\" class=\"muted\">Ready.</p></section></main>");

  page += F("<script>async function saveSystemConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("ntp_server1:document.getElementById('ntp_server1').value,ntp_server2:document.getElementById('ntp_server2').value,");
  page += F("debug_enabled:document.getElementById('debug_enabled').checked?'1':'0'});try{const r=await fetch('/api/system/save',{method:'POST',");
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}");
  page += F("catch(e){s.textContent='Save failed'}}");

  // OTA JS functions
  page += F("function startOtaUpdate(){");
  page += F("const fi=document.getElementById('ota_file');const s=document.getElementById('otaState');");
  page += F("const c=document.getElementById('ota_progress_container');const b=document.getElementById('ota_progress_bar');");
  page += F("if(fi.files.length===0){alert('Please select a firmware .bin file first!');return;}");
  page += F("const file=fi.files[0];const fd=new FormData();fd.append('update',file);");
  page += F("s.textContent='Uploading firmware...';c.style.display='block';b.style.width='0%';b.textContent='0%';b.style.background='#3b82f6';");
  page += F("const xhr=new XMLHttpRequest();xhr.open('POST','/api/update',true);");
  page += F("xhr.upload.onprogress=function(e){if(e.lengthComputable){const pct=Math.round((e.loaded/e.total)*100);b.style.width=pct+'%';b.textContent=pct+'%';}};");
  page += F("xhr.onload=function(){if(xhr.status===200){const res=JSON.parse(xhr.responseText);if(res.ok){");
  page += F("s.textContent='Update successful! Device is rebooting, please wait...';b.style.background='#10b981';");
  page += F("setTimeout(()=>{document.body.insertAdjacentHTML('beforeend','<div style=\"position:fixed;inset:0;display:flex;align-items:center;justify-content:center;background:rgba(17,24,39,.86);color:#fff;font:600 18px/1.4 sans-serif;z-index:9999\">Rebooting after update...</div>');},1000);");
  page += F("setTimeout(()=>{window.location.href='/';},8000);");
  page += F("}else{s.textContent='Update failed: '+(res.error||'unknown');b.style.background='#ef4444';}}");
  page += F("else{s.textContent='Upload failed with status '+xhr.status;b.style.background='#ef4444';}};");
  page += F("xhr.onerror=function(){s.textContent='Upload error occurred!';b.style.background='#ef4444';};");
  page += F("xhr.send(fd);}");
  page += F("</script>");

  page += buildPageFooter();
  return page;
}

// ============================================================================
// CONFIG PAGE HANDLERS
// ============================================================================
bool requireConfigMode() {
  if (currentMode == AppMode::Config || configApStarted || ethLinkUp || webConfigUnlocked) {
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
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildMqttConfigPage());
}

void handleModbusConfigPage() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildModbusConfigPage());
}

// Hidden — never linked from <nav>; reachable only by typing /configmod.
void handleModConfigPage() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;
  configServer.send(200, "text/html", buildModConfigPage());
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

  // device_name feeds getMqttBaseTopic() (see line ~420), so every MQTT
  // topic — /cmd, /relay/set, /control/slave_N, /test, /state, etc. —
  // shifts the moment this changes. Force a reconnect so the client
  // re-subscribes under the new topics instead of sitting connected with
  // subscriptions still pinned to the old device name (silently breaking
  // relay/command control and the Push Test round-trip alike).
  const bool deviceNameChanged = strcmp(currentDeviceConfig.device_name, cfg.device_name) != 0;

  bool ok = ConfigManager::saveDeviceConfig(cfg);
  if (ok) {
    currentDeviceConfig = cfg;
    restartNtpSync();
    if (deviceNameChanged) {
      mqttClientConfigured = false;
      if ((*activeMqttClient).connected()) {
        (*activeMqttClient).disconnect();
      }
      mqttConnected = false;
    }
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
    // Field is entered/displayed in whole minutes now (so the wall-clock
    // aligned scheduler below always lands on a clean minute mark), stored
    // internally as seconds. Clamp to at least 1 minute.
    int minutes = configServer.arg("mqtt_publish_interval").toInt();
    if (minutes < 1) minutes = 1;
    cfg.publish_interval_sec = static_cast<uint16_t>(minutes * 60);
  }
  cfg.enabled = configServer.hasArg("mqtt_enabled") && configServer.arg("mqtt_enabled") == "1";

  bool ok = ConfigManager::saveMqttConfig(cfg);
  if (ok) {
    currentMqttConfig = cfg;
    mqttClientConfigured = false;
    if ((*activeMqttClient).connected()) {
      (*activeMqttClient).disconnect();
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

// "Push Data Now" — publishes the actual production payloads (state,
// elc_data/elc_wh per slave, control-state, switch states) to their real
// topics immediately, bypassing the publish-interval wait, exactly like the
// periodic loop in updateMqttRuntime() does. It ALSO republishes the
// diagnostic <base>/test message the device is subscribed to (see
// connectMqtt), so /api/mqtt/test_status can still report whether the echo
// comes back — that round-trip is what tells "connected but broker won't
// let this client subscribe" apart from "not connected at all", the exact
// gap reported for nocola_2. The real-topic publishes above don't need that
// round-trip to be useful on their own — they're what actually shows up for
// whatever's subscribed to production topics (e.g. nocola_2's dashboard).
void handleMqttTestPublishApi() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;

  if (!(*activeMqttClient).connected()) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"not_connected\"}");
    return;
  }

  publishMqttState();
  publishMqttControlState();
  mqttLastPublishMs = millis();  // restart the interval countdown from this manual push

  mqttTestSeq++;
  String payload;
  payload.reserve(96);
  payload += F("{\"seq\":");
  payload += String(mqttTestSeq);
  payload += F(",\"device\":\"");
  payload += jsonEscape(getMqttClientId());
  payload += F("\",\"uptime_ms\":");
  payload += String(millis());
  payload += F("}");

  const String testTopic = mqttTestTopic();
  const bool pubOk = (*activeMqttClient).publish(testTopic.c_str(), payload.c_str());

  mqttTestPublishOk = pubOk;
  mqttTestPublishMs = millis();
  mqttTestPublishPayload = payload;
  // Clear any stale echo from a previous push so test_status can't report a
  // false round-trip against this new attempt.
  mqttTestEchoPayload = "";
  mqttTestEchoMs = 0;

  String body;
  body.reserve(512);
  body += F("{\"ok\":");
  body += pubOk ? F("true") : F("false");
  body += F(",\"test_topic\":\"");
  body += jsonEscape(testTopic);
  body += F("\",\"rc\":");
  body += String((*activeMqttClient).state());
  body += F(",\"topics\":[\"");
  body += jsonEscape(mqttStateTopic());
  body += F("\"");
  for (size_t m = 0; m < 3; m++) {
    const uint8_t slaveId = currentModbusConfig.slave_id[m] == 0 ? static_cast<uint8_t>(m + 1) : currentModbusConfig.slave_id[m];
    body += F(",\"");
    body += jsonEscape(getMqttBaseTopic() + "/elc_data/slave_" + String(slaveId));
    body += F("\",\"");
    body += jsonEscape(getMqttBaseTopic() + "/elc_wh/slave_" + String(slaveId));
    body += F("\"");
  }
  body += F(",\"");
  body += jsonEscape(getMqttBaseTopic() + "/control-state");
  body += F("\"]}");
  configServer.send(200, "application/json", body);
}

// Polled by the /mqtt page after Push Test Data to see whether the broker
// echoed the message back (proving subscribe works, not just publish).
void handleMqttTestStatusApi() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;

  const int rc = (*activeMqttClient).state();
  const uint32_t nowMs = millis();
  const bool haveEcho = mqttTestEchoMs != 0;
  const bool roundTripOk = haveEcho && mqttTestEchoPayload == mqttTestPublishPayload;

  String body;
  body.reserve(384);
  body += F("{\"mqtt_connected\":");
  body += mqttConnected ? F("true") : F("false");
  body += F(",\"rc\":");
  body += String(rc);
  body += F(",\"rc_text\":\"");
  body += jsonEscape(String(mqttStateToString(rc)));
  body += F("\",\"client_id\":\"");
  body += jsonEscape(getMqttClientId());
  body += F("\",\"transport\":\"");
  body += (activeMqttClient == &mqttEthClient) ? F("Ethernet") : F("WiFi");
  body += F("\",\"test_topic\":\"");
  body += jsonEscape(mqttTestTopic());
  body += F("\",\"last_publish_ok\":");
  body += mqttTestPublishOk ? F("true") : F("false");
  body += F(",\"last_publish_payload\":\"");
  body += jsonEscape(mqttTestPublishPayload);
  body += F("\",\"last_publish_ms_ago\":");
  body += mqttTestPublishMs == 0 ? F("null") : String(nowMs - mqttTestPublishMs);
  body += F(",\"echo_received\":");
  body += haveEcho ? F("true") : F("false");
  body += F(",\"echo_payload\":\"");
  body += jsonEscape(mqttTestEchoPayload);
  body += F("\",\"echo_ms_ago\":");
  body += haveEcho ? String(nowMs - mqttTestEchoMs) : F("null");
  body += F(",\"round_trip_ok\":");
  body += roundTripOk ? F("true") : F("false");
  body += F("}");
  configServer.send(200, "application/json", body);
}

void handleModbusConfigSaveApi() {
  ModbusConfig cfg = currentModbusConfig;

  cfg.baudrate = configServer.hasArg("modbus_baudrate") ? configServer.arg("modbus_baudrate").toInt() : 9600;
  cfg.slave_id[0] = configServer.hasArg("modbus_slave_id_1") ? configServer.arg("modbus_slave_id_1").toInt() : 1;
  cfg.slave_id[1] = configServer.hasArg("modbus_slave_id_2") ? configServer.arg("modbus_slave_id_2").toInt() : 2;
  cfg.slave_id[2] = configServer.hasArg("modbus_slave_id_3") ? configServer.arg("modbus_slave_id_3").toInt() : 3;
  cfg.parity = configServer.hasArg("modbus_parity") ? configServer.arg("modbus_parity").toInt() : 2;
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

// ============================================================================
// /configmod API HANDLERS — hidden Modbus scan & mapping tool
// ============================================================================
// All handlers here explicitly call requireConfigMode(), even the POST
// endpoints (unlike some existing */save APIs) — deliberate hardening since
// this tool can transiently disrupt production polling.

void handleModMapUartApplyApi() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;

  const uint32_t baudrate = configServer.hasArg("baudrate") ? configServer.arg("baudrate").toInt() : 9600;
  const uint8_t parity = configServer.hasArg("parity") ? configServer.arg("parity").toInt() : 2;
  const uint8_t stopBits = configServer.hasArg("stop_bits") ? configServer.arg("stop_bits").toInt() : 1;
  const uint16_t timeoutMs = configServer.hasArg("timeout_ms") ? configServer.arg("timeout_ms").toInt() : 1000;

  applyModbusTestModeConfig(baudrate, parity, stopBits, timeoutMs);
  configServer.send(200, "application/json", "{\"ok\":true}");
}

void handleModMapUartRestoreApi() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;

  restoreModbusNormalMode();
  configServer.send(200, "application/json", "{\"ok\":true}");
}

// Reads a contiguous block of raw 16-bit registers in a single Modbus
// transaction (Modbus-Poll-style "Slave ID / Function / Address / Quantity"
// read) and returns each register's raw value undecoded — combining two
// adjacent registers into a 32-bit field is a separate client-side step
// (see mergeAsRow32 in the page script), not done here.
// Standard Modbus exception codes (the slave answered — correct ID, valid
// CRC — but refused the request). This is a *protocol-level* response, not
// a communication failure, so it must never be reported as timeout/CRC noise.
String modbusExceptionText(uint16_t code) {
  switch (code) {
    case 1: return F("ILLEGAL FUNCTION — this slave doesn't support function code 03/04 as requested");
    case 2: return F("ILLEGAL DATA ADDRESS — Address (or Address+Quantity range) doesn't exist on this slave; check the meter's register map/datasheet");
    case 3: return F("ILLEGAL DATA VALUE — Quantity is out of the range this slave accepts");
    case 4: return F("SLAVE DEVICE FAILURE — an error occurred on the meter itself while processing the request");
    case 5: return F("ACKNOWLEDGE — slave accepted but needs more time; retry");
    case 6: return F("SLAVE DEVICE BUSY — slave is processing a long-duration command; retry");
    case 8: return F("MEMORY PARITY ERROR");
    case 10: return F("GATEWAY PATH UNAVAILABLE");
    case 11: return F("GATEWAY TARGET DEVICE FAILED TO RESPOND");
    default: {
      String s; s.reserve(32);
      s += F("Unknown exception code ");
      s += String(code);
      return s;
    }
  }
}

// Turns a failed-read ModbusReadDiag into an actionable detail string for the
// /configmod UI, instead of a bare "timeout_or_crc" that can't tell "nothing
// answered" apart from "something answered but garbled" or "wrong slave".
// Only meaningful when exceptionCode == 0 — a nonzero exceptionCode means the
// slave DID answer validly and modbusExceptionText() should be used instead.
String modbusReadDiagDetail(const ModbusReadDiag& diag, uint8_t expectedSlaveId) {
  if (!diag.anyBytesReceived) {
    return F("no_response — check RS485 A/B wiring, baudrate/parity, and that Slave ID matches the meter");
  }
  if (diag.slaveIdMismatch) {
    String s;
    s.reserve(64);
    s += F("wrong_slave_responded (got id ");
    s += String(diag.respondingSlaveId);
    s += F(", expected ");
    s += String(expectedSlaveId);
    s += F(") — another device is answering on this bus");
    return s;
  }
  if (diag.crcMismatch) {
    return F("crc_mismatch — a response arrived but failed CRC check; likely wiring noise, parity mismatch, or baudrate mismatch");
  }
  return F("partial_response — response started but didn't complete before timeout; try a longer Timeout");
}

void handleModMapReadBlockApi() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;

  if (!modbusTestModeActive) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"test_mode_not_active\"}");
    return;
  }
  modbusTestModeLastActivityMs = millis();

  const uint8_t slaveId = configServer.hasArg("slave_id") ? configServer.arg("slave_id").toInt() : 0;
  const uint8_t function = configServer.hasArg("function") ? configServer.arg("function").toInt() : 3;
  const uint16_t address = configServer.hasArg("address") ? configServer.arg("address").toInt() : 0;
  const uint16_t quantity = configServer.hasArg("quantity") ? configServer.arg("quantity").toInt() : 0;

  if (slaveId == 0 || slaveId > 247 || (function != 3 && function != 4) ||
      quantity == 0 || quantity > 32 || (uint32_t)address + quantity > 65536UL) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_params\"}");
    return;
  }

  uint8_t buf[64] = {0};
  uint16_t exceptionCode = 0;
  ModbusReadDiag diag;
  const bool ok = modbusReadRegisters(function, slaveId, address, quantity, buf, sizeof(buf), exceptionCode, modbusTestModeConfig.timeout_ms, &diag);

  if (!ok) {
    String body;
    body.reserve(192);
    body += F("{\"ok\":false,\"error\":\"");
    body += exceptionCode != 0 ? F("modbus_exception") : F("timeout_or_crc");
    body += F("\",\"exception_code\":");
    body += String(exceptionCode);
    body += F(",\"detail\":\"");
    body += jsonEscape(exceptionCode != 0 ? modbusExceptionText(exceptionCode) : modbusReadDiagDetail(diag, slaveId));
    body += F("\"}");
    configServer.send(200, "application/json", body);
    return;
  }

  String body;
  body.reserve(32 + quantity * 24);
  body += F("{\"ok\":true,\"registers\":[");
  for (uint16_t i = 0; i < quantity; i++) {
    const uint16_t raw = (static_cast<uint16_t>(buf[i * 2]) << 8) | buf[i * 2 + 1];
    const int16_t signedRaw = static_cast<int16_t>(raw);
    if (i > 0) body += F(",");
    body += F("{\"addr\":");
    body += String(address + i);
    body += F(",\"uint16\":");
    body += String(raw);
    body += F(",\"int16\":");
    body += String(signedRaw);
    body += F("}");
  }
  body += F("]}");
  configServer.send(200, "application/json", body);
}

void handleModMapReadApi() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;

  if (!modbusTestModeActive) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"test_mode_not_active\"}");
    return;
  }
  modbusTestModeLastActivityMs = millis();

  const uint8_t slaveId = configServer.hasArg("slave_id") ? configServer.arg("slave_id").toInt() : 0;
  const uint8_t function = configServer.hasArg("function") ? configServer.arg("function").toInt() : 3;
  const uint16_t address = configServer.hasArg("address") ? configServer.arg("address").toInt() : 0;
  const uint8_t datatype = configServer.hasArg("datatype") ? configServer.arg("datatype").toInt() : 3;
  const float scale = configServer.hasArg("scale") ? configServer.arg("scale").toFloat() : 1.0f;

  if (slaveId == 0 || slaveId > 247 || (function != 3 && function != 4) || datatype > 4) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_params\"}");
    return;
  }

  const uint16_t quantity = modbusDataTypeQuantity(datatype);
  uint8_t buf[4] = {0, 0, 0, 0};
  uint16_t exceptionCode = 0;
  ModbusReadDiag diag;
  const bool ok = modbusReadRegisters(function, slaveId, address, quantity, buf, sizeof(buf), exceptionCode, modbusTestModeConfig.timeout_ms, &diag);

  String body;
  body.reserve(192);
  if (!ok) {
    body += F("{\"ok\":false,\"error\":\"");
    body += exceptionCode != 0 ? F("modbus_exception") : F("timeout_or_crc");
    body += F("\",\"exception_code\":");
    body += String(exceptionCode);
    body += F(",\"detail\":\"");
    body += jsonEscape(exceptionCode != 0 ? modbusExceptionText(exceptionCode) : modbusReadDiagDetail(diag, slaveId));
    body += F("\"}");
    configServer.send(200, "application/json", body);
    return;
  }

  const float decoded = decodeModbusValue(datatype, buf, scale);
  char rawHex[9] = {0};
  const uint8_t rawBytes = quantity * 2;
  for (uint8_t i = 0; i < rawBytes; i++) {
    snprintf(rawHex + (i * 2), 3, "%02X", buf[i]);
  }

  body += F("{\"ok\":true,\"raw_hex\":\"");
  body += rawHex;
  body += F("\",\"decoded\":");
  body += String(decoded, 6);
  body += F(",\"exception_code\":0}");
  configServer.send(200, "application/json", body);
}

void handleModMapSaveApi() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;

  const uint8_t count = configServer.hasArg("count") ? configServer.arg("count").toInt() : 0;
  if (count > kModbusMapMaxEntries) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"too_many_entries\"}");
    return;
  }

  ModbusMapConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.count = 0;
  if (configServer.hasArg("map_name")) {
    String mapName = configServer.arg("map_name");
    mapName.trim();
    strncpy(cfg.name, mapName.c_str(), sizeof(cfg.name) - 1);
  }

  for (uint8_t i = 0; i < count; i++) {
    const String prefix = "f" + String(i) + "_";
    if (!configServer.hasArg(prefix + "key") || !configServer.hasArg(prefix + "slave") ||
        !configServer.hasArg(prefix + "fn") || !configServer.hasArg(prefix + "addr") ||
        !configServer.hasArg(prefix + "type") || !configServer.hasArg(prefix + "scale")) {
      continue;
    }

    const uint8_t slaveId = configServer.arg(prefix + "slave").toInt();
    const uint8_t function = configServer.arg(prefix + "fn").toInt();
    const uint8_t datatype = configServer.arg(prefix + "type").toInt();
    if (slaveId == 0 || slaveId > 247 || (function != 3 && function != 4) || datatype > 4) continue;

    String key = configServer.arg(prefix + "key");
    key.trim();
    if (key.length() == 0) continue;

    ModbusMapEntry& entry = cfg.entries[cfg.count];
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.field_key, key.c_str(), sizeof(entry.field_key) - 1);
    entry.slave_id = slaveId;
    entry.function = function;
    entry.address = static_cast<uint16_t>(configServer.arg(prefix + "addr").toInt());
    entry.datatype = datatype;
    entry.scale = configServer.arg(prefix + "scale").toFloat();
    if (entry.scale == 0.0f) entry.scale = 1.0f;
    cfg.count++;
  }

  const bool ok = ConfigManager::saveModbusMapConfig(cfg);
  if (ok) {
    modMapCacheDirty = true;
  }

  String body;
  body.reserve(64);
  if (ok) {
    body += F("{\"ok\":true,\"saved\":");
    body += String(cfg.count);
    body += F("}");
  } else {
    body = F("{\"ok\":false,\"error\":\"save_failed\"}");
  }
  configServer.send(ok ? 200 : 500, "application/json", body);
}

void handleModMapLoadApi() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;

  const ModbusMapConfig cfg = ConfigManager::loadModbusMapConfig();
  String body;
  body.reserve(cfg.count * 96 + 64);
  body += F("{\"ok\":true,\"name\":\"");
  body += jsonEscape(cfg.name);
  body += F("\",\"entries\":[");
  for (uint8_t i = 0; i < cfg.count; i++) {
    const ModbusMapEntry& entry = cfg.entries[i];
    if (i > 0) body += F(",");
    body += F("{\"key\":\"");
    body += jsonEscape(entry.field_key);
    body += F("\",\"slave\":");
    body += String(entry.slave_id);
    body += F(",\"fn\":");
    body += String(entry.function);
    body += F(",\"addr\":");
    body += String(entry.address);
    body += F(",\"type\":");
    body += String(entry.datatype);
    body += F(",\"scale\":");
    body += String(entry.scale, 6);
    body += F("}");
  }
  body += F("]}");
  configServer.send(200, "application/json", body);
}

void handleModMapStatusApi() {
  sendNoCacheHeader();
  if (!requireConfigMode()) return;

  String body;
  body.reserve(96);
  body += F("{\"test_mode_active\":");
  body += modbusTestModeActive ? F("true") : F("false");
  body += F(",\"baudrate\":");
  body += String(modbusTestModeActive ? modbusTestModeConfig.baudrate : currentModbusConfig.baudrate);
  body += F("}");
  configServer.send(200, "application/json", body);
}

void handleProtectionConfigSaveApi() {
  ProtectionConfig cfg = currentProtectionConfig;

  cfg.relay_enabled = configServer.hasArg("relay_enabled") && configServer.arg("relay_enabled") == "1";
  cfg.relay_pin[0] = configServer.hasArg("relay_pin_1") ? configServer.arg("relay_pin_1").toInt() : 2;
  cfg.relay_pin[1] = configServer.hasArg("relay_pin_2") ? configServer.arg("relay_pin_2").toInt() : 15;
  cfg.relay_pin[2] = configServer.hasArg("relay_pin_3") ? configServer.arg("relay_pin_3").toInt() : 14;
  cfg.relay_pin[3] = configServer.hasArg("relay_pin_4") ? configServer.arg("relay_pin_4").toInt() : 13;
  cfg.relay_slave_id[0] = configServer.hasArg("relay_slave_id_1") ? configServer.arg("relay_slave_id_1").toInt() : 1;
  cfg.relay_slave_id[1] = configServer.hasArg("relay_slave_id_2") ? configServer.arg("relay_slave_id_2").toInt() : 2;
  cfg.relay_slave_id[2] = configServer.hasArg("relay_slave_id_3") ? configServer.arg("relay_slave_id_3").toInt() : 3;
  cfg.relay_slave_id[3] = configServer.hasArg("relay_slave_id_4") ? configServer.arg("relay_slave_id_4").toInt() : 4;
  cfg.current_limit_a = configServer.hasArg("current_limit") ? configServer.arg("current_limit").toInt() : 16;
  cfg.trip_delay_ms = configServer.hasArg("trip_delay") ? configServer.arg("trip_delay").toInt() : 1000;
  cfg.reset_mode = configServer.hasArg("reset_mode") ? configServer.arg("reset_mode").toInt() : 0;
  cfg.auto_retry_enabled = configServer.hasArg("auto_retry_enabled") && configServer.arg("auto_retry_enabled") == "1";
  cfg.auto_retry_delay_sec = configServer.hasArg("auto_retry_delay") ? configServer.arg("auto_retry_delay").toInt() : 300;
  cfg.trip_on_meter_stale = configServer.hasArg("trip_on_stale") && configServer.arg("trip_on_stale") == "1";

  // relay_slave_id feeds mqttSwitchTopic() (control/slave_N per relay), so a
  // change here shifts which topics the device is subscribed to. Force a
  // reconnect so it re-subscribes under the new topics instead of sitting
  // connected with subscriptions still pinned to the old slave_N mapping.
  bool relaySlaveIdChanged = false;
  for (size_t r = 0; r < 4; r++) {
    if (currentProtectionConfig.relay_slave_id[r] != cfg.relay_slave_id[r]) {
      relaySlaveIdChanged = true;
      break;
    }
  }

  bool ok = ConfigManager::saveProtectionConfig(cfg);
  if (ok) {
    currentProtectionConfig = cfg;
    if (relaySlaveIdChanged && (*activeMqttClient).connected()) {
      (*activeMqttClient).disconnect();
      mqttConnected = false;
    }
    for (size_t r = 0; r < 4; r++) {
      uint8_t pin = currentProtectionConfig.relay_pin[r];
      pinMode(pin, OUTPUT);
      setRelayOutput(r, relayState[r]);
    }
    if (!currentProtectionConfig.relay_enabled) {
      for (size_t r = 0; r < 4; r++) {
        requestRelayState(r, 0, "protection_disabled");
      }
    }
  }
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleDisplayConfigSaveApi() {
  DisplayConfig cfg = currentDisplayConfig;

  cfg.enabled = configServer.hasArg("display_enabled") && configServer.arg("display_enabled") == "1";
  cfg.type = configServer.hasArg("display_type") ? configServer.arg("display_type").toInt() : 0;
  cfg.i2c_address = configServer.hasArg("i2c_address") ? (uint8_t)strtol(configServer.arg("i2c_address").c_str(), nullptr, 16) : 0x3C;
  cfg.rotation_interval_sec = configServer.hasArg("rotation_interval") ? configServer.arg("rotation_interval").toInt() : 5;
  cfg.brightness = configServer.hasArg("brightness") ? configServer.arg("brightness").toInt() : 200;

  bool ok = ConfigManager::saveDisplayConfig(cfg);
  if (ok) {
    currentDisplayConfig = cfg;
    displayReady = false;
  }
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
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
    for (size_t m = 0; m < 3; m++) {
      historyRuntime[m].loaded = false;
    }
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
  body.reserve(128);
  body += F("{\"ok\":true,\"relay_enabled\":");
  body += currentProtectionConfig.relay_enabled ? F("true") : F("false");
  body += F(",\"r1\":"); body += String(relayState[0]);
  body += F(",\"r2\":"); body += String(relayState[1]);
  body += F(",\"r3\":"); body += String(relayState[2]);
  body += F(",\"r4\":"); body += String(relayState[3]);
  body += F("}");
  sendNoCacheHeader();
  configServer.send(200, "application/json", body);
}

void handleRelaySetApi() {
  size_t targetRelay = 0;
  bool specifyRelay = false;
  if (configServer.hasArg("relay")) {
    targetRelay = configServer.arg("relay").toInt();
    specifyRelay = true;
  } else if (configServer.hasArg("r")) {
    targetRelay = configServer.arg("r").toInt();
    specifyRelay = true;
  }

  String action;
  if (configServer.hasArg("action")) {
    action = configServer.arg("action");
  } else if (configServer.hasArg("state")) {
    action = configServer.arg("state");
  }
  action.toLowerCase();

  uint8_t st = (action == "set" || action == "1" || action == "on" || action == "true") ? 1 : 0;

  if (specifyRelay && targetRelay >= 1 && targetRelay <= 4) {
    requestRelayState(targetRelay - 1, st, "web", true);
  } else {
    for (size_t r = 0; r < 4; r++) {
      requestRelayState(r, st, "web", false);
    }
    publishMqttControlState();
  }

  handleRelayStateApi();
}

void handleSetupRoot() {
  sendNoCacheHeader();
  configServer.send(200, "text/html", buildHomePage());
}

void handleNetworkPage() {
  if (!requireConfigMode()) return;

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
  body += F(",\"modbus_slave_id_1\":");
  body += String(currentModbusConfig.slave_id[0]);
  body += F(",\"modbus_slave_id_2\":");
  body += String(currentModbusConfig.slave_id[1]);
  body += F(",\"modbus_slave_id_3\":");
  body += String(currentModbusConfig.slave_id[2]);
  body += F(",\"modbus_profile\":");
  body += String(currentModbusConfig.meter_profile);
  body += F(",\"relay_enabled\":");
  body += currentProtectionConfig.relay_enabled ? F("true") : F("false");
  body += F(",\"r1\":"); body += String(relayState[0]);
  body += F(",\"r2\":"); body += String(relayState[1]);
  body += F(",\"r3\":"); body += String(relayState[2]);
  body += F(",\"r4\":"); body += String(relayState[3]);
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
  body += formatDayKey(historyRuntime[0].dayKey);
  body += F("\",\"energy_history\":[");
  for (size_t m = 0; m < 3; m++) {
    if (m > 0) body += F(",");
    body += buildEnergyHistoryJson(m);
  }
  body += F("]");
  body += F(",\"sta_ip\":\"");
  body += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  body += F("\",\"eth_link\":");
  body += ethLinkUp ? F("true") : F("false");
  body += F(",\"eth_ip\":\"");
  body += ethLinkUp ? Ethernet.localIP().toString() : "";
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
  body += F(",\"meters\":[");
  for (size_t m = 0; m < 3; m++) {
    const MeterSnapshot& s = meterSnapshots[m];
    if (m > 0) body += F(",");
    body += F("{\"slave_id\":");
    body += String(currentModbusConfig.slave_id[m]);
    body += F(",\"online\":");
    body += s.online ? F("true") : F("false");
    body += F(",\"valid\":");
    body += s.valid ? F("true") : F("false");
    body += F(",\"voltage\":\"");
    body += jsonEscape(formatFloatValue(s.voltage, 2, "V"));
    body += F("\",\"current\":\"");
    body += jsonEscape(formatFloatValue(s.current, 2, "A"));
    body += F("\",\"power\":\"");
    body += jsonEscape(formatFloatValue(s.power, 2, "kW"));
    body += F("\",\"frequency\":\"");
    body += jsonEscape(formatFloatValue(s.frequency, 2, "Hz"));
    body += F("\",\"pf\":\"");
    body += jsonEscape(formatFloatValue(s.pf, 2, ""));
    body += F("\",\"energy\":\"");
    body += jsonEscape(formatFloatValue(s.energy, 3, "kWh"));
    body += F("\",\"last_poll_ms\":");
    body += String(s.lastPollMs);
    body += F(",\"last_success_ms\":");
    body += String(s.lastSuccessMs);
    body += F("}");
  }
  body += F("]");
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
  body.reserve(768);
  body += F("{\"meters\":[");
  for (size_t m = 0; m < 3; m++) {
    const MeterSnapshot& s = meterSnapshots[m];
    if (m > 0) body += F(",");
    body += F("{\"slave_id\":");
    body += String(currentModbusConfig.slave_id[m]);
    body += F(",\"online\":");
    body += s.online ? F("true") : F("false");
    body += F(",\"valid\":");
    body += s.valid ? F("true") : F("false");
    body += F(",\"voltage\":\"");
    body += jsonEscape(formatFloatValue(s.voltage, 2, "V"));
    body += F("\",\"current\":\"");
    body += jsonEscape(formatFloatValue(s.current, 2, "A"));
    body += F("\",\"power\":\"");
    body += jsonEscape(formatFloatValue(s.power, 2, "kW"));
    body += F("\",\"frequency\":\"");
    body += jsonEscape(formatFloatValue(s.frequency, 2, "Hz"));
    body += F("\",\"pf\":\"");
    body += jsonEscape(formatFloatValue(s.pf, 2, ""));
    body += F("\",\"energy\":\"");
    body += jsonEscape(formatFloatValue(s.energy, 3, "kWh"));
    body += F("\",\"last_poll_ms\":");
    body += String(s.lastPollMs);
    body += F(",\"last_success_ms\":");
    body += String(s.lastSuccessMs);
    body += F("}");
  }
  body += F("]}");

  sendNoCacheHeader();
  configServer.send(200, "application/json", body);
}

void handleWifiScanApi() {
  Serial.println("WiFi scan requested from setup UI");
  WiFi.mode(WIFI_AP_STA);
  // Bug fix: first scan in AP/Config Mode often returns 0 (not -1) because
  // the STA netif hasn't fully initialised after softAP().  Retry up to 2x
  // on count <= 0 (covers both error returns AND the "0 networks" false-zero).
  int count = WiFi.scanNetworks(false, false);
  for (int _retry = 0; _retry < 2 && count <= 0; _retry++) {
    Serial.printf("WiFi scan returned %d, retry %d/2...\n", count, _retry + 1);
    delay(300);
    WiFi.scanDelete();
    count = WiFi.scanNetworks(false, false);
  }
  String body;
  body.reserve(512 + (count > 0 ? count * 96 : 0));
  body += F("{\"ok\":true,\"count\":");
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
    body += F(",\"security\":\"");
    body += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? F("open") : F("secured");
    body += F("\"}");
  }

  body += F("]}");
  WiFi.scanDelete();

  sendNoCacheHeader();
  configServer.send(200, "application/json", body);
}

void handleWifiSaveApi() {
  String ssid = "";
  String password = "";

  if (configServer.hasArg("plain")) {
    String plain = configServer.arg("plain");
    int ssidIdx = plain.indexOf("\"ssid\"");
    if (ssidIdx >= 0) {
      int start = plain.indexOf('"', plain.indexOf(':', ssidIdx) + 1);
      int end = plain.indexOf('"', start + 1);
      if (start >= 0 && end > start) {
        ssid = plain.substring(start + 1, end);
      }
    }
    int passIdx = plain.indexOf("\"password\"");
    if (passIdx >= 0) {
      int start = plain.indexOf('"', plain.indexOf(':', passIdx) + 1);
      int end = plain.indexOf('"', start + 1);
      if (start >= 0 && end > start) {
        password = plain.substring(start + 1, end);
      }
    }
  }

  if (ssid.length() == 0 && configServer.hasArg("ssid")) {
    ssid = configServer.arg("ssid");
    password = configServer.hasArg("password") ? configServer.arg("password") : "";
  }

  ssid.trim();

  if (ssid.length() == 0 || ssid.length() > 32) {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}");
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

void handleUnlockApi() {
  if (!configServer.hasArg("pin")) {
    sendNoCacheHeader();
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"pin_required\"}");
    return;
  }
  
  String pin = configServer.arg("pin");
  if (pin == "1234") {
    webConfigUnlocked = true;
    sendNoCacheHeader();
    configServer.send(200, "application/json", "{\"ok\":true}");
    Serial.println("Web UI config unlocked via PIN");
  } else {
    sendNoCacheHeader();
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"wrong_pin\"}");
    Serial.println("Web UI config unlock attempt failed: invalid PIN");
  }
}

void handleOtaResponse() {
  if (!requireConfigMode()) return;
  sendNoCacheHeader();
  if (Update.hasError()) {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"update_failed\"}");
  } else {
    configServer.send(200, "application/json", "{\"ok\":true}");
    rebootRequested = true;
    rebootAtMs = millis() + 1000;
  }
}

void handleOtaUpload() {
  if (currentMode != AppMode::Config && !configApStarted && !ethLinkUp && !webConfigUnlocked) {
    return;
  }
  HTTPUpload& upload = configServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Update Start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %u bytes\nRebooting...\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
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
  configServer.on("/api/status", HTTP_GET, handleStatusApi);
  configServer.on("/api/meter/status", HTTP_GET, handleMeterStatusApi);
  configServer.on("/api/wifi/scan", HTTP_GET, handleWifiScanApi);
  configServer.on("/api/wifi/save", HTTP_POST, handleWifiSaveApi);
  configServer.on("/api/device/save", HTTP_POST, handleDeviceConfigSaveApi);
  configServer.on("/api/mqtt/save", HTTP_POST, handleMqttConfigSaveApi);
  configServer.on("/api/mqtt/test_publish", HTTP_POST, handleMqttTestPublishApi);
  configServer.on("/api/mqtt/test_status", HTTP_GET, handleMqttTestStatusApi);
  configServer.on("/api/modbus/save", HTTP_POST, handleModbusConfigSaveApi);
  // Hidden Modbus scan/mapping tool — no <nav> link anywhere, URL-only access.
  configServer.on("/configmod", HTTP_GET, handleModConfigPage);
  configServer.on("/api/modmap/uart/apply", HTTP_POST, handleModMapUartApplyApi);
  configServer.on("/api/modmap/uart/restore", HTTP_POST, handleModMapUartRestoreApi);
  configServer.on("/api/modmap/read", HTTP_POST, handleModMapReadApi);
  configServer.on("/api/modmap/read_block", HTTP_POST, handleModMapReadBlockApi);
  configServer.on("/api/modmap/save", HTTP_POST, handleModMapSaveApi);
  configServer.on("/api/modmap/load", HTTP_GET, handleModMapLoadApi);
  configServer.on("/api/modmap/status", HTTP_GET, handleModMapStatusApi);
  configServer.on("/api/protection/save", HTTP_POST, handleProtectionConfigSaveApi);
  configServer.on("/api/display/save", HTTP_POST, handleDisplayConfigSaveApi);
  configServer.on("/api/history/save", HTTP_POST, handleHistoryConfigSaveApi);
  configServer.on("/api/system/save", HTTP_POST, handleSystemConfigSaveApi);
  configServer.on("/api/relay/state", HTTP_GET, handleRelayStateApi);
  configServer.on("/api/relay/set", HTTP_POST, handleRelaySetApi);
  configServer.on("/api/reboot", HTTP_POST, handleRebootApi);
  configServer.on("/api/unlock", HTTP_POST, handleUnlockApi);
  configServer.on("/api/update", HTTP_POST, handleOtaResponse, handleOtaUpload);
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

  // Warm-up: kick off an async background scan immediately after softAP so
  // the STA netif is fully initialised before the user clicks "Scan" in the
  // web UI.  Results are intentionally discarded — the sole purpose is to
  // prime the radio so the first real scan returns accurate results.
  WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);

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

// GPIO4 (TTP223 touch sensor, active-HIGH) — multi-function navigation button.
// Short press (<2s) : navigate cursor/pages
// Hold >=3s (outside menu, not info mode) : enter Setting Menu
// Hold >=2s (inside menu)                : select menu option
// Hold >=2s (inside info mode)           : exit info mode -> dashboard
void handleBtn2(uint32_t nowMs) {
  const bool pressed = isConfigButtonPressed(); // GPIO4 active-HIGH via PinMap

  if (pressed) {
    if (configButtonPressedSinceMs == 0) {
      configButtonPressedSinceMs = nowMs;
    } else {
      const uint32_t holdTime = nowMs - configButtonPressedSinceMs;

      // --- In Info Mode: hold >=2s to exit back to dashboard ---
      if (oledInfoModeActive && !oledInMenu) {
        if (holdTime >= 2000) {
          configButtonPressedSinceMs = 0;
          oledInfoModeActive = false;
          displayPage = 0;
          displayLastUpdateMs = 0; // force immediate redraw
          Serial.println("[BTN2] Exit Info Mode -> Dashboard");
        }

      // --- Outside menu (dashboard): hold >=3s to enter Setting Menu ---
      } else if (!oledInMenu) {
        if (holdTime >= 3000) {
          configButtonPressedSinceMs = 0;
          oledInMenu = true;
          oledMenuCursor = 0;
          displayLastUpdateMs = 0;
          Serial.println("[BTN2] Entered Setting Menu");
        }

      // --- Inside Setting Menu: hold >=2s to select current option ---
      } else {
        if (holdTime >= 2000) {
          configButtonPressedSinceMs = 0;
          oledInMenu = false;
          displayLastUpdateMs = 0;
          if (oledMenuCursor == 0) {
            // Toggle Config/Normal mode (in-place, no reboot)
            Serial.println("[BTN2] Menu: Boot Mode selected");
            if (currentMode == AppMode::Config) {
              // Switch back to Normal mode
              currentMode = AppMode::Normal;
              Serial.println("[BTN2] -> Normal Mode");
            } else {
              enterConfigMode();
            }
          } else if (oledMenuCursor == 1) {
            // Enter Info Mode: manual paging pages 1+()
            oledInfoModeActive = true;
            displayPage = 1;
            Serial.println("[BTN2] Menu: View Info -> page 1");
          } else if (oledMenuCursor == 2) {
            // Exit menu
            Serial.println("[BTN2] Menu: Exit");
          }
        }
      }
    }
  } else {
    // Button released
    if (configButtonPressedSinceMs > 0) {
      const uint32_t pressDuration = nowMs - configButtonPressedSinceMs;
      configButtonPressedSinceMs = 0;

      if (pressDuration < 2000) {
        // Short press action
        if (oledInMenu) {
          // Advance menu cursor: 0 -> 1 -> 2 -> 0
          oledMenuCursor = (oledMenuCursor + 1) % 3;
          displayLastUpdateMs = 0;
          Serial.printf("[BTN2] Menu cursor -> %d\n", oledMenuCursor);
        } else if (oledInfoModeActive) {
          // Next info page, skip page 0 (dashboard)
          displayPage = (displayPage + 1) % 3;
          if (displayPage == 0) displayPage = 1;
          displayLastUpdateMs = 0;
          Serial.printf("[BTN2] Info page -> %d\n", displayPage);
        } else {
          // Dashboard: force immediate redraw
          displayPage = 0;
          displayLastUpdateMs = 0;
          Serial.println("[BTN2] Dashboard redraw");
        }
      }
    }
  }
}
void initEthernet() {
  pinMode(PinMap::kEthRst, OUTPUT);
  digitalWrite(PinMap::kEthRst, LOW);
  delay(20);
  digitalWrite(PinMap::kEthRst, HIGH);
  delay(50);

  // Derive MAC from ESP32 chip ID (last 3 bytes), prefix locally-administered
  uint64_t chipId = ESP.getEfuseMac();
  uint8_t mac[6] = {
    0x02,
    0x00,
    (uint8_t)((chipId >> 32) & 0xFF),
    (uint8_t)((chipId >> 24) & 0xFF),
    (uint8_t)((chipId >> 16) & 0xFF),
    (uint8_t)((chipId >>  8) & 0xFF),
  };

  Ethernet.init(PinMap::kEthCs);
  if (Ethernet.begin(mac, 8000) == 0) {
    Serial.println("ETH: DHCP failed, link may be down");
    ethLinkUp = false;
  } else {
    ethLinkUp = true;
    ethConfigured = true;
    Serial.print("ETH: IP=");
    Serial.println(Ethernet.localIP());
    startConfigWebServer();
    startNtpSync();
    Serial.print("Open ETH Web UI: http://");
    Serial.println(Ethernet.localIP());
  }
}

void updateEthernetRuntime(uint32_t nowMs) {
  if (nowMs - ethLastCheckMs < 5000) return;
  ethLastCheckMs = nowMs;

  EthernetLinkStatus linkStatus = Ethernet.linkStatus();
  if (linkStatus == LinkOFF) {
    if (ethLinkUp) {
      ethLinkUp = false;
      Serial.println("ETH: link down");
    }
    return;
  }

  if (!ethLinkUp) {
    if (verboseLog) Serial.println("ETH: link up, requesting DHCP...");
    if (Ethernet.begin(nullptr, 8000) != 0) {
      ethLinkUp = true;
      ethConfigured = true;
      Serial.print("ETH: IP=");
      Serial.println(Ethernet.localIP());
      startConfigWebServer();
      startNtpSync();
      Serial.print("Open ETH Web UI: http://");
      Serial.println(Ethernet.localIP());
    }
  } else {
    Ethernet.maintain();
  }

  if (ethLinkUp && !timeSynced) {
    if (!ntpSyncStarted) {
      startNtpSync();
    } else {
      checkNtpSync();
    }
  }
}
}  // namespace

void setup() {
  // Drive every relay pin to its OFF level before anything else — Serial
  // init, delay(500), NVS reads for the other config namespaces, etc. all
  // cost tens-to-hundreds of ms during which an active-LOW relay pin left
  // floating/undriven can read as ON, clicking the relay briefly at every
  // boot/reset. Doing this first shrinks that firmware-side window to
  // ~nothing; it can't remove the very first ROM-bootloader-level glitch
  // before user code even runs — that part needs an external pull-up
  // (to 3.3V) on each relay control line so the pin defaults HIGH (=OFF
  // for this active-LOW board) while it's floating.
  {
    const uint8_t defPins[4] = {2, 15, 14, 13};
    currentProtectionConfig = ConfigManager::loadProtectionConfig();
    for (size_t r = 0; r < 4; r++) {
      uint8_t pin = (currentProtectionConfig.relay_pin[r] == 0) ? defPins[r] : currentProtectionConfig.relay_pin[r];
      pinMode(pin, OUTPUT);
      setRelayOutput(r, 0);
    }
  }

  Serial.begin(kSerialBaud);
  delay(500);

  printBootBanner();
  printChipInfo();
  printButtonInfo();
  currentDeviceConfig = ConfigManager::loadDeviceConfig();
  currentMqttConfig = ConfigManager::loadMqttConfig();
  loadWifiConfig();
  currentModbusConfig = ConfigManager::loadModbusConfig();
  currentDisplayConfig = ConfigManager::loadDisplayConfig();
  currentHistoryConfig = ConfigManager::loadHistoryConfig();
  currentSystemConfig = ConfigManager::loadSystemConfig();
  currentModbusConfigLoaded = true;
  loadFeatureRuntime();
  for (size_t m = 0; m < 3; m++) {
    resetMeterSnapshot(meterSnapshots[m]);
  }
  if (hasSavedWifi) {
    wifiConnecting = true;
    wifiConnectStartedMs = millis();
    connectToSavedWifi();
  } else {
    Serial.println("No saved WiFi configuration. Entering CONFIG_MODE.");
    enterConfigMode();
  }
  initEthernet();
  printModeInfo();
}

void handleVerboseLogSerialInput() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '0') {
      verboseLog = false;
      Serial.println("Verbose log OFF (routine Modbus/heartbeat/ETH logs silenced; send '1' to re-enable)");
    } else if (c == '1') {
      verboseLog = true;
      Serial.println("Verbose log ON");
    }
  }
}

void loop() {
  const uint32_t nowMs = millis();
  handleVerboseLogSerialInput();
  updateStatusLed(nowMs);
  updateEthernetRuntime(nowMs);
  handleWiFiLifecycle(nowMs);
  handleBtn2(nowMs);
  if (modbusTestModeActive && nowMs - modbusTestModeLastActivityMs >= kModbusTestModeIdleTimeoutMs) {
    restoreModbusNormalMode();
  }
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

    if (verboseLog) {
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
}
