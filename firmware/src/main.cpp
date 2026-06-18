#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <Ethernet_Generic.h>
#include <PubSubClient.h>

// --- pins ---
static constexpr uint8_t kLedPin   = 2;
static constexpr uint8_t kEthCs    = 5;
static constexpr uint8_t kEthInt   = 27;
static constexpr uint8_t kEthRst   = 26;

// --- W5500 state ---
static bool ethReady = false;
static byte ethMac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// --- MQTT config ---
struct MqttConfig {
  String host;
  uint16_t port = 1883;
  String user;
  String pass;
  String topicPrefix;  // e.g. "sems/device1"
  bool enabled = false;
};
static MqttConfig mqttCfg;
static constexpr char kMqttNs[] = "mqtt";

// --- MQTT state ---
static WiFiClient    mqttWifiClient;
static PubSubClient  mqttClient(mqttWifiClient);
static bool mqttConnected   = false;
static uint32_t mqttLastTryMs = 0;
static constexpr uint32_t kMqttRetryMs = 5000;

// --- AP config ---
static constexpr char kApSsidPrefix[]  = "SEMS-SETUP";
static constexpr char kApPassword[]    = "sems1234";
static constexpr char kNvsNamespace[]  = "wifi";
static constexpr uint32_t kStaTimeoutMs = 15000;
static constexpr uint8_t kMaxSavedWifi = 5;

// RTC memory survives reboot but NOT power-off
// Used to signal "this reboot was triggered by WiFi failure — start AP"
RTC_DATA_ATTR static uint32_t rtcWifiFailMagic = 0;
static constexpr uint32_t kWifiFailMagic = 0xDEADF17E;

// --- OLED ---
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R2, U8X8_PIN_NONE);
static bool oledReady = false;
static uint32_t oledUntilMs = 0;
static uint8_t oledPage = 0;
static uint32_t oledPageMs = 0;
static constexpr uint32_t kPageIntervalMs = 3000;
static uint32_t oledHeartbeatMs = 0;
static uint8_t  oledHeartTick = 0;

// --- WiFi credentials list ---
struct WifiEntry { String ssid; String pass; };
static WifiEntry wifiList[kMaxSavedWifi];
static uint8_t wifiCount = 0;

// --- WiFi state ---
static String savedSsid;      // currently connecting/connected SSID
static bool apStarted    = false;
static bool staConnected = false;
static bool staConnecting = false;
static uint32_t staStartMs = 0;
static uint8_t triedMask = 0;   // bitmask of wifiList indices tried this round
static uint32_t rebootAtMs = 0; // 0 = no reboot scheduled
static uint8_t scanRetryCount = 0;
static constexpr uint8_t kMaxScanRetry = 3;
static bool scanPending = false;  // async scan kicked, waiting for result

static WebServer server(80);

// ============================================================
// OLED helpers
// ============================================================
// Draw a horizontal divider line
static void oledHLine(uint8_t y) {
  oled.drawHLine(0, y, 128);
}

// Right-align a string within [x, 128)
static void oledDrawRight(const String& s, uint8_t y) {
  uint8_t w = oled.getStrWidth(s.c_str());
  oled.drawStr(128 - w, y, s.c_str());
}

// ============================================================
// OLED status overlay  — call oledShow() for transient events
// ============================================================
void oledShow(const char* l1, const char* l2 = "", const char* l3 = "", uint32_t durationMs = 4000) {
  oledUntilMs = millis() + durationMs;
  if (!oledReady) return;
  oled.clearBuffer();

  // Large icon left, title right — use open_iconic_embedded 2x (16px)
  // \x4e = chip/cpu icon in open_iconic_embedded_2x_t
  oled.setFont(u8g2_font_open_iconic_embedded_2x_t);
  oled.drawGlyph(0, 20, 0x4e);

  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(22, 14, l1);

  oled.setFont(u8g2_font_6x12_tf);
  if (l2[0]) oled.drawStr(22, 28, l2);

  oledHLine(34);
  if (l3[0]) oled.drawStr(0, 48, l3);

  oled.sendBuffer();
}

// ============================================================
// OLED page rotation
// ============================================================

// Page 0: Device info
static void drawPageDevice() {
  // Header bar
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "SEMS AIoT");
  oledDrawRight("v" FW_VERSION, 10);
  oledHLine(13);

  // Icon: chip
  oled.setFont(u8g2_font_open_iconic_embedded_2x_t);
  oled.drawGlyph(0, 56, 0x4e);  // cpu/chip

  // Uptime
  oled.setFont(u8g2_font_6x12_tf);
  uint32_t s = millis() / 1000;
  uint32_t m = s / 60; s %= 60;
  uint32_t h = m / 60; m %= 60;
  char uptime[16]; snprintf(uptime, sizeof(uptime), "%02luh%02lum%02lus", h, m, s);
  oled.drawStr(22, 30, "Uptime:");
  oled.drawStr(22, 44, uptime);

  // Free heap
  char heap[16]; snprintf(heap, sizeof(heap), "Heap:%lukB", ESP.getFreeHeap() / 1024);
  oled.drawStr(22, 58, heap);
}

// Page 1: WiFi status
static void drawPageNetwork() {
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "WiFi");
  oledHLine(13);

  oled.setFont(u8g2_font_open_iconic_www_2x_t);
  oled.drawGlyph(0, 36, 0x51);

  oled.setFont(u8g2_font_6x12_tf);
  if (staConnected) {
    oled.setFont(u8g2_font_helvB08_tf);
    oled.drawStr(22, 26, WiFi.localIP().toString().c_str());
    oled.setFont(u8g2_font_6x12_tf);
    String ssidStr = savedSsid.length() > 17 ? savedSsid.substring(0, 16) + "~" : savedSsid;
    oled.drawStr(22, 38, ssidStr.c_str());
    char rssi[12]; snprintf(rssi, sizeof(rssi), "RSSI:%ddBm", WiFi.RSSI());
    oled.drawStr(22, 50, rssi);
  } else if (staConnecting) {
    oled.drawStr(22, 26, "Connecting...");
    String ssidStr = savedSsid.length() > 17 ? savedSsid.substring(0, 16) + "~" : savedSsid;
    oled.drawStr(22, 38, ssidStr.c_str());
  } else {
    oled.drawStr(22, 26, "No WiFi STA");
  }

  oledHLine(53);
  oled.setFont(u8g2_font_5x7_tf);
  if (apStarted) {
    String apLine = "AP " + WiFi.softAPSSID();
    oled.drawStr(0, 63, apLine.c_str());
  } else {
    oled.drawStr(0, 63, "AP: off");
  }
}

