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
#include <Update.h>
#include <NetworkClient.h>
#include <PubSubClient.h>
#include <time.h>

#define FW_VERSION "0.1.0"

// ============================================================
// Hardware constants
// ============================================================
static constexpr uint8_t  kLedPin      = 2;
static constexpr uint8_t  kBtnPin      = 0;   // BOOT button, active LOW
static constexpr uint32_t kBtnHoldMs   = 3000;
static constexpr uint8_t  kBtn2Pin     = 13;  // multifungsi: short=next page, long=toggle autoscroll

static constexpr int kEthCs  = 5;
static constexpr int kEthIrq = 27;
static constexpr int kEthRst = 26;

// ============================================================
// RS485 / Modbus
// ============================================================
static constexpr int kRs485Rx = 16;
static constexpr int kRs485Tx = 17;

static constexpr char kNvsModbus[] = "modbus";

// phase == 1: single-phase, phase == 3: three-phase
struct ModbusConfig {
  uint8_t  slaveId   = 1;
  uint32_t baud      = 19200;
  uint32_t pollMs    = 1000;
  uint8_t  phase     = 1;      // 1 or 3
  // 1-phase register map (FC03, FP32 big-endian, 0-based, PM2230 defaults)
  uint16_t r1V       = 3027;   // Voltage A-N
  uint16_t r1A       = 2999;   // Current A
  uint16_t r1Kw      = 3053;   // Active Power A
  uint16_t r1Kvar    = 3061;   // Reactive Power A
  uint16_t r1Kva     = 3069;   // Apparent Power A
  uint16_t r1Pf      = 3077;   // Power Factor A (4Q_FP_PF)
  uint16_t r1Kwh     = 2675;   // Active Energy Delivered (permanent)
  uint16_t r1Hz      = 3109;   // Frequency
  // 3-phase register map
  uint16_t r3Va      = 3027;   // Voltage A-N
  uint16_t r3Vb      = 3029;   // Voltage B-N
  uint16_t r3Vc      = 3031;   // Voltage C-N
  uint16_t r3Vll     = 3025;   // Voltage L-L Avg
  uint16_t r3Vln     = 3035;   // Voltage L-N Avg
  uint16_t r3Ia      = 2999;   // Current A
  uint16_t r3Ib      = 3001;   // Current B
  uint16_t r3Ic      = 3003;   // Current C
  uint16_t r3Iavg    = 3009;   // Current Avg
  uint16_t r3Pa      = 3053;   // Active Power A
  uint16_t r3Pb      = 3055;   // Active Power B
  uint16_t r3Pc      = 3057;   // Active Power C
  uint16_t r3Ptot    = 3059;   // Active Power Total
  uint16_t r3Qa      = 3061;   // Reactive Power A
  uint16_t r3Qb      = 3063;   // Reactive Power B
  uint16_t r3Qc      = 3065;   // Reactive Power C
  uint16_t r3Qtot    = 3067;   // Reactive Power Total
  uint16_t r3Sa      = 3069;   // Apparent Power A
  uint16_t r3Sb      = 3071;   // Apparent Power B
  uint16_t r3Sc      = 3073;   // Apparent Power C
  uint16_t r3Stot    = 3075;   // Apparent Power Total
  uint16_t r3Pfa     = 3077;   // Power Factor A (4Q_FP_PF)
  uint16_t r3Pfb     = 3079;   // Power Factor B (4Q_FP_PF)
  uint16_t r3Pfc     = 3081;   // Power Factor C (4Q_FP_PF)
  uint16_t r3Pftot   = 3191;   // Power Factor Total (FLOAT32)
  uint16_t r3Kwh     = 2675;   // Active Energy Delivered (permanent)
  uint16_t r3Hz      = 3109;   // Frequency
};
static ModbusConfig modbusCfg;

// ============================================================
// RTC state — survives soft reboot, NOT power-off
// ============================================================
RTC_DATA_ATTR static uint32_t rtcWifiFailMagic = 0;
RTC_DATA_ATTR static uint32_t rtcConfigMagic   = 0;
RTC_DATA_ATTR static uint32_t rtcWifiFailCount = 0;

static constexpr uint32_t kWifiFailMagic = 0xDEADF17E;
static constexpr uint32_t kConfigMagic   = 0xC0FEF00D;

// ============================================================
// WiFi credentials (NVS: namespace "wifi")
// ============================================================
static constexpr char    kNvsWifi[]     = "wifi";
static constexpr uint8_t kMaxSavedWifi = 5;
static constexpr char    kApSsidPfx[]  = "SEMS-SETUP";
static constexpr char    kApPass[]     = "sems1234";
static constexpr uint32_t kStaTimeoutMs = 25000;

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
static bool     configMode   = false;  // set at boot, stable for all pages
static uint8_t  wifiFailCycles = 0;    // full cycles attempted (all SSIDs tried once = 1 cycle)
static constexpr uint8_t kMaxFailCycles = 3; // activate AP after this many full cycles

// ============================================================
// FastConnect — cache BSSID+channel of last successful STA
// ============================================================
static constexpr char kNvsFast[] = "wfast";
struct FastConnCache { uint8_t bssid[6]; uint8_t ch; String ssid; };
static FastConnCache fcCache;
static bool          fcCacheValid = false;

static void fcLoad() {
  Preferences p; p.begin(kNvsFast, true);
  if (p.getBytesLength("bssid") == 6) {
    p.getBytes("bssid", fcCache.bssid, 6);
    fcCache.ch   = p.getUChar("ch", 0);
    fcCache.ssid = p.getString("ssid", "");
    fcCacheValid = (fcCache.ch > 0 && fcCache.ssid.length() > 0);
  }
  p.end();
  if (fcCacheValid) Serial.printf("[FC] Cache: %s ch%d\n", fcCache.ssid.c_str(), fcCache.ch);
}

static void fcSave(const uint8_t* bssid, uint8_t ch, const String& ssid) {
  memcpy(fcCache.bssid, bssid, 6);
  fcCache.ch   = ch;
  fcCache.ssid = ssid;
  fcCacheValid = true;
  Preferences p; p.begin(kNvsFast, false);
  p.putBytes("bssid", bssid, 6);
  p.putUChar("ch", ch);
  p.putString("ssid", ssid);
  p.end();
  Serial.printf("[FC] Saved: %s ch%d\n", ssid.c_str(), ch);
}

static void fcClear() {
  fcCacheValid = false;
  Preferences p; p.begin(kNvsFast, false);
  p.clear(); p.end();
  Serial.println("[FC] Cache cleared");
}

// ============================================================
// FreeRTOS Scan Task State
// ============================================================
static volatile bool scanTaskRunning = false;

// ============================================================
// MQTT — NetworkClient routes via lwIP best path automatically
// ============================================================
static NetworkClient netClient;
static PubSubClient  mqttClient(netClient);
static bool          mqttConnected  = false;
static uint32_t      mqttLastTryMs    = 0;
static uint32_t      mqttHealthLastMs = 0;
static uint32_t      mqttTxUntilMs   = 0;
static constexpr uint32_t kHealthIntervalMs = 30000;
static String        mqttDeviceId;   // "AABBCCDD1122" — set in setup()

// ============================================================
// Modbus / Meter data
// ============================================================
struct MeterData1Ph {
  float v    = 0;   // Voltage A-N   V
  float a    = 0;   // Current A     A
  float kw   = 0;   // Active Power  kW
  float kvar = 0;   // Reactive Power kVAR
  float kva  = 0;   // Apparent Power kVA
  float pf   = 0;   // Power Factor
  float kwh  = 0;   // Active Energy Delivered  kWh
  float hz   = 0;   // Frequency     Hz
  bool     valid  = false;
  uint32_t lastMs = 0;
};

struct MeterData3Ph {
  // Voltage
  float va = 0, vb = 0, vc = 0;    // V A-N, B-N, C-N
  float vll = 0, vln = 0;           // L-L avg, L-N avg
  // Current
  float ia = 0, ib = 0, ic = 0, iavg = 0;
  // Active Power
  float pa = 0, pb = 0, pc = 0, ptot = 0;   // kW
  // Reactive Power
  float qa = 0, qb = 0, qc = 0, qtot = 0;   // kVAR
  // Apparent Power
  float sa = 0, sb = 0, sc = 0, stot = 0;   // kVA
  // Power Factor (FLOAT32 total, 4Q per-phase decoded)
  float pfa = 0, pfb = 0, pfc = 0, pftot = 0;
  // Energy + Freq
  float kwh = 0;   // Active Energy Delivered kWh
  float hz  = 0;   // Frequency Hz
  bool     valid  = false;
  uint32_t lastMs = 0;
};

static MeterData1Ph  meter1;
static MeterData3Ph  meter3;
static uint32_t      modbusLastPollMs = 0;
static uint32_t      mqttMeterLastMs  = 0;
static constexpr uint32_t kMeterPublishMs = 5000;

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
static uint32_t btnPressedMs  = 0;
static uint32_t btn2PressedMs = 0;

// ============================================================
// OLED autoscroll
// ============================================================
static bool oledAutoScroll = true;

// ============================================================
// NTP / RTC
// ============================================================
static bool     ntpSynced   = false;
static uint32_t ntpLastSync = 0;
static constexpr uint32_t kNtpResyncMs = 3600000UL; // resync every 1h
static constexpr char kNtpServer[] = "pool.ntp.org";
static constexpr long kTzOffset    = 7 * 3600; // WIB UTC+7

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
void mqttPublish(const char* subtopic, const String& payload);
void ntpBeginSync();
void getTimeStr(char* buf, size_t len);

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
      rtcWifiFailMagic = 0;
      rtcWifiFailCount = 0;
      wifiFailCycles = 0;   // reset fail counter on success
      scanPending   = false; // Cancel background scans immediately
      fcSave(WiFi.BSSID(), WiFi.channel(), savedSsid);  // FastConnect cache
      if (!ethReady) WiFi.STA.setDefault();  // use WiFi only when LAN absent
      Serial.printf("[WiFi] Connected: %s  IP: %s\n",
                    savedSsid.c_str(),
                    WiFi.localIP().toString().c_str());
      // AP off once STA is up (only when in Normal Mode / STA only)
      if (!configMode) {
        WiFi.softAPdisconnect(true);
        apStarted = false;
      }
      oledShow("WiFi Connected", WiFi.localIP().toString().c_str(), savedSsid.c_str(), 6000);
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (staConnected) {
        staConnected = false;
        Serial.println("[WiFi] STA disconnected, retrying in background...");
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
  char timeBuf[8];
  getTimeStr(timeBuf, sizeof(timeBuf));
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "SEMS SYSTEM");
  oledDrawRight(timeBuf, 10);
  oled.drawHLine(0, 14, 128);
  
  // Blue Zone Content
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
    uint8_t anim = (millis() / 400) % 4;
    oled.drawBox(2, 42, 2, 4);
    if (anim >= 1) oled.drawBox(5, 38, 2, 8);
    if (anim >= 2) oled.drawBox(8, 34, 2, 12);
    if (anim >= 3) oled.drawBox(11, 30, 2, 16);
    
    oled.drawStr(20, 28, "Connecting...");
    String ss = savedSsid.length() > 17 ? savedSsid.substring(0,16)+"~" : savedSsid;
    oled.drawStr(20, 42, ss.c_str());
  } else {
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

static void oledDrawOverlay(uint32_t now) {
  // Heartbeat dot — pojok kanan atas (x=122..127)
  oled.setDrawColor(0); oled.drawBox(122, 57, 6, 7);
  oled.setDrawColor(1);
  if (oledHbTick & 1) oled.drawBox(123, 58, 4, 5);

  // MQTT indicator — kanan bawah, kiri heartbeat (x=108..121)
  oled.setDrawColor(0); oled.drawBox(108, 57, 14, 7);
  oled.setDrawColor(1);
  if (mqttCfg.enabled) {
    if (now < mqttTxUntilMs) {
      // TX aktif: ikon broadcast (node + gelombang melebar ke kanan)
      oled.drawBox(108, 59, 2, 3);  // titik transmitter
      oled.drawVLine(112, 60,  1);  // gelombang 1 (1px)
      oled.drawVLine(114, 59,  3);  // gelombang 2 (3px)
      oled.drawVLine(116, 58,  5);  // gelombang 3 (5px)
      oled.drawVLine(118, 57,  7);  // gelombang 4 (7px penuh)
    } else if (mqttConnected) {
      oled.drawBox(113, 59, 5, 4);   // connected: kotak padat
    } else {
      oled.drawFrame(113, 59, 5, 4); // disconnected: kotak kosong
    }
  }
  oled.setDrawColor(1);
}

static void drawPageMqtt() {
  // Yellow Zone Header
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "MQTT BROKER");
  oled.drawHLine(0, 14, 128);

  oled.setFont(u8g2_font_6x12_tf);
  if (!mqttCfg.enabled) {
    oled.drawStr(10, 38, "MQTT Disabled");
    oled.drawHLine(0, 54, 128);
    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(2, 62, "Enable via web /mqtt");
    return;
  }

  // Ikon status: kotak padat = connected, kerangka = offline
  if (mqttConnected) {
    oled.drawBox(2, 19, 12, 10);
    oled.setDrawColor(0);
    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(4, 28, "ON");
    oled.setDrawColor(1);
    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(18, 27, "Connected");
  } else {
    oled.drawFrame(2, 19, 12, 10);
    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(4, 28, "--");
    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(18, 27, "Offline");
  }

  // Host (truncate kalau panjang)
  String h = mqttCfg.host;
  if (h.length() > 17) h = h.substring(0, 16) + "~";
  oled.drawStr(2, 39, h.c_str());

  // Port & topic
  char portBuf[24];
  snprintf(portBuf, sizeof(portBuf), ":%d  [%s]", mqttCfg.port, mqttCfg.topicPrefix.c_str());
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(2, 49, portBuf);

  // Status bar
  oled.drawHLine(0, 54, 128);
  oled.drawStr(2, 62, mqttConnected ? "Broker reachable" : "Reconnecting...");
}

