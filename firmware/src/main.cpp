// SEMS AIoT — ESP32 firmware
// Platform: arduino-esp32 v3.x (pioarduino espressif32@55.x)
// Network:  W5500 via ETH.h (native lwIP) + WiFi AP/STA
// WebServer listens on all interfaces automatically (single lwIP stack)

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <ETH.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <NetworkClient.h>
#include <PubSubClient.h>

// ============================================================
// Hardware constants
// ============================================================
static constexpr uint8_t  kLedPin      = 2;
static constexpr uint8_t  kBtnPin      = 0;   // BOOT button, active LOW
static constexpr uint32_t kBtnHoldMs   = 3000;

static constexpr int kEthCs  = 5;
static constexpr int kEthIrq = 27;
static constexpr int kEthRst = 26;

// ============================================================
// RTC state — survives soft reboot, NOT power-off
// ============================================================
RTC_DATA_ATTR static uint32_t rtcWifiFailMagic = 0;
RTC_DATA_ATTR static uint32_t rtcConfigMagic   = 0;
RTC_DATA_ATTR static uint32_t rtcWifiFailCount = 0;

static constexpr uint32_t kWifiFailMagic = 0xDEADF17E;
static constexpr uint32_t kConfigMagic   = 0xC0FEF00D;

// ============================================================
// WiFi credentials (NVS: namespace "wifi", keys n/s0..s4/p0..p4)
// ============================================================
static constexpr char    kNvsWifi[]     = "wifi";
static constexpr uint8_t kMaxSavedWifi = 5;
static constexpr char    kApSsidPfx[]  = "SEMS-SETUP";
static constexpr char    kApPass[]     = "sems1234";
static constexpr uint32_t kStaTimeoutMs = 15000;

struct WifiEntry { String ssid; String pass; };
static WifiEntry wifiList[kMaxSavedWifi];
static uint8_t   wifiCount = 0;

// ============================================================
// MQTT config (NVS: namespace "mqtt")
// ============================================================
static constexpr char kNvsMqtt[] = "mqtt";
static constexpr uint32_t kMqttRetryMs = 5000;

struct MqttConfig {
  String   host;
  uint16_t port        = 1883;
  String   user;
  String   pass;
  String   topicPrefix = "sems";
  bool     enabled     = false;
};
static MqttConfig mqttCfg;

// ============================================================
// Network state (written from event handler + loop)
// ============================================================
static volatile bool ethReady     = false;  // ETH has IP
static volatile bool ethLink      = false;  // physical link
static String        ethIp;

static bool     staConnected  = false;
static bool     staConnecting = false;
static uint32_t staStartMs    = 0;
static String   savedSsid;

static bool     apStarted    = false;
static uint8_t  triedMask    = 0;
static uint8_t  scanRetryCount = 0;
static constexpr uint8_t kMaxScanRetry = 3;
static bool     scanPending  = false;
static uint32_t rebootAtMs   = 0;

// ============================================================
// MQTT — NetworkClient routes via lwIP best path automatically
// ============================================================
static NetworkClient netClient;
static PubSubClient  mqttClient(netClient);
static bool          mqttConnected  = false;
static uint32_t      mqttLastTryMs  = 0;

// ============================================================
// Web server
// ============================================================
static WebServer server(80);

// ============================================================
// OLED
// ============================================================
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, 21, 22);
static bool     oledReady      = false;
static uint32_t oledUntilMs   = 0;
static uint8_t  oledPage       = 0;
static uint32_t oledPageMs     = 0;
static uint32_t oledHbMs       = 0;
static uint8_t  oledHbTick     = 0;
static constexpr uint32_t kPageIntervalMs = 3000;

// ============================================================
// Button
// ============================================================
static uint32_t btnPressedMs = 0;

// ============================================================
// LED blink
// ============================================================
static uint32_t lastBlinkMs = 0;
static bool     ledState    = false;

// ============================================================
// Forward declarations
// ============================================================
void startAp();
void restoreAp();
void enterConfigMode();
void beginStaConnect();
void oledShow(const char* l1, const char* l2 = "", const char* l3 = "", uint32_t ms = 4000);

// ============================================================
// NETWORK EVENT HANDLER — unified WiFi + ETH events (v3.x)
// ============================================================
static void onNetworkEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {

    // --- Ethernet ---
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname("sems-eth");
      break;

    case ARDUINO_EVENT_ETH_CONNECTED:
      ethLink = true;
      Serial.println("[ETH] Cable connected");
      break;

    case ARDUINO_EVENT_ETH_GOT_IP:
      ethReady = true;
      ethIp    = ETH.localIP().toString();
      ETH.setDefault();   // LAN takes routing priority
      Serial.printf("[ETH] IP: %s  speed: %dMbps %s\n",
                    ethIp.c_str(),
                    ETH.linkSpeed(),
                    ETH.fullDuplex() ? "FD" : "HD");
      oledShow("LAN Ready", ethIp.c_str(), "W5500", 4000);
      break;

    case ARDUINO_EVENT_ETH_LOST_IP:
      ethReady = false;
      ethIp    = "";
      if (staConnected) WiFi.STA.setDefault();
      Serial.println("[ETH] IP lost");
      break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
      ethLink  = false;
      ethReady = false;
      ethIp    = "";
      if (staConnected) WiFi.STA.setDefault();
      Serial.println("[ETH] Disconnected");
      break;

    // --- WiFi STA ---
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      staConnecting = false;
      staConnected  = true;
      rtcWifiFailCount = 0;
      if (!ethReady) WiFi.STA.setDefault();  // use WiFi only when LAN absent
      Serial.printf("[WiFi] Connected: %s  IP: %s\n",
                    savedSsid.c_str(),
                    WiFi.localIP().toString().c_str());
      // AP off once STA is up — single interface sufficient
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      apStarted = false;
      oledShow("WiFi Connected", WiFi.localIP().toString().c_str(), savedSsid.c_str(), 6000);
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (staConnected) {
        staConnected = false;
        Serial.println("[WiFi] STA dropped — restoring AP");
        restoreAp();
        oledShow("WiFi Lost", "Reconnecting...", "", 3000);
        beginStaConnect();
      }
      break;

    default:
      break;
  }
}

