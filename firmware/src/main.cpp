#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <Ethernet_Generic.h>

// --- pins ---
static constexpr uint8_t kLedPin   = 2;
static constexpr uint8_t kEthCs    = 5;
static constexpr uint8_t kEthInt   = 27;
static constexpr uint8_t kEthRst   = 26;

// --- W5500 state ---
static bool ethReady = false;
static byte ethMac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

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
static constexpr uint32_t kPageIntervalMs = 4000;

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
static uint8_t scanRetryCount = 0;  // how many times scan came back empty this round
static constexpr uint8_t kMaxScanRetry = 3;

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

  // Ethernet plug icon: open_iconic_www_2x_t \x47 = cloud/globe, use embedded \x50 = plug
  oled.setFont(u8g2_font_open_iconic_embedded_2x_t);
  oled.drawGlyph(0, 36, 0x50);  // plug icon

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

void updateOled(uint32_t now) {
  if (!oledReady) return;
  if (now < oledUntilMs) return;
  if (now - oledPageMs < kPageIntervalMs) return;
  oledPageMs = now;
  oledPage = (oledPage + 1) % 2;
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

// Scan visible networks, pick saved entry with strongest RSSI, skipping triedMask.
// Returns index into wifiList, or -1 if none found.
int pickBestWifi(uint8_t skipMask = 0, bool silent = false) {
  if (!silent) oledShow("Scanning WiFi", "Finding best AP...", "", 10000);
  Serial.println("Scanning for best saved WiFi...");
  delay(500);  // let radio settle after mode switch
  const int found = WiFi.scanNetworks(false, false);
  if (found <= 0) { Serial.println("Scan: nothing found"); WiFi.scanDelete(); return -1; }

  int bestIdx = -1;
  int bestRssi = -999;
  for (int s = 0; s < found; s++) {
    const String scannedSsid = WiFi.SSID(s);
    const int rssi = WiFi.RSSI(s);
    for (uint8_t w = 0; w < wifiCount; w++) {
      if (skipMask & (1 << w)) continue;  // already tried
      if (wifiList[w].ssid == scannedSsid && rssi > bestRssi) {
        bestRssi = rssi;
        bestIdx  = w;
      }
    }
  }
  WiFi.scanDelete();
  if (bestIdx >= 0)
    Serial.printf("Best: [%d] %s (%d dBm)\n", bestIdx, wifiList[bestIdx].ssid.c_str(), bestRssi);
  else
    Serial.printf("No untried saved network visible (tried: 0x%02X)\n", skipMask);
  return bestIdx;
}

// ============================================================
// Web handlers
// ============================================================
void handleRoot() {
  // Build saved WiFi list HTML
  String savedInfo;
  if (wifiCount == 0) {
    savedInfo = "<p style='color:#666'>No saved WiFi.</p>";
  } else {
    savedInfo = "<div id=savedList>";
    for (uint8_t i = 0; i < wifiCount; i++) {
      String s = wifiList[i].ssid;
      // basic HTML escape
      s.replace("&","&amp;"); s.replace("<","&lt;"); s.replace(">","&gt;");
      String connected = (staConnected && wifiList[i].ssid == savedSsid)
        ? " <span style='color:#0f766e'>&#10003; " + WiFi.localIP().toString() + "</span>" : "";
      savedInfo += "<div class=net><div><b>" + s + "</b>" + connected + "</div>"
        "<button type=button class=danger data-d=\"" + s + "\">&#10005;</button></div>";
    }
    savedInfo += "</div>";
  }

  // LAN status card
  String lanCard = "<div class=card>";
  if (ethReady) {
    lanCard += "<b>LAN</b> <span class=ok>&#9679; Connected</span><br>"
               "<span class=tag>IP: " + Ethernet.localIP().toString() + " &nbsp; W5500</span>";
  } else {
    String linkStr = Ethernet.linkStatus() == LinkON ? "Cable OK, no DHCP" : "No cable";
    lanCard += "<b>LAN</b> <span class=err>&#9679; " + linkStr + "</span><br>"
               "<span class=tag>W5500 — MQTT transport</span>";
  }
  lanCard += "</div>";

  String html;
  html +=
    F("<!doctype html><html><head><meta charset=utf-8>"
      "<meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>SEMS Setup</title>"
      "<style>"
      "body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 16px}"
      "h2{color:#0f766e}"
      "input{width:100%;box-sizing:border-box;padding:10px;margin:6px 0 14px;"
        "border:1px solid #ccc;border-radius:6px;font-size:15px}"
      "button{background:#0f766e;color:#fff;border:0;border-radius:6px;"
        "padding:10px 16px;font-size:15px;cursor:pointer;margin:4px 2px}"
      ".danger{background:#b91c1c}"
      ".net{border-top:1px solid #eee;padding:10px 0;"
        "display:flex;justify-content:space-between;align-items:center}"
      ".ssid{font-weight:700}.tag{font-size:12px;color:#666}"
      ".card{background:#f8fafc;border:1px solid #e2e8f0;border-radius:8px;"
        "padding:12px 14px;margin:12px 0}"
      ".ok{color:#0f766e}.err{color:#b91c1c}"
      "#msg{margin:12px 0;color:#0f766e}"
      "</style></head><body>"
      "<h2>SEMS AIoT &mdash; Setup</h2>");
  html += lanCard;
  html += savedInfo;
  html += F("<p id=msg>Ready.</p>"
      "<button onclick=scanWifi()>Scan WiFi</button> "
      "<button class=danger onclick=clearWifi()>Clear Saved</button>"
      "<div id=list></div>"
      "<form id=form>"
        "<label>SSID<input id=ssid name=ssid required></label>"
        "<label>Password<input id=pass name=pass type=password></label>"
        "<button type=submit>Save &amp; Reboot</button>"
      "</form>"
      "<script>"
      "function eh(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));}"
      "const msg=document.getElementById('msg');"
      "async function scanWifi(){"
        "msg.textContent='Scanning...';"
        "document.getElementById('list').innerHTML='';"
        "await fetch('/api/scan',{method:'POST'});"
        "pollScan(20);"
      "}"
      "async function pollScan(n){"
        "if(n<=0){msg.textContent='Scan timeout — coba lagi';return;}"
        "const r=await fetch('/api/scan/result').then(x=>x.json());"
        "if(r.status==='scanning'){setTimeout(()=>pollScan(n-1),1500);return;}"
        "msg.textContent='Found '+r.networks.length+' network(s).';"
        "document.getElementById('list').innerHTML=r.networks.map(n=>"
          "'<div class=net>"
            "<div><div class=ssid>'+eh(n.ssid||'(hidden)')+'</div>"
            "<div class=tag>'+eh(n.security)+' | ch'+n.ch+' | '+n.rssi+' dBm</div></div>"
            "<button type=button data-s=\"'+eh(n.ssid||'')+'\">Use</button>"
          "</div>'"
        ").join('');"
        "document.querySelectorAll('[data-s]').forEach(b=>b.onclick=()=>{"
          "document.getElementById('ssid').value=b.dataset.s;"
          "document.getElementById('pass').focus();"
        "});"
      "}"
      "async function clearWifi(){"
        "if(!confirm('Hapus semua WiFi?'))return;"
        "await fetch('/api/wifi/clear',{method:'POST'});"
        "location.reload();"
      "}"
      "async function deleteWifi(ssid){"
        "await fetch('/api/wifi/delete',{method:'POST',"
          "headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid})});"
        "location.reload();"
      "}"
      "document.querySelectorAll('[data-d]').forEach(b=>b.onclick=()=>deleteWifi(b.dataset.d));"
      "document.getElementById('form').onsubmit=async function(e){"
        "e.preventDefault();"
        "msg.textContent='Saving...';"
        "const b=JSON.stringify({ssid:document.getElementById('ssid').value,"
          "pass:document.getElementById('pass').value});"
        "const r=await fetch('/api/wifi/save',{method:'POST',"
          "headers:{'Content-Type':'application/json'},body:b}).then(x=>x.json());"
        "if(r.ok){msg.textContent='Saved! Rebooting...';setTimeout(()=>fetch('/api/reboot',{method:'POST'}),800);}"
        "else msg.textContent='Error: '+(r.error||'unknown');"
      "};"
      "</script></body></html>");
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
bool tryNextWifi() {
  const int best = pickBestWifi(triedMask);
  if (best < 0) {
    // Scan found nothing untried — could be scan miss or all tried
    if (__builtin_popcount(triedMask) == 0) {
      // Scan empty, no network tried yet — retry scan up to kMaxScanRetry times
      scanRetryCount++;
      Serial.printf("Scan empty, retry %d/%d\n", scanRetryCount, kMaxScanRetry);
      if (scanRetryCount < kMaxScanRetry) {
        // Schedule retry via staStartMs trick: set staConnecting so timeout fires
        // Actually: just return false here, caller (beginStaConnect / timeout handler)
        // will call scheduleReboot — but we override: reschedule beginStaConnect instead
        staConnecting = true;
        staStartMs = millis();  // will timeout in kStaTimeoutMs and call tryNextWifi again
        oledShow("Scan empty", "Retrying...", "", kStaTimeoutMs);
        return true;  // tell caller we're still trying
      }
    }
    return false;
  }
  scanRetryCount = 0;
  triedMask |= (1 << best);
  savedSsid = wifiList[best].ssid;
  char attempt[24];
  snprintf(attempt, sizeof(attempt), "(%d/%d)", __builtin_popcount(triedMask), wifiCount);
  Serial.printf("STA connecting to: %s %s\n", savedSsid.c_str(), attempt);
  oledShow("WiFi Connecting", savedSsid.c_str(), attempt, 15000);
  WiFi.disconnect(false);
  WiFi.begin(wifiList[best].ssid.c_str(), wifiList[best].pass.c_str());
  staConnecting = true;
  staStartMs = millis();
  return true;
}

void scheduleReboot() {
  if (rebootAtMs != 0) return;  // already scheduled
  rtcWifiFailMagic = kWifiFailMagic;  // signal next boot to start AP
  rebootAtMs = millis() + 60000;
  Serial.println("No WiFi — rebooting in 60s (AP will start on next boot)");
}

void beginStaConnect() {
  triedMask = 0;
  scanRetryCount = 0;
  rebootAtMs = 0;  // cancel any pending reboot
  if (!tryNextWifi()) {
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
      // Try next saved network before giving up
      if (!tryNextWifi()) {
        scheduleReboot();
      }
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

  // Every 10s during countdown, check if any saved network is now visible.
  // If found, cancel reboot and reconnect.
  static uint32_t lastRetryMs = 0;
  if (now - lastRetryMs >= 10000) {
    lastRetryMs = now;
    Serial.println("Countdown: checking if any saved WiFi visible...");
    const int best = pickBestWifi(0, true);  // silent scan, fresh round
    if (best >= 0) {
      Serial.printf("WiFi appeared: %s — cancelling reboot\n", wifiList[best].ssid.c_str());
      rebootAtMs = 0;
      rtcWifiFailMagic = 0;
      lastRetryMs = 0;
      beginStaConnect();
      return;
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
  if (now - lastBlinkMs >= 500) { lastBlinkMs = now; ledState = !ledState; digitalWrite(kLedPin, ledState); }
  updateOled(now);
}