// Page 2: LAN (W5500) status
static void drawPageLan() {
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "LAN");
  oledHLine(13);

  oled.setFont(u8g2_font_open_iconic_embedded_2x_t);
  oled.drawGlyph(0, 36, ethReady ? 0x71 : 0x70);  // link-intact / link-broken

  oled.setFont(u8g2_font_6x12_tf);
  if (ethReady) {
    oled.setFont(u8g2_font_helvB08_tf);
    oled.drawStr(22, 26, Ethernet.localIP().toString().c_str());
    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(22, 38, "W5500");
    oled.drawStr(22, 50, Ethernet.linkStatus() == LinkON ? "Link: UP" : "Link: DOWN");
  } else {
    oled.drawStr(22, 26, "No LAN");
    oled.drawStr(22, 38, Ethernet.linkStatus() == LinkON ? "Cable OK" : "No cable");
  }

  oledHLine(53);
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(0, 63, "MQTT transport");
}

void oledDrawPage(uint8_t page) {
  oled.clearBuffer();
  switch (page % 3) {
    case 0: drawPageDevice();  break;
    case 1: drawPageNetwork(); break;
    case 2: drawPageLan();     break;
  }
  oled.sendBuffer();
}

// Draw/erase heartbeat dot at bottom-right corner
static void oledHeartbeat(uint32_t now) {
  if (!oledReady) return;
  if (now < oledUntilMs) return;  // overlay active — skip
  if (now - oledHeartbeatMs < 500) return;
  oledHeartbeatMs = now;
  oledHeartTick++;
  // Erase previous dot area (5x5 px at 123,59)
  oled.setDrawColor(0);
  oled.drawBox(123, 59, 5, 5);
  oled.setDrawColor(1);
  // Draw dot on odd ticks
  if (oledHeartTick & 1) oled.drawBox(124, 60, 3, 3);
  oled.sendBuffer();
}

void updateOled(uint32_t now) {
  if (!oledReady) return;
  if (now < oledUntilMs) return;
  oledHeartbeat(now);
  if (now - oledPageMs < kPageIntervalMs) return;
  oledPageMs = now;
  oledPage = (oledPage + 1) % 3;
  oledDrawPage(oledPage);
}

// ============================================================
// NVS — multi WiFi list
// Keys: "n"=count, "s0".."s4"=ssid, "p0".."p4"=pass
// ============================================================
void loadWifiList() {
  Preferences prefs;
  prefs.begin(kNvsNamespace, true);
  wifiCount = min((uint8_t)prefs.getUChar("n", 0), kMaxSavedWifi);
  for (uint8_t i = 0; i < wifiCount; i++) {
    wifiList[i].ssid = prefs.getString(("s" + String(i)).c_str(), "");
    wifiList[i].pass = prefs.getString(("p" + String(i)).c_str(), "");
  }
  prefs.end();
  Serial.printf("Loaded %d saved WiFi network(s)\n", wifiCount);
  for (uint8_t i = 0; i < wifiCount; i++)
    Serial.printf("  [%d] %s\n", i, wifiList[i].ssid.c_str());
}

void saveWifiList() {
  Preferences prefs;
  prefs.begin(kNvsNamespace, false);
  prefs.putUChar("n", wifiCount);
  for (uint8_t i = 0; i < wifiCount; i++) {
    prefs.putString(("s" + String(i)).c_str(), wifiList[i].ssid);
    prefs.putString(("p" + String(i)).c_str(), wifiList[i].pass);
  }
  prefs.end();
}

// Add or update entry. Returns false if list is full and SSID not found.
bool addOrUpdateWifi(const String& ssid, const String& pass) {
  for (uint8_t i = 0; i < wifiCount; i++) {
    if (wifiList[i].ssid == ssid) {
      wifiList[i].pass = pass;
      saveWifiList();
      return true;
    }
  }
  if (wifiCount >= kMaxSavedWifi) return false;
  wifiList[wifiCount++] = {ssid, pass};
  saveWifiList();
  return true;
}

void removeWifi(const String& ssid) {
  for (uint8_t i = 0; i < wifiCount; i++) {
    if (wifiList[i].ssid == ssid) {
      for (uint8_t j = i; j < wifiCount - 1; j++) wifiList[j] = wifiList[j+1];
      wifiCount--;
      saveWifiList();
      return;
    }
  }
}

void clearAllWifi() {
  wifiCount = 0;
  Preferences prefs;
  prefs.begin(kNvsNamespace, false);
  prefs.clear();
  prefs.end();
}

// ============================================================
// MQTT NVS + lifecycle
// ============================================================
void loadMqttConfig() {
  Preferences prefs;
  prefs.begin(kMqttNs, true);
  mqttCfg.enabled     = prefs.getBool("en", false);
  mqttCfg.host        = prefs.getString("host", "");
  mqttCfg.port        = prefs.getUShort("port", 1883);
  mqttCfg.user        = prefs.getString("user", "");
  mqttCfg.pass        = prefs.getString("pass", "");
  mqttCfg.topicPrefix = prefs.getString("topic", "sems");
  prefs.end();
  Serial.printf("MQTT config: %s:%d en=%d\n", mqttCfg.host.c_str(), mqttCfg.port, mqttCfg.enabled);
}

void saveMqttConfig() {
  Preferences prefs;
  prefs.begin(kMqttNs, false);
  prefs.putBool("en",      mqttCfg.enabled);
  prefs.putString("host",  mqttCfg.host);
  prefs.putUShort("port",  mqttCfg.port);
  prefs.putString("user",  mqttCfg.user);
  prefs.putString("pass",  mqttCfg.pass);
  prefs.putString("topic", mqttCfg.topicPrefix);
  prefs.end();
}

