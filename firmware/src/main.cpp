#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// --- pins ---
static constexpr uint8_t kLedPin = 2;

// --- AP config ---
static constexpr char kApSsidPrefix[] = "SEMS-SETUP";
static constexpr char kApPassword[]   = "sems1234";
static constexpr char kNvsNamespace[] = "wifi";

// --- OLED ---
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R2, U8X8_PIN_NONE);
static bool oledReady = false;
static String oledL1, oledL2, oledL3;
static uint32_t oledUntilMs = 0;
static uint8_t oledPage = 0;
static uint32_t oledPageMs = 0;
static constexpr uint32_t kPageIntervalMs = 4000;

// --- state ---
static String savedSsid;
static String savedPassword;
static bool apStarted = false;
static WebServer server(80);

// ============================================================
// OLED
// ============================================================
void oledShow(const char* l1, const char* l2 = "", const char* l3 = "", uint32_t durationMs = 4000) {
  oledL1 = l1; oledL2 = l2; oledL3 = l3;
  oledUntilMs = millis() + durationMs;
  if (!oledReady) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawStr(0, 12, l1);
  if (l2[0]) oled.drawStr(0, 28, l2);
  if (l3[0]) oled.drawStr(0, 44, l3);
  oled.sendBuffer();
}

void oledDrawPage(uint8_t page) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  switch (page % 2) {
    case 0:
      oled.drawStr(0, 12, "SEMS AIoT");
      oled.drawStr(0, 28, "FW: " FW_VERSION);
      oled.drawStr(0, 44, (String("Up: ") + String(millis() / 1000) + "s").c_str());
      break;
    case 1:
      oled.drawStr(0, 12, "Network");
      if (apStarted) {
        oled.drawStr(0, 28, WiFi.softAPSSID().c_str());
        oled.drawStr(0, 44, WiFi.softAPIP().toString().c_str());
      } else {
        oled.drawStr(0, 28, "AP: off");
      }
      break;
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
  String html = F("<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>SEMS Setup</title>"
    "<style>body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 16px}"
    "h2{color:#0f766e}input{width:100%;box-sizing:border-box;padding:10px;margin:6px 0 14px;"
    "border:1px solid #ccc;border-radius:6px;font-size:15px}"
    "button{background:#0f766e;color:#fff;border:0;border-radius:6px;"
    "padding:10px 16px;font-size:15px;cursor:pointer;margin:4px 2px}"
    ".danger{background:#b91c1c}.net{border-top:1px solid #eee;padding:10px 0;"
    "display:flex;justify-content:space-between;align-items:center}"
    ".ssid{font-weight:700}.tag{font-size:12px;color:#666}"
    "#status{margin:12px 0;color:#0f766e}</style></head>"
    "<body><h2>SEMS AIoT Setup</h2>"
    "<p id=status>Ready.</p>"
    "<button onclick=scanWifi()>Scan WiFi</button> "
    "<button class=danger onclick=clearWifi()>Clear Saved</button>"
    "<div id=list></div>"
    "<form id=form><label>SSID<input id=ssid name=ssid required></label>"
    "<label>Password<input id=pass name=pass type=password></label>"
    "<button type=submit>Save &amp; Reboot</button></form>"
    "<script>"
    // escapeHtml — safe for inserting into HTML text nodes via innerHTML
    "function eh(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));}"
    // scanWifi — async scan, poll result
    "async function scanWifi(){"
      "const st=document.getElementById('status');"
      "st.textContent='Scanning...';"
      "document.getElementById('list').innerHTML='';"
      "await fetch('/api/scan',{method:'POST'});"
      "pollScan(20);"
    "}"
    "async function pollScan(n){"
      "if(n<=0){document.getElementById('status').textContent='Scan timeout';return;}"
      "const r=await fetch('/api/scan/result').then(x=>x.json());"
      "if(r.status==='scanning'){setTimeout(()=>pollScan(n-1),1500);return;}"
      "const st=document.getElementById('status');"
      "st.textContent='Found '+r.networks.length+' network(s).';"
      "document.getElementById('list').innerHTML=r.networks.map(n=>"
        "'<div class=net><div><div class=ssid>'+eh(n.ssid||'(hidden)')+'</div>"
        "<div class=tag>'+eh(n.security)+' | ch'+n.ch+' | '+n.rssi+' dBm</div></div>"
        "<button type=button data-s=\"'+eh(n.ssid||'')+'\">Use</button></div>'"
      ").join('');"
      "document.querySelectorAll('[data-s]').forEach(b=>b.onclick=()=>{"
        "document.getElementById('ssid').value=b.dataset.s;"
        "document.getElementById('pass').focus();"
      "});"
    "}"
    "async function clearWifi(){"
      "if(!confirm('Hapus WiFi credentials?'))return;"
      "await fetch('/api/wifi/clear',{method:'POST'});"
      "document.getElementById('status').textContent='Cleared.';"
    "}"
    "document.getElementById('form').onsubmit=async function(e){"
      "e.preventDefault();"
      "const st=document.getElementById('status');"
      "st.textContent='Saving...';"
      "const body=JSON.stringify({ssid:document.getElementById('ssid').value,"
        "pass:document.getElementById('pass').value});"
      "const r=await fetch('/api/wifi/save',{method:'POST',"
        "headers:{'Content-Type':'application/json'},body}).then(x=>x.json());"
      "if(r.ok){st.textContent='Saved! Rebooting...';setTimeout(()=>fetch('/api/reboot',{method:'POST'}),800);}"
      "else st.textContent='Error: '+(r.error||'unknown');"
    "};"
    "</script></body></html>");
  server.send(200, "text/html", html);
}