// ============================================================
// OLED helpers
// ============================================================
static void oledHLine(uint8_t y) { oled.drawHLine(0, y, 128); }

static void oledDrawRight(const char* s, uint8_t y) {
  oled.drawStr(128 - oled.getStrWidth(s), y, s);
}

void oledShow(const char* l1, const char* l2, const char* l3, uint32_t ms) {
  oledUntilMs = millis() + ms;
  if (!oledReady) return;
  oled.clearBuffer();
  
  // Header / Title in Yellow Zone
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, l1);
  oled.drawHLine(0, 14, 128);
  
  // Content in Blue Zone
  oled.setFont(u8g2_font_6x12_tf);
  if (l2 && l2[0]) oled.drawStr(6, 32, l2);
  if (l3 && l3[0]) oled.drawStr(6, 48, l3);
  
  // Left-edge indicator bar
  oled.drawBox(0, 18, 2, 42);
  
  oled.sendBuffer();
}

static void drawPageDevice() {
  char buf[24];
  
  // Yellow Zone Header
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "SEMS SYSTEM");
  oledDrawRight("v" FW_VERSION, 10);
  oled.drawHLine(0, 14, 128);
  
  // Blue Zone Content
  // Stylized Microchip Icon on the Left
  oled.drawFrame(4, 26, 12, 12);
  oled.drawBox(7, 29, 6, 6);
  // Microchip pins
  oled.drawHLine(0, 29, 4); oled.drawHLine(0, 35, 4);
  oled.drawHLine(16, 29, 4); oled.drawHLine(16, 35, 4);
  oled.drawVLine(7, 22, 4); oled.drawVLine(13, 22, 4);
  oled.drawVLine(7, 38, 4); oled.drawVLine(13, 38, 4);

  oled.setFont(u8g2_font_6x12_tf);
  uint32_t s = millis()/1000, m = s/60; s%=60; uint32_t h = m/60; m%=60;
  snprintf(buf, sizeof(buf), "%02luh %02lum %02lus", h, m, s);
  
  oled.drawStr(24, 26, "Uptime:");
  oled.drawStr(24, 37, buf);
  
  // Dynamic RAM Heap bar graph
  uint32_t totalHeap = 327680;
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t usedHeap = totalHeap > freeHeap ? totalHeap - freeHeap : 0;
  uint8_t barWidth = (usedHeap * 80) / totalHeap;
  if (barWidth > 80) barWidth = 80;
  
  oled.drawStr(24, 49, "Heap Usage:");
  oled.drawFrame(24, 53, 82, 6);
  oled.drawBox(25, 54, barWidth, 4);
}

static void drawPageWifi() {
  // Yellow Zone Header
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "WIFI CONNECTION");
  oled.drawHLine(0, 14, 128);
  
  // Blue Zone Content
  oled.setFont(u8g2_font_6x12_tf);
  if (staConnected) {
    // Wi-Fi bars graph on the left
    int rssi = WiFi.RSSI();
    oled.drawBox(2, 42, 2, 4);
    if (rssi > -80) oled.drawBox(5, 38, 2, 8);
    else oled.drawFrame(5, 38, 2, 8);
    
    if (rssi > -70) oled.drawBox(8, 34, 2, 12);
    else oled.drawFrame(8, 34, 2, 12);
    
    if (rssi > -60) oled.drawBox(11, 30, 2, 16);
    else oled.drawFrame(11, 30, 2, 16);
    
    oled.drawStr(20, 26, WiFi.localIP().toString().c_str());
    String ss = savedSsid.length() > 17 ? savedSsid.substring(0,16)+"~" : savedSsid;
    oled.drawStr(20, 37, ss.c_str());
    char rssiBuf[16]; snprintf(rssiBuf, sizeof(rssiBuf), "Signal: %d dBm", rssi);
    oled.drawStr(20, 48, rssiBuf);
  } else if (staConnecting) {
    // Blinking scanning/connecting animation
    uint8_t anim = (millis() / 400) % 4;
    oled.drawBox(2, 42, 2, 4);
    if (anim >= 1) oled.drawBox(5, 38, 2, 8);
    if (anim >= 2) oled.drawBox(8, 34, 2, 12);
    if (anim >= 3) oled.drawBox(11, 30, 2, 16);
    
    oled.drawStr(20, 28, "Connecting...");
    String ss = savedSsid.length() > 17 ? savedSsid.substring(0,16)+"~" : savedSsid;
    oled.drawStr(20, 42, ss.c_str());
  } else {
    // Disconnected icon (Cross)
    oled.drawLine(2, 30, 14, 42);
    oled.drawLine(14, 30, 2, 42);
    
    oled.drawStr(20, 34, "Disconnected");
  }
  
  // AP status bar
  oled.drawHLine(0, 54, 128);
  oled.setFont(u8g2_font_5x7_tf);
  if (apStarted) {
    String ap = "AP: " + WiFi.softAPSSID() + " (192.168.4.1)";
    oled.drawStr(2, 62, ap.c_str());
  } else {
    oled.drawStr(2, 62, "AP Mode: Disabled");
  }
}

static void drawPageLan() {
  // Yellow Zone Header
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "ETHERNET LAN");
  oled.drawHLine(0, 14, 128);
  
  // Blue Zone Content
  oled.setFont(u8g2_font_6x12_tf);
  
  // Ethernet port icon
  oled.drawFrame(2, 30, 12, 10);
  oled.drawBox(5, 40, 6, 2);
  oled.drawVLine(8, 42, 4);
  oled.drawHLine(6, 45, 5);
  
  if (ethReady) {
    oled.drawStr(20, 26, ethIp.c_str());
    oled.drawStr(20, 37, "Speed: 100 Mbps");
    oled.drawStr(20, 48, ethLink ? "Link: Connected" : "Link: Disconnected");
  } else {
    oled.drawStr(20, 28, "LAN Offline");
    oled.drawStr(20, 42, ethLink ? "Cable Plugged" : "Insert Cable");
  }
  
  // Routing priority status bar
  oled.drawHLine(0, 54, 128);
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(2, 62, ethReady ? "Route: Ethernet (Primary)" : "Route: Standby");
}