void mqttPublish(const char* subtopic, const String& payload) {
  if (!mqttConnected) return;
  String topic = mqttCfg.topicPrefix + "/" + subtopic;
  mqttClient.publish(topic.c_str(), payload.c_str());
}

void handleMqttLifecycle(uint32_t now) {
  if (!mqttCfg.enabled || mqttCfg.host.isEmpty()) return;
  if (!staConnected) return;  // need WiFi STA

  if (mqttClient.connected()) {
    mqttConnected = true;
    mqttClient.loop();
    return;
  }

  mqttConnected = false;
  if (now - mqttLastTryMs < kMqttRetryMs) return;
  mqttLastTryMs = now;

  mqttClient.setServer(mqttCfg.host.c_str(), mqttCfg.port);
  String clientId = "sems-" + WiFi.macAddress();
  clientId.replace(":", "");

  bool ok;
  if (mqttCfg.user.isEmpty()) {
    ok = mqttClient.connect(clientId.c_str());
  } else {
    ok = mqttClient.connect(clientId.c_str(), mqttCfg.user.c_str(), mqttCfg.pass.c_str());
  }

  if (ok) {
    mqttConnected = true;
    Serial.println("MQTT connected");
    oledShow("MQTT", "Connected", mqttCfg.host.c_str(), 3000);
    mqttPublish("status", "{\"online\":true,\"fw\":\"" FW_VERSION "\"}");
  } else {
    Serial.printf("MQTT failed rc=%d\n", mqttClient.state());
  }
}

// ============================================================
// Scan visible networks, pick saved entry with strongest RSSI, skipping triedMask.
// Returns index into wifiList, or -1 if none found.
// Kick async scan. Results readable via pickBestFromLastScan() once complete.
void kickBackgroundScan(bool showOled = true) {
  int st = WiFi.scanComplete();
  if (st == WIFI_SCAN_RUNNING) return;  // already scanning
  WiFi.scanDelete();
  if (showOled) oledShow("Scanning WiFi", "Finding best AP...", "", 10000);
  Serial.println("Scanning (async)...");
  WiFi.scanNetworks(true, false);  // async, no hidden
}

// Pick best saved network from last completed scan. Returns wifiList index or -1.
// Returns -2 if scan still running.
int pickBestFromLastScan(uint8_t skipMask = 0) {
  const int found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) return -2;
  if (found <= 0) { WiFi.scanDelete(); return -1; }

  int bestIdx = -1, bestRssi = -999;
  for (int s = 0; s < found; s++) {
    for (uint8_t w = 0; w < wifiCount; w++) {
      if (skipMask & (1 << w)) continue;
      if (wifiList[w].ssid == WiFi.SSID(s) && WiFi.RSSI(s) > bestRssi) {
        bestRssi = WiFi.RSSI(s); bestIdx = w;
      }
    }
  }
  WiFi.scanDelete();
  if (bestIdx >= 0)
    Serial.printf("Best: [%d] %s (%d dBm)\n", bestIdx, wifiList[bestIdx].ssid.c_str(), bestRssi);
  else
    Serial.printf("No untried network visible (tried: 0x%02X)\n", skipMask);
  return bestIdx;
}