static void oledDrawPage(uint8_t page) {
  oled.clearBuffer();
  switch (page % 4) {
    case 0: drawPageDevice(); break;
    case 1: drawPageWifi();   break;
    case 2: drawPageLan();    break;
    case 3: drawPageMqtt();   break;
  }
  oledDrawOverlay(millis());
  oled.sendBuffer();
}

static void updateOled(uint32_t now) {
  if (!oledReady) return;

  bool hbTick = (now - oledHbMs >= 500);
  if (hbTick) { oledHbMs = now; oledHbTick++; }

  if (now < oledUntilMs) {
    if (hbTick) { oledDrawOverlay(now); oled.sendBuffer(); }
    return;
  }

  bool needRedraw = false;
  if (!oledAutoScroll) {
    needRedraw = hbTick;
  } else if (now - oledPageMs >= kPageIntervalMs) {
    oledPageMs = now;
    oledPage = (oledPage + 1) % 4;  // 4 pages: device, wifi, lan, mqtt
    needRedraw = true;
  } else {
    needRedraw = hbTick;
  }

  if (needRedraw) {
    oled.clearBuffer();
    switch (oledPage % 4) {
      case 0: drawPageDevice(); break;
      case 1: drawPageWifi();   break;
      case 2: drawPageLan();    break;
      case 3: drawPageMqtt();   break;
    }
    oledDrawOverlay(now);
    oled.sendBuffer();
  }
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
// NVS — Modbus config
// ============================================================
static void loadModbusConfig() {
  Preferences p; p.begin(kNvsModbus, true);
  modbusCfg.slaveId = (uint8_t)p.getUChar("slave",  1);
  modbusCfg.baud    = p.getULong("baud",    19200);
  modbusCfg.pollMs  = p.getULong("poll",    1000);
  modbusCfg.phase   = (uint8_t)p.getUChar("phase",  1);
  // 1-phase
  modbusCfg.r1V     = p.getUShort("1V",    3027);
  modbusCfg.r1A     = p.getUShort("1A",    2999);
  modbusCfg.r1Kw    = p.getUShort("1Kw",   3053);
  modbusCfg.r1Kvar  = p.getUShort("1Kvar", 3061);
  modbusCfg.r1Kva   = p.getUShort("1Kva",  3069);
  modbusCfg.r1Pf    = p.getUShort("1Pf",   3077);
  modbusCfg.r1Kwh   = p.getUShort("1Kwh",  2675);
  modbusCfg.r1Hz    = p.getUShort("1Hz",   3109);
  // 3-phase
  modbusCfg.r3Va    = p.getUShort("3Va",   3027);
  modbusCfg.r3Vb    = p.getUShort("3Vb",   3029);
  modbusCfg.r3Vc    = p.getUShort("3Vc",   3031);
  modbusCfg.r3Vll   = p.getUShort("3Vll",  3025);
  modbusCfg.r3Vln   = p.getUShort("3Vln",  3035);
  modbusCfg.r3Ia    = p.getUShort("3Ia",   2999);
  modbusCfg.r3Ib    = p.getUShort("3Ib",   3001);
  modbusCfg.r3Ic    = p.getUShort("3Ic",   3003);
  modbusCfg.r3Iavg  = p.getUShort("3Iavg", 3009);
  modbusCfg.r3Pa    = p.getUShort("3Pa",   3053);
  modbusCfg.r3Pb    = p.getUShort("3Pb",   3055);
  modbusCfg.r3Pc    = p.getUShort("3Pc",   3057);
  modbusCfg.r3Ptot  = p.getUShort("3Pt",   3059);
  modbusCfg.r3Qa    = p.getUShort("3Qa",   3061);
  modbusCfg.r3Qb    = p.getUShort("3Qb",   3063);
  modbusCfg.r3Qc    = p.getUShort("3Qc",   3065);
  modbusCfg.r3Qtot  = p.getUShort("3Qt",   3067);
  modbusCfg.r3Sa    = p.getUShort("3Sa",   3069);
  modbusCfg.r3Sb    = p.getUShort("3Sb",   3071);
  modbusCfg.r3Sc    = p.getUShort("3Sc",   3073);
  modbusCfg.r3Stot  = p.getUShort("3St",   3075);
  modbusCfg.r3Pfa   = p.getUShort("3Pfa",  3077);
  modbusCfg.r3Pfb   = p.getUShort("3Pfb",  3079);
  modbusCfg.r3Pfc   = p.getUShort("3Pfc",  3081);
  modbusCfg.r3Pftot = p.getUShort("3Pft",  3191);
  modbusCfg.r3Kwh   = p.getUShort("3Kwh",  2675);
  modbusCfg.r3Hz    = p.getUShort("3Hz",   3109);
  p.end();
  Serial.printf("[NVS] Modbus slave=%d baud=%lu poll=%lums phase=%d\n",
    modbusCfg.slaveId, modbusCfg.baud, modbusCfg.pollMs, modbusCfg.phase);
}

static void saveModbusConfig() {
  Preferences p; p.begin(kNvsModbus, false);
  p.putUChar("slave",   modbusCfg.slaveId);
  p.putULong("baud",    modbusCfg.baud);
  p.putULong("poll",    modbusCfg.pollMs);
  p.putUChar("phase",   modbusCfg.phase);
  p.putUShort("1V",     modbusCfg.r1V);
  p.putUShort("1A",     modbusCfg.r1A);
  p.putUShort("1Kw",    modbusCfg.r1Kw);
  p.putUShort("1Kvar",  modbusCfg.r1Kvar);
  p.putUShort("1Kva",   modbusCfg.r1Kva);
  p.putUShort("1Pf",    modbusCfg.r1Pf);
  p.putUShort("1Kwh",   modbusCfg.r1Kwh);
  p.putUShort("1Hz",    modbusCfg.r1Hz);
  p.putUShort("3Va",    modbusCfg.r3Va);
  p.putUShort("3Vb",    modbusCfg.r3Vb);
  p.putUShort("3Vc",    modbusCfg.r3Vc);
  p.putUShort("3Vll",   modbusCfg.r3Vll);
  p.putUShort("3Vln",   modbusCfg.r3Vln);
  p.putUShort("3Ia",    modbusCfg.r3Ia);
  p.putUShort("3Ib",    modbusCfg.r3Ib);
  p.putUShort("3Ic",    modbusCfg.r3Ic);
  p.putUShort("3Iavg",  modbusCfg.r3Iavg);
  p.putUShort("3Pa",    modbusCfg.r3Pa);
  p.putUShort("3Pb",    modbusCfg.r3Pb);
  p.putUShort("3Pc",    modbusCfg.r3Pc);
  p.putUShort("3Pt",    modbusCfg.r3Ptot);
  p.putUShort("3Qa",    modbusCfg.r3Qa);
  p.putUShort("3Qb",    modbusCfg.r3Qb);
  p.putUShort("3Qc",    modbusCfg.r3Qc);
  p.putUShort("3Qt",    modbusCfg.r3Qtot);
  p.putUShort("3Sa",    modbusCfg.r3Sa);
  p.putUShort("3Sb",    modbusCfg.r3Sb);
  p.putUShort("3Sc",    modbusCfg.r3Sc);
  p.putUShort("3St",    modbusCfg.r3Stot);
  p.putUShort("3Pfa",   modbusCfg.r3Pfa);
  p.putUShort("3Pfb",   modbusCfg.r3Pfb);
  p.putUShort("3Pfc",   modbusCfg.r3Pfc);
  p.putUShort("3Pft",   modbusCfg.r3Pftot);
  p.putUShort("3Kwh",   modbusCfg.r3Kwh);
  p.putUShort("3Hz",    modbusCfg.r3Hz);
  p.end();
}

// ============================================================
// MQTT lifecycle
// ============================================================
// ============================================================
// Modbus RTU helpers
// ============================================================
static uint16_t modbusCrc(const uint8_t* buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  }
  return crc;
}

static float bytesToFloat(const uint8_t* b) {
  // big-endian IEEE 754: b[0]=MSB b[1] b[2] b[3]=LSB
  uint32_t u = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
             | ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
  float f; memcpy(&f, &u, 4);
  return f;
}