static void oledDrawPage(uint8_t page) {
  oled.clearBuffer();
  switch (page % 3) {
    case 0: drawPageDevice(); break;
    case 1: drawPageWifi();   break;
    case 2: drawPageLan();    break;
  }
  oled.sendBuffer();
}

static void oledHeartbeat(uint32_t now) {
  if (!oledReady || now - oledHbMs < 500) return;
  oledHbMs = now;
  oledHbTick++;
  // Heartbeat is placed in the top-right corner of the yellow header zone
  oled.setDrawColor(0); oled.drawBox(122, 2, 6, 6);
  oled.setDrawColor(1);
  if (oledHbTick & 1) {
    oled.drawBox(123, 3, 4, 4);
  }
  oled.sendBuffer();
}

static void updateOled(uint32_t now) {
  if (!oledReady) return;
  oledHeartbeat(now);
  if (now < oledUntilMs) return;
  if (now - oledPageMs < kPageIntervalMs) return;
  oledPageMs = now;
  oledPage = (oledPage + 1) % 3;
  oledDrawPage(oledPage);
}

// ============================================================
// NVS — WiFi list
// ============================================================
static void loadWifiList() {
  Preferences p; p.begin(kNvsWifi, true);
  wifiCount = min((uint8_t)p.getUChar("n", 0), kMaxSavedWifi);
  for (uint8_t i = 0; i < wifiCount; i++) {
    wifiList[i].ssid = p.getString(("s"+String(i)).c_str(), "");
    wifiList[i].pass = p.getString(("p"+String(i)).c_str(), "");
  }
  p.end();
  Serial.printf("[NVS] %d WiFi(s)\n", wifiCount);
}

static void saveWifiList() {
  Preferences p; p.begin(kNvsWifi, false);
  p.putUChar("n", wifiCount);
  for (uint8_t i = 0; i < wifiCount; i++) {
    p.putString(("s"+String(i)).c_str(), wifiList[i].ssid);
    p.putString(("p"+String(i)).c_str(), wifiList[i].pass);
  }
  p.end();
}

static bool addOrUpdateWifi(const String& ssid, const String& pass) {
  for (uint8_t i = 0; i < wifiCount; i++) {
    if (wifiList[i].ssid == ssid) { wifiList[i].pass = pass; saveWifiList(); return true; }
  }
  if (wifiCount >= kMaxSavedWifi) return false;
  wifiList[wifiCount++] = {ssid, pass};
  saveWifiList(); return true;
}

static void removeWifi(const String& ssid) {
  for (uint8_t i = 0; i < wifiCount; i++) {
    if (wifiList[i].ssid == ssid) {
      for (uint8_t j = i; j < wifiCount-1; j++) wifiList[j] = wifiList[j+1];
      wifiCount--; saveWifiList(); return;
    }
  }
}

static void clearAllWifi() {
  wifiCount = 0;
  Preferences p; p.begin(kNvsWifi, false); p.clear(); p.end();
}

// ============================================================
// NVS — MQTT config
// ============================================================
static void loadMqttConfig() {
  Preferences p; p.begin(kNvsMqtt, true);
  mqttCfg.enabled     = p.getBool("en", false);
  mqttCfg.host        = p.getString("host", "");
  mqttCfg.port        = p.getUShort("port", 1883);
  mqttCfg.user        = p.getString("user", "");
  mqttCfg.pass        = p.getString("pass", "");
  mqttCfg.topicPrefix = p.getString("topic", "sems");
  p.end();
  Serial.printf("[NVS] MQTT %s:%d en=%d\n", mqttCfg.host.c_str(), mqttCfg.port, mqttCfg.enabled);
}

static void saveMqttConfig() {
  Preferences p; p.begin(kNvsMqtt, false);
  p.putBool("en",      mqttCfg.enabled);
  p.putString("host",  mqttCfg.host);
  p.putUShort("port",  mqttCfg.port);
  p.putString("user",  mqttCfg.user);
  p.putString("pass",  mqttCfg.pass);
  p.putString("topic", mqttCfg.topicPrefix);
  p.end();
}

// ============================================================
// MQTT lifecycle
// ============================================================
void mqttPublish(const char* subtopic, const String& payload) {
  if (!mqttConnected) return;
  String topic = mqttCfg.topicPrefix + "/" + subtopic;
  mqttClient.publish(topic.c_str(), payload.c_str());
}

static void handleMqttLifecycle(uint32_t now) {
  if (!mqttCfg.enabled || mqttCfg.host.isEmpty()) return;
  if (!staConnected && !ethReady) return;  // need at least one network

  if (mqttClient.connected()) {
    mqttConnected = true;
    mqttClient.loop();
    return;
  }
  mqttConnected = false;
  if (now - mqttLastTryMs < kMqttRetryMs) return;
  mqttLastTryMs = now;

  mqttClient.setServer(mqttCfg.host.c_str(), mqttCfg.port);
  String id = "sems-" + WiFi.macAddress(); id.replace(":", "");
  bool ok = mqttCfg.user.isEmpty()
    ? mqttClient.connect(id.c_str())
    : mqttClient.connect(id.c_str(), mqttCfg.user.c_str(), mqttCfg.pass.c_str());

  if (ok) {
    mqttConnected = true;
    Serial.println("[MQTT] Connected");
    oledShow("MQTT", "Connected", mqttCfg.host.c_str(), 3000);
    mqttPublish("status", "{\"online\":true,\"fw\":\"" FW_VERSION "\"}");
  } else {
    Serial.printf("[MQTT] Failed rc=%d\n", mqttClient.state());
  }
}

// ============================================================
// WiFi scan helpers
// ============================================================
static void kickBackgroundScan(bool showOled = true) {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  WiFi.scanDelete();
  if (showOled) oledShow("Scanning WiFi", "Finding best AP...", "", 10000);
  Serial.println("[WiFi] Scan (async)...");
  WiFi.scanNetworks(true, false);
}