// ============================================================
// Shared HTML helpers
// ============================================================
static const char kSharedStyle[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<style>"
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{font-family:system-ui,sans-serif;background:#f1f5f9;min-height:100vh;padding:16px}"
  ".wrap{max-width:480px;margin:0 auto}"
  "h1{font-size:18px;font-weight:700;color:#0f172a;margin-bottom:16px}"
  ".card{background:#fff;border-radius:12px;padding:16px;margin-bottom:12px;"
    "box-shadow:0 1px 3px rgba(0,0,0,.08)}"
  ".card-title{font-size:11px;font-weight:600;color:#94a3b8;text-transform:uppercase;"
    "letter-spacing:.05em;margin-bottom:10px}"
  ".row{display:flex;justify-content:space-between;align-items:center;padding:4px 0}"
  ".label{font-size:13px;color:#64748b}"
  ".val{font-size:13px;font-weight:600;color:#0f172a;text-align:right;word-break:break-all}"
  ".badge{display:inline-flex;align-items:center;gap:5px;font-size:12px;font-weight:600;"
    "padding:3px 10px;border-radius:99px}"
  ".up{background:#dcfce7;color:#15803d}"
  ".down{background:#fee2e2;color:#b91c1c}"
  ".connecting{background:#fef9c3;color:#92400e}"
  ".dot{width:7px;height:7px;border-radius:50%;background:currentColor}"
  ".sep{height:1px;background:#f1f5f9;margin:6px 0}"
  ".nav{display:flex;gap:8px;margin-bottom:16px;flex-wrap:wrap}"
  ".nav a{font-size:13px;color:#0f766e;text-decoration:none;padding:6px 12px;"
    "border-radius:8px;background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.06)}"
  ".nav a:hover{background:#f0fdf4}"
  "label{display:block;font-size:13px;color:#64748b;margin:10px 0 4px}"
  "input[type=text],input[type=password]{"
    "width:100%;padding:9px 12px;border:1px solid #e2e8f0;border-radius:8px;"
    "font-size:14px;color:#0f172a;outline:none}"
  "input:focus{border-color:#0f766e;box-shadow:0 0 0 2px rgba(15,118,110,.15)}"
  ".btn{display:inline-block;background:#0f766e;color:#fff;border:0;border-radius:8px;"
    "padding:9px 16px;font-size:14px;font-weight:600;cursor:pointer;margin:4px 4px 0 0}"
  ".btn-sm{padding:5px 12px;font-size:12px}"
  ".btn-danger{background:#b91c1c}"
  ".btn-ghost{background:#e2e8f0;color:#0f172a}"
  ".net-item{display:flex;justify-content:space-between;align-items:center;"
    "padding:10px 0;border-top:1px solid #f1f5f9}"
  ".net-ssid{font-size:13px;font-weight:600;color:#0f172a}"
  ".net-tag{font-size:11px;color:#94a3b8;margin-top:2px}"
  "#msg{margin-top:10px;font-size:13px;min-height:18px}"
  ".ok{color:#15803d}.err{color:#b91c1c}"
  "</style>";

static const char kNavLinks[] PROGMEM =
  "<div class=nav>"
  "<a href=/>&#9881; Setup</a>"
  "<a href=/network>&#127760; Network</a>"
  "<a href=/mqtt>&#128236; MQTT</a>"
  "</div>";

static const char kScanScript[] PROGMEM =
  "function eh(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));}"
  "async function scanWifi(targetSsidId){"
    "const msg=document.getElementById('msg');"
    "msg.className='err';msg.textContent='Scanning...';"
    "document.getElementById('scanList').innerHTML='';"
    "await fetch('/api/scan',{method:'POST'});"
    "pollScan(20,targetSsidId);"
  "}"
  "async function pollScan(n,tid){"
    "const msg=document.getElementById('msg');"
    "if(n<=0){msg.textContent='Scan timeout';return;}"
    "const r=await fetch('/api/scan/result').then(x=>x.json());"
    "if(r.status==='scanning'){setTimeout(()=>pollScan(n-1,tid),1500);return;}"
    "msg.className='ok';msg.textContent='Ditemukan '+r.networks.length+' jaringan.';"
    "document.getElementById('scanList').innerHTML=r.networks.map(n=>"
      "'<div class=net-item>"
        "<div><div class=net-ssid>'+eh(n.ssid||'(hidden)')+'</div>"
        "<div class=net-tag>'+eh(n.security)+' &bull; ch'+n.ch+' &bull; '+n.rssi+' dBm</div></div>"
        "<button class=\"btn btn-sm btn-ghost\" type=button data-s=\"'+eh(n.ssid||'')+'\">Pilih</button>"
      "</div>'"
    ").join('');"
    "document.querySelectorAll('[data-s]').forEach(b=>b.onclick=()=>{"
      "document.getElementById(tid).value=b.dataset.s;"
      "document.getElementById('pass').focus();"
    "});"
  "}";

// ============================================================
// Web handlers
// ============================================================
void handleRoot() {
  // Hardware info
  uint32_t sec = millis() / 1000;
  uint32_t mn = sec / 60; sec %= 60;
  uint32_t hr = mn / 60; mn %= 60;
  char uptime[16]; snprintf(uptime, sizeof(uptime), "%02luh%02lum%02lus", hr, mn, sec);
  char mac[18]; uint8_t m[6]; WiFi.macAddress(m);
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", m[0],m[1],m[2],m[3],m[4],m[5]);

  String html = FPSTR(kSharedStyle);
  html += F("<title>SEMS Setup</title></head><body><div class=wrap>"
    "<h1>SEMS AIoT &mdash; Setup</h1>");
  html += FPSTR(kNavLinks);

  // Hardware info card
  html += F("<div class=card><div class=card-title>Hardware</div>");
  html += "<div class=row><span class=label>Chip</span><span class=val>ESP32 " + String(ESP.getChipModel()) + " rev" + String(ESP.getChipRevision()) + "</span></div>";
  html += "<div class=sep></div>";
  html += "<div class=row><span class=label>Flash</span><span class=val>" + String(ESP.getFlashChipSize()/1024) + " kB</span></div>";
  html += "<div class=row><span class=label>Free Heap</span><span class=val>" + String(ESP.getFreeHeap()/1024) + " kB</span></div>";
  html += "<div class=sep></div>";
  html += "<div class=row><span class=label>MAC Address</span><span class=val style='font-family:monospace;font-size:12px'>" + String(mac) + "</span></div>";
  html += "<div class=row><span class=label>Firmware</span><span class=val>v" FW_VERSION "</span></div>";
  html += "<div class=row><span class=label>Uptime</span><span class=val>" + String(uptime) + "</span></div>";
  html += F("</div>");

  // Saved WiFi card
  html += F("<div class=card><div class=card-title>Saved WiFi Networks</div>");
  if (wifiCount == 0) {
    html += F("<p style='font-size:13px;color:#94a3b8'>Belum ada jaringan tersimpan.</p>");
  } else {
    for (uint8_t i = 0; i < wifiCount; i++) {
      String s = wifiList[i].ssid;
      s.replace("&","&amp;"); s.replace("<","&lt;"); s.replace(">","&gt;");
      bool active = staConnected && wifiList[i].ssid == savedSsid;
      html += "<div class=net-item><div><div class=net-ssid>" + s;
      if (active) html += " <span class=ok>&#10003; " + WiFi.localIP().toString() + "</span>";
      html += "</div></div>";
      html += "<button class='btn btn-sm btn-danger' type=button data-d=\"" + s + "\">&#10005;</button></div>";
    }
  }
  html += F("</div>");

  // Quick links card
  html += F("<div class=card><div class=card-title>Konfigurasi</div>"
    "<a class='btn btn-sm' href=/network style='text-decoration:none'>&#127760; Network &amp; WiFi</a> "
    "<a class='btn btn-sm' href=/mqtt style='text-decoration:none'>&#128236; MQTT</a>"
    "</div>");

  html += F("<script>"
    "document.querySelectorAll('[data-d]').forEach(b=>b.onclick=async()=>{"
      "await fetch('/api/wifi/delete',{method:'POST',"
        "headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:b.dataset.d})});"
      "location.reload();"
    "});"
    "</script></div></body></html>");
  server.send(200, "text/html", html);
}

void handleScanStart() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
  server.send(202, "application/json", "{\"ok\":true}");
}

void handleScanResult() {
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  String body = "{\"status\":\"done\",\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i) body += ',';
    String s = WiFi.SSID(i);
    s.replace("\\", "\\\\"); s.replace("\"", "\\\"");
    body += "{\"ssid\":\""; body += s;
    body += "\",\"rssi\":"; body += WiFi.RSSI(i);
    body += ",\"ch\":";     body += WiFi.channel(i);
    body += ",\"security\":\"";
    body += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured";
    body += "\"}";
  }
  body += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", body);
}