// Send FC04 request, return true + fill 4 bytes into out[] on success
static bool modbusRead2Regs(uint8_t slaveId, uint16_t regAddr, uint8_t out[4]) {
  uint8_t req[8];
  req[0] = slaveId;
  req[1] = 0x03;           // FC03 Read Holding Registers
  req[2] = regAddr >> 8;
  req[3] = regAddr & 0xFF;
  req[4] = 0x00; req[5] = 0x02;  // quantity = 2 registers = 4 bytes
  uint16_t crc = modbusCrc(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  while (Serial2.available()) Serial2.read();  // flush
  Serial2.write(req, 8);
  Serial2.flush();

  // expect 9 bytes: slaveId + FC + byteCount + 4 data + 2 CRC
  uint32_t t = millis();
  while (Serial2.available() < 9 && millis() - t < 200);
  if (Serial2.available() < 9) return false;

  uint8_t resp[9];
  Serial2.readBytes(resp, 9);

  uint16_t rcrc = modbusCrc(resp, 7);
  if (resp[7] != (rcrc & 0xFF) || resp[8] != (rcrc >> 8)) return false;
  if (resp[0] != slaveId || resp[1] != 0x03 || resp[2] != 4) return false;

  memcpy(out, &resp[3], 4);
  return true;
}

// Decode 4Q Floating Point Power Factor (Schneider PM2xxx format) → signed float -1..1
static float decode4QFpPf(float reg) {
  if (reg > 1.0f)       return  2.0f - reg;  // leading
  else if (reg < -1.0f) return -2.0f - reg;  // leading
  return reg;                                  // lagging or unity
}

// Macro: read 2 holding regs into float; on fail set ok=false and skip rest
#define MB_READ(field, reg) \
  if (ok && modbusRead2Regs(modbusCfg.slaveId, (reg), raw)) \
    (field) = bytesToFloat(raw); \
  else ok = false;

#define MB_READ4Q(field, reg) \
  if (ok && modbusRead2Regs(modbusCfg.slaveId, (reg), raw)) \
    (field) = decode4QFpPf(bytesToFloat(raw)); \
  else ok = false;

static void handleModbus(uint32_t now) {
  if (now - modbusLastPollMs < modbusCfg.pollMs) return;
  modbusLastPollMs = now;

  uint8_t raw[4];
  bool ok = true;
  const uint8_t sl = modbusCfg.slaveId;

  if (modbusCfg.phase == 1) {
    // ── Single-phase ──────────────────────────────────────
    MeterData1Ph m;
    MB_READ(m.v,    modbusCfg.r1V)
    MB_READ(m.a,    modbusCfg.r1A)
    MB_READ(m.kw,   modbusCfg.r1Kw)
    MB_READ(m.kvar, modbusCfg.r1Kvar)
    MB_READ(m.kva,  modbusCfg.r1Kva)
    MB_READ4Q(m.pf, modbusCfg.r1Pf)
    MB_READ(m.kwh,  modbusCfg.r1Kwh)
    MB_READ(m.hz,   modbusCfg.r1Hz)
    if (ok) {
      m.valid = true; m.lastMs = now;
      meter1 = m;
      meter3.valid = false;
      Serial.printf("[MB1] V=%.1f A=%.3f kW=%.3f kVAR=%.3f kVA=%.3f PF=%.3f kWh=%.3f Hz=%.2f\n",
        m.v, m.a, m.kw, m.kvar, m.kva, m.pf, m.kwh, m.hz);
    } else {
      meter1.valid = false;
      Serial.println("[MB1] Poll failed");
    }

  } else {
    // ── Three-phase ───────────────────────────────────────
    MeterData3Ph m;
    // Voltage
    MB_READ(m.va,   modbusCfg.r3Va)
    MB_READ(m.vb,   modbusCfg.r3Vb)
    MB_READ(m.vc,   modbusCfg.r3Vc)
    MB_READ(m.vll,  modbusCfg.r3Vll)
    MB_READ(m.vln,  modbusCfg.r3Vln)
    // Current
    MB_READ(m.ia,   modbusCfg.r3Ia)
    MB_READ(m.ib,   modbusCfg.r3Ib)
    MB_READ(m.ic,   modbusCfg.r3Ic)
    MB_READ(m.iavg, modbusCfg.r3Iavg)
    // Active Power
    MB_READ(m.pa,   modbusCfg.r3Pa)
    MB_READ(m.pb,   modbusCfg.r3Pb)
    MB_READ(m.pc,   modbusCfg.r3Pc)
    MB_READ(m.ptot, modbusCfg.r3Ptot)
    // Reactive Power
    MB_READ(m.qa,   modbusCfg.r3Qa)
    MB_READ(m.qb,   modbusCfg.r3Qb)
    MB_READ(m.qc,   modbusCfg.r3Qc)
    MB_READ(m.qtot, modbusCfg.r3Qtot)
    // Apparent Power
    MB_READ(m.sa,   modbusCfg.r3Sa)
    MB_READ(m.sb,   modbusCfg.r3Sb)
    MB_READ(m.sc,   modbusCfg.r3Sc)
    MB_READ(m.stot, modbusCfg.r3Stot)
    // Power Factor
    MB_READ4Q(m.pfa,  modbusCfg.r3Pfa)
    MB_READ4Q(m.pfb,  modbusCfg.r3Pfb)
    MB_READ4Q(m.pfc,  modbusCfg.r3Pfc)
    MB_READ(m.pftot,  modbusCfg.r3Pftot)
    // Energy + Freq
    MB_READ(m.kwh,  modbusCfg.r3Kwh)
    MB_READ(m.hz,   modbusCfg.r3Hz)
    if (ok) {
      m.valid = true; m.lastMs = now;
      meter3 = m;
      meter1.valid = false;
      Serial.printf("[MB3] Va=%.1f Vb=%.1f Vc=%.1f Ia=%.3f Ib=%.3f Ic=%.3f Ptot=%.3f kWh=%.3f Hz=%.2f\n",
        m.va, m.vb, m.vc, m.ia, m.ib, m.ic, m.ptot, m.kwh, m.hz);
    } else {
      meter3.valid = false;
      Serial.println("[MB3] Poll failed");
    }
  }
}

#undef MB_READ
#undef MB_READ4Q

static void publishMeter(uint32_t now) {
  if (!mqttConnected) return;
  if (now - mqttMeterLastMs < kMeterPublishMs) return;

  if (modbusCfg.phase == 1 && meter1.valid) {
    mqttMeterLastMs = now;
    char buf[192];
    snprintf(buf, sizeof(buf),
      "{\"v\":%.1f,\"a\":%.3f,\"kw\":%.3f,\"kvar\":%.3f,\"kva\":%.3f,\"pf\":%.3f,\"kwh\":%.3f,\"hz\":%.2f}",
      meter1.v, meter1.a, meter1.kw, meter1.kvar, meter1.kva, meter1.pf, meter1.kwh, meter1.hz);
    mqttPublish("meter", buf);

  } else if (modbusCfg.phase == 3 && meter3.valid) {
    mqttMeterLastMs = now;
    // split into two topics to stay under 256-byte MQTT payload limit
    char buf[256];
    snprintf(buf, sizeof(buf),
      "{\"va\":%.1f,\"vb\":%.1f,\"vc\":%.1f,\"vll\":%.1f,\"vln\":%.1f,"
       "\"ia\":%.3f,\"ib\":%.3f,\"ic\":%.3f,\"iavg\":%.3f,"
       "\"pa\":%.3f,\"pb\":%.3f,\"pc\":%.3f,\"ptot\":%.3f}",
      meter3.va, meter3.vb, meter3.vc, meter3.vll, meter3.vln,
      meter3.ia, meter3.ib, meter3.ic, meter3.iavg,
      meter3.pa, meter3.pb, meter3.pc, meter3.ptot);
    mqttPublish("meter/vi", buf);

    snprintf(buf, sizeof(buf),
      "{\"qa\":%.3f,\"qb\":%.3f,\"qc\":%.3f,\"qtot\":%.3f,"
       "\"sa\":%.3f,\"sb\":%.3f,\"sc\":%.3f,\"stot\":%.3f,"
       "\"pfa\":%.3f,\"pfb\":%.3f,\"pfc\":%.3f,\"pftot\":%.3f,"
       "\"kwh\":%.3f,\"hz\":%.2f}",
      meter3.qa, meter3.qb, meter3.qc, meter3.qtot,
      meter3.sa, meter3.sb, meter3.sc, meter3.stot,
      meter3.pfa, meter3.pfb, meter3.pfc, meter3.pftot,
      meter3.kwh, meter3.hz);
    mqttPublish("meter/pq", buf);
  }
}

// ============================================================
// MQTT publish
// ============================================================
void mqttPublish(const char* subtopic, const String& payload) {
  if (!mqttConnected) return;
  String topic = mqttCfg.topicPrefix + "/" + mqttDeviceId + "/" + subtopic;
  bool ok = mqttClient.publish(topic.c_str(), payload.c_str());
  Serial.printf("[MQTT] publish %s (%d bytes) %s\n",
    topic.c_str(), payload.length(), ok ? "OK" : "FAIL");
  if (ok) mqttTxUntilMs = millis() + 1000;
}

static void publishHealth(uint32_t now) {
  if (!mqttConnected) return;
  if (now - mqttHealthLastMs < kHealthIntervalMs) return;
  mqttHealthLastMs = now;

  char buf[256];
  char timeBuf[8]; getTimeStr(timeBuf, sizeof(timeBuf));

  // ip: aktif interface (ETH prioritas)
  const char* ip   = ethReady    ? ethIp.c_str()
                   : staConnected ? WiFi.localIP().toString().c_str()
                   : "0.0.0.0";
  const char* iface = ethReady ? "eth" : staConnected ? "wifi" : "none";

  snprintf(buf, sizeof(buf),
    "{\"up\":%lu,\"heap\":%lu,\"rssi\":%d,\"ip\":\"%s\",\"if\":\"%s\",\"time\":\"%s\"}",
    millis() / 1000,
    (unsigned long)ESP.getFreeHeap(),
    staConnected ? WiFi.RSSI() : 0,
    ip, iface, timeBuf);

  mqttPublish("health", buf);
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
  mqttClient.setBufferSize(512);
  String id = "sems-" + WiFi.macAddress(); id.replace(":", "");
  String lwtTopic = mqttCfg.topicPrefix + "/" + mqttDeviceId + "/status";
  const char* lwtMsg = "{\"online\":false}";
  bool ok = mqttCfg.user.isEmpty()
    ? mqttClient.connect(id.c_str(), lwtTopic.c_str(), 0, true, lwtMsg)
    : mqttClient.connect(id.c_str(), mqttCfg.user.c_str(), mqttCfg.pass.c_str(), lwtTopic.c_str(), 0, true, lwtMsg);

  if (ok) {
    mqttConnected = true;
    mqttHealthLastMs = 0;  // publish health segera setelah connect
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
  if (found <= 0) {
    if (found == WIFI_SCAN_FAILED) {
      Serial.println("[WiFi] Scan FAILED!");
    } else {
      Serial.println("[WiFi] Scan finished, 0 networks found.");
    }
    WiFi.scanDelete();
    return -1;
  }

  Serial.printf("[WiFi] Scan found %d networks:\n", found);
  for (int s = 0; s < found; s++) {
    Serial.printf("  - %s (%d dBm) ch:%d sec:%s\n",
                  WiFi.SSID(s).c_str(),
                  WiFi.RSSI(s),
                  WiFi.channel(s),
                  WiFi.encryptionType(s) == WIFI_AUTH_OPEN ? "open" : "secured");
  }

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
    Serial.printf("[WiFi] Best match: [%d] %s (%ddBm)\n", bestIdx, wifiList[bestIdx].ssid.c_str(), bestRssi);
  else
    Serial.println("[WiFi] No saved networks matched the scan results.");
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
  WiFi.disconnect(false);

  // FastConnect: jika cache valid dan SSID cocok, langsung connect tanpa scan
  if (fcCacheValid && fcCache.ssid == wifiList[idx].ssid) {
    Serial.printf("[FC] Fast connect → %s ch%d %s\n", savedSsid.c_str(), fcCache.ch, attempt);
    oledShow("WiFi FastConnect", savedSsid.c_str(), attempt, 15000);
    WiFi.begin(wifiList[idx].ssid.c_str(), wifiList[idx].pass.c_str(),
               fcCache.ch, fcCache.bssid);
  } else {
    Serial.printf("[WiFi] → %s %s\n", savedSsid.c_str(), attempt);
    oledShow("WiFi Connecting", savedSsid.c_str(), attempt, 15000);
    WiFi.begin(wifiList[idx].ssid.c_str(), wifiList[idx].pass.c_str());
  }

  staConnecting = true;
  staStartMs    = millis();
  scanPending   = false;
}

void beginStaConnect() {
  triedMask      = 0;
  scanRetryCount = 0;
  scanPending    = false;
  WiFi.disconnect(false);
  if (wifiCount > 0) {
    connectToEntry(0);
  } else {
    Serial.println("[WiFi] No saved networks to connect.");
  }
}

// Reset fail-cycle counter on successful connection
static void resetWifiFailCycles() {
  wifiFailCycles = 0;
}

static void scheduleReboot() {
  Serial.println("[WiFi] Retrying from start...");
  beginStaConnect();
}

static void processScanResult() {
  // Background scanning is disabled in Normal Mode.
}

static void handleStaTimeout(uint32_t now) {
  if (configMode) return;  // config mode: biarkan user yang trigger connect via web UI

  // 1. Watchdog: staConnected tapi IP hilang atau status bukan WL_CONNECTED
  if (staConnected && !staConnecting) {
    bool stale = (WiFi.status() != WL_CONNECTED) ||
                 (WiFi.localIP() == IPAddress(0, 0, 0, 0));
    if (stale) {
      Serial.println("[WiFi] Stale connected state detected (no IP), reconnecting...");
      staConnected = false;
      WiFi.disconnect(false);
      beginStaConnect();
      return;
    }
  }

  // 2. Timeout saat connecting
  if (staConnecting) {
    if (now - staStartMs < kStaTimeoutMs) return;
    staConnecting = false;
    Serial.printf("[WiFi] Timeout/Failed: %s\n", savedSsid.c_str());
    WiFi.disconnect(false);
    // Jika gagal dengan FastConnect cache, hapus — mungkin AP sudah ganti channel/BSSID
    if (fcCacheValid && fcCache.ssid == savedSsid) fcClear();

    // Coba network berikutnya yang belum dicoba
    int nextIdx = -1;
    for (uint8_t i = 0; i < wifiCount; i++) {
      if (!(triedMask & (1 << i))) {
        nextIdx = i;
        break;
      }
    }
    if (nextIdx >= 0) {
      connectToEntry(nextIdx);
    } else {
      wifiFailCycles++;
      Serial.printf("[WiFi] All saved networks tried. Cycle #%d\n", wifiFailCycles);

      if (wifiFailCycles >= kMaxFailCycles) {
        // After N full cycles, enable AP so user can reconfigure
        Serial.printf("[WiFi] %d cycles failed — activating AP mode\n", wifiFailCycles);
        oledShow("WiFi Gagal", "AP Aktif", "192.168.4.1", 5000);
        restoreAp();
        // Keep retrying STA in background — reset cycle counter so AP isn't re-triggered every cycle
        wifiFailCycles = 0;
      } else {
        char buf[24];
        snprintf(buf, sizeof(buf), "Coba ulang %d/%d", wifiFailCycles, kMaxFailCycles);
        oledShow("WiFi Offline", buf, "Retrying...", 4000);
      }
      beginStaConnect();
    }
    return;
  }

  // 3. Idle watchdog: tidak ada proses koneksi dan tidak connected → retry setelah 30 detik
  static uint32_t idleSinceMs = 0;
  if (!staConnected && !staConnecting && wifiCount > 0) {
    if (idleSinceMs == 0) idleSinceMs = now;
    else if (now - idleSinceMs > 30000) {
      Serial.println("[WiFi] Idle watchdog: not connected, starting connect cycle.");
      idleSinceMs = 0;
      beginStaConnect();
    }
  } else {
    idleSinceMs = 0;  // reset timer selagi connected atau connecting
  }
}

// ============================================================
// FreeRTOS Task for Synchronous Scanning (to prevent lockups)
// ============================================================
static void wifiScanTask(void *pvParameters) {
  scanTaskRunning = true;
  Serial.println("[Scan Task] Initiating synchronous WiFi scan in Config Mode...");
  WiFi.scanDelete();
  int res = WiFi.scanNetworks(false, false);
  Serial.printf("[Scan Task] Scan completed. Found: %d networks.\n", res);
  scanTaskRunning = false;
  vTaskDelete(NULL);
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
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  const String ssid = getApSsid();
  WiFi.softAP(ssid.c_str(), kApPass);
  apStarted = true;
  Serial.println("[AP] Restored");
}

// ============================================================
// Config mode
// ============================================================
void enterConfigMode() {
  Preferences p;
  p.begin(kNvsWifi, false);
  p.putBool("cfgMode", true);
  p.end();
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

// GPIO13 — short press: next OLED page; long press (3s): toggle autoscroll
static void handleBtn2(uint32_t now) {
  const bool pressed = (digitalRead(kBtn2Pin) == LOW);
  if (pressed) {
    if (btn2PressedMs == 0) btn2PressedMs = now;
    else if (now - btn2PressedMs >= kBtnHoldMs) {
      btn2PressedMs = 0;
      oledAutoScroll = !oledAutoScroll;
      oledShow(oledAutoScroll ? "Display" : "Display",
               oledAutoScroll ? "Auto Scroll ON" : "Auto Scroll OFF",
               "", 2000);
    }
  } else {
    if (btn2PressedMs > 0 && now - btn2PressedMs < kBtnHoldMs) {
      // short press: advance page immediately
      oledPage = (oledPage + 1) % 3;
      oledPageMs = now;
      oledDrawPage(oledPage);
    }
    btn2PressedMs = 0;
  }
}

// ============================================================
// NTP
// ============================================================
void ntpBeginSync() {
  configTime(kTzOffset, 0, kNtpServer);
  ntpLastSync = millis();
  Serial.println("[NTP] Sync started");
}

static void handleNtp(uint32_t now) {
  if (!staConnected && !ethReady) return;
  if (ntpSynced && now - ntpLastSync < kNtpResyncMs) return;
  // Check if time is valid (year > 2020)
  struct tm ti;
  if (getLocalTime(&ti, 0) && ti.tm_year > 120) {
    if (!ntpSynced) {
      ntpSynced = true;
      char buf[32];
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d WIB", ti.tm_hour, ti.tm_min, ti.tm_sec);
      oledShow("NTP Synced", buf, "", 3000);
      Serial.printf("[NTP] Synced: %s\n", buf);
    }
    ntpLastSync = now;
  } else if (ntpLastSync == 0 || now - ntpLastSync > 10000) {
    ntpBeginSync();
  }
}

// Helper: get time string "HH:MM" or "--:--" if not synced
void getTimeStr(char* buf, size_t len) {
  if (!ntpSynced) { snprintf(buf, len, "--:--"); return; }
  struct tm ti;
  if (getLocalTime(&ti, 0)) snprintf(buf, len, "%02d:%02d", ti.tm_hour, ti.tm_min);
  else snprintf(buf, len, "--:--");
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

static String getNav() {
  String nav;
  nav.reserve(384);
  nav = "<div class=nav>";
  if (!configMode) {
    nav += "<a href=/>&#127968; Home</a>";
  }
  nav += "<a href=/network>&#127760; Network</a>";
  nav += "<a href=/mqtt>&#128236; MQTT</a>";
  nav += "<a href=/modbus>&#128268; Modbus</a>";
  nav += "<a href=/update>&#128229; OTA</a>";
  if (configMode) {
    nav += "<a href='#' onclick='if(confirm(\"Reboot device ke Normal Mode?\"))fetch(\"/api/reboot\",{method:\"POST\"})' style='background:#fee2e2;color:#b91c1c;margin-left:auto'>&#8635; Reboot (Normal Mode)</a>";
  }
  nav += "</div>";
  return nav;
}

static const char kScanScript[] PROGMEM =
  "function eh(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));}"
  "async function goToConfigMode(){"
    "if(!confirm('Masuk Config Mode? Device akan restart dan broadcast AP.'))return;"
    "await fetch('/api/config-mode',{method:'POST'});"
    "document.body.innerHTML='<div class=wrap><div class=card><h3>Restarting...</h3><p style=\"font-size:13px;color:#64748b;margin-top:8px\">Device sedang memuat ulang ke Config Mode. Hubungkan ke AP SEMS-SETUP-xx jika menggunakan WiFi.</p></div></div>';"
    "setTimeout(async function poll(){"
      "try{let r=await fetch('/api/network');if(r.ok){location.href='/network';return;}}catch(e){}"
      "setTimeout(poll,1500);"
    "},4000);"
  "}"
  "async function scanWifi(tid){"
    "const msg=document.getElementById('msg');"
    "msg.className='err';msg.textContent='Scanning...';"
    "document.getElementById('scanList').innerHTML='';"
    "const r=await fetch('/api/scan',{method:'POST'});"
    "if(!r.ok){"
      "const d=await r.json();"
      "if(d.error==='scan_restricted')msg.innerHTML='Scan hanya diperbolehkan di Config Mode. <button class=\"btn btn-sm btn-danger\" type=button onclick=goToConfigMode() style=\"margin-top:5px;display:block\">Masuk Config Mode</button>';"
      "else msg.textContent='Scan gagal: '+(d.error||'unknown');"
      "return;"
    "}"
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
    "r.networks.forEach(net=>{"
      "document.querySelectorAll('[data-saved-ssid]').forEach(span=>{"
        "if(span.dataset.savedSsid===net.ssid){"
          "span.textContent=' [ch'+net.ch+' • '+net.rssi+' dBm]';"
        "}"
      "});"
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
  Serial.printf("[Web] Request on / from %s\n", server.client().remoteIP().toString().c_str());
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
  html += getNav();

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

  // Config links
  html += F("<div class=card><div class=card-title>Konfigurasi</div>"
            "<a class='btn btn-sm' href=/network style='text-decoration:none'>&#127760; Network &amp; WiFi</a> "
            "<a class='btn btn-sm' href=/mqtt style='text-decoration:none'>&#128236; MQTT</a></div>");

  // System buttons
  html += F("<div class=card><div class=card-title>Sistem</div>");
  if (!configMode) {
    html += F("<button class='btn btn-sm btn-danger' id=btnCfg type=button>&#128268; Masuk Config Mode (AP)</button> ");
  } else {
    html += F("<button class='btn btn-sm btn-ghost' style='cursor:not-allowed;opacity:0.6' disabled type=button>Sudah di Config Mode (AP)</button> ");
  }
  html += F("<button class='btn btn-sm btn-danger' id=btnRbt type=button>&#8635; Reboot</button></div>");

  html += F("<script>"
    "const btnCfg=document.getElementById('btnCfg');"
    "if(btnCfg)btnCfg.onclick=async()=>{"
      "if(!confirm('Masuk Config Mode? Device akan restart dan broadcast AP.'))return;"
      "await fetch('/api/config-mode',{method:'POST'});"
      "document.body.innerHTML='<div class=wrap><div class=card><h3>Restarting...</h3><p style=\"font-size:13px;color:#64748b;margin-top:8px\">Device sedang memuat ulang ke Config Mode. Hubungkan ke AP SEMS-SETUP-xx jika menggunakan WiFi.</p></div></div>';"
      "setTimeout(async function poll(){"
        "try{let r=await fetch('/api/network');if(r.ok){location.href='/network';return;}}catch(e){}"
        "setTimeout(poll,1500);"
      "},4000);"
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
  html += getNav();
  if (!configMode) {
    html += F("<div class='card' style='background:#fee2e2;border:1px solid #fca5a5;padding:12px;margin-bottom:12px;'>"
              "<div style='font-weight:600;color:#991b1b;font-size:14px;margin-bottom:4px;'>Normal Mode Aktif</div>"
              "<div style='font-size:12px;color:#7f1d1d;margin-bottom:8px;'>Fitur WiFi Scan dikunci untuk menjaga kestabilan koneksi.</div>"
              "<button class='btn btn-sm btn-danger' type=button onclick='goToConfigMode()'>&#128268; Masuk Config Mode</button>"
              "</div>");
  }
  html += F("<div id=root><p style='color:#94a3b8;font-size:13px'>Loading...</p></div>"
            "<div class=refresh id=ts></div>");

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
      html += "<span data-saved-ssid=\""; html += s; html += "\" style=\"font-size:11px;color:#64748b;margin-left:6px\"></span>";
      html += F("</div></div>");
      if (configMode) {
        html += F("<button class='btn btn-sm btn-danger' type=button data-d=\"");
        html += s; html += F("\">&#10005;</button>");
      }
      html += F("</div>");
    }
  }
  html += F("</div>");

  if (configMode) {
    html += F("<div class=card style='margin-top:12px'><div class=card-title>Scan WiFi</div>"
              "<button class='btn btn-sm' type=button onclick=\"scanWifi('ssid')\">&#128268; Scan Sekarang</button>"
              "<div id=msg style='margin-top:8px;font-size:13px'></div>"
              "<div id=scanList></div>"
              "<div id=addForm style='display:none'>"
              "<label>SSID</label><input type=text id=ssid readonly>"
              "<label>Password</label><input type=password id=pass placeholder='Password'>"
              "<button class=btn style='width:100%;margin-top:10px' onclick=saveWifi()>Connect &amp; Simpan</button>"
              "</div></div>");
  }
  html += F("<script>"
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
            "if(document.getElementById('scanList')){scanWifi('ssid');}"
            "window.addEventListener('click',e=>{"
              "if(e.target.dataset.s!==undefined){"
                "document.getElementById('ssid').value=e.target.dataset.s;"
                "document.getElementById('pass').value='';"
                "document.getElementById('addForm').style.display='block';"
                "document.getElementById('pass').focus();}"
            "});"
            "document.querySelectorAll('[data-d]').forEach(b=>b.onclick=async()=>{"
              "if(!confirm('Hapus jaringan '+b.dataset.d+'?'))return;"
              "await fetch('/api/wifi/delete',{method:'POST',"
                "headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:b.dataset.d})});"
              "location.reload();"
            "});"
             "async function saveWifi(){"
               "const msg=document.getElementById('msg');"
               "msg.className='ok';msg.textContent='Sedang menguji koneksi WiFi, mohon tunggu...';"
               "const b=JSON.stringify({ssid:document.getElementById('ssid').value,"
                 "pass:document.getElementById('pass').value});"
               "try{"
                 "const r=await fetch('/api/wifi/connect-test',{method:'POST',"
                   "headers:{'Content-Type':'application/json'},body:b}).then(x=>x.json());"
                 "if(r.ok){"
                   "msg.className='ok';msg.innerHTML='Koneksi sukses! IP: '+r.ip+'<br>Konfigurasi WiFi telah disimpan.';"
                   "setTimeout(()=>location.reload(),2000);"
                 "}else{"
                   "msg.className='err';msg.textContent='Gagal tersambung ke WiFi. Periksa kembali Password/Sinyal Anda (status: '+r.status+').';"
                 "}"
               "}catch(e){"
                 "msg.className='err';msg.textContent='Error saat menguji koneksi: '+e;"
               "}"
             "}");
  html += FPSTR(kScanScript);
  html += F("</script></div></body></html>");
  server.send(200, "text/html", html);
}

static void handleMqttPage() {
  oledShow("Web UI", "MQTT dibuka", server.client().remoteIP().toString().c_str(), 3000);

  String html;
  html.reserve(3800);
  html += FPSTR(kStyle);
  html += F("<title>SEMS MQTT</title></head><body><div class=wrap>"
            "<h1>SEMS AIoT &mdash; MQTT</h1>");
  html += getNav();

  if (!configMode) {
    // Normal Mode: tampilkan banner info bahwa config hanya bisa di config mode
    html += F("<div class='card' style='background:#fee2e2;border:1px solid #fca5a5;padding:12px;margin-bottom:12px;'>"
              "<div style='font-weight:600;color:#991b1b;font-size:14px;margin-bottom:4px;'>Normal Mode Aktif</div>"
              "<div style='font-size:12px;color:#7f1d1d;margin-bottom:8px;'>Konfigurasi MQTT hanya dapat diubah di Config Mode.</div>"
              "<button class='btn btn-sm btn-danger' type=button onclick='goToConfigMode()'>&#128268; Masuk Config Mode</button>"
              "</div>"
              "<div class=card><div class=card-title>Status MQTT</div><div id=status>Loading...</div></div>"
              "<script>"
              "async function load(){"
                "const d=await fetch('/api/mqtt').then(r=>r.json());"
                "const ok=d.connected;"
                "document.getElementById('status').innerHTML="
                  "'<span class=\"badge '+(ok?'up':'down')+'\"><span class=dot></span>'+(ok?'Connected':'Disconnected')+'</span> '+"
                  "'<span style=\"font-size:12px;color:#64748b;margin-left:8px\">'+(d.host?d.host+':'+d.port:'Belum dikonfigurasi')+'</span>';}"
              "load();setInterval(load,5000);"
              "</script></div></body></html>");
    server.send(200, "text/html", html);
    return;
  }

  // Config Mode: full editor + banner kuning info
  html += F("<div class='card' style='background:#fef9c3;border:1px solid #fde047;padding:12px;margin-bottom:12px;'>"
            "<div style='font-weight:600;color:#854d0e;font-size:14px;margin-bottom:4px;'>Config Mode</div>"
            "<div style='font-size:12px;color:#713f12;'>Koneksi ke broker tidak akan dicoba sampai reboot ke Normal Mode.</div>"
            "</div>");
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
            "let dirty=false;"
            "document.addEventListener('DOMContentLoaded',()=>{"
              "['host','port','user','pass','topic','enabled'].forEach(id=>{"
                "const el=document.getElementById(id);"
                "if(el)el.addEventListener('input',()=>{dirty=true;});});"
            "});"
            "async function load(force){"
              "if(dirty&&!force){"
                // only refresh status badge, never overwrite form fields
                "const d=await fetch('/api/mqtt').then(r=>r.json());"
                "const ok=d.connected;"
                "document.getElementById('status').innerHTML="
                  "'<span class=\"badge '+(ok?'up':'down')+'\">"
                    "<span class=dot></span>'+(ok?'Connected':'Disconnected')+'</span> '+"
                  "'<span style=\"font-size:12px;color:#64748b;margin-left:8px\">'+"
                    "(d.host?d.host+':'+d.port:'Belum dikonfigurasi')+'</span>';"
                "return;}"
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
              "dirty=false;}"
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
              "if(r.ok){dirty=false;msg.className='ok';msg.textContent='Tersimpan!';setTimeout(()=>load(true),1500);}"
              "else{msg.className='err';msg.textContent='Error: '+(r.error||'unknown');}}"
            "load(true);setInterval(load,5000);");
  html += F("</script></div></body></html>");
  server.send(200, "text/html", html);
}

// --- API handlers ---

static void handleScanStart() {
  bool inConfigMode = configMode;
  if (!inConfigMode) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"scan_restricted\"}");
    return;
  }

  if (scanTaskRunning) {
    server.send(202, "application/json", "{\"ok\":true}");
    return;
  }

  WiFi.scanDelete();
  xTaskCreate(wifiScanTask, "wifiScanTask", 4096, NULL, 1, NULL);
  server.send(202, "application/json", "{\"ok\":true}");
}