static int pickBestFromLastScan(uint8_t skipMask = 0) {
  const int found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) return -2;
  if (found <= 0) { WiFi.scanDelete(); return -1; }
  int bestIdx = -1, bestRssi = -999;
  for (int s = 0; s < found; s++) {
    for (uint8_t w = 0; w < wifiCount; w++) {
      if (skipMask & (1<<w)) continue;
      if (wifiList[w].ssid == WiFi.SSID(s) && WiFi.RSSI(s) > bestRssi) {
        bestRssi = WiFi.RSSI(s); bestIdx = w;
      }
    }
  }
  WiFi.scanDelete();
  if (bestIdx >= 0)
    Serial.printf("[WiFi] Best: [%d] %s (%ddBm)\n", bestIdx, wifiList[bestIdx].ssid.c_str(), bestRssi);
  return bestIdx;
}

// ============================================================
// WiFi STA lifecycle
// ============================================================
static void connectToEntry(uint8_t idx) {
  scanRetryCount = 0;
  triedMask |= (1<<idx);
  savedSsid = wifiList[idx].ssid;
  char attempt[24];
  snprintf(attempt, sizeof(attempt), "(%d/%d)", __builtin_popcount(triedMask), wifiCount);
  Serial.printf("[WiFi] → %s %s\n", savedSsid.c_str(), attempt);
  oledShow("WiFi Connecting", savedSsid.c_str(), attempt, 15000);
  WiFi.disconnect(false);
  WiFi.begin(wifiList[idx].ssid.c_str(), wifiList[idx].pass.c_str());
  staConnecting = true;
  staStartMs    = millis();
  scanPending   = false;
}

void beginStaConnect() {
  triedMask      = 0;
  scanRetryCount = 0;
  rebootAtMs     = 0;
  scanPending    = false;
  WiFi.disconnect(false);
  kickBackgroundScan(true);
  scanPending = true;
}

static void scheduleReboot() {
  if (rebootAtMs != 0) return;
  rtcWifiFailMagic = kWifiFailMagic;
  rtcWifiFailCount++;
  rebootAtMs = millis() + 60000;
  Serial.printf("[WiFi] No network — reboot in 60s (fail #%lu)\n", rtcWifiFailCount);
  restoreAp();
  char line[20]; snprintf(line, sizeof(line), "Gagal #%lu", rtcWifiFailCount);
  oledShow("No WiFi Found", "AP: 192.168.4.1", line, 6000);
}

static void processScanResult() {
  if (!scanPending) return;
  const int best = pickBestFromLastScan(triedMask);
  if (best == -2) return;   // still scanning
  scanPending = false;
  if (best >= 0) { connectToEntry((uint8_t)best); return; }
  if (scanRetryCount < kMaxScanRetry) {
    scanRetryCount++;
    Serial.printf("[WiFi] Scan empty, retry %d/%d\n", scanRetryCount, kMaxScanRetry);
    oledShow("Scan empty", "Retrying...", "", 5000);
    kickBackgroundScan(false);
    scanPending = true;
  } else {
    scheduleReboot();
  }
}

static void handleStaTimeout(uint32_t now) {
  if (!staConnecting) return;
  if (now - staStartMs < kStaTimeoutMs) return;
  staConnecting = false;
  Serial.printf("[WiFi] Timeout: %s\n", savedSsid.c_str());
  kickBackgroundScan(false);
  scanPending = true;
}

// ============================================================
// AP helpers
// ============================================================
static String getApSsid() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char sfx[7]; snprintf(sfx, sizeof(sfx), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(kApSsidPfx) + "-" + sfx;
}

void startAp() {
  const String ssid = getApSsid();
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(ssid.c_str(), kApPass);
  apStarted = true;
  Serial.printf("[AP] %s  192.168.4.1\n", ssid.c_str());
  oledShow("AP Ready", ssid.c_str(), "192.168.4.1", 6000);
}

void restoreAp() {
  if (apStarted) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  const String ssid = getApSsid();
  WiFi.softAP(ssid.c_str(), kApPass);
  apStarted = true;
  Serial.println("[AP] Restored");
}

// ============================================================
// Reboot countdown
// ============================================================
static void handleRebootCountdown(uint32_t now) {
  if (rebootAtMs == 0) return;
  const int32_t secsLeft = (int32_t)(rebootAtMs - now) / 1000;

  if (secsLeft <= 0) {
    if (WiFi.softAPgetStationNum() > 0) {
      rebootAtMs = now + 30000;
      Serial.println("[Reboot] AP client connected — postpone 30s");
      oledShow("Client terhubung", "Tunda reboot 30s", "AP: 192.168.4.1", 4000);
      return;
    }
    Serial.println("[Reboot] Rebooting now");
    ESP.restart();
  }

  // Every 10s: scan to see if a saved network appeared
  static uint32_t lastRetryMs = 0;
  if (now - lastRetryMs >= 10000) {
    lastRetryMs = now;
    if (!scanPending) { kickBackgroundScan(false); scanPending = true; }
  }
  if (scanPending) {
    const int best = pickBestFromLastScan(0);
    if (best == -2) { /* still scanning */ }
    else if (best >= 0) {
      Serial.printf("[Reboot] WiFi appeared: %s — cancel\n", wifiList[best].ssid.c_str());
      rebootAtMs = 0; lastRetryMs = 0; scanPending = false;
      rtcWifiFailMagic = 0;
      beginStaConnect();
      return;
    } else {
      scanPending = false;
    }
  }

  static int32_t lastSec = -1;
  if (secsLeft != lastSec) {
    lastSec = secsLeft;
    char buf[18]; snprintf(buf, sizeof(buf), "Reboot in %ds", secsLeft);
    oledShow("No WiFi Found", buf, "AP: 192.168.4.1", 1100);
    Serial.printf("[Reboot] %ds\n", secsLeft);
  }
}

