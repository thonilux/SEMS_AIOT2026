#include <Arduino.h>
#include <WebServer.h>
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
bool configWebStarted = false;
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

void sendNoCacheHeader() {
  configServer.sendHeader("Cache-Control", "no-store");
}

String buildSetupPage() {
  const String apSsid = getConfigApSsid();
  String page;
  page.reserve(5200);
  page += F("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">");
  page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  page += F("<title>PM1611 Setup</title><style>");
  page += F(":root{font-family:Inter,Segoe UI,Arial,sans-serif;color:#17202a;background:#f4f7f9}");
  page += F("body{margin:0}.bar{background:#111827;color:white;padding:14px 18px}");
  page += F(".wrap{max-width:760px;margin:0 auto;padding:18px}.panel{background:white;border:1px solid #d9e1e8;border-radius:8px;padding:16px;margin:14px 0}");
  page += F("h1{font-size:21px;margin:0}.muted{color:#667085}.grid{display:grid;grid-template-columns:150px 1fr;gap:8px 12px}");
  page += F("button{background:#0f766e;color:white;border:0;border-radius:6px;padding:10px 13px;font-weight:700}");
  page += F("button:disabled{background:#94a3b8}.net{display:flex;justify-content:space-between;gap:10px;border-top:1px solid #e5e7eb;padding:10px 0}");
  page += F(".ssid{font-weight:700;overflow-wrap:anywhere}.tag{font-size:12px;color:#475467;background:#eef2f6;border-radius:999px;padding:2px 8px}");
  page += F("@media(max-width:560px){.grid{grid-template-columns:1fr}.net{display:block}}");
  page += F("</style></head><body>");
  page += F("<div class=\"bar\"><h1>PM1611 RS485 Setup</h1><div class=\"muted\">Config Mode</div></div>");
  page += F("<main class=\"wrap\"><section class=\"panel\"><h2>Device</h2><div class=\"grid\">");
  page += F("<div>Firmware</div><div>");
  page += FW_VERSION;
  page += F("</div><div>Mode</div><div>");
  page += appModeToString(currentMode);
  page += F("</div><div>AP SSID</div><div>");
  page += apSsid;
  page += F("</div><div>AP IP</div><div>");
  page += WiFi.softAPIP().toString();
  page += F("</div><div>MAC suffix</div><div>");
  page += getMacSuffix();
  page += F("</div><div>Free heap</div><div id=\"heap\">");
  page += String(ESP.getFreeHeap());
  page += F(" bytes</div></div></section>");
  page += F("<section class=\"panel\"><h2>WiFi Nearby</h2>");
  page += F("<button id=\"scanBtn\" onclick=\"scanWifi()\">Scan WiFi</button>");
  page += F("<p id=\"scanState\" class=\"muted\">Ready.</p><div id=\"networks\"></div></section></main>");
  page += F("<script>");
  page += F("async function scanWifi(){const b=document.getElementById('scanBtn'),s=document.getElementById('scanState'),n=document.getElementById('networks');");
  page += F("b.disabled=true;s.textContent='Scanning...';n.innerHTML='';try{const r=await fetch('/api/wifi/scan');const d=await r.json();");
  page += F("s.textContent=d.count+' network(s) found';n.innerHTML=d.networks.map(x=>'<div class=\"net\"><div><div class=\"ssid\">'+esc(x.ssid||'(hidden)')+'</div><div class=\"muted\">CH '+x.channel+' · '+x.encryption+'</div></div><div class=\"tag\">'+x.rssi+' dBm</div></div>').join('');");
  page += F("}catch(e){s.textContent='Scan failed';}b.disabled=false}");
  page += F("function esc(v){return String(v).replace(/[&<>\"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[m]))}");
  page += F("</script></body></html>");
  return page;
}

void handleSetupRoot() {
  sendNoCacheHeader();
  configServer.send(200, "text/html", buildSetupPage());
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
  body += F("\",\"mac_suffix\":\"");
  body += getMacSuffix();
  body += F("\",\"free_heap\":");
  body += String(ESP.getFreeHeap());
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

void handleNotFound() {
  configServer.sendHeader("Location", "/", true);
  configServer.send(302, "text/plain", "");
}

void startConfigWebServer() {
  if (configWebStarted) {
    return;
  }

  configServer.on("/", HTTP_GET, handleSetupRoot);
  configServer.on("/api/status", HTTP_GET, handleStatusApi);
  configServer.on("/api/wifi/scan", HTTP_GET, handleWifiScanApi);
  configServer.onNotFound(handleNotFound);
  configServer.begin();

  configWebStarted = true;
  Serial.println("Config Web UI started");
  Serial.println("Open: http://192.168.4.1/");
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
  printModeInfo();
}

void loop() {
  const uint32_t nowMs = millis();

  handleConfigButton(nowMs);
  if (configWebStarted) {
    configServer.handleClient();
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
    Serial.println(appModeToString(currentMode));
  }
}