static void handleScanResult() {
  const int n = WiFi.scanComplete();
  Serial.printf("[Web Scan API] Query status: %d (Task Running: %d)\n", n, scanTaskRunning);
  
  if (n == WIFI_SCAN_RUNNING || scanTaskRunning) {
    server.send(200, "application/json", "{\"status\":\"scanning\"}"); return;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanDelete();
    server.send(200, "application/json", "{\"status\":\"failed\",\"networks\":[]}"); return;
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

static void handleModbusPage() {
  oledShow("Web UI", "Modbus dibuka", server.client().remoteIP().toString().c_str(), 3000);

  String html;
  html.reserve(3200);
  html += FPSTR(kStyle);
  html += F("<title>SEMS Modbus</title></head><body><div class=wrap>"
            "<h1>SEMS AIoT &mdash; Modbus</h1>");
  html += getNav();

  if (!configMode) {
    html += F("<style>"
              ".mg{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px;margin-top:8px}"
              ".mv{background:#f8fafc;border:1px solid #e2e8f0;border-radius:10px;padding:10px 12px;text-align:center}"
              ".ml{font-size:11px;color:#64748b;margin-bottom:2px}"
              ".mn{font-size:22px;font-weight:700;color:#0f766e;line-height:1.1}"
              ".mu{font-size:11px;color:#94a3b8}"
              ".msec{font-size:13px;font-weight:600;color:#334155;margin:14px 0 6px;border-bottom:1px solid #e2e8f0;padding-bottom:4px}"
              ".na{color:#cbd5e1!important}"
              "</style>"
              "<div class='card' style='background:#fee2e2;border:1px solid #fca5a5;padding:10px 12px;margin-bottom:12px;"
                "display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px'>"
              "<div>"
                "<div style='font-weight:600;color:#991b1b;font-size:13px'>Normal Mode — Konfigurasi terkunci</div>"
                "<div style='font-size:11px;color:#7f1d1d'>Masuk Config Mode untuk ubah register map / baud rate</div>"
              "</div>"
              "<button class='btn btn-sm btn-danger' type=button onclick='goToConfigMode()'>Config Mode</button>"
              "</div>"
              "<div class=card>"
                "<div style='display:flex;align-items:center;justify-content:space-between'>"
                  "<div class=card-title style='margin:0'>Pembacaan Meter</div>"
                  "<span id=badge style='font-size:12px'>—</span>"
                "</div>"
                "<div id=readings><p style='color:#94a3b8;font-size:13px;margin-top:10px'>Menunggu data...</p></div>"
              "</div>"
              "<script>"
              "function row(label,val,unit,na){"
                "return '<div class=mv>'+"
                  "'<div class=ml>'+label+'</div>'+"
                  "'<div class=\"mn'+(na?' na':'')+'\">'+val+'</div>'+"
                  "'<div class=\"mu'+(na?' na':'')+'\">'+unit+'</div>'+"
                  "'</div>';}"
              "function render(d){"
                "const badge=document.getElementById('badge');"
                "const out=document.getElementById('readings');"
                "const na=!d.valid;"
                "const fmt1=(v,n)=>na?'&mdash;':v.toFixed(n);"
                "if(na)badge.innerHTML='<span class=\"badge down\"><span class=dot></span>Tidak Ada Data</span>';"
                "else badge.innerHTML='<span class=\"badge up\"><span class=dot></span>'+(d.phase===1?'1-Phase':'3-Phase')+'</span>';"
                "const f1=(k,n)=>fmt1(na?0:(d[k]||0),n);"
                "let h='';"
                "if(d.phase!==3){"
                  "h+='<div class=mg>';"
                  "h+=row('Voltage',f1('v',1),'V',na);"
                  "h+=row('Current',f1('a',3),'A',na);"
                  "h+=row('Active Power',f1('kw',3),'kW',na);"
                  "h+=row('Reactive Power',f1('kvar',3),'kVAR',na);"
                  "h+=row('Apparent Power',f1('kva',3),'kVA',na);"
                  "h+=row('Power Factor',f1('pf',3),'',na);"
                  "h+=row('Energy',f1('kwh',3),'kWh',na);"
                  "h+=row('Frequency',f1('hz',2),'Hz',na);"
                  "h+='</div>';"
                "}else{"
                  "h+='<div class=msec>Tegangan</div><div class=mg>';"
                  "h+=row('V A-N',f1('va',1),'V',na);"
                  "h+=row('V B-N',f1('vb',1),'V',na);"
                  "h+=row('V C-N',f1('vc',1),'V',na);"
                  "h+=row('V L-N Avg',f1('vln',1),'V',na);"
                  "h+='</div>';"
                  "h+='<div class=msec>Arus</div><div class=mg>';"
                  "h+=row('I A',f1('ia',3),'A',na);"
                  "h+=row('I B',f1('ib',3),'A',na);"
                  "h+=row('I C',f1('ic',3),'A',na);"
                  "h+=row('I Avg',f1('iavg',3),'A',na);"
                  "h+='</div>';"
                  "h+='<div class=msec>Daya Aktif</div><div class=mg>';"
                  "h+=row('P A',f1('pa',3),'kW',na);"
                  "h+=row('P B',f1('pb',3),'kW',na);"
                  "h+=row('P C',f1('pc',3),'kW',na);"
                  "h+=row('P Total',f1('ptot',3),'kW',na);"
                  "h+='</div>';"
                  "h+='<div class=msec>Daya Reaktif &amp; Semu</div><div class=mg>';"
                  "h+=row('Q Total',f1('qtot',3),'kVAR',na);"
                  "h+=row('S Total',f1('stot',3),'kVA',na);"
                  "h+=row('PF Total',f1('pftot',3),'',na);"
                  "h+=row('PF A',f1('pfa',3),'',na);"
                  "h+=row('PF B',f1('pfb',3),'',na);"
                  "h+=row('PF C',f1('pfc',3),'',na);"
                  "h+='</div>';"
                  "h+='<div class=msec>Energi &amp; Frekuensi</div><div class=mg>';"
                  "h+=row('Energy',f1('kwh',3),'kWh',na);"
                  "h+=row('Frequency',f1('hz',2),'Hz',na);"
                  "h+='</div>';"
                "}"
                "out.innerHTML=h;}"
              "async function load(){"
                "try{const d=await fetch('/api/modbus').then(r=>r.json());render(d);}"
                "catch(e){document.getElementById('badge').textContent='Error';}"
              "}"
              "load();setInterval(load,2000);"
              "</script></div></body></html>");
    server.send(200, "text/html", html);
    return;
  }

  html += F("<div class='card' style='background:#fef9c3;border:1px solid #fde047;padding:12px;margin-bottom:12px;'>"
            "<div style='font-weight:600;color:#854d0e;font-size:14px;margin-bottom:4px;'>Config Mode</div>"
            "<div style='font-size:12px;color:#713f12;'>Perubahan baud rate aktif setelah reboot.</div>"
            "</div>");

  // ── Status card ──────────────────────────────────────────
  html += F("<div class=card><div class=card-title>Status Meter</div><div id=status>Loading...</div></div>");

  // ── Preset card ──────────────────────────────────────────
  html += F("<div class=card><div class=card-title>Preset</div>"
            "<div style='font-size:12px;color:#64748b;margin-bottom:8px'>Isi register map otomatis. Semua preset: FC03, FP32 big-endian, addr 0-based.</div>"
            "<div style='display:flex;gap:8px;flex-wrap:wrap'>"
              "<button class='btn btn-sm' onclick='applyPreset(\"p1_1ph\")'>PM2230 / PM2120 / EM6400 &mdash; 1-Phase</button>"
              "<button class='btn btn-sm' onclick='applyPreset(\"p1_3ph\")'>PM2230 / EM6400 &mdash; 3-Phase</button>"
            "</div></div>");

  // ── RS485 connection card ─────────────────────────────────
  html += F("<div class=card><div class=card-title>Koneksi RS485</div>"
            "<label>Slave ID (1&ndash;247)</label>"
            "<input type=number id=slave min=1 max=247>"
            "<label>Baud Rate</label>"
            "<select id=baud>"
              "<option value=1200>1200</option><option value=2400>2400</option>"
              "<option value=4800>4800</option><option value=9600>9600</option>"
              "<option value=19200>19200</option><option value=38400>38400</option>"
              "<option value=115200>115200</option>"
            "</select>"
            "<label>Poll Interval (ms)</label>"
            "<input type=number id=poll min=200 max=60000>"
            "<label>Mode Fasa</label>"
            "<div style='display:flex;gap:12px;margin-top:4px'>"
              "<label style='display:flex;align-items:center;gap:6px;font-size:14px;color:#1e293b'>"
                "<input type=radio name=phase id=ph1 value=1> 1-Phase</label>"
              "<label style='display:flex;align-items:center;gap:6px;font-size:14px;color:#1e293b'>"
                "<input type=radio name=phase id=ph3 value=3> 3-Phase</label>"
            "</div></div>");

  // ── 1-Phase register map ──────────────────────────────────
  html += F("<div class=card id=card1ph><div class=card-title>Register Map &mdash; 1-Phase</div>"
            "<div style='display:grid;grid-template-columns:1fr 1fr;gap:0 16px'>"
            "<div><label>Voltage A-N (V)</label><input type=number class=reg1 id=r1V min=0 max=65535></div>"
            "<div><label>Current A (A)</label><input type=number class=reg1 id=r1A min=0 max=65535></div>"
            "<div><label>Active Power (kW)</label><input type=number class=reg1 id=r1Kw min=0 max=65535></div>"
            "<div><label>Reactive Power (kVAR)</label><input type=number class=reg1 id=r1Kvar min=0 max=65535></div>"
            "<div><label>Apparent Power (kVA)</label><input type=number class=reg1 id=r1Kva min=0 max=65535></div>"
            "<div><label>Power Factor (4Q)</label><input type=number class=reg1 id=r1Pf min=0 max=65535></div>"
            "<div><label>Energy kWh</label><input type=number class=reg1 id=r1Kwh min=0 max=65535></div>"
            "<div><label>Frequency (Hz)</label><input type=number class=reg1 id=r1Hz min=0 max=65535></div>"
            "</div></div>");

  // ── 3-Phase register map ──────────────────────────────────
  html += F("<div class=card id=card3ph><div class=card-title>Register Map &mdash; 3-Phase</div>"
            "<div style='display:grid;grid-template-columns:1fr 1fr;gap:0 16px'>"
            "<div><label>Voltage A-N (V)</label><input type=number class=reg3 id=r3Va min=0 max=65535></div>"
            "<div><label>Voltage B-N (V)</label><input type=number class=reg3 id=r3Vb min=0 max=65535></div>"
            "<div><label>Voltage C-N (V)</label><input type=number class=reg3 id=r3Vc min=0 max=65535></div>"
            "<div><label>Voltage L-L Avg (V)</label><input type=number class=reg3 id=r3Vll min=0 max=65535></div>"
            "<div><label>Voltage L-N Avg (V)</label><input type=number class=reg3 id=r3Vln min=0 max=65535></div>"
            "<div><label>Current A (A)</label><input type=number class=reg3 id=r3Ia min=0 max=65535></div>"
            "<div><label>Current B (A)</label><input type=number class=reg3 id=r3Ib min=0 max=65535></div>"
            "<div><label>Current C (A)</label><input type=number class=reg3 id=r3Ic min=0 max=65535></div>"
            "<div><label>Current Avg (A)</label><input type=number class=reg3 id=r3Iavg min=0 max=65535></div>"
            "<div><label>Active Power A (kW)</label><input type=number class=reg3 id=r3Pa min=0 max=65535></div>"
            "<div><label>Active Power B (kW)</label><input type=number class=reg3 id=r3Pb min=0 max=65535></div>"
            "<div><label>Active Power C (kW)</label><input type=number class=reg3 id=r3Pc min=0 max=65535></div>"
            "<div><label>Active Power Total (kW)</label><input type=number class=reg3 id=r3Pt min=0 max=65535></div>"
            "<div><label>Reactive Power A (kVAR)</label><input type=number class=reg3 id=r3Qa min=0 max=65535></div>"
            "<div><label>Reactive Power B (kVAR)</label><input type=number class=reg3 id=r3Qb min=0 max=65535></div>"
            "<div><label>Reactive Power C (kVAR)</label><input type=number class=reg3 id=r3Qc min=0 max=65535></div>"
            "<div><label>Reactive Power Total (kVAR)</label><input type=number class=reg3 id=r3Qt min=0 max=65535></div>"
            "<div><label>Apparent Power A (kVA)</label><input type=number class=reg3 id=r3Sa min=0 max=65535></div>"
            "<div><label>Apparent Power B (kVA)</label><input type=number class=reg3 id=r3Sb min=0 max=65535></div>"
            "<div><label>Apparent Power C (kVA)</label><input type=number class=reg3 id=r3Sc min=0 max=65535></div>"
            "<div><label>Apparent Power Total (kVA)</label><input type=number class=reg3 id=r3St min=0 max=65535></div>"
            "<div><label>PF Phase A (4Q)</label><input type=number class=reg3 id=r3Pfa min=0 max=65535></div>"
            "<div><label>PF Phase B (4Q)</label><input type=number class=reg3 id=r3Pfb min=0 max=65535></div>"
            "<div><label>PF Phase C (4Q)</label><input type=number class=reg3 id=r3Pfc min=0 max=65535></div>"
            "<div><label>PF Total (FLOAT32)</label><input type=number class=reg3 id=r3Pft min=0 max=65535></div>"
            "<div><label>Energy kWh</label><input type=number class=reg3 id=r3Kwh min=0 max=65535></div>"
            "<div><label>Frequency (Hz)</label><input type=number class=reg3 id=r3Hz min=0 max=65535></div>"
            "</div></div>");

  html += F("<div class=card>"
            "<button class=btn style='width:100%' onclick=save()>Simpan</button>"
            "<div id=msg></div></div>");

  // ── JavaScript ────────────────────────────────────────────
  html += F("<script>"
  // Presets: p1_1ph and p1_3ph fill all respective fields
  "const P1={'r1V':3027,'r1A':2999,'r1Kw':3053,'r1Kvar':3061,'r1Kva':3069,'r1Pf':3077,'r1Kwh':2675,'r1Hz':3109};"
  "const P3={'r3Va':3027,'r3Vb':3029,'r3Vc':3031,'r3Vll':3025,'r3Vln':3035,"
             "'r3Ia':2999,'r3Ib':3001,'r3Ic':3003,'r3Iavg':3009,"
             "'r3Pa':3053,'r3Pb':3055,'r3Pc':3057,'r3Pt':3059,"
             "'r3Qa':3061,'r3Qb':3063,'r3Qc':3065,'r3Qt':3067,"
             "'r3Sa':3069,'r3Sb':3071,'r3Sc':3073,'r3St':3075,"
             "'r3Pfa':3077,'r3Pfb':3079,'r3Pfc':3081,'r3Pft':3191,"
             "'r3Kwh':2675,'r3Hz':3109};"
  "function applyPreset(k){"
    "const map=k==='p1_1ph'?P1:P3;"
    "Object.entries(map).forEach(([id,v])=>{"
      "const el=document.getElementById(id);if(el)el.value=v;});"
    "if(k==='p1_1ph'){document.getElementById('ph1').checked=true;}"
    "else{document.getElementById('ph3').checked=true;}"
    "updatePhaseView();"
    "dirty=true;"
    "document.getElementById('msg').className='';"
    "document.getElementById('msg').textContent='Preset diterapkan — klik Simpan.';}"
  "function updatePhaseView(){"
    "const is3=document.getElementById('ph3').checked;"
    "document.getElementById('card1ph').style.display=is3?'none':'';"
    "document.getElementById('card3ph').style.display=is3?'':'none';}"
  "document.getElementById('ph1').addEventListener('change',updatePhaseView);"
  "document.getElementById('ph3').addEventListener('change',updatePhaseView);"
  "let dirty=false;"
  "document.querySelectorAll('input,select').forEach(el=>el.addEventListener('input',()=>{dirty=true;}));"
  "function statusHtml(d){"
    "if(!d.valid)return '<span class=\"badge down\"><span class=dot></span>Tidak Ada Data</span>';"
    "if(d.phase===1)return '<span class=\"badge up\"><span class=dot></span>Data Valid (1-Phase)</span> '+"
      "'V='+d.v.toFixed(1)+' A='+d.a.toFixed(3)+' kW='+d.kw.toFixed(3)+"
      "' PF='+d.pf.toFixed(3)+' kWh='+d.kwh.toFixed(3)+' Hz='+d.hz.toFixed(2);"
    "return '<span class=\"badge up\"><span class=dot></span>Data Valid (3-Phase)</span> '+"
      "'Va='+d.va.toFixed(1)+' Ia='+d.ia.toFixed(3)+' Ptot='+d.ptot.toFixed(3)+"
      "' PF='+d.pftot.toFixed(3)+' kWh='+d.kwh.toFixed(3)+' Hz='+d.hz.toFixed(2);}"
  "async function load(force){"
    "const d=await fetch('/api/modbus').then(r=>r.json());"
    "document.getElementById('status').innerHTML=statusHtml(d);"
    "if(dirty&&!force)return;"
    "document.getElementById('slave').value=d.slave;"
    "const sel=document.getElementById('baud');"
    "for(let i=0;i<sel.options.length;i++)if(parseInt(sel.options[i].value)===d.baud)sel.selectedIndex=i;"
    "document.getElementById('poll').value=d.poll;"
    "if(d.phase===3)document.getElementById('ph3').checked=true;"
    "else document.getElementById('ph1').checked=true;"
    "updatePhaseView();"
    // 1-phase fields
    "['r1V','r1A','r1Kw','r1Kvar','r1Kva','r1Pf','r1Kwh','r1Hz'].forEach(id=>{"
      "const el=document.getElementById(id);if(el&&d[id]!==undefined)el.value=d['1'+id.slice(2)];});"
    // map API key names (1V,1A,...) to field ids (r1V,r1A,...)
    "[['1V','r1V'],['1A','r1A'],['1Kw','r1Kw'],['1Kvar','r1Kvar'],['1Kva','r1Kva'],"
     "['1Pf','r1Pf'],['1Kwh','r1Kwh'],['1Hz','r1Hz'],"
     "['3Va','r3Va'],['3Vb','r3Vb'],['3Vc','r3Vc'],['3Vll','r3Vll'],['3Vln','r3Vln'],"
     "['3Ia','r3Ia'],['3Ib','r3Ib'],['3Ic','r3Ic'],['3Iavg','r3Iavg'],"
     "['3Pa','r3Pa'],['3Pb','r3Pb'],['3Pc','r3Pc'],['3Pt','r3Pt'],"
     "['3Qa','r3Qa'],['3Qb','r3Qb'],['3Qc','r3Qc'],['3Qt','r3Qt'],"
     "['3Sa','r3Sa'],['3Sb','r3Sb'],['3Sc','r3Sc'],['3St','r3St'],"
     "['3Pfa','r3Pfa'],['3Pfb','r3Pfb'],['3Pfc','r3Pfc'],['3Pft','r3Pft'],"
     "['3Kwh','r3Kwh'],['3Hz','r3Hz']"
    "].forEach(([k,id])=>{"
      "const el=document.getElementById(id);if(el&&d[k]!==undefined)el.value=d[k];});"
    "dirty=false;}"
  "function gv(id,def){const el=document.getElementById(id);return el?parseInt(el.value)||def:def;}"
  "async function save(){"
    "const msg=document.getElementById('msg');"
    "msg.className='';msg.textContent='Menyimpan...';"
    "const phase=document.getElementById('ph3').checked?3:1;"
    "const b={slave:gv('slave',1),baud:parseInt(document.getElementById('baud').value)||19200,"
      "poll:gv('poll',1000),phase,"
      "'1V':gv('r1V',3027),'1A':gv('r1A',2999),'1Kw':gv('r1Kw',3053),"
      "'1Kvar':gv('r1Kvar',3061),'1Kva':gv('r1Kva',3069),'1Pf':gv('r1Pf',3077),"
      "'1Kwh':gv('r1Kwh',2675),'1Hz':gv('r1Hz',3109),"
      "'3Va':gv('r3Va',3027),'3Vb':gv('r3Vb',3029),'3Vc':gv('r3Vc',3031),"
      "'3Vll':gv('r3Vll',3025),'3Vln':gv('r3Vln',3035),"
      "'3Ia':gv('r3Ia',2999),'3Ib':gv('r3Ib',3001),'3Ic':gv('r3Ic',3003),'3Iavg':gv('r3Iavg',3009),"
      "'3Pa':gv('r3Pa',3053),'3Pb':gv('r3Pb',3055),'3Pc':gv('r3Pc',3057),'3Pt':gv('r3Pt',3059),"
      "'3Qa':gv('r3Qa',3061),'3Qb':gv('r3Qb',3063),'3Qc':gv('r3Qc',3065),'3Qt':gv('r3Qt',3067),"
      "'3Sa':gv('r3Sa',3069),'3Sb':gv('r3Sb',3071),'3Sc':gv('r3Sc',3073),'3St':gv('r3St',3075),"
      "'3Pfa':gv('r3Pfa',3077),'3Pfb':gv('r3Pfb',3079),'3Pfc':gv('r3Pfc',3081),'3Pft':gv('r3Pft',3191),"
      "'3Kwh':gv('r3Kwh',2675),'3Hz':gv('r3Hz',3109)};"
    "const r=await fetch('/api/modbus/save',{method:'POST',"
      "headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(x=>x.json());"
    "if(r.ok){dirty=false;msg.className='ok';msg.textContent='Tersimpan! Reboot untuk terapkan baud rate baru.';setTimeout(()=>load(true),1500);}"
    "else{msg.className='err';msg.textContent='Error: '+(r.error||'unknown');}}"
  "load(true);setInterval(()=>load(false),3000);"
  "</script></div></body></html>");
  server.send(200, "text/html", html);
}

static void handleModbusApi() {
  String body;
  body.reserve(512);
  body  = "{\"ok\":true";
  body += ",\"slave\":";  body += modbusCfg.slaveId;
  body += ",\"baud\":";   body += modbusCfg.baud;
  body += ",\"poll\":";   body += modbusCfg.pollMs;
  body += ",\"phase\":";  body += modbusCfg.phase;
  // 1-phase regs
  body += ",\"1V\":";     body += modbusCfg.r1V;
  body += ",\"1A\":";     body += modbusCfg.r1A;
  body += ",\"1Kw\":";    body += modbusCfg.r1Kw;
  body += ",\"1Kvar\":";  body += modbusCfg.r1Kvar;
  body += ",\"1Kva\":";   body += modbusCfg.r1Kva;
  body += ",\"1Pf\":";    body += modbusCfg.r1Pf;
  body += ",\"1Kwh\":";   body += modbusCfg.r1Kwh;
  body += ",\"1Hz\":";    body += modbusCfg.r1Hz;
  // 3-phase regs
  body += ",\"3Va\":";    body += modbusCfg.r3Va;
  body += ",\"3Vb\":";    body += modbusCfg.r3Vb;
  body += ",\"3Vc\":";    body += modbusCfg.r3Vc;
  body += ",\"3Vll\":";   body += modbusCfg.r3Vll;
  body += ",\"3Vln\":";   body += modbusCfg.r3Vln;
  body += ",\"3Ia\":";    body += modbusCfg.r3Ia;
  body += ",\"3Ib\":";    body += modbusCfg.r3Ib;
  body += ",\"3Ic\":";    body += modbusCfg.r3Ic;
  body += ",\"3Iavg\":";  body += modbusCfg.r3Iavg;
  body += ",\"3Pa\":";    body += modbusCfg.r3Pa;
  body += ",\"3Pb\":";    body += modbusCfg.r3Pb;
  body += ",\"3Pc\":";    body += modbusCfg.r3Pc;
  body += ",\"3Pt\":";    body += modbusCfg.r3Ptot;
  body += ",\"3Qa\":";    body += modbusCfg.r3Qa;
  body += ",\"3Qb\":";    body += modbusCfg.r3Qb;
  body += ",\"3Qc\":";    body += modbusCfg.r3Qc;
  body += ",\"3Qt\":";    body += modbusCfg.r3Qtot;
  body += ",\"3Sa\":";    body += modbusCfg.r3Sa;
  body += ",\"3Sb\":";    body += modbusCfg.r3Sb;
  body += ",\"3Sc\":";    body += modbusCfg.r3Sc;
  body += ",\"3St\":";    body += modbusCfg.r3Stot;
  body += ",\"3Pfa\":";   body += modbusCfg.r3Pfa;
  body += ",\"3Pfb\":";   body += modbusCfg.r3Pfb;
  body += ",\"3Pfc\":";   body += modbusCfg.r3Pfc;
  body += ",\"3Pft\":";   body += modbusCfg.r3Pftot;
  body += ",\"3Kwh\":";   body += modbusCfg.r3Kwh;
  body += ",\"3Hz\":";    body += modbusCfg.r3Hz;
  // live meter data
  if (modbusCfg.phase == 1 && meter1.valid) {
    char buf[160];
    snprintf(buf, sizeof(buf),
      ",\"valid\":true"
      ",\"v\":%.1f,\"a\":%.3f,\"kw\":%.3f,\"kvar\":%.3f,\"kva\":%.3f,\"pf\":%.3f,\"kwh\":%.3f,\"hz\":%.2f",
      meter1.v, meter1.a, meter1.kw, meter1.kvar, meter1.kva, meter1.pf, meter1.kwh, meter1.hz);
    body += buf;
  } else if (modbusCfg.phase == 3 && meter3.valid) {
    char buf[320];
    snprintf(buf, sizeof(buf),
      ",\"valid\":true"
      ",\"va\":%.1f,\"vb\":%.1f,\"vc\":%.1f,\"vln\":%.1f"
      ",\"ia\":%.3f,\"ib\":%.3f,\"ic\":%.3f,\"iavg\":%.3f"
      ",\"pa\":%.3f,\"pb\":%.3f,\"pc\":%.3f,\"ptot\":%.3f"
      ",\"qtot\":%.3f,\"stot\":%.3f"
      ",\"pfa\":%.3f,\"pfb\":%.3f,\"pfc\":%.3f,\"pftot\":%.3f"
      ",\"kwh\":%.3f,\"hz\":%.2f",
      meter3.va, meter3.vb, meter3.vc, meter3.vln,
      meter3.ia, meter3.ib, meter3.ic, meter3.iavg,
      meter3.pa, meter3.pb, meter3.pc, meter3.ptot,
      meter3.qtot, meter3.stot,
      meter3.pfa, meter3.pfb, meter3.pfc, meter3.pftot,
      meter3.kwh, meter3.hz);
    body += buf;
  } else {
    body += ",\"valid\":false";
  }
  // always include phase so JS can render the correct tile layout
  body += ",\"phase\":"; body += modbusCfg.phase;
  body += "}";
  server.send(200, "application/json", body);
}

static void handleModbusSave() {
  String body = server.arg("plain");
  uint8_t  slave = (uint8_t)jsonInt(body, "slave", 1);
  uint32_t baud  = (uint32_t)jsonInt(body, "baud",  19200);
  uint32_t poll  = (uint32_t)jsonInt(body, "poll",  1000);
  uint8_t  phase = (uint8_t)jsonInt(body, "phase", 1);
  if (slave < 1 || slave > 247) slave = 1;
  if (phase != 1 && phase != 3) phase = 1;
  const uint32_t validBauds[] = {1200,2400,4800,9600,19200,38400,115200};
  bool baudOk = false;
  for (uint32_t vb : validBauds) if (baud == vb) { baudOk = true; break; }
  if (!baudOk) baud = 19200;
  if (poll < 200) poll = 200;

  modbusCfg.slaveId = slave;
  modbusCfg.baud    = baud;
  modbusCfg.pollMs  = poll;
  modbusCfg.phase   = phase;
  // 1-phase
  modbusCfg.r1V     = (uint16_t)jsonInt(body, "1V",    3027);
  modbusCfg.r1A     = (uint16_t)jsonInt(body, "1A",    2999);
  modbusCfg.r1Kw    = (uint16_t)jsonInt(body, "1Kw",   3053);
  modbusCfg.r1Kvar  = (uint16_t)jsonInt(body, "1Kvar", 3061);
  modbusCfg.r1Kva   = (uint16_t)jsonInt(body, "1Kva",  3069);
  modbusCfg.r1Pf    = (uint16_t)jsonInt(body, "1Pf",   3077);
  modbusCfg.r1Kwh   = (uint16_t)jsonInt(body, "1Kwh",  2675);
  modbusCfg.r1Hz    = (uint16_t)jsonInt(body, "1Hz",   3109);
  // 3-phase
  modbusCfg.r3Va    = (uint16_t)jsonInt(body, "3Va",   3027);
  modbusCfg.r3Vb    = (uint16_t)jsonInt(body, "3Vb",   3029);
  modbusCfg.r3Vc    = (uint16_t)jsonInt(body, "3Vc",   3031);
  modbusCfg.r3Vll   = (uint16_t)jsonInt(body, "3Vll",  3025);
  modbusCfg.r3Vln   = (uint16_t)jsonInt(body, "3Vln",  3035);
  modbusCfg.r3Ia    = (uint16_t)jsonInt(body, "3Ia",   2999);
  modbusCfg.r3Ib    = (uint16_t)jsonInt(body, "3Ib",   3001);
  modbusCfg.r3Ic    = (uint16_t)jsonInt(body, "3Ic",   3003);
  modbusCfg.r3Iavg  = (uint16_t)jsonInt(body, "3Iavg", 3009);
  modbusCfg.r3Pa    = (uint16_t)jsonInt(body, "3Pa",   3053);
  modbusCfg.r3Pb    = (uint16_t)jsonInt(body, "3Pb",   3055);
  modbusCfg.r3Pc    = (uint16_t)jsonInt(body, "3Pc",   3057);
  modbusCfg.r3Ptot  = (uint16_t)jsonInt(body, "3Pt",   3059);
  modbusCfg.r3Qa    = (uint16_t)jsonInt(body, "3Qa",   3061);
  modbusCfg.r3Qb    = (uint16_t)jsonInt(body, "3Qb",   3063);
  modbusCfg.r3Qc    = (uint16_t)jsonInt(body, "3Qc",   3065);
  modbusCfg.r3Qtot  = (uint16_t)jsonInt(body, "3Qt",   3067);
  modbusCfg.r3Sa    = (uint16_t)jsonInt(body, "3Sa",   3069);
  modbusCfg.r3Sb    = (uint16_t)jsonInt(body, "3Sb",   3071);
  modbusCfg.r3Sc    = (uint16_t)jsonInt(body, "3Sc",   3073);
  modbusCfg.r3Stot  = (uint16_t)jsonInt(body, "3St",   3075);
  modbusCfg.r3Pfa   = (uint16_t)jsonInt(body, "3Pfa",  3077);
  modbusCfg.r3Pfb   = (uint16_t)jsonInt(body, "3Pfb",  3079);
  modbusCfg.r3Pfc   = (uint16_t)jsonInt(body, "3Pfc",  3081);
  modbusCfg.r3Pftot = (uint16_t)jsonInt(body, "3Pft",  3191);
  modbusCfg.r3Kwh   = (uint16_t)jsonInt(body, "3Kwh",  2675);
  modbusCfg.r3Hz    = (uint16_t)jsonInt(body, "3Hz",   3109);
  saveModbusConfig();
  Serial.printf("[Modbus] Saved: slave=%d baud=%lu poll=%lums phase=%d\n",
    modbusCfg.slaveId, modbusCfg.baud, modbusCfg.pollMs, modbusCfg.phase);
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiSave() {
  String body = server.arg("plain");
  String ssid = jsonExtract(body, "ssid"), pass = jsonExtract(body, "pass");
  if (ssid.isEmpty()) { server.send(400,"application/json","{\"ok\":false,\"error\":\"ssid_required\"}"); return; }
  if (!addOrUpdateWifi(ssid, pass)) { server.send(400,"application/json","{\"ok\":false,\"error\":\"list_full\"}"); return; }
  server.send(200,"application/json","{\"ok\":true}");
}

static void handleWifiConnectTest() {
  String body = server.arg("plain");
  String ssid = jsonExtract(body, "ssid"), pass = jsonExtract(body, "pass");
  if (ssid.isEmpty()) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}");
    return;
  }

  Serial.printf("[WiFi Test] Testing connection to SSID: %s\n", ssid.c_str());
  oledShow("WiFi Test", ssid.c_str(), "Connecting...", 15000);

  // Switch to WIFI_AP_STA dynamically to test connection
  if (WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
    delay(100);
  }

  WiFi.disconnect(false);
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t startMs = millis();
  bool connected = false;
  while (millis() - startMs < 12000) {
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    delay(200);
    yield();
  }

  if (connected) {
    IPAddress ip = WiFi.localIP();
    Serial.printf("[WiFi Test] Success! IP: %s\n", ip.toString().c_str());
    oledShow("WiFi Test OK", ssid.c_str(), ip.toString().c_str(), 6000);
    addOrUpdateWifi(ssid, pass);
    server.send(200, "application/json", "{\"ok\":true,\"ip\":\"" + ip.toString() + "\"}");
  } else {
    int status = WiFi.status();
    Serial.printf("[WiFi Test] Failed! Status: %d\n", status);
    oledShow("WiFi Test FAIL", ssid.c_str(), "Check password", 6000);
    WiFi.disconnect(true);
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"conn_failed\",\"status\":" + String(status) + "}");
  }
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

static const char kUpdateHtml[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8><title>SEMS OTA</title>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<style>"
  "body{font-family:system-ui,sans-serif;background:#f1f5f9;padding:16px;display:flex;justify-content:center;align-items:center;min-height:90vh;margin:0}"
  ".card{background:#fff;border-radius:12px;padding:24px;width:100%;max-width:360px;box-shadow:0 4px 6px rgba(0,0,0,.05)}"
  "h1{font-size:16px;font-weight:700;color:#0f172a;margin-bottom:16px}"
  "input[type=file]{margin:16px 0;display:block;width:100%}"
  ".btn{display:block;width:100%;background:#0f766e;color:#fff;border:0;border-radius:8px;padding:10px;font-weight:600;cursor:pointer;text-align:center}"
  "#prg{margin-top:12px;font-size:12px;color:#64748b;text-align:center}"
  "</style></head><body><div class=card><h1>SEMS OTA Update</h1>"
  "<form method=POST action=/update enctype=multipart/form-data id=upForm>"
  "<input type=file name=update accept=.bin>"
  "<input type=submit class=btn value='Update Firmware'></form><div id=prg></div>"
  "<script>document.getElementById('upForm').onsubmit=function(){"
  "document.getElementById('prg').textContent='Uploading... Mohon tunggu.';};</script>"
  "</div></body></html>";

static void handleUpdateGet() {
  oledShow("OTA Mode", "Awaiting file...", "http://sems.local/update", 30000);
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", FPSTR(kUpdateHtml));
}

static void handleUpdatePost() {
  oledShow("OTA Finished", "Rebooting...", "", 5000);
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  delay(1000);
  ESP.restart();
}

static void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[Update] Start: %s\n", upload.filename.c_str());
    oledShow("OTA Uploading", "Please wait...", "Do not power off", 60000);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[Update] Success: %u bytes. Rebooting...\n", upload.totalSize);
      oledShow("OTA Success", "Rebooting...", "Please wait", 5000);
    } else {
      Update.printError(Serial);
      oledShow("OTA Failed", "Error writing file", "Please retry", 5000);
    }
  }
}