// ============================================================
// Config mode
// ============================================================
void enterConfigMode() {
  rtcConfigMagic = kConfigMagic;
  oledShow("Config Mode", "Restarting...", "", 2000);
  Serial.println("[Boot] Config mode — restarting");
  delay(500);
  ESP.restart();
}

static void handleConfigButton(uint32_t now) {
  const bool pressed = (digitalRead(kBtnPin) == LOW);
  if (pressed) {
    if (btnPressedMs == 0) btnPressedMs = now;
    else if (now - btnPressedMs >= kBtnHoldMs) { btnPressedMs = 0; enterConfigMode(); }
  } else {
    btnPressedMs = 0;
  }
}

// ============================================================
// HTML shared assets (PROGMEM)
// ============================================================
static const char kStyle[] PROGMEM =
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
  ".up{background:#dcfce7;color:#15803d}.down{background:#fee2e2;color:#b91c1c}"
  ".connecting{background:#fef9c3;color:#92400e}"
  ".dot{width:7px;height:7px;border-radius:50%;background:currentColor}"
  ".sep{height:1px;background:#f1f5f9;margin:6px 0}"
  ".nav{display:flex;gap:8px;margin-bottom:16px;flex-wrap:wrap}"
  ".nav a{font-size:13px;color:#0f766e;text-decoration:none;padding:6px 12px;"
          "border-radius:8px;background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.06)}"
  ".nav a:hover{background:#f0fdf4}"
  "label{display:block;font-size:13px;color:#64748b;margin:10px 0 4px}"
  "input[type=text],input[type=password],input[type=number]{"
    "width:100%;padding:9px 12px;border:1px solid #e2e8f0;border-radius:8px;"
    "font-size:14px;color:#0f172a;outline:none}"
  "input:focus{border-color:#0f766e;box-shadow:0 0 0 2px rgba(15,118,110,.15)}"
  ".btn{display:inline-block;background:#0f766e;color:#fff;border:0;border-radius:8px;"
        "padding:9px 16px;font-size:14px;font-weight:600;cursor:pointer;margin:4px 4px 0 0}"
  ".btn-sm{padding:5px 12px;font-size:12px}"
  ".btn-danger{background:#b91c1c}.btn-ghost{background:#e2e8f0;color:#0f172a}"
  ".net-item{display:flex;justify-content:space-between;align-items:center;"
             "padding:10px 0;border-top:1px solid #f1f5f9}"
  ".net-ssid{font-size:13px;font-weight:600;color:#0f172a}"
  ".net-tag{font-size:11px;color:#94a3b8;margin-top:2px}"
  ".ok{color:#15803d}.err{color:#b91c1c}"
  "#msg{margin-top:10px;font-size:13px;min-height:18px}"
  ".toggle{display:flex;align-items:center;gap:10px;margin-bottom:8px}"
  ".toggle input{width:auto;margin:0}"
  ".toggle span{font-size:14px;color:#0f172a;font-weight:500}"
  "</style>";

static const char kNav[] PROGMEM =
  "<div class=nav>"
  "<a href=/>&#9881; Setup</a>"
  "<a href=/network>&#127760; Network</a>"
  "<a href=/mqtt>&#128236; MQTT</a>"
  "</div>";

static const char kScanScript[] PROGMEM =
  "function eh(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));}"
  "async function scanWifi(tid){"
    "const msg=document.getElementById('msg');"
    "msg.className='err';msg.textContent='Scanning...';"
    "document.getElementById('scanList').innerHTML='';"
    "await fetch('/api/scan',{method:'POST'});"
    "pollScan(20,tid);"
  "}"
  "async function pollScan(n,tid){"
    "const msg=document.getElementById('msg');"
    "if(n<=0){msg.textContent='Scan timeout';return;}"
    "const r=await fetch('/api/scan/result').then(x=>x.json());"
    "if(r.status==='scanning'){setTimeout(()=>pollScan(n-1,tid),1500);return;}"
    "msg.className='ok';msg.textContent=''+r.networks.length+' jaringan ditemukan.';"
    "document.getElementById('scanList').innerHTML=r.networks.map(n=>"
      "'<div class=net-item>"
        "<div><div class=net-ssid>'+eh(n.ssid||'(hidden)')+'</div>"
        "<div class=net-tag>'+eh(n.security)+' • ch'+n.ch+' • '+n.rssi+' dBm</div></div>"
        "<button class=\"btn btn-sm btn-ghost\" type=button data-s=\"'+eh(n.ssid||'')+'\">Pilih</button>"
      "</div>'"
    ").join('');"
    "document.querySelectorAll('[data-s]').forEach(b=>b.onclick=()=>{"
      "document.getElementById(tid).value=b.dataset.s;"
      "document.getElementById('pass').focus();"
    "});"
  "}";

// ============================================================
// JSON helpers
// ============================================================
static String jsonExtract(const String& body, const char* key) {
  String k = String('"') + key + "\":\"";
  int s = body.indexOf(k); if (s < 0) return "";
  s += k.length(); int e = body.indexOf('"', s);
  return e < 0 ? "" : body.substring(s, e);
}

static bool jsonBool(const String& body, const char* key) {
  String k = String('"') + key + "\":";
  int s = body.indexOf(k); if (s < 0) return false;
  return body.substring(s + k.length(), s + k.length() + 4) == "true";
}

static int jsonInt(const String& body, const char* key, int def) {
  String k = String('"') + key + "\":";
  int s = body.indexOf(k); if (s < 0) return def;
  return body.substring(s + k.length()).toInt();
}

static String htmlEsc(String s) {
  s.replace("&","&amp;"); s.replace("<","&lt;"); s.replace(">","&gt;");
  return s;
}

