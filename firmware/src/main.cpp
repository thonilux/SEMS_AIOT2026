#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// --- pins ---
static constexpr uint8_t kLedPin = 2;

// --- AP config ---
static constexpr char kApSsidPrefix[]  = "SEMS-SETUP";
static constexpr char kApPassword[]    = "sems1234";
static constexpr char kNvsNamespace[]  = "wifi";
static constexpr uint32_t kStaTimeoutMs = 15000;

// --- OLED ---
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R2, U8X8_PIN_NONE);
static bool oledReady = false;
static uint32_t oledUntilMs = 0;
static uint8_t oledPage = 0;
static uint32_t oledPageMs = 0;
static constexpr uint32_t kPageIntervalMs = 4000;

// --- WiFi state ---
static String savedSsid;
static String savedPassword;
static bool apStarted    = false;
static bool staConnected = false;
static bool staConnecting = false;
static uint32_t staStartMs = 0;

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

// Page 1: Network
static void drawPageNetwork() {
  // Header
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "Network");
  oledHLine(13);

  // WiFi icon (open_iconic_www_2x_t \x51 = wifi signal)
  oled.setFont(u8g2_font_open_iconic_www_2x_t);
  oled.drawGlyph(0, 36, 0x51);

  oled.setFont(u8g2_font_6x12_tf);
  if (staConnected) {
    oled.setFont(u8g2_font_helvB08_tf);
    oled.drawStr(22, 26, WiFi.localIP().toString().c_str());
    oled.setFont(u8g2_font_6x12_tf);
    // Truncate SSID to fit
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

  // AP status bar at bottom
  oledHLine(53);
  oled.setFont(u8g2_font_5x7_tf);
  if (apStarted) {
    String apLine = "AP " + WiFi.softAPSSID() + " 192.168.4.1";
    oled.drawStr(0, 63, apLine.c_str());
  } else {
    oled.drawStr(0, 63, "AP: off");
  }
}

void oledDrawPage(uint8_t page) {
  oled.clearBuffer();
  switch (page % 2) {
    case 0: drawPageDevice();  break;
    case 1: drawPageNetwork(); break;
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
// NVS
// ============================================================
void loadWifiCredentials() {
  Preferences prefs;
  prefs.begin(kNvsNamespace, true);
  savedSsid     = prefs.getString("ssid", "");
  savedPassword = prefs.getString("pass", "");
  prefs.end();
  Serial.printf("Saved SSID: %s\n", savedSsid.isEmpty() ? "(none)" : savedSsid.c_str());
}

void saveWifiCredentials(const String& ssid, const String& pass) {
  Preferences prefs;
  prefs.begin(kNvsNamespace, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  savedSsid = ssid;
  savedPassword = pass;
}

void clearWifiCredentials() {
  Preferences prefs;
  prefs.begin(kNvsNamespace, false);
  prefs.clear();
  prefs.end();
  savedSsid = "";
  savedPassword = "";
}

// ============================================================
// Web handlers
// ============================================================
void handleRoot() {
  // Saved WiFi info to pre-fill
  String savedInfo = savedSsid.isEmpty()
    ? "<p style='color:#666'>No saved WiFi.</p>"
    : "<p>Saved: <b>" + savedSsid + "</b> &mdash; "
      + (staConnected ? "<span style='color:#0f766e'>Connected, IP: " + WiFi.localIP().toString() + "</span>"
                      : "<span style='color:#b91c1c'>Not connected</span>") + "</p>";

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
      "#msg{margin:12px 0;color:#0f766e}"
      "</style></head><body>"
      "<h2>SEMS AIoT &mdash; WiFi Setup</h2>");
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
        "if(!confirm('Hapus WiFi credentials?'))return;"
        "await fetch('/api/wifi/clear',{method:'POST'});"
        "msg.textContent='Cleared. Reboot untuk efek.';"
      "}"
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
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
    return;
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
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}");
    return;
  }
  saveWifiCredentials(ssid, pass);
  Serial.printf("WiFi saved: %s\n", ssid.c_str());
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWifiClear() {
  clearWifiCredentials();
  server.send(200, "application/json", "{\"ok\":true}");
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
  server.on("/",                HTTP_GET,  handleRoot);
  server.on("/api/scan",        HTTP_POST, handleScanStart);
  server.on("/api/scan/result", HTTP_GET,  handleScanResult);
  server.on("/api/wifi/save",   HTTP_POST, handleWifiSave);
  server.on("/api/wifi/clear",  HTTP_POST, handleWifiClear);
  server.on("/api/reboot",      HTTP_POST, handleReboot);
  server.begin();
  Serial.println("WebServer started");
}

// ============================================================
// WiFi STA lifecycle
// ============================================================
void beginStaConnect() {
  Serial.printf("STA connecting to: %s\n", savedSsid.c_str());
  oledShow("WiFi Connecting", savedSsid.c_str(), "", 15000);
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
  staConnecting = true;
  staStartMs = millis();
}

void handleStaLifecycle(uint32_t now) {
  if (!staConnecting) return;

  if (WiFi.status() == WL_CONNECTED) {
    staConnecting = false;
    staConnected  = true;
    Serial.printf("STA connected! IP: %s\n", WiFi.localIP().toString().c_str());
    oledShow("WiFi Connected", WiFi.localIP().toString().c_str(), savedSsid.c_str(), 6000);
    return;
  }

  if (now - staStartMs >= kStaTimeoutMs) {
    staConnecting = false;
    staConnected  = false;
    Serial.println("STA timeout — staying on AP");
    oledShow("WiFi Failed", savedSsid.c_str(), "Using AP only", 6000);
  }
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

  loadWifiCredentials();

  // Always start AP — web UI always accessible
  WiFi.mode(WIFI_AP_STA);
  startAp();
  startWebServer();

  // Try STA if credentials exist
  if (!savedSsid.isEmpty()) {
    beginStaConnect();
  } else {
    oledShow("No saved WiFi", "Open 192.168.4.1", "to configure", 8000);
  }
}

static uint32_t lastBlinkMs = 0;
static bool ledState = false;

void loop() {
  const uint32_t now = millis();
  server.handleClient();
  handleStaLifecycle(now);
  if (now - lastBlinkMs >= 500) { lastBlinkMs = now; ledState = !ledState; digitalWrite(kLedPin, ledState); }
  updateOled(now);
}