// ============================================================
// Web server init
// ============================================================
static void startWebServer() {
  server.on("/",                HTTP_GET,  handleRoot);
  server.on("/network",         HTTP_GET,  handleNetworkPage);
  server.on("/mqtt",            HTTP_GET,  handleMqttPage);
  server.on("/modbus",          HTTP_GET,  handleModbusPage);
  server.on("/api/network",     HTTP_GET,  handleNetworkApi);
  server.on("/api/mqtt",        HTTP_GET,  handleMqttApi);
  server.on("/api/modbus",      HTTP_GET,  handleModbusApi);
  server.on("/api/modbus/save", HTTP_POST, handleModbusSave);
  server.on("/api/scan",        HTTP_POST, handleScanStart);
  server.on("/api/scan/result", HTTP_GET,  handleScanResult);
  server.on("/api/wifi/save",   HTTP_POST, handleWifiSave);
  server.on("/api/wifi/connect-test", HTTP_POST, handleWifiConnectTest);
  server.on("/api/wifi/delete", HTTP_POST, handleWifiDelete);
  server.on("/api/wifi/list",   HTTP_GET,  handleWifiList);
  server.on("/api/wifi/clear",  HTTP_POST, handleWifiClear);
  server.on("/api/mqtt/save",   HTTP_POST, handleMqttSave);
  server.on("/api/reboot",      HTTP_POST, handleReboot);
  server.on("/api/config-mode", HTTP_POST, [](){
    server.send(200,"application/json","{\"ok\":true}");
    enterConfigMode();
  });
  server.on("/update",          HTTP_GET,  handleUpdateGet);
  server.on("/update",          HTTP_POST, handleUpdatePost, handleUpdateUpload);
  server.begin();
  if (MDNS.begin("sems")) {
    MDNS.addService("http","tcp",80);
    Serial.println("[mDNS] http://sems.local");
  }
  Serial.println("[Web] Server started on port 80");
}