// ============================================================
// Web handlers
// ============================================================
static void handleRoot() {
  oledShow("Web UI", "Setup dibuka", server.client().remoteIP().toString().c_str(), 3000);

  char uptime[18], mac[18];
  { uint32_t s=millis()/1000, m=s/60; s%=60; uint32_t h=m/60; m%=60;
    snprintf(uptime, sizeof(uptime), "%02luh%02lum%02lus", h, m, s); }
  { uint8_t b[6]; WiFi.macAddress(b);
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", b[0],b[1],b[2],b[3],b[4],b[5]); }

  String html;
  html.reserve(5000);
  html += FPSTR(kStyle);
  html += F("<title>SEMS Setup</title></head><body><div class=wrap>"
            "<h1>SEMS AIoT &mdash; Setup</h1>");
  html += FPSTR(kNav);

  // Hardware card
  html += F("<div class=card><div class=card-title>Hardware</div>");
  html += "<div class=row><span class=label>Chip</span><span class=val>ESP32 ";
  html += ESP.getChipModel(); html += " rev"; html += ESP.getChipRevision();
  html += F("</span></div><div class=sep></div>");
  html += "<div class=row><span class=label>Flash</span><span class=val>";
  html += ESP.getFlashChipSize()/1024;
  html += F(" kB</span></div><div class=row><span class=label>Free Heap</span><span class=val>");
  html += ESP.getFreeHeap()/1024;
  html += F(" kB</span></div><div class=sep></div>");
  html += "<div class=row><span class=label>MAC</span><span class=val style='font-family:monospace;font-size:12px'>";
  html += mac;
  html += F("</span></div><div class=row><span class=label>Firmware</span><span class=val>v" FW_VERSION "</span></div>");
  html += "<div class=row><span class=label>Uptime</span><span class=val>"; html += uptime;
  html += F("</span></div></div>");

  // Saved WiFi card
  html += F("<div class=card><div class=card-title>Saved WiFi Networks</div>");
  if (wifiCount == 0) {
    html += F("<p style='font-size:13px;color:#94a3b8'>Belum ada jaringan tersimpan.</p>");
  } else {
    for (uint8_t i = 0; i < wifiCount; i++) {
      String s = htmlEsc(wifiList[i].ssid);
      bool active = staConnected && wifiList[i].ssid == savedSsid;
      html += "<div class=net-item><div><div class=net-ssid>"; html += s;
      if (active) { html += F(" <span class=ok>&#10003; "); html += WiFi.localIP().toString(); html += F("</span>"); }
      html += F("</div></div><button class='btn btn-sm btn-danger' type=button data-d=\"");
      html += s; html += F("\">&#10005;</button></div>");
    }
  }
  html += F("</div>");

  // Config links
  html += F("<div class=card><div class=card-title>Konfigurasi</div>"
            "<a class='btn btn-sm' href=/network style='text-decoration:none'>&#127760; Network &amp; WiFi</a> "
            "<a class='btn btn-sm' href=/mqtt style='text-decoration:none'>&#128236; MQTT</a></div>");

  // System buttons
  html += F("<div class=card><div class=card-title>Sistem</div>"
            "<button class='btn btn-sm' id=btnCfg type=button>&#128268; Config Mode (AP)</button> "
            "<button class='btn btn-sm btn-danger' id=btnRbt type=button>&#8635; Reboot</button></div>");

  html += F("<script>"
    "document.querySelectorAll('[data-d]').forEach(b=>b.onclick=async()=>{"
      "await fetch('/api/wifi/delete',{method:'POST',"
        "headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:b.dataset.d})});"
      "location.reload();"
    "});"
    "document.getElementById('btnCfg').onclick=async()=>{"
      "if(!confirm('Masuk Config Mode? Device akan restart dan broadcast AP.'))return;"
      "await fetch('/api/config-mode',{method:'POST'});"
      "document.body.innerHTML='<div class=wrap><div class=card>"
        "<p>Restarting... Hubungkan ke AP SEMS-SETUP-xx</p></div></div>';"
    "};"
    "document.getElementById('btnRbt').onclick=async()=>{"
      "if(!confirm('Reboot device?'))return;"
      "await fetch('/api/reboot',{method:'POST'});"
    "};"
    "</script></div></body></html>");

  server.send(200, "text/html", html);
}

static void handleNetworkPage() {
  oledShow("Web UI", "Network dibuka", server.client().remoteIP().toString().c_str(), 3000);

  String html;
  html.reserve(5500);
  html += FPSTR(kStyle);
  html += F("<title>SEMS Network</title>"
            "<style>.refresh{font-size:11px;color:#94a3b8;text-align:right;margin-top:4px}</style>"
            "</head><body><div class=wrap><h1>SEMS AIoT &mdash; Network</h1>");
  html += FPSTR(kNav);
  html += F("<div id=root><p style='color:#94a3b8;font-size:13px'>Loading...</p></div>"
            "<div class=refresh id=ts></div>"
            "<div class=card style='margin-top:12px'><div class=card-title>Scan WiFi</div>"
            "<button class='btn btn-sm' type=button onclick=\"scanWifi('ssid')\">&#128268; Scan Sekarang</button>"
            "<div id=msg style='margin-top:8px;font-size:13px'></div>"
            "<div id=scanList></div>"
            "<div id=addForm style='display:none'>"
            "<label>SSID</label><input type=text id=ssid readonly>"
            "<label>Password</label><input type=password id=pass placeholder='Password'>"
            "<button class=btn style='width:100%;margin-top:10px' onclick=saveWifi()>Simpan &amp; Reboot</button>"
            "</div></div>"
            "<script>"
            "function badge(ok,yes,no,mid){"
              "const s=ok===null?'connecting':ok?'up':'down';"
              "const t=ok===null?mid:ok?yes:no;"
              "return'<span class=\"badge '+s+'\"><span class=dot></span>'+t+'</span>';}"
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
                "h+=row('Status',badge(l.ready,'Connected (IP)','No IP / No cable'));"
                "h+=row('Link',badge(l.link,'Up','Down'));"
                "if(l.ip)h+=row('IP Address',l.ip);"
                "h+=row('Route',l.ready?'Default (priority)':'Standby');"
                "h+='</div>';"
                "h+='<div class=card><div class=card-title>Device</div>';"
                "h+=row('Firmware','v'+dv.fw);"
                "h+=row('Uptime',dv.uptime);"
                "h+=row('Free Heap',Math.round(dv.heap/1024)+' kB');"
                "h+='</div>';"
                "document.getElementById('root').innerHTML=h;"
                "document.getElementById('ts').textContent='Updated '+new Date().toLocaleTimeString();"
              "}catch(e){document.getElementById('root').innerHTML='<p style=color:#b91c1c>'+e+'</p>';}"
            "}"
            "load();setInterval(load,5000);"
            "window.addEventListener('click',e=>{"
              "if(e.target.dataset.s!==undefined){"
                "document.getElementById('ssid').value=e.target.dataset.s;"
                "document.getElementById('pass').value='';"
                "document.getElementById('addForm').style.display='block';"
                "document.getElementById('pass').focus();}"
            "});"
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
            "}");
  html += FPSTR(kScanScript);
  html += F("</script></div></body></html>");
  server.send(200, "text/html", html);
}

static void handleMqttPage() {
  oledShow("Web UI", "MQTT dibuka", server.client().remoteIP().toString().c_str(), 3000);

  String html;
  html.reserve(3500);
  html += FPSTR(kStyle);
  html += F("<title>SEMS MQTT</title></head><body><div class=wrap>"
            "<h1>SEMS AIoT &mdash; MQTT</h1>");
  html += FPSTR(kNav);
  html += F("<div class=card><div class=card-title>Status</div><div id=status>Loading...</div></div>"
            "<div class=card><div class=card-title>Konfigurasi Broker</div>"
            "<div class=toggle><input type=checkbox id=enabled><span>MQTT Aktif</span></div>"
            "<label>Host / IP Broker</label>"
            "<input type=text id=host placeholder='192.168.1.x'>"
            "<label>Port</label>"
            "<input type=number id=port value=1883 min=1 max=65535>"
            "<label>Username</label>"
            "<input type=text id=user placeholder='(kosong jika tidak ada)'>"
            "<label>Password</label>"
            "<input type=password id=pass placeholder='kosong = tidak berubah'>"
            "<label>Topic Prefix</label>"
            "<input type=text id=topic placeholder='sems'>"
            "<button class=btn style='width:100%;margin-top:14px' onclick=save()>Simpan</button>"
            "<div id=msg></div></div>"
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
                  "(d.host?d.host+':'+d.port:'Belum dikonfigurasi')+'</span>';}"
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
              "else{msg.className='err';msg.textContent='Error: '+(r.error||'unknown');}}"
            "load();setInterval(load,5000);"
            "</script></div></body></html>");
  server.send(200, "text/html", html);
}

// --- API handlers ---

static void handleScanStart() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
  server.send(202, "application/json", "{\"ok\":true}");
}

