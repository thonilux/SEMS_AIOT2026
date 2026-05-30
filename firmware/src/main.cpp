#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
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
WebServer configServer(80);

bool littleFsReady = false;

void startConfigWebServer();
void enterConfigMode();
void startConfigAccessPoint(bool disconnectSta = true);
void sendNoCacheHeader();

bool isConfigButtonPressed() {
  const int rawState = digitalRead(PinMap::kConfigButton);
  return PinMap::kConfigButtonActiveLow ? rawState == LOW : rawState == HIGH;
}

void setBuiltinLed(bool on) {
  const bool outputHigh = PinMap::kBuiltinLedActiveHigh ? on : !on;
  digitalWrite(PinMap::kBuiltinLed, outputHigh ? HIGH : LOW);
}

bool initLittleFs() {
  littleFsReady = LittleFS.begin(true);
  Serial.print("LittleFS: ");
  Serial.println(littleFsReady ? "mounted" : "mount_failed");
  return littleFsReady;
}

bool serveLittleFsPage(const char* path, const char* contentType) {
  if (!littleFsReady || !LittleFS.exists(path)) {
    return false;
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }

  sendNoCacheHeader();
  configServer.streamFile(file, contentType);
  file.close();
  return true;
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

uint32_t getLittleFsTotalBytes() {
  return littleFsReady ? static_cast<uint32_t>(LittleFS.totalBytes()) : 0;
}

uint32_t getLittleFsUsedBytes() {
  return littleFsReady ? static_cast<uint32_t>(LittleFS.usedBytes()) : 0;
}

uint8_t getLittleFsUsedPercent() {
  return percentUsed(getLittleFsUsedBytes(), getLittleFsTotalBytes());
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

void startNtpSync() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("NTP skipped: WiFi is not connected");
    return;
  }

  Serial.print("NTP sync started: ");
  Serial.print(kNtpServer1);
  Serial.print(", ");
  Serial.println(kNtpServer2);

  configTzTime(kNtpTimezone, kNtpServer1, kNtpServer2);
  ntpSyncStarted = true;
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

String buildPageHeader(const String& title) {
  String page;
  page.reserve(2300);
  page += F("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">");
  page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  page += F("<title>");
  page += htmlEscape(title);
  page += F("</title><style>");
  page += F(":root{font-family:Inter,Segoe UI,Arial,sans-serif;color:#17202a;background:#f4f7f9}");
  page += F("body{margin:0}.bar{background:#111827;color:white;padding:14px 18px}.top{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}");
  page += F("nav{display:flex;gap:8px}nav a{color:#d1fae5;text-decoration:none;font-weight:700;padding:7px 9px;border-radius:6px}nav a:hover{background:#1f2937}");
  page += F(".wrap{max-width:760px;margin:0 auto;padding:18px}.panel{background:white;border:1px solid #d9e1e8;border-radius:8px;padding:16px;margin:14px 0}");
  page += F("h1{font-size:21px;margin:0}.muted{color:#667085}.grid{display:grid;grid-template-columns:150px 1fr;gap:8px 12px}");
  page += F("label{font-weight:700;display:block;margin:12px 0 6px}input{box-sizing:border-box;width:100%;border:1px solid #cbd5e1;border-radius:6px;padding:10px;font-size:15px}");
  page += F(".actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px}button{background:#0f766e;color:white;border:0;border-radius:6px;padding:10px 13px;font-weight:700}");
  page += F("button:disabled{background:#94a3b8}.net{display:flex;justify-content:space-between;gap:10px;border-top:1px solid #e5e7eb;padding:10px 0}");
  page += F(".ssid{font-weight:700;overflow-wrap:anywhere}.tag{font-size:12px;color:#475467;background:#eef2f6;border-radius:999px;padding:2px 8px}.use{background:#334155;padding:7px 10px}");
  page += F(".metric{margin:12px 0}.metricTop{display:flex;justify-content:space-between;gap:10px}.track{height:10px;background:#e5e7eb;border-radius:999px;overflow:hidden}.fill{height:100%;background:#0f766e;border-radius:999px}.warn{background:#d97706}.bad{background:#dc2626}");
  page += F(".menuBtn{display:none;background:#1f2937;color:white;border:1px solid #374151;border-radius:6px;padding:8px 10px;font-size:18px}");
  page += F("@media(max-width:560px){.grid{grid-template-columns:1fr}.net{display:block}.top{display:block}.menuBtn{display:block;margin-top:10px}nav{display:none;flex-direction:column;margin-top:10px}nav.open{display:flex}}");
  page += F("</style></head><body><div class=\"bar\"><div class=\"top\"><div><h1>");
  page += htmlEscape(title);
  page += F("</h1><div class=\"muted\">PM1611 RS485 Reader</div></div>");
  page += F("<button class=\"menuBtn\" onclick=\"toggleMenu()\">☰</button>");
  page += F("<nav id=\"mainNav\"><a href=\"/\">🏠 Home</a>");

  if (currentMode == AppMode::Config || configApStarted) {
      page += F("<a href=\"/network\">📶 Network</a>");
      page += F("<a href=\"/device\">⚙ Device</a>");
      page += F("<a href=\"/mqtt\">📡 MQTT</a>");
      page += F("<a href=\"/modbus\">🔌 Modbus</a>");
      page += F("<a href=\"/protection\">🛡 Protection</a>");
      page += F("<a href=\"/display\">🖥 Display</a>");
      page += F("<a href=\"/history\">📊 History</a>");
      page += F("<a href=\"/system\">🔧 System</a>");
  }

  page += F("<a href=\"#\" onclick=\"rebootDevice();return false;\">🔄 Reboot</a>");
  page += F("</nav></div></div>");
  page += F("<main class=\"wrap\">");
  return page;
}

String buildPageFooter() {
  // return String(F("<script>function toggleMenu(){document.getElementById('mainNav').classList.toggle('open')}</script></main></body></html>"));
  return String(F(
    "<script>"
    "function toggleMenu(){document.getElementById('mainNav').classList.toggle('open')}"
    "async function rebootDevice(){"
    "if(!confirm('Reboot device?')) return;"
    "await fetch('/api/reboot',{method:'POST'}).catch(()=>{});"
    "}"
    "</script>"
    "</main></body></html>"
    ));
}

String buildHomePage() {
  const String apSsid = getConfigApSsid();
  const uint8_t heapPercent = getHeapUsedPercent();
  const uint8_t sketchPercent = getSketchUsedPercent();
  const uint8_t wifiPercent = getWifiQualityPercent();
  String page = buildPageHeader("Home");
  page.reserve(6500);
  page += F("<section class=\"panel\"><h2>Status</h2><div class=\"grid\">");
  page += F("<div>Firmware</div><div>");
  page += FW_VERSION;
  page += F("</div><div>Mode</div><div>");
  page += appModeToString(currentMode);
  page += F("</div><div>AP SSID</div><div>");
  page += apSsid;
  page += F("</div><div>AP IP</div><div>");
  page += WiFi.softAPIP().toString();
  page += F("</div><div>Saved WiFi</div><div>");
  page += hasSavedWifi ? htmlEscape(savedWifiSsid) : F("(none)");
  page += F("</div><div>STA status</div><div id=\"sta_status\">");
  page += wifiStatusToString(WiFi.status());
  page += F("</div><div>STA IP</div><div id=\"sta_ip\">");
  page += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : F("-");
  page += F("</div><div>RTC</div><div id=\"rtc\">");
  page += htmlEscape(getRtcString());
  page += F("</div><div>Last NTP Sync</div><div id=\"last_ntp\">");
  page += timeSynced ? htmlEscape(formatDateTime(lastNtpSyncEpoch)) : F("-");
  page += F("</div><div>Uptime</div><div id=\"uptime\">");
  page += formatUptime(millis());
  page += F("</div><div>CPU</div><div>");
  page += String(ESP.getCpuFreqMHz());
  page += F(" MHz</div><div>WiFi RSSI</div><div id=\"wifi_rssi\">");
  if (WiFi.status() == WL_CONNECTED) {
    page += String(getWifiRssi());
    page += F(" dBm");
  } else {
    page += F("-");
  }
  page += F("</div><div>MAC suffix</div><div>");
  page += getMacSuffix();
  page += F("</div><div>Free heap</div><div id=\"heap\">");
  page += formatBytesHuman(ESP.getFreeHeap());
  page += F(" (");
  page += String(ESP.getFreeHeap());
  page += F(" bytes)</div></div></section>");
  page += F("<section class=\"panel\"><h2>Resources</h2>");
  page += F("<div class=\"metric\"><div class=\"metricTop\"><strong>RAM Used</strong><span id=\"heap_pct\">");
  page += String(heapPercent);
  page += F("% · ");
  page += formatBytesHuman(getHeapUsedBytes());
  page += F(" / ");
  page += formatBytesHuman(getHeapTotalBytes());
  page += F("</span></div><div class=\"track\"><div id=\"heap_fill\" class=\"fill ");
  page += heapPercent >= 85 ? F("bad") : heapPercent >= 70 ? F("warn") : F("");
  page += F("\" style=\"width:");
  page += String(heapPercent);
  page += F("%\"></div></div></div>");
  page += F("<div class=\"metric\"><div class=\"metricTop\"><strong>Firmware Slot Used</strong><span id=\"sketch_pct\">");
  page += String(sketchPercent);
  page += F("% · ");
  page += formatBytesHuman(ESP.getSketchSize());
  page += F(" / ");
  page += formatBytesHuman(getSketchCapacityBytes());
  page += F("</span></div><div class=\"track\"><div id=\"sketch_fill\" class=\"fill ");
  page += sketchPercent >= 85 ? F("bad") : sketchPercent >= 70 ? F("warn") : F("");
  page += F("\" style=\"width:");
  page += String(sketchPercent);
  page += F("%\"></div></div></div>");
  page += F("<div class=\"metric\"><div class=\"metricTop\"><strong>WiFi Signal</strong><span id=\"wifi_pct\">");
  page += WiFi.status() == WL_CONNECTED ? String(wifiPercent) + F("% · ") + String(getWifiRssi()) + F(" dBm") : String("not connected");
  page += F("</span></div><div class=\"track\"><div id=\"wifi_fill\" class=\"fill ");
  page += wifiPercent <= 30 ? F("bad") : wifiPercent <= 55 ? F("warn") : F("");
  page += F("\" style=\"width:");
  page += String(wifiPercent);
  page += F("%\"></div></div></div></section>");
  // page += F("<section class=\"panel\"><h2>Quick Actions</h2><div class=\"actions\"><a href=\"/network\"><button>Network Settings</button></a><button onclick=\"rebootDevice()\">Reboot</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section>");
  // page += F("<script src=\"https://code.jquery.com/jquery-3.7.1.min.js\" integrity=\"sha256-3fpL0Lpn0lLDDWl12gXoK3l4U23j+gZM8x6Y1kubUFU=\" crossorigin=\"anonymous\"></script>");
  page += F("<script>");
  page += F("async function refreshStatus(){try{const r=await fetch('/api/status?_='+Date.now(),{cache:'no-store'});const d=await r.json();");
  page += F("document.getElementById('sta_status').textContent=d.sta_status||'-';");
  page += F("document.getElementById('sta_ip').textContent=d.sta_ip||'-';");
  page += F("document.getElementById('rtc').textContent=d.rtc||'-';");
  page += F("document.getElementById('last_ntp').textContent=d.last_ntp_sync||'-';");
  page += F("document.getElementById('uptime').textContent=d.uptime_text||'-';");
  page += F("document.getElementById('wifi_rssi').textContent=d.wifi_rssi<=-120?'-':d.wifi_rssi+' dBm';");
  page += F("document.getElementById('heap').textContent=(100-d.heap_used_percent)+'% free';");
  page += F("document.getElementById('heap_pct').textContent=d.heap_used_percent+'% · '+d.heap_used_human+' / '+d.heap_total_human;");
  page += F("document.getElementById('heap_fill').style.width=d.heap_used_percent+'%';");
  page += F("document.getElementById('sketch_pct').textContent=d.sketch_used_percent+'% · '+d.sketch_size_human+' / '+d.sketch_capacity_human;");
  page += F("document.getElementById('sketch_fill').style.width=d.sketch_used_percent+'%';");
  page += F("document.getElementById('wifi_pct').textContent=d.wifi_quality_percent+'% · '+(d.wifi_rssi<=-120?'-':d.wifi_rssi+' dBm');");
  page += F("document.getElementById('wifi_fill').style.width=d.wifi_quality_percent+'%';");
  page += F("}catch(e){console.log('refreshStatus failed',e)}}");
  page += F("document.addEventListener('DOMContentLoaded',function(){refreshStatus();setInterval(refreshStatus,1000);});");
  page += F("</script>");
  page += buildPageFooter();
  return page;
}

String buildNetworkPage() {
  String page = buildPageHeader("Network");
  page.reserve(5600);
  page += F("<section class=\"panel\"><h2>WiFi Nearby</h2>");
  page += F("<button id=\"scanBtn\" onclick=\"scanWifi()\">Scan WiFi</button>");
  page += F("<p id=\"scanState\" class=\"muted\">Ready.</p><div id=\"networks\"></div></section>");
  page += F("<section class=\"panel\"><h2>WiFi Connection</h2>");
  page += F("<label for=\"ssid\">SSID</label><input id=\"ssid\" maxlength=\"32\" placeholder=\"WiFi SSID\" value=\"");
  page += htmlEscape(savedWifiSsid);
  page += F("\"><label for=\"password\">Password</label><input id=\"password\" type=\"password\" maxlength=\"64\" placeholder=\"WiFi password\">");
  page += F("<div class=\"actions\"><button onclick=\"saveWifi()\">Save WiFi</button><button onclick=\"rebootDevice()\">Reboot</button></div>");
  page += F("<p id=\"saveState\" class=\"muted\">Saved credentials apply after reboot.</p></section></main>");
  page += F("<script>");
  page += F("async function scanWifi(){const b=document.getElementById('scanBtn'),s=document.getElementById('scanState'),n=document.getElementById('networks');");
  page += F("b.disabled=true;s.textContent='Scanning...';n.innerHTML='';try{const r=await fetch('/api/wifi/scan');const d=await r.json();");
  page += F("s.textContent=d.count+' network(s) found';n.innerHTML=d.networks.map(x=>'<div class=\"net\"><div><div class=\"ssid\">'+esc(x.ssid||'(hidden)')+'</div><div class=\"muted\">CH '+x.channel+' · '+x.encryption+'</div></div><div><span class=\"tag\">'+x.rssi+' dBm</span> <button class=\"use\" onclick=\"useSsid(\\''+escAttr(x.ssid)+'\\')\">Use</button></div></div>').join('');");
  page += F("}catch(e){s.textContent='Scan failed';}b.disabled=false}");
  page += F("function useSsid(v){document.getElementById('ssid').value=v;document.getElementById('password').focus()}");
  page += F("async function saveWifi(){const s=document.getElementById('saveState'),ssid=document.getElementById('ssid').value.trim(),password=document.getElementById('password').value;");
  page += F("if(!ssid){s.textContent='SSID is required';return}const body=new URLSearchParams({ssid,password});s.textContent='Saving...';");
  page += F("try{const r=await fetch('/api/wifi/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved. Reboot to connect.':(d.error||'Save failed')}catch(e){s.textContent='Save failed'}}");
  page += F("async function rebootDevice(){document.getElementById('saveState').textContent='Rebooting...';await fetch('/api/reboot',{method:'POST'}).catch(()=>{});}");
  page += F("function esc(v){return String(v).replace(/[&<>\"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[m]))}");
  page += F("function escAttr(v){return String(v).replace(/[\\\\']/g,m=>'\\\\'+m).replace(/[\\r\\n]/g,'')}");
  page += F("</script>");
  page += buildPageFooter();
  return page;
}

// ============================================================================
// CONFIG PAGES - Device, MQTT, Modbus, Protection, Display, History, System
// ============================================================================
// Author: Claude (AI Assistant) @ 2026-05-30
// Provides web UI pages for configuring all device settings
// ============================================================================

String buildDeviceConfigPage() {
  DeviceConfig cfg = ConfigManager::loadDeviceConfig();
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
  MqttConfig cfg = ConfigManager::loadMqttConfig();
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
  page += F("\"><div class=\"actions\"><button onclick=\"saveMqttConfig()\">Save MQTT Config</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section></main>");
  page += F("<script>async function saveMqttConfig(){const s=document.getElementById('saveState');s.textContent='Saving...';const body=new URLSearchParams({");
  page += F("mqtt_enabled:document.getElementById('mqtt_enabled').checked?'1':'0',");
  page += F("mqtt_host:document.getElementById('mqtt_host').value,mqtt_port:document.getElementById('mqtt_port').value,");
  page += F("mqtt_username:document.getElementById('mqtt_username').value,mqtt_password:document.getElementById('mqtt_password').value,");
  page += F("mqtt_client_id:document.getElementById('mqtt_client_id').value,mqtt_base_topic:document.getElementById('mqtt_base_topic').value,");
  page += F("mqtt_publish_interval:document.getElementById('mqtt_publish_interval').value});try{const r=await fetch('/api/mqtt/save',{method:'POST',");
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}");
  page += F("catch(e){s.textContent='Save failed'}}</script>");
  page += buildPageFooter();
  return page;
}

String buildModbusConfigPage() {
  ModbusConfig cfg = ConfigManager::loadModbusConfig();
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
  page += F(">PM2230</option><option value=\"1\"");
  page += cfg.meter_profile == 1 ? F(" selected") : F("");
  page += F(">PM1611</option></select>");
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
  ProtectionConfig cfg = ConfigManager::loadProtectionConfig();
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
  page += F("headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const d=await r.json();s.textContent=d.ok?'Saved successfully':'Save failed'}");
  page += F("catch(e){s.textContent='Save failed'}}</script>");
  page += buildPageFooter();
  return page;
}

String buildDisplayConfigPage() {
  DisplayConfig cfg = ConfigManager::loadDisplayConfig();
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
  HistoryConfig cfg = ConfigManager::loadHistoryConfig();
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
  SystemConfig cfg = ConfigManager::loadSystemConfig();
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
  if (!requireConfigMode()) return;
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

// ============================================================================
// CONFIG API HANDLERS
// ============================================================================
void handleDeviceConfigSaveApi() {
  DeviceConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

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
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleMqttConfigSaveApi() {
  MqttConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

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
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleModbusConfigSaveApi() {
  ModbusConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.baudrate = configServer.hasArg("modbus_baudrate") ? configServer.arg("modbus_baudrate").toInt() : 19200;
  cfg.slave_id = configServer.hasArg("modbus_slave_id") ? configServer.arg("modbus_slave_id").toInt() : 1;
  cfg.parity = configServer.hasArg("modbus_parity") ? configServer.arg("modbus_parity").toInt() : 0;
  cfg.stop_bits = configServer.hasArg("modbus_stop_bits") ? configServer.arg("modbus_stop_bits").toInt() : 1;
  cfg.poll_interval_ms = configServer.hasArg("modbus_poll_interval") ? configServer.arg("modbus_poll_interval").toInt() : 1000;
  cfg.timeout_ms = configServer.hasArg("modbus_timeout") ? configServer.arg("modbus_timeout").toInt() : 1000;
  cfg.retry_count = configServer.hasArg("modbus_retry_count") ? configServer.arg("modbus_retry_count").toInt() : 3;
  cfg.meter_profile = configServer.hasArg("modbus_profile") ? configServer.arg("modbus_profile").toInt() : 0;

  bool ok = ConfigManager::saveModbusConfig(cfg);
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleProtectionConfigSaveApi() {
  ProtectionConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.relay_enabled = configServer.hasArg("relay_enabled") && configServer.arg("relay_enabled") == "1";
  cfg.current_limit_a = configServer.hasArg("current_limit") ? configServer.arg("current_limit").toInt() : 16;
  cfg.trip_delay_ms = configServer.hasArg("trip_delay") ? configServer.arg("trip_delay").toInt() : 1000;
  cfg.reset_mode = configServer.hasArg("reset_mode") ? configServer.arg("reset_mode").toInt() : 0;
  cfg.auto_retry_enabled = configServer.hasArg("auto_retry_enabled") && configServer.arg("auto_retry_enabled") == "1";
  cfg.auto_retry_delay_sec = configServer.hasArg("auto_retry_delay") ? configServer.arg("auto_retry_delay").toInt() : 300;
  cfg.trip_on_meter_stale = configServer.hasArg("trip_on_stale") && configServer.arg("trip_on_stale") == "1";

  bool ok = ConfigManager::saveProtectionConfig(cfg);
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleDisplayConfigSaveApi() {
  DisplayConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.enabled = configServer.hasArg("display_enabled") && configServer.arg("display_enabled") == "1";
  cfg.type = configServer.hasArg("display_type") ? configServer.arg("display_type").toInt() : 0;
  cfg.i2c_address = configServer.hasArg("i2c_address") ? (uint8_t)strtol(configServer.arg("i2c_address").c_str(), nullptr, 16) : 0x3C;
  cfg.rotation_interval_sec = configServer.hasArg("rotation_interval") ? configServer.arg("rotation_interval").toInt() : 5;
  cfg.brightness = configServer.hasArg("brightness") ? configServer.arg("brightness").toInt() : 200;

  bool ok = ConfigManager::saveDisplayConfig(cfg);
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleHistoryConfigSaveApi() {
  HistoryConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.enabled = configServer.hasArg("history_enabled") && configServer.arg("history_enabled") == "1";
  cfg.days_retained = configServer.hasArg("days_retained") ? configServer.arg("days_retained").toInt() : 7;
  cfg.flush_interval_sec = configServer.hasArg("flush_interval") ? configServer.arg("flush_interval").toInt() : 3600;

  bool ok = ConfigManager::saveHistoryConfig(cfg);
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleSystemConfigSaveApi() {
  SystemConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

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
  sendNoCacheHeader();
  if (ok) {
    configServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    configServer.send(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
  }
}

void handleSetupRoot() {
  if (serveLittleFsPage("/index.html", "text/html")) {
    return;
  }

  sendNoCacheHeader();
  configServer.send(200, "text/html", buildHomePage());
}

void handleNetworkPage() {
  if (!requireConfigMode()) return;

  if (serveLittleFsPage("/network.html", "text/html")) {
    return;
  }

  sendNoCacheHeader();
  configServer.send(200, "text/html", buildNetworkPage());
}

void handleAppCss() {
  if (!serveLittleFsPage("/app.css", "text/css")) {
    configServer.send(404, "text/plain", "missing");
  }
}

void handleAppJs() {
  if (!serveLittleFsPage("/app.js", "application/javascript")) {
    configServer.send(404, "text/plain", "missing");
  }
}

void handleStatusApi() {
  String body;
  body.reserve(256);
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
  body += F("\",\"sta_ip\":\"");
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
  body += F(",\"cpu_mhz\":");
  body += String(ESP.getCpuFreqMHz());
  body += F(",\"flash_mb\":");
  body += String(ESP.getFlashChipSize() / (1024 * 1024));
  body += F(",\"littlefs_ready\":");
  body += littleFsReady ? F("true") : F("false");
  body += F(",\"littlefs_total\":");
  body += String(getLittleFsTotalBytes());
  body += F(",\"littlefs_total_human\":\"");
  body += jsonEscape(formatBytesHuman(getLittleFsTotalBytes()));
  body += F("\"");
  body += F(",\"littlefs_used\":");
  body += String(getLittleFsUsedBytes());
  body += F(",\"littlefs_used_human\":\"");
  body += jsonEscape(formatBytesHuman(getLittleFsUsedBytes()));
  body += F("\"");
  body += F(",\"littlefs_used_percent\":");
  body += String(getLittleFsUsedPercent());
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
  configServer.on("/network", HTTP_GET, handleNetworkPage);
  configServer.on("/app.css", HTTP_GET, handleAppCss);
  configServer.on("/app.js", HTTP_GET, handleAppJs);
  configServer.on("/device", HTTP_GET, handleDeviceConfigPage);
  configServer.on("/mqtt", HTTP_GET, handleMqttConfigPage);
  configServer.on("/modbus", HTTP_GET, handleModbusConfigPage);
  configServer.on("/protection", HTTP_GET, handleProtectionConfigPage);
  configServer.on("/display", HTTP_GET, handleDisplayConfigPage);
  configServer.on("/history", HTTP_GET, handleHistoryConfigPage);
  configServer.on("/system", HTTP_GET, handleSystemConfigPage);
  configServer.on("/api/status", HTTP_GET, handleStatusApi);
  configServer.on("/api/wifi/scan", HTTP_GET, handleWifiScanApi);
  configServer.on("/api/wifi/save", HTTP_POST, handleWifiSaveApi);
  configServer.on("/api/device/save", HTTP_POST, handleDeviceConfigSaveApi);
  configServer.on("/api/mqtt/save", HTTP_POST, handleMqttConfigSaveApi);
  configServer.on("/api/modbus/save", HTTP_POST, handleModbusConfigSaveApi);
  configServer.on("/api/protection/save", HTTP_POST, handleProtectionConfigSaveApi);
  configServer.on("/api/display/save", HTTP_POST, handleDisplayConfigSaveApi);
  configServer.on("/api/history/save", HTTP_POST, handleHistoryConfigSaveApi);
  configServer.on("/api/system/save", HTTP_POST, handleSystemConfigSaveApi);
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
  // setBuiltinLed(false);

  printBootBanner();
  printChipInfo();
  printButtonInfo();
  initLittleFs();
  loadWifiConfig();
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