void handleWifiSave() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}"); return;
  }
  String body = server.arg("plain");
  auto extract = [&](const char* key) -> String {
    String k = String("\"") + key + "\":\"";
    int s = body.indexOf(k); if (s < 0) return "";
    s += k.length();
    int e = body.indexOf('"', s); return e < 0 ? "" : body.substring(s, e);
  };
  String ssid = extract("ssid");
  String pass = extract("pass");
  if (ssid.isEmpty()) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}"); return;
  }
  if (!addOrUpdateWifi(ssid, pass)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"list_full\"}"); return;
  }
  Serial.printf("WiFi saved: %s (total: %d)\n", ssid.c_str(), wifiCount);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWifiDelete() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}"); return;
  }
  String body = server.arg("plain");
  String k = String("\"ssid\":\"");
  int s = body.indexOf(k); if (s < 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}"); return; }
  s += k.length();
  int e = body.indexOf('"', s);
  String ssid = e < 0 ? "" : body.substring(s, e);
  removeWifi(ssid);
  Serial.printf("WiFi removed: %s (total: %d)\n", ssid.c_str(), wifiCount);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWifiClear() {
  clearAllWifi();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWifiList() {
  String body = "{\"ok\":true,\"count\":";
  body += wifiCount;
  body += ",\"networks\":[";
  for (uint8_t i = 0; i < wifiCount; i++) {
    if (i) body += ',';
    String s = wifiList[i].ssid; s.replace("\\","\\\\"); s.replace("\"","\\\"");
    body += "{\"ssid\":\""; body += s; body += "\"}";
  }
  body += "]}";
  server.send(200, "application/json", body);
}

void handleReboot() {
  server.send(200, "application/json", "{\"ok\":true}");
  delay(300);
  ESP.restart();
}

void handleMqttConfigGet() {
  String body = "{\"ok\":true";
  body += ",\"enabled\":";   body += mqttCfg.enabled ? "true" : "false";
  body += ",\"host\":\"";    body += mqttCfg.host; body += "\"";
  body += ",\"port\":";      body += mqttCfg.port;
  body += ",\"user\":\"";    body += mqttCfg.user; body += "\"";
  body += ",\"topic\":\"";   body += mqttCfg.topicPrefix; body += "\"";
  body += ",\"connected\":"; body += mqttConnected ? "true" : "false";
  body += ",\"state\":";     body += mqttClient.state();
  body += "}";
  server.send(200, "application/json", body);
}

void handleMqttConfigSave() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}"); return;
  }
  String body = server.arg("plain");
  auto get = [&](const char* key) -> String {
    String k = String("\"") + key + "\":\"";
    int s = body.indexOf(k); if (s < 0) return "";
    s += k.length();
    int e = body.indexOf('"', s); return e < 0 ? "" : body.substring(s, e);
  };
  auto getBool = [&](const char* key) -> bool {
    String k = String("\"") + key + "\":";
    int s = body.indexOf(k); if (s < 0) return false;
    s += k.length();
    return body.substring(s, s + 4) == "true";
  };
  auto getInt = [&](const char* key, int def) -> int {
    String k = String("\"") + key + "\":";
    int s = body.indexOf(k); if (s < 0) return def;
    s += k.length();
    return body.substring(s).toInt();
  };

  mqttCfg.enabled     = getBool("enabled");
  mqttCfg.host        = get("host");
  mqttCfg.port        = (uint16_t)getInt("port", 1883);
  mqttCfg.user        = get("user");
  String newPass      = get("pass");
  if (!newPass.isEmpty()) mqttCfg.pass = newPass;  // blank = keep existing
  mqttCfg.topicPrefix = get("topic");
  if (mqttCfg.topicPrefix.isEmpty()) mqttCfg.topicPrefix = "sems";

  saveMqttConfig();
  // Force reconnect
  mqttClient.disconnect();
  mqttConnected = false;
  mqttLastTryMs = 0;

  Serial.printf("MQTT config saved: %s:%d en=%d\n", mqttCfg.host.c_str(), mqttCfg.port, mqttCfg.enabled);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleMqttPage() {
  String html = FPSTR(kSharedStyle);
  html += F("<title>SEMS MQTT</title>"
    "<style>"
    "input[type=number]{width:100%;padding:9px 12px;border:1px solid #e2e8f0;"
      "border-radius:8px;font-size:14px;color:#0f172a;outline:none}"
    ".toggle{display:flex;align-items:center;gap:10px;margin-bottom:8px}"
    ".toggle input{width:auto;margin:0}"
    ".toggle span{font-size:14px;color:#0f172a;font-weight:500}"
    "</style>"
    "</head><body><div class=wrap>"
    "<h1>SEMS AIoT &mdash; MQTT</h1>");
  html += FPSTR(kNavLinks);
  html += F("<div class=card>"
    "<div class=card-title>Status</div>"
    "<div id=status>Loading...</div>"
    "</div>"
    "<div class=card>"
    "<div class=card-title>Konfigurasi Broker</div>"
    "<div class=toggle><input type=checkbox id=enabled><span>MQTT Aktif</span></div>"
    "<label>Host / IP Broker</label>"
    "<input type=text id=host placeholder='128.199.x.x'>"
    "<label>Port</label>"
    "<input type=number id=port value=1883 min=1 max=65535>"
    "<label>Username</label>"
    "<input type=text id=user placeholder='user_1'>"
    "<label>Password</label>"
    "<input type=password id=pass placeholder='kosong = tidak berubah'>"
    "<label>Topic Prefix</label>"
    "<input type=text id=topic placeholder='sems'>"
    "<button class=btn style='width:100%;margin-top:14px' onclick=save()>Simpan</button>"
    "<div id=msg style='margin-top:10px;font-size:13px'></div>"
    "</div>"
    "<script>"
    "async function load(){"
      "const d=await fetch('/api/mqtt').then(r=>r.json());"
      "document.getElementById('enabled').checked=d.enabled;"
      "document.getElementById('host').value=d.host||'';"
      "document.getElementById('port').value=d.port||1883;"
      "document.getElementById('user').value=d.user||'';"
      "document.getElementById('topic').value=d.topic||'sems';"
      "const ok=d.connected;"
      "document.getElementById('status').innerHTML="
        "'<span class=\"badge '+(ok?'up':'down')+'\">"
          "<span class=dot></span>'+(ok?'Connected':'Disconnected')+'</span> '+"
        "'<span style=\"font-size:12px;color:#64748b;margin-left:8px\">'+"
          "(d.host?d.host+':'+d.port:'Belum dikonfigurasi')+'</span>';"
    "}"
    "async function save(){"
      "const msg=document.getElementById('msg');"
      "msg.className='';msg.textContent='Menyimpan...';"
      "const b={enabled:document.getElementById('enabled').checked,"
        "host:document.getElementById('host').value,"
        "port:parseInt(document.getElementById('port').value)||1883,"
        "user:document.getElementById('user').value,"
        "pass:document.getElementById('pass').value,"
        "topic:document.getElementById('topic').value||'sems'};"
      "const r=await fetch('/api/mqtt/save',{method:'POST',"
        "headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(x=>x.json());"
      "if(r.ok){msg.className='ok';msg.textContent='Tersimpan! Reconnecting...';setTimeout(load,2000);}"
      "else{msg.className='err';msg.textContent='Error: '+(r.error||'unknown');}"
    "}"
    "load();setInterval(load,5000);"
    "</script></div></body></html>");
  server.send(200, "text/html", html);
}