static void handleScanResult() {
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    server.send(200, "application/json", "{\"status\":\"scanning\"}"); return;
  }
  String body;
  body.reserve(512);
  body = "{\"status\":\"done\",\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i) body += ',';
    String s = WiFi.SSID(i); s.replace("\\","\\\\"); s.replace("\"","\\\"");
    body += "{\"ssid\":\""; body += s;
    body += "\",\"rssi\":"; body += WiFi.RSSI(i);
    body += ",\"ch\":";    body += WiFi.channel(i);
    body += ",\"security\":\"";
    body += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured";
    body += "\"}";
  }
  body += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", body);
}

static void handleNetworkApi() {
  char uptime[18];
  { uint32_t s=millis()/1000, m=s/60; s%=60; uint32_t h=m/60; m%=60;
    snprintf(uptime, sizeof(uptime), "%02luh%02lum%02lus", h, m, s); }

  String body;
  body.reserve(512);
  body  = "{\"wifi\":{\"connected\":"; body += staConnected?"true":"false";
  body += ",\"connecting\":"; body += staConnecting?"true":"false";
  if (staConnected||staConnecting) {
    String ss = savedSsid; ss.replace("\\","\\\\"); ss.replace("\"","\\\"");
    body += ",\"ssid\":\""; body += ss; body += "\"";
  }
  if (staConnected) {
    body += ",\"ip\":\""; body += WiFi.localIP().toString(); body += "\"";
    body += ",\"rssi\":";  body += WiFi.RSSI();
  }
  body += "},\"ap\":{\"active\":"; body += apStarted?"true":"false";
  if (apStarted) {
    body += ",\"ssid\":\""; body += WiFi.softAPSSID(); body += "\"";
    body += ",\"ip\":\"192.168.4.1\",\"clients\":"; body += WiFi.softAPgetStationNum();
  }
  body += "},\"lan\":{\"ready\":"; body += ethReady?"true":"false";
  body += ",\"link\":";            body += ethLink?"true":"false";
  if (ethReady) { body += ",\"ip\":\""; body += ethIp; body += "\""; }
  body += "},\"device\":{\"uptime\":\""; body += uptime;
  body += "\",\"heap\":"; body += ESP.getFreeHeap();
  body += ",\"fw\":\"" FW_VERSION "\"}}";
  server.send(200, "application/json", body);
}

static void handleMqttApi() {
  String body;
  body.reserve(256);
  body  = "{\"ok\":true,\"enabled\":"; body += mqttCfg.enabled?"true":"false";
  body += ",\"host\":\"";              body += mqttCfg.host;   body += "\"";
  body += ",\"port\":";               body += mqttCfg.port;
  body += ",\"user\":\"";             body += mqttCfg.user;   body += "\"";
  body += ",\"topic\":\"";            body += mqttCfg.topicPrefix; body += "\"";
  body += ",\"connected\":";          body += mqttConnected?"true":"false";
  body += ",\"state\":";              body += mqttClient.state();
  body += "}";
  server.send(200, "application/json", body);
}