void handleScanStart() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);  // async, show hidden
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
    body += "{\"ssid\":\"";
    // safe JSON: escape only backslash and double-quote
    String s = WiFi.SSID(i);
    s.replace("\\", "\\\\"); s.replace("\"", "\\\"");
    body += s;
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
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}"); return; }
  // Parse JSON manually — tiny subset, no library needed
  String body = server.arg("plain");
  auto extract = [&](const char* key) -> String {
    String k = String("\"") + key + "\":\"";
    int s = body.indexOf(k);
    if (s < 0) return "";
    s += k.length();
    int e = body.indexOf('"', s);
    return e < 0 ? "" : body.substring(s, e);
  };
  String ssid = extract("ssid");
  String pass = extract("pass");
  if (ssid.isEmpty()) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}"); return; }
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
// AP + WebServer
// ============================================================
String getApSsid() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char suffix[7]; snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(kApSsidPrefix) + "-" + suffix;
}

void startAp() {
  const String ssid = getApSsid();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid.c_str(), kApPassword);
  apStarted = true;
  Serial.printf("AP started: %s  IP: %s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
  oledShow("AP Ready", ssid.c_str(), "192.168.4.1", 8000);

  server.on("/",               HTTP_GET,  handleRoot);
  server.on("/api/scan",       HTTP_POST, handleScanStart);
  server.on("/api/scan/result",HTTP_GET,  handleScanResult);
  server.on("/api/wifi/save",  HTTP_POST, handleWifiSave);
  server.on("/api/wifi/clear", HTTP_POST, handleWifiClear);
  server.on("/api/reboot",     HTTP_POST, handleReboot);
  server.begin();
  Serial.println("WebServer started");
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
  oledShow("SEMS AIoT", "FW: " FW_VERSION, "Booting...", 3000);

  loadWifiCredentials();
  delay(500);

  startAp();
}

static uint32_t lastBlinkMs = 0;
static bool ledState = false;

void loop() {
  const uint32_t now = millis();
  server.handleClient();
  if (now - lastBlinkMs >= 500) { lastBlinkMs = now; ledState = !ledState; digitalWrite(kLedPin, ledState); }
  updateOled(now);
}