void handleNetworkApi() {
  uint32_t s = millis() / 1000;
  uint32_t m = s / 60; s %= 60;
  uint32_t h = m / 60; m %= 60;
  char uptime[16]; snprintf(uptime, sizeof(uptime), "%02luh%02lum%02lus", h, m, s);

  String body = "{";
  // WiFi STA
  body += "\"wifi\":{";
  body += "\"connected\":"; body += staConnected ? "true" : "false";
  body += ",\"connecting\":"; body += staConnecting ? "true" : "false";
  if (staConnected) {
    String ip = WiFi.localIP().toString();
    String ssid = savedSsid; ssid.replace("\\","\\\\"); ssid.replace("\"","\\\"");
    body += ",\"ip\":\""; body += ip; body += "\"";
    body += ",\"ssid\":\""; body += ssid; body += "\"";
    body += ",\"rssi\":"; body += WiFi.RSSI();
  } else if (staConnecting) {
    String ssid = savedSsid; ssid.replace("\\","\\\\"); ssid.replace("\"","\\\"");
    body += ",\"ssid\":\""; body += ssid; body += "\"";
  }
  body += "}";
  // AP
  body += ",\"ap\":{";
  body += "\"active\":"; body += apStarted ? "true" : "false";
  if (apStarted) {
    body += ",\"ssid\":\""; body += WiFi.softAPSSID(); body += "\"";
    body += ",\"ip\":\"192.168.4.1\"";
    body += ",\"clients\":"; body += WiFi.softAPgetStationNum();
  }
  body += "}";
  // LAN
  body += ",\"lan\":{";
  body += "\"ready\":"; body += ethReady ? "true" : "false";
  body += ",\"link\":"; body += (Ethernet.linkStatus() == LinkON) ? "true" : "false";
  if (ethReady) {
    body += ",\"ip\":\""; body += Ethernet.localIP().toString(); body += "\"";
  }
  body += "}";
  // Device
  body += ",\"device\":{";
  body += "\"uptime\":\""; body += uptime; body += "\"";
  body += ",\"heap\":"; body += ESP.getFreeHeap();
  body += ",\"fw\":\"" FW_VERSION "\"";
  body += "}";
  body += "}";
  server.send(200, "application/json", body);
}

void handleNetworkPage() {
  String html = FPSTR(kSharedStyle);
  html += F("<title>SEMS Network</title>"
    "<style>.refresh{font-size:11px;color:#94a3b8;text-align:right;margin-top:4px}</style>"
    "</head><body><div class=wrap>"
    "<h1>SEMS AIoT &mdash; Network</h1>");
  html += FPSTR(kNavLinks);
  html += F("<div id=root><p style='color:#94a3b8;font-size:13px'>Loading...</p></div>"
    "<div class=refresh id=ts></div>"
    // Scan section
    "<div class=card style='margin-top:12px'>"
    "<div class=card-title>Scan WiFi</div>"
    "<button class='btn btn-sm' type=button onclick=\"scanWifi('ssid')\">&#128268; Scan Sekarang</button>"
    "<div id=msg style='margin-top:8px;font-size:13px'></div>"
    "<div id=scanList></div>"
    "<div id=addForm style='display:none'>"
    "<label>SSID</label><input type=text id=ssid readonly>"
    "<label>Password</label><input type=password id=pass placeholder='Password'>"
    "<button class=btn style='width:100%;margin-top:10px' onclick=saveWifi()>Simpan &amp; Reboot</button>"
    "</div>"
    "</div>"
    "<script>"
    "function badge(ok,yes,no,mid){"
      "const s=ok===null?'connecting':ok?'up':'down';"
      "const t=ok===null?mid:ok?yes:no;"
      "return'<span class=\"badge '+s+'\"><span class=dot></span>'+t+'</span>';"
    "}"
    "function row(l,v){return'<div class=row><span class=label>'+l+'</span><span class=val>'+v+'</span></div>';}"
    "function sep(){return'<div class=sep></div>';}"
    "async function load(){"
      "try{"
        "const d=await fetch('/api/network').then(r=>r.json());"
        "const w=d.wifi,a=d.ap,l=d.lan,dv=d.device;"
        "let h='';"
        "h+='<div class=card><div class=card-title>WiFi STA</div>';"
        "const wok=w.connected?true:w.connecting?null:false;"
        "h+=row('Status',badge(wok,'Connected','Disconnected','Connecting...'));"
        "if(w.ssid)h+=row('SSID','<code>'+w.ssid+'</code>');"
        "if(w.ip)h+=row('IP Address',w.ip);"
        "if(w.rssi!=null)h+=row('Signal',w.rssi+' dBm');"
        "h+='</div>';"
        "h+='<div class=card><div class=card-title>Access Point</div>';"
        "h+=row('Status',badge(a.active,'Active','Off'));"
        "if(a.ssid){h+=sep();h+=row('SSID','<code>'+a.ssid+'</code>');}"
        "if(a.ip)h+=row('IP',a.ip);"
        "if(a.active)h+=row('Clients',a.clients);"
        "h+='</div>';"
        "h+='<div class=card><div class=card-title>LAN &mdash; W5500</div>';"
        "h+=row('Status',badge(l.ready,'Connected','No DHCP / No cable'));"
        "h+=row('Link',badge(l.link,'Up','Down'));"
        "if(l.ip)h+=row('IP Address',l.ip);"
        "h+=row('Role','MQTT transport');"
        "h+='</div>';"
        "h+='<div class=card><div class=card-title>Device</div>';"
        "h+=row('Firmware','v'+dv.fw);"
        "h+=row('Uptime',dv.uptime);"
        "h+=row('Free Heap',Math.round(dv.heap/1024)+' kB');"
        "h+='</div>';"
        "document.getElementById('root').innerHTML=h;"
        "document.getElementById('ts').textContent='Updated '+new Date().toLocaleTimeString();"
      "}catch(e){document.getElementById('root').innerHTML='<p style=color:#b91c1c>Error: '+e+'</p>';}"
    "}"
    "load();setInterval(load,5000);");
  html += FPSTR(kScanScript);
  html += F(
    // Override data-s handler to show addForm
    "const _orig=pollScan;"
    "const _scanWifi=scanWifi;"
    "document.querySelectorAll&&(window.addEventListener('click',e=>{"
      "if(e.target.dataset.s!==undefined){"
        "document.getElementById('ssid').value=e.target.dataset.s;"
        "document.getElementById('pass').value='';"
        "document.getElementById('addForm').style.display='block';"
        "document.getElementById('pass').focus();"
      "}"
    "}));"
    "async function saveWifi(){"
      "const msg=document.getElementById('msg');"
      "msg.className='err';msg.textContent='Menyimpan...';"
      "const b=JSON.stringify({ssid:document.getElementById('ssid').value,"
        "pass:document.getElementById('pass').value});"
      "const r=await fetch('/api/wifi/save',{method:'POST',"
        "headers:{'Content-Type':'application/json'},body:b}).then(x=>x.json());"
      "if(r.ok){msg.className='ok';msg.textContent='Tersimpan! Rebooting...';"
        "setTimeout(()=>fetch('/api/reboot',{method:'POST'}),800);}"
      "else{msg.className='err';msg.textContent='Error: '+(r.error||'unknown');}"
    "}"
    "</script></div></body></html>");
  server.send(200, "text/html", html);
}