static void handleMqttSave() {
  String body = server.arg("plain");
  mqttCfg.enabled     = jsonBool(body, "enabled");
  mqttCfg.host        = jsonExtract(body, "host");
  mqttCfg.port        = (uint16_t)jsonInt(body, "port", 1883);
  mqttCfg.user        = jsonExtract(body, "user");
  String np           = jsonExtract(body, "pass");
  if (!np.isEmpty()) mqttCfg.pass = np;
  mqttCfg.topicPrefix = jsonExtract(body, "topic");
  if (mqttCfg.topicPrefix.isEmpty()) mqttCfg.topicPrefix = "sems";
  saveMqttConfig();
  mqttClient.disconnect(); mqttConnected = false; mqttLastTryMs = 0;
  Serial.printf("[MQTT] Config saved: %s:%d en=%d\n", mqttCfg.host.c_str(), mqttCfg.port, mqttCfg.enabled);
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiSave() {
  String body = server.arg("plain");
  String ssid = jsonExtract(body, "ssid"), pass = jsonExtract(body, "pass");
  if (ssid.isEmpty()) { server.send(400,"application/json","{\"ok\":false,\"error\":\"ssid_required\"}"); return; }
  if (!addOrUpdateWifi(ssid, pass)) { server.send(400,"application/json","{\"ok\":false,\"error\":\"list_full\"}"); return; }
  server.send(200,"application/json","{\"ok\":true}");
}

static void handleWifiDelete() {
  String ssid = jsonExtract(server.arg("plain"), "ssid");
  if (ssid.isEmpty()) { server.send(400,"application/json","{\"ok\":false,\"error\":\"ssid_required\"}"); return; }
  removeWifi(ssid);
  server.send(200,"application/json","{\"ok\":true}");
}

static void handleWifiList() {
  String body; body.reserve(256);
  body = "{\"ok\":true,\"count\":"; body += wifiCount; body += ",\"networks\":[";
  for (uint8_t i = 0; i < wifiCount; i++) {
    if (i) body += ',';
    String s = wifiList[i].ssid; s.replace("\\","\\\\"); s.replace("\"","\\\"");
    body += "{\"ssid\":\""; body += s; body += "\"}";
  }
  body += "]}";
  server.send(200,"application/json",body);
}

static void handleReboot() {
  server.send(200,"application/json","{\"ok\":true}");
  delay(300); ESP.restart();
}

static void handleWifiClear() {
  clearAllWifi();
  server.send(200,"application/json","{\"ok\":true}");
}

// ============================================================
// Web server init
// ============================================================
static void startWebServer() {
  server.on("/",                HTTP_GET,  handleRoot);
  server.on("/network",         HTTP_GET,  handleNetworkPage);
  server.on("/mqtt",            HTTP_GET,  handleMqttPage);
  server.on("/api/network",     HTTP_GET,  handleNetworkApi);
  server.on("/api/mqtt",        HTTP_GET,  handleMqttApi);
  server.on("/api/scan",        HTTP_POST, handleScanStart);
  server.on("/api/scan/result", HTTP_GET,  handleScanResult);
  server.on("/api/wifi/save",   HTTP_POST, handleWifiSave);
  server.on("/api/wifi/delete", HTTP_POST, handleWifiDelete);
  server.on("/api/wifi/list",   HTTP_GET,  handleWifiList);
  server.on("/api/wifi/clear",  HTTP_POST, handleWifiClear);
  server.on("/api/mqtt/save",   HTTP_POST, handleMqttSave);
  server.on("/api/reboot",      HTTP_POST, handleReboot);
  server.on("/api/config-mode", HTTP_POST, [](){
    server.send(200,"application/json","{\"ok\":true}");
    enterConfigMode();
  });
  server.begin();
  if (MDNS.begin("sems")) {
    MDNS.addService("http","tcp",80);
    Serial.println("[mDNS] http://sems.local");
  }
  Serial.println("[Web] Server started on port 80");
}

// ============================================================
// setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(kLedPin, OUTPUT);
  pinMode(kBtnPin, INPUT_PULLUP);

  Wire.begin(22, 21);
  if (oled.begin()) {
    oledReady = true;
  } else {
    Serial.println("[OLED] Init failed");
  }

  Serial.println("\n=== SEMS AIoT " FW_VERSION " ===");
  oledShow("SEMS AIoT", "FW: " FW_VERSION, "Booting...", 2000);

  loadWifiList();
  loadMqttConfig();

  const bool wifiFailReboot   = (rtcWifiFailMagic == kWifiFailMagic);
  const bool configModeReboot = (rtcConfigMagic   == kConfigMagic);
  rtcWifiFailMagic = 0;
  rtcConfigMagic   = 0;

  // Register unified event handler BEFORE any WiFi/ETH init
  WiFi.onEvent(onNetworkEvent);

  // Manual hardware reset for W5500
  pinMode(kEthRst, OUTPUT);
  digitalWrite(kEthRst, LOW);
  delay(50);
  digitalWrite(kEthRst, HIGH);
  delay(100);

  // Init W5500 via native ETH.h using SPIClass in v3.x
  SPI.begin(18, 19, 23, kEthCs);
  if (ETH.begin(ETH_PHY_W5500, -1, kEthCs, -1, kEthRst, SPI)) {
    Serial.println("[ETH] W5500 init success");
  } else {
    Serial.println("[ETH] W5500 init FAILED!");
  }

  if (configModeReboot) {
    WiFi.mode(WIFI_AP);
    startAp();
    startWebServer();
    oledShow("Config Mode", "AP: 192.168.4.1", kApPass, 10000);
    Serial.println("[Boot] Config mode — AP only");

  } else if (wifiCount == 0 || wifiFailReboot) {
    WiFi.mode(WIFI_AP_STA);
    startAp();
    startWebServer();
    if (wifiCount == 0) {
      oledShow("No saved WiFi", "Open 192.168.4.1", "to configure", 8000);
    } else {
      beginStaConnect();
    }

  } else {
    // Normal boot: STA only — AP restored if STA fails (via event handler)
    WiFi.mode(WIFI_STA);
    startWebServer();
    beginStaConnect();
  }
}

// ============================================================
// loop() — clean, non-blocking
// ============================================================
void loop() {
  const uint32_t now = millis();

  server.handleClient();
  handleMqttLifecycle(now);
  processScanResult();
  handleStaTimeout(now);
  handleRebootCountdown(now);
  handleConfigButton(now);

  // LED heartbeat
  if (now - lastBlinkMs >= 500) {
    lastBlinkMs = now;
    ledState = !ledState;
    digitalWrite(kLedPin, ledState);
  }

  updateOled(now);
}