// ============================================================
// Ethernet IP / Link Active Loop Synchronizer
// ============================================================
static void handleEthSync(uint32_t now) {
  static uint32_t lastEthSyncMs = 0;
  if (now - lastEthSyncMs < 1000) return;
  lastEthSyncMs = now;

  bool currentEthLink = ETH.linkUp();
  IPAddress localIp = ETH.localIP();
  bool currentEthReady = (localIp != IPAddress(0,0,0,0));
  
  if (currentEthLink != ethLink || currentEthReady != ethReady || (currentEthReady && ethIp != localIp.toString())) {
    ethLink = currentEthLink;
    ethReady = currentEthReady;
    ethIp = ethReady ? localIp.toString() : "";
    if (ethReady) {
      ETH.setDefault();
      Serial.printf("[ETH Sync] Link: %d, IP: %s\n", ethLink, ethIp.c_str());
    } else {
      if (staConnected) WiFi.STA.setDefault();
      Serial.printf("[ETH Sync] Offline (Link: %d)\n", ethLink);
    }
  }
}

// ============================================================
// setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  loadModbusConfig();
  Serial2.begin(modbusCfg.baud, SERIAL_8E1, kRs485Rx, kRs485Tx);
  pinMode(kLedPin, OUTPUT);
  pinMode(kBtnPin,  INPUT_PULLUP);
  pinMode(kBtn2Pin, INPUT_PULLUP);

  Wire.begin(22, 21);
  if (oled.begin()) {
    oledReady = true;
  } else {
    Serial.println("[OLED] Init failed");
  }

  Serial.println("\n=== SEMS AIoT " FW_VERSION " ===");
  oledShow("SEMS AIoT", "FW: " FW_VERSION, "Booting...", 2000);

  // Build device ID from last 3 bytes of MAC: "AABBCCDD1122"
  { uint8_t mac[6]; WiFi.macAddress(mac);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    mqttDeviceId = buf; }

  loadWifiList();
  fcLoad();
  loadMqttConfig();

  Preferences p;
  p.begin(kNvsWifi, false);
  const bool configModeReboot = p.getBool("cfgMode", false);
  if (configModeReboot) {
    p.putBool("cfgMode", false);
  }
  p.end();

  const bool wifiFailReboot   = (rtcWifiFailMagic == kWifiFailMagic);
  rtcWifiFailMagic = 0;

  // Register unified event handler BEFORE any WiFi/ETH init
  WiFi.onEvent(onNetworkEvent);
  WiFi.setSleep(false); // Disable modem sleep globally

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
    // Mode AP Saja (Config Mode)
    configMode = true;
    WiFi.mode(WIFI_AP);
    startAp();
    startWebServer();
    oledShow("Config Mode", "AP: 192.168.4.1", kApPass, 10000);
    Serial.println("[Boot] Config mode — AP only");

  } else if (wifiCount == 0 || wifiFailReboot) {
    // Masuk Config Mode jika tidak ada wifi tersimpan atau setelah wifi fail reboot
    configMode = true;
    WiFi.mode(WIFI_AP_STA);
    startAp();
    startWebServer();
    // Jangan connectToSTA di config mode — biarkan user scan & pilih via web UI
    if (wifiCount == 0) {
      oledShow("No saved WiFi", "Open 192.168.4.1", "to configure", 8000);
    } else {
      oledShow("WiFi Gagal", "Config Mode", "192.168.4.1", 8000);
    }
  } else {
    // Normal Boot: Hanya STA (Client Mode), AP radio fully OFF!
    configMode = false;
    WiFi.mode(WIFI_STA);
    startWebServer();
    beginStaConnect();
    Serial.println("[Boot] Normal mode — STA only (AP disabled)");
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
  handleConfigButton(now);
  handleBtn2(now);
  handleNtp(now);
  handleEthSync(now);
  handleModbus(now);
  publishMeter(now);
  publishHealth(now);

  // LED heartbeat
  if (now - lastBlinkMs >= 500) {
    lastBlinkMs = now;
    ledState = !ledState;
    digitalWrite(kLedPin, ledState);
  }

  updateOled(now);
}