// ============================================================
// AP
// ============================================================
String getApSsid() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char suffix[7]; snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(kApSsidPrefix) + "-" + suffix;
}

void startAp() {
  const String apSsid = getApSsid();
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(apSsid.c_str(), kApPassword);
  apStarted = true;
  Serial.printf("AP: %s  %s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
  oledShow("AP Ready", apSsid.c_str(), "192.168.4.1", 6000);
}

void startWebServer() {
  server.on("/",                  HTTP_GET,  handleRoot);
  server.on("/network",           HTTP_GET,  handleNetworkPage);
  server.on("/mqtt",              HTTP_GET,  handleMqttPage);
  server.on("/api/network",       HTTP_GET,  handleNetworkApi);
  server.on("/api/mqtt",          HTTP_GET,  handleMqttConfigGet);
  server.on("/api/mqtt/save",     HTTP_POST, handleMqttConfigSave);
  server.on("/api/scan",          HTTP_POST, handleScanStart);
  server.on("/api/scan/result",   HTTP_GET,  handleScanResult);
  server.on("/api/wifi/save",     HTTP_POST, handleWifiSave);
  server.on("/api/wifi/delete",   HTTP_POST, handleWifiDelete);
  server.on("/api/wifi/clear",    HTTP_POST, handleWifiClear);
  server.on("/api/wifi/list",     HTTP_GET,  handleWifiList);
  server.on("/api/reboot",        HTTP_POST, handleReboot);
  server.begin();
  if (MDNS.begin("sems")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://sems.local");
  } else {
    Serial.println("mDNS failed");
  }
  Serial.println("WebServer started");
}

// ============================================================
// WiFi STA lifecycle
// ============================================================
// Try connecting to best visible network not yet tried this round.
// Returns false if all saved networks have been tried.
// Connect to specific wifiList entry — never blocks.
void connectToEntry(uint8_t idx) {
  scanRetryCount = 0;
  triedMask |= (1 << idx);
  savedSsid = wifiList[idx].ssid;
  char attempt[24];
  snprintf(attempt, sizeof(attempt), "(%d/%d)", __builtin_popcount(triedMask), wifiCount);
  Serial.printf("STA connecting to: %s %s\n", savedSsid.c_str(), attempt);
  oledShow("WiFi Connecting", savedSsid.c_str(), attempt, 15000);
  WiFi.disconnect(false);
  WiFi.begin(wifiList[idx].ssid.c_str(), wifiList[idx].pass.c_str());
  staConnecting = true;
  staStartMs = millis();
  scanPending = false;
}

void scheduleReboot() {
  if (rebootAtMs != 0) return;
  rtcWifiFailMagic = kWifiFailMagic;
  rebootAtMs = millis() + 60000;
  Serial.println("No WiFi — rebooting in 60s");
}

// Kick async scan then call handleScanResult in loop to pick & connect.
void beginStaConnect() {
  triedMask = 0;
  scanRetryCount = 0;
  rebootAtMs = 0;
  scanPending = false;
  WiFi.disconnect(false);
  kickBackgroundScan(true);
  scanPending = true;
}

// Called from loop — reads completed async scan and connects or retries.
void processScanResult(uint32_t now) {
  if (!scanPending) return;
  const int best = pickBestFromLastScan(triedMask);
  if (best == -2) return;  // still scanning — check next loop
  scanPending = false;
  if (best >= 0) {
    connectToEntry((uint8_t)best);
    return;
  }
  // Nothing found
  if (scanRetryCount < kMaxScanRetry) {
    scanRetryCount++;
    Serial.printf("Scan empty, retry %d/%d\n", scanRetryCount, kMaxScanRetry);
    oledShow("Scan empty", "Retrying...", "", 5000);
    kickBackgroundScan(false);
    scanPending = true;
  } else {
    scheduleReboot();
  }
}

void restoreAp() {
  if (apStarted) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  const String apSsid = getApSsid();
  WiFi.softAP(apSsid.c_str(), kApPassword);
  apStarted = true;
  Serial.println("AP restored");
}

void handleStaLifecycle(uint32_t now) {
  if (staConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      staConnecting = false;
      staConnected  = true;
      Serial.printf("STA connected! IP: %s\n", WiFi.localIP().toString().c_str());
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      apStarted = false;
      Serial.println("AP stopped");
      oledShow("WiFi Connected", WiFi.localIP().toString().c_str(), savedSsid.c_str(), 6000);
      return;
    }
    if (now - staStartMs >= kStaTimeoutMs) {
      staConnecting = false;
      Serial.printf("STA timeout: %s\n", savedSsid.c_str());
      // Scan for next untried network
      kickBackgroundScan(false);
      scanPending = true;
    }
    return;
  }

  // Monitor STA drop — restore AP and start fresh round
  if (staConnected && WiFi.status() != WL_CONNECTED) {
    staConnected = false;
    Serial.println("STA dropped — restoring AP, trying all networks");
    restoreAp();
    oledShow("WiFi Lost", "Reconnecting...", "", 3000);
    beginStaConnect();
  }
}

