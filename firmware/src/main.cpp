#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>
#include "AppMode.h"
#include "PinMap.h"
#include "Version.h"

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
bool timeSynced = false;
time_t lastNtpSyncEpoch = 0;
WebServer configServer(80);

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

void syncTimeFromNtp() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("NTP skipped: WiFi is not connected");
    return;
  }

  Serial.print("NTP sync: ");
  Serial.print(kNtpServer1);
  Serial.print(", ");
  Serial.println(kNtpServer2);

  configTzTime(kNtpTimezone, kNtpServer1, kNtpServer2);

  struct tm timeInfo {};
  const uint32_t startMs = millis();
  while (!getLocalTime(&timeInfo, 250) && millis() - startMs < kNtpSyncTimeoutMs) {
    Serial.print('.');
  }
  Serial.println();

  if (!getLocalTime(&timeInfo, 1)) {
    timeSynced = false;
    Serial.println("NTP sync failed");
    return;
  }

  lastNtpSyncEpoch = time(nullptr);
  timeSynced = true;

  Serial.print("RTC synced: ");
  Serial.println(getRtcString());
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

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < kWifiConnectTimeoutMs) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi STA connected");
    Serial.print("STA IP: ");
    Serial.println(WiFi.localIP());
    return;
  }

  Serial.print("WiFi STA failed: ");
  Serial.println(wifiStatusToString(WiFi.status()));
  WiFi.disconnect(false, false);
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
  page += F("@media(max-width:560px){.grid{grid-template-columns:1fr}.net{display:block}.top{display:block}nav{margin-top:10px}}");
  page += F("</style></head><body><div class=\"bar\"><div class=\"top\"><div><h1>");
  page += htmlEscape(title);
  page += F("</h1><div class=\"muted\">PM1611 RS485 Reader</div></div><nav><a href=\"/\">Home</a><a href=\"/network\">Network</a></nav></div></div>");
  page += F("<main class=\"wrap\">");
  return page;
}

String buildPageFooter() {
  return String(F("</main></body></html>"));
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
  page += F("</div><div>STA status</div><div>");
  page += wifiStatusToString(WiFi.status());
  page += F("</div><div>STA IP</div><div>");
  page += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : F("-");
  page += F("</div><div>RTC</div><div>");
  page += htmlEscape(getRtcString());
  page += F("</div><div>Last NTP Sync</div><div>");
  page += timeSynced ? htmlEscape(formatDateTime(lastNtpSyncEpoch)) : F("-");
  page += F("</div><div>Uptime</div><div>");
  page += formatUptime(millis());
  page += F("</div><div>CPU</div><div>");
  page += String(ESP.getCpuFreqMHz());
  page += F(" MHz</div><div>WiFi RSSI</div><div>");
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
  page += F("<div class=\"metric\"><div class=\"metricTop\"><strong>RAM Used</strong><span>");
  page += String(heapPercent);
  page += F("% · ");
  page += formatBytesHuman(getHeapUsedBytes());
  page += F(" / ");
  page += formatBytesHuman(getHeapTotalBytes());
  page += F("</span></div><div class=\"track\"><div class=\"fill ");
  page += heapPercent >= 85 ? F("bad") : heapPercent >= 70 ? F("warn") : F("");
  page += F("\" style=\"width:");
  page += String(heapPercent);
  page += F("%\"></div></div></div>");
  page += F("<div class=\"metric\"><div class=\"metricTop\"><strong>Firmware Slot Used</strong><span>");
  page += String(sketchPercent);
  page += F("% · ");
  page += formatBytesHuman(ESP.getSketchSize());
  page += F(" / ");
  page += formatBytesHuman(getSketchCapacityBytes());
  page += F("</span></div><div class=\"track\"><div class=\"fill ");
  page += sketchPercent >= 85 ? F("bad") : sketchPercent >= 70 ? F("warn") : F("");
  page += F("\" style=\"width:");
  page += String(sketchPercent);
  page += F("%\"></div></div></div>");
  page += F("<div class=\"metric\"><div class=\"metricTop\"><strong>WiFi Signal</strong><span>");
  page += WiFi.status() == WL_CONNECTED ? String(wifiPercent) + F("% · ") + String(getWifiRssi()) + F(" dBm") : String("not connected");
  page += F("</span></div><div class=\"track\"><div class=\"fill ");
  page += wifiPercent <= 30 ? F("bad") : wifiPercent <= 55 ? F("warn") : F("");
  page += F("\" style=\"width:");
  page += String(wifiPercent);
  page += F("%\"></div></div></div></section>");
  page += F("<section class=\"panel\"><h2>Quick Actions</h2><div class=\"actions\"><a href=\"/network\"><button>Network Settings</button></a><button onclick=\"rebootDevice()\">Reboot</button></div><p id=\"saveState\" class=\"muted\">Ready.</p></section>");
  page += F("<script>");
  page += F("async function rebootDevice(){document.getElementById('saveState').textContent='Rebooting...';await fetch('/api/reboot',{method:'POST'}).catch(()=>{});}");
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
  configServer.on("/api/status", HTTP_GET, handleStatusApi);
  configServer.on("/api/wifi/scan", HTTP_GET, handleWifiScanApi);
  configServer.on("/api/wifi/save", HTTP_POST, handleWifiSaveApi);
  configServer.on("/api/reboot", HTTP_POST, handleRebootApi);
  configServer.onNotFound(handleNotFound);
  configServer.begin();

  configWebStarted = true;
  Serial.println("Web UI started");
  printWebUiAddresses();
}

void startConfigAccessPoint() {
  if (configApStarted) {
    startConfigWebServer();
    return;
  }

  const String ssid = getConfigApSsid();
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);

  WiFi.disconnect(true, true);
  delay(100);
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
  loadWifiConfig();
  connectToSavedWifi();
  if (WiFi.status() == WL_CONNECTED) {
    syncTimeFromNtp();
  }
  if (WiFi.status() == WL_CONNECTED) {
    startConfigWebServer();
  }
  printModeInfo();
}

void loop() {
  const uint32_t nowMs = millis();

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