// ============================================================
// W5500 Ethernet
// ============================================================
void initEthernet() {
  // Hardware reset W5500
  pinMode(kEthRst, OUTPUT);
  digitalWrite(kEthRst, LOW);
  delay(10);
  digitalWrite(kEthRst, HIGH);
  delay(200);

  Ethernet.init(kEthCs);
  oledShow("LAN", "Requesting DHCP...", "", 8000);
  Serial.println("W5500: DHCP...");

  if (Ethernet.begin(ethMac, 5000) == 0) {
    Serial.println("W5500: DHCP failed");
    // Try link detection to distinguish no-cable vs no-DHCP
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("W5500: no cable");
      oledShow("LAN", "No cable", "", 3000);
    } else {
      Serial.println("W5500: no DHCP response");
      oledShow("LAN", "DHCP timeout", "", 3000);
    }
    ethReady = false;
    return;
  }

  ethReady = true;
  Serial.printf("W5500 IP: %s\n", Ethernet.localIP().toString().c_str());
  oledShow("LAN Ready", Ethernet.localIP().toString().c_str(), "", 5000);
}

// Call periodically to renew DHCP lease
void maintainEthernet() {
  if (!ethReady) return;
  Ethernet.maintain();
}

// ============================================================
// Arduino
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(kLedPin, OUTPUT);

  Wire.begin(21, 22);
  if (oled.begin()) {
    oledReady = true;
    Serial.println("OLED ready");
  } else {
    Serial.println("OLED init failed");
  }

  Serial.println("\n=== SEMS AIoT " FW_VERSION " ===");
  oledShow("SEMS AIoT", "FW: " FW_VERSION, "Booting...", 2000);

  initEthernet();

  loadWifiList();
  loadMqttConfig();

  const bool wifiFailReboot = (rtcWifiFailMagic == kWifiFailMagic);
  rtcWifiFailMagic = 0;  // clear — next reboot is treated as cold boot unless set again

  if (wifiCount == 0 || wifiFailReboot) {
    // No credentials, or previous boot failed all networks — start AP for config access
    WiFi.mode(WIFI_AP_STA);
    startAp();
    startWebServer();
    if (wifiCount == 0) {
      oledShow("No saved WiFi", "Open 192.168.4.1", "to configure", 8000);
    } else {
      // Still try STA in background even with AP on
      beginStaConnect();
    }
  } else {
    // Cold boot with credentials — STA only, no AP until needed
    WiFi.mode(WIFI_STA);
    startWebServer();  // server ready but only reachable after STA connects
    beginStaConnect();
  }
}

static uint32_t lastBlinkMs = 0;
static bool ledState = false;

void handleRebootCountdown(uint32_t now) {
  if (rebootAtMs == 0) return;
  const int32_t secsLeft = (int32_t)(rebootAtMs - now) / 1000;
  if (secsLeft <= 0) {
    Serial.println("Countdown done — rebooting");
    ESP.restart();
  }

  // Every 10s during countdown, kick async scan to check if WiFi appeared.
  static uint32_t lastRetryMs = 0;
  if (now - lastRetryMs >= 10000) {
    lastRetryMs = now;
    if (!scanPending) {
      Serial.println("Countdown: kicking scan...");
      kickBackgroundScan(false);
      scanPending = true;
    }
  }
  // If scan completed and found something, cancel reboot
  if (scanPending) {
    const int best = pickBestFromLastScan(0);
    if (best == -2) { /* still scanning */ }
    else if (best >= 0) {
      Serial.printf("WiFi appeared: %s — cancelling reboot\n", wifiList[best].ssid.c_str());
      rebootAtMs = 0;
      rtcWifiFailMagic = 0;
      lastRetryMs = 0;
      scanPending = false;
      beginStaConnect();
      return;
    } else {
      scanPending = false;  // scan done, nothing found
    }
  }

  // Update OLED every second with countdown
  static int32_t lastSec = -1;
  if (secsLeft != lastSec) {
    lastSec = secsLeft;
    char buf[16]; snprintf(buf, sizeof(buf), "Reboot in %ds", secsLeft);
    oledShow("No WiFi Found", buf, "AP: 192.168.4.1", 1100);
    Serial.printf("Reboot in %ds\n", secsLeft);
  }
}

void loop() {
  const uint32_t now = millis();
  server.handleClient();
  handleStaLifecycle(now);
  handleRebootCountdown(now);
  maintainEthernet();
  handleMqttLifecycle(now);
  processScanResult(now);
  if (now - lastBlinkMs >= 500) { lastBlinkMs = now; ledState = !ledState; digitalWrite(kLedPin, ledState); }
  updateOled(now);
}
