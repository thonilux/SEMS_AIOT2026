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

#define FW_VERSION "1.0.0"

// ============================================================
// Hardware constants
// ============================================================
static constexpr uint8_t  kLedPin      = 2;
static constexpr uint8_t  kBtn2Pin     = 4;   // satu-satunya tombol fisik: short=next page, hold=masuk menu/select


static constexpr int kEthCs  = 5;
static constexpr int kEthIrq = 27;
static constexpr int kEthRst = 26;

// ============================================================
// RS485 / Modbus
// ============================================================
static constexpr int kRs485Rx = 16;
static constexpr int kRs485Tx = 17;

static constexpr char kNvsModbus[] = "modbus";

// ============================================================
// Multi-slot meter config (4 slots, round-robin polled)
// ============================================================
static constexpr uint8_t kMaxMeterSlots = 4;

// phase == 1: single-phase, phase == 3: three-phase
// meterType == 0: FP32 Schneider-family (PM2xxx/EM6400) register map
// meterType == 1: Renata AX9L (INT32 block-read, fixed register map)
struct MeterSlotConfig {
  bool     enabled   = false;
  char     label[16] = "";
  uint8_t  meterType = 0;      // 0 = FP32 Schneider-family, 1 = Renata AX9L
  uint8_t  slaveId   = 1;
  uint32_t pollMs    = 3000;   // slower default to ease RS485 bus load
  uint8_t  phase     = 3;      // 1 or 3 (Renata is effectively fixed 3-phase)
  // MQTT publish target, split in two per the gist spec's fixed topic shape
  // "<base>/elc_data/<suffix>" and "<base>/elc_wh/<suffix>" — elc_data/elc_wh
  // is a fixed middle segment, NOT something the user appends themselves.
  // mqttTopic = base path, e.g. "trofis/enms/demo-sems" (no trailing slash).
  // mqttSuffix = the per-meter identity segment, e.g. "slave_1". Empty
  // mqttTopic = not published.
  char     mqttTopic[40] = "";
  char     mqttSuffix[16] = "";
  // Publish schedule, user-configurable in H/M/S from the web UI (stored
  // here as total seconds). Anchored to NTP epoch time (not millis()) so the
  // schedule stays aligned to wall-clock time even across reboots, and
  // multiple slots publishing at the same interval don't collide — each
  // slot is offset by (slotIndex+1) seconds. 0 = use the built-in defaults
  // (kDefaultDataIntervalSec/kDefaultWhIntervalSec).
  uint32_t dataIntervalSec = 0;
  uint32_t whIntervalSec   = 0;
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
static MeterSlotConfig meterSlots[kMaxMeterSlots];

// Single RS485 bus-wide baud + parity — every meter on this bus (Schneider
// AND Renata alike) must physically share one UART framing, since Serial2
// only has one active configuration at a time. Per-slot baud/parity override
// was removed; this is the only knob now. parity: 0=8N1, 1=8E1, 2=8O1, 3=8N2.
static constexpr char kNvsBus[] = "bus";
struct BusConfig {
  uint32_t baud   = 9600;
  uint8_t  parity = 0;  // default 8N1 — matches this deployment's meters
};
static BusConfig busCfg;

static uint32_t busSerialConfig() {
  switch (busCfg.parity) {
    case 0: return SERIAL_8N1;
    case 2: return SERIAL_8O1;
    case 3: return SERIAL_8N2;
    default: return SERIAL_8E1;
  }
}

// ============================================================
// Relay control (4 channels) — NVS namespace "relay"
// ============================================================
static constexpr char kNvsRelay[] = "relay";
static constexpr uint8_t kRelayCount = 4;
static constexpr uint8_t kRelayDefaultPins[kRelayCount] = {2, 15, 14, 13};

struct RelayConfig {
  uint8_t  pin[kRelayCount] = {2, 15, 14, 13};
  bool     activeHigh       = false;  // output polarity — this deployment's relay module is active-LOW
  bool     enabled          = true;   // master enable — false forces all relays OFF
  bool     autoRetryEnabled = false;  // auto-retry after trip
  uint16_t autoRetryDelaySec = 30;    // seconds before retry attempt
  uint16_t currentLimitA    = 0;      // 0 = overcurrent protection disabled
  uint32_t tripDelayMs      = 3000;   // sustained overcurrent duration before trip
};
static RelayConfig relayCfg;

// state: 0=OFF, 1=ON, 2=TRIP
static uint8_t  relayState[kRelayCount]              = {0, 0, 0, 0};
static uint8_t  relayRequestedState[kRelayCount]     = {0, 0, 0, 0};
static uint32_t relayTripUntilMs[kRelayCount]        = {0, 0, 0, 0};
static uint32_t relayOvercurrentSinceMs[kRelayCount] = {0, 0, 0, 0};

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

// Relay control topics use a fixed base per hardware-team spec, independent
// of the user-configurable meter-data topicPrefix:
//   cmd:   trofis/enms/demo-sems/control/relay_<1..4>
//   state: trofis/enms/demo-sems/control/relay_<1..4>/state  (retained, QoS1)
static constexpr char kRelayMqttBase[] = "trofis/enms/demo-sems/control/relay_";

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
// Web Serial — ring buffer (captured via wlog)
// ============================================================
static constexpr uint8_t kWsLines = 80;
static String            wsLines[kWsLines];
static uint8_t           wsHead  = 0;
static uint32_t          wsSeq   = 0;

// ============================================================
// Setting Menu state
// ============================================================
static bool oledInMenu = false;
static uint8_t oledMenuCursor = 0; // 0 = Boot Mode, 1 = View Info, 2 = Exit

// ============================================================
// MQTT stats
// ============================================================
static uint32_t mqttPostCount = 0;

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
// One result struct covers both 1-phase and 3-phase readings (Schneider FP32
// and Renata INT32-block). For 1-phase slots only the "*a" fields plus
// kwh/hz/pf1/pf_avg are meaningful; the web/OLED code branches on cfg.phase.
struct MeterSlotResult {
  // Voltage
  float va = 0, vb = 0, vc = 0;    // V A-N, B-N, C-N (or single-phase V in va)
  float vll = 0, vln = 0;          // L-L avg, L-N avg
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
  bool     valid      = false;   // last poll produced a full, consistent reading
  bool     online     = false;   // slave is responding at all
  uint32_t lastPollMs = 0;
  uint32_t lastOkMs   = 0;
};

static MeterSlotResult meterResults[kMaxMeterSlots];
static uint32_t      modbusLastPollMs = 0;
static uint8_t        modbusCurrentSlot = 0;
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
static uint32_t btn2PressedMs = 0;

// ============================================================
// OLED state
// ============================================================
static bool oledInfoModeActive = false; // true when manually viewing detail pages
static uint32_t oledInfoIdleMs = 0;      // last activity in Info Mode, for auto-timeout back to dashboard
static constexpr uint32_t kOledInfoTimeoutMs = 15000; // Info Mode auto-exit after 15s idle
static uint32_t oledLastActivityMs = 0;  // last button press, for auto-blank (backlight/panel saver)
static bool     oledBlanked        = false;
static constexpr uint32_t kOledBlankTimeoutMs = 60000; // panel blank after 60s idle

// ============================================================
// NTP / RTC
// ============================================================
static bool     ntpSynced   = false;
static uint32_t ntpLastSync = 0;
static constexpr uint32_t kNtpResyncMs = 3600000UL; // resync every 1h
static constexpr char kNtpServer[] = "pool.ntp.org";
static int8_t sysTzHour = 7; // WIB UTC+7 default, loaded dynamically from NVS

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
void getDateStr(char* buf, size_t len);
void requestRelayState(uint8_t index, uint8_t state, const char* reason, bool notifyMqtt = true);
void publishRelayState(uint8_t index);
void updateRelayRuntime(uint32_t nowMs);

// ============================================================
// wlog — serial + web ring buffer
// ============================================================
static void wlog(const char* fmt, ...) {
  char buf[220];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  Serial.println(buf);
  uint32_t ms = millis();
  uint32_t s=ms/1000, m=s/60; s%=60; uint32_t h=m/60; m%=60;
  char ts[240];
  snprintf(ts, sizeof(ts), "[%02lu:%02lu:%02lu.%03lu] %s", h, m, s, ms%1000, buf);
  wsLines[wsHead] = String(ts);
  wsHead = (wsHead + 1) % kWsLines;
  wsSeq++;
}

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
      wlog("[ETH] Cable connected");
      break;

    case ARDUINO_EVENT_ETH_GOT_IP:
      ethReady = true;
      ethIp    = ETH.localIP().toString();
      ETH.setDefault();   // LAN takes routing priority
      wlog("[ETH] IP: %s  speed: %dMbps %s",
           ethIp.c_str(), ETH.linkSpeed(),
           ETH.fullDuplex() ? "FD" : "HD");
      oledShow("LAN Ready", ethIp.c_str(), "W5500", 4000);
      break;

    case ARDUINO_EVENT_ETH_LOST_IP:
      ethReady = false;
      ethIp    = "";
      if (staConnected) WiFi.STA.setDefault();
      wlog("[ETH] IP lost");
      break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
      ethLink  = false;
      ethReady = false;
      ethIp    = "";
      if (staConnected) WiFi.STA.setDefault();
      wlog("[ETH] Disconnected");
      break;

    // --- WiFi STA ---
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      staConnecting = false;
      staConnected  = true;
      rtcWifiFailMagic = 0;
      rtcWifiFailCount = 0;
      wifiFailCycles = 0;
      scanPending   = false;
      fcSave(WiFi.BSSID(), WiFi.channel(), savedSsid);
      if (!ethReady) WiFi.STA.setDefault();
      wlog("[WiFi] Connected: %s  IP: %s", savedSsid.c_str(), WiFi.localIP().toString().c_str());
      if (!configMode) {
        WiFi.softAPdisconnect(true);
        apStarted = false;
      }
      oledShow("WiFi Connected", WiFi.localIP().toString().c_str(), savedSsid.c_str(), 6000);
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (staConnected) {
        staConnected = false;
        wlog("[WiFi] STA disconnected — retrying...");
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
  oledLastActivityMs = millis();
  if (!oledReady) return;
  if (oledBlanked) { oledBlanked = false; oled.setPowerSave(0); }
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

static void drawCheckmark(uint8_t x, uint8_t y) {
  // Simbol centang ✔ premium
  oled.drawVLine(x, y + 2, 3);
  oled.drawPixel(x + 1, y + 3);
  oled.drawVLine(x + 2, y + 1, 4);
  oled.drawPixel(x + 3, y);
}

static void drawCross(uint8_t x, uint8_t y) {
  // Simbol silang ✘ premium
  oled.drawLine(x, y, x + 3, y + 3);
  oled.drawLine(x + 3, y, x, y + 3);
}

static void drawPageDashboard() {
  char timeBuf[12];
  char dateBuf[16];
  getTimeStr(timeBuf, sizeof(timeBuf));
  getDateStr(dateBuf, sizeof(dateBuf));

  // 1. Tampilkan Date & Time besar di bagian atas (Yellow zone)
  oled.setFont(u8g2_font_helvB14_tf);
  oled.drawStr(2, 14, timeBuf);
  
  oled.setFont(u8g2_font_6x10_tf);
  oledDrawRight(dateBuf, 12);
  
  oled.drawHLine(0, 16, 128); // Pembatas yellow/blue zone

  // 2. Tampilkan Ikon & Simbol Status di bagian tengah (Blue zone)
  oled.setFont(u8g2_font_6x12_tf);

  // --- Baris 1: LAN Status ---
  // Ikon Port RJ45 (LAN)
  oled.drawFrame(4, 21, 9, 6);
  oled.drawBox(6, 27, 5, 2);
  // Status Indicator
  if (ethReady) {
    drawCheckmark(20, 22);
    oled.drawStr(32, 28, ethIp.c_str());
  } else {
    drawCross(20, 22);
    oled.drawStr(32, 28, ethLink ? "No IP" : "Cable Off");
  }

  // --- Baris 2: WiFi Status ---
  // Ikon Sinyal WiFi (Bar Graph)
  oled.drawBox(4, 38, 2, 2);
  oled.drawBox(7, 36, 2, 4);
  oled.drawBox(10, 34, 2, 6);
  oled.drawBox(13, 32, 2, 8);
  // Status Indicator
  if (staConnected) {
    drawCheckmark(20, 34);
    // Tampilkan RSSI
    char rssiBuf[12];
    snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", WiFi.RSSI());
    oled.drawStr(32, 40, rssiBuf);
  } else {
    drawCross(20, 34);
    oled.drawStr(32, 40, staConnecting ? "Connecting" : "Offline");
  }

  // --- Baris 3: MQTT Status ---
  // Ikon Cloud MQTT
  oled.drawCircle(6, 49, 2);
  oled.drawCircle(10, 49, 2);
  oled.drawCircle(8, 47, 2);
  oled.drawBox(5, 49, 6, 3);
  // Status Indicator
  if (mqttCfg.enabled && mqttConnected) {
    drawCheckmark(20, 46);
    char mqtBuf[24];
    snprintf(mqtBuf, sizeof(mqtBuf), "Post: %lu", mqttPostCount);
    oled.drawStr(32, 52, mqtBuf);
  } else {
    drawCross(20, 46);
    oled.drawStr(32, 52, mqttCfg.enabled ? "Broker Off" : "Disabled");
  }

  // 3. Rute aktif di bagian paling bawah
  oled.drawHLine(0, 54, 128);
  oled.setFont(u8g2_font_5x7_tf);
  if (configMode) {
    oled.drawStr(2, 62, "Mode: CONFIG (AP Active)");
  } else {
    oled.drawStr(2, 62, ethReady ? "Route: Ethernet (Primary)" : staConnected ? "Route: WiFi Client" : "Route: Offline");
  }
}

static void drawPageDevice() {
  char buf[24];
  
  // Yellow Zone Header
  char timeBuf[8];
  getTimeStr(timeBuf, sizeof(timeBuf));
  oled.setFont(u8g2_font_helvB08_tf);
  oled.drawStr(0, 10, "DEVICE INFO");
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
  switch (page % 5) {
    case 0: drawPageDashboard(); break;
    case 1: drawPageDevice();    break;
    case 2: drawPageWifi();      break;
    case 3: drawPageLan();       break;
    case 4: drawPageMqtt();      break;
  }
  oledDrawOverlay(millis());
  oled.sendBuffer();
}

static void updateOled(uint32_t now) {
  if (!oledReady) return;

  // Panel blank (backlight saver) — independent of page/menu/info-mode state.
  bool idleForBlank = (now - oledLastActivityMs >= kOledBlankTimeoutMs) && (now >= oledUntilMs);
  if (idleForBlank && !oledBlanked) {
    oledBlanked = true;
    oled.setPowerSave(1);
    return;
  }
  if (!idleForBlank && oledBlanked) {
    oledBlanked = false;
    oled.setPowerSave(0);
    // fall through to redraw immediately
  }
  if (oledBlanked) return;

  bool hbTick = (now - oledHbMs >= 500);
  if (hbTick) { oledHbMs = now; oledHbTick++; }

  if (now < oledUntilMs) {
    if (hbTick) { oledDrawOverlay(now); oled.sendBuffer(); }
    return;
  }

  bool needRedraw = false;
  if (oledInMenu) {
    needRedraw = hbTick || (btn2PressedMs > 0); // redraw untuk feedback hold timer/blink
  } else if (!oledInfoModeActive) {
    // Mode normal = selalu di Dashboard (Page 0)
    if (oledPage != 0) {
      oledPage = 0;
      oledPageMs = now;
    }
    needRedraw = hbTick;
  } else {
    // Info Mode (Manual paging) — auto-exit ke dashboard kalo idle lewat timeout
    if (now - oledInfoIdleMs >= kOledInfoTimeoutMs) {
      oledInfoModeActive = false;
      oledPage = 0;
      oledPageMs = now;
      needRedraw = true;
    } else {
      needRedraw = hbTick;
    }
  }

  if (needRedraw) {
    oled.clearBuffer();
    if (oledInMenu) {
      // Draw Setting Menu
      oled.setFont(u8g2_font_helvB08_tf);
      oled.drawStr(0, 10, "SETTING MENU");
      oled.drawHLine(0, 14, 128);
      
      oled.setFont(u8g2_font_6x12_tf);
      
      // Option 0: Boot Mode
      if (oledMenuCursor == 0) oled.drawStr(2, 26, "> 1. Boot Mode");
      else oled.drawStr(10, 26, "1. Boot Mode");
      oled.drawStr(98, 26, configMode ? "[CFG]" : "[NRM]");
      
      // Option 1: View Info
      if (oledMenuCursor == 1) oled.drawStr(2, 38, "> 2. View Info");
      else oled.drawStr(10, 38, "2. View Info");
      
      // Option 2: Exit
      if (oledMenuCursor == 2) oled.drawStr(2, 50, "> 3. Exit");
      else oled.drawStr(10, 50, "3. Exit");
      
      // Progress/Hint bar at bottom
      oled.drawHLine(0, 55, 128);
      oled.setFont(u8g2_font_5x7_tf);
      if (btn2PressedMs > 0) {
        // Tampilkan visual bar waktu menahan hold
        uint32_t pressMs = now - btn2PressedMs;
        uint8_t bar = min((uint8_t)(pressMs * 128 / 2000), (uint8_t)128);
        oled.drawBox(0, 57, bar, 7);
      } else {
        oled.drawStr(2, 63, "Tap: Next  |  Hold 2s: Select");
      }
    } else {
      switch (oledPage % 5) {
        case 0: drawPageDashboard(); break;
        case 1: drawPageDevice();    break;
        case 2: drawPageWifi();      break;
        case 3: drawPageLan();       break;
        case 4: drawPageMqtt();      break;
      }
      oledDrawOverlay(now);
    }
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
// NVS — Modbus config (per-slot, namespace "modbus")
// ============================================================
// Key scheme: short prefix "s<N>" + field tag, built via snprintf to avoid
// duplicating 4x the literal strings (mirrors the WiFi-list loop idiom).
// Preferences keys are limited to 15 chars — "sN" + up to 6 chars fits.
static void seedMeterSlotDefaults() {
  // Slot 0: Renata AX9L, wired 1-phase (INT32 block-read, fixed register
  // map — same layout as 3-phase per datasheet, meterType==2 just skips
  // writing the unwired B/C fields).
  meterSlots[0] = MeterSlotConfig();
  meterSlots[0].enabled   = true;
  snprintf(meterSlots[0].label, sizeof(meterSlots[0].label), "Renata1");
  meterSlots[0].meterType = 2;
  meterSlots[0].slaveId   = 1;
  meterSlots[0].pollMs    = 3000;
  // phase=1 controls the MQTT payload SHAPE (B/C fields empty per gist spec).
  meterSlots[0].phase     = 1;
  snprintf(meterSlots[0].mqttTopic, sizeof(meterSlots[0].mqttTopic), "trofis/enms/demo-sems");
  snprintf(meterSlots[0].mqttSuffix, sizeof(meterSlots[0].mqttSuffix), "slave_2");

  // Slot 1: Schneider PM2120 (FP32 register map) — defaults match the
  // register map previously used by the single-meter ModbusConfig.
  meterSlots[1] = MeterSlotConfig();
  meterSlots[1].enabled   = true;
  snprintf(meterSlots[1].label, sizeof(meterSlots[1].label), "PM2120");
  meterSlots[1].meterType = 0;
  meterSlots[1].slaveId   = 2;
  meterSlots[1].pollMs    = 3000;
  meterSlots[1].phase     = 3;
  // Gist spec: slave_1 = 3-phase meter (Floor 1) — Schneider PM2120 is the
  // 3-phase unit in this deployment.
  snprintf(meterSlots[1].mqttTopic, sizeof(meterSlots[1].mqttTopic), "trofis/enms/demo-sems");
  snprintf(meterSlots[1].mqttSuffix, sizeof(meterSlots[1].mqttSuffix), "slave_1");
  meterSlots[1].r3Va      = 3027;
  meterSlots[1].r3Vb      = 3029;
  meterSlots[1].r3Vc      = 3031;
  meterSlots[1].r3Ia      = 2999;
  meterSlots[1].r3Ib      = 3001;
  meterSlots[1].r3Ic      = 3003;
  meterSlots[1].r3Pa      = 3053;
  meterSlots[1].r3Pb      = 3055;
  meterSlots[1].r3Pc      = 3057;
  meterSlots[1].r3Ptot    = 3059;
  meterSlots[1].r3Qa      = 3061;
  meterSlots[1].r3Qb      = 3063;
  meterSlots[1].r3Qc      = 3065;
  meterSlots[1].r3Qtot    = 3067;
  meterSlots[1].r3Sa      = 3069;
  meterSlots[1].r3Sb      = 3071;
  meterSlots[1].r3Sc      = 3073;
  meterSlots[1].r3Stot    = 3075;
  meterSlots[1].r3Pfa     = 3077;
  meterSlots[1].r3Pfb     = 3079;
  meterSlots[1].r3Pfc     = 3081;
  meterSlots[1].r3Pftot   = 3191;
  meterSlots[1].r3Kwh     = 2675;
  meterSlots[1].r3Hz      = 3109;

  // Slots 2/3: disabled/unconfigured for now.
  meterSlots[2] = MeterSlotConfig();
  meterSlots[3] = MeterSlotConfig();
}

// field tags used to build NVS keys "s<N><tag>" — kept short (<=6 chars) so
// the full key stays within the 15-char Preferences limit.
static const char* const kMbU16Tags[] = {
  "r1V","r1A","r1Kw","r1Kvar","r1Kva","r1Pf","r1Kwh","r1Hz",
  "r3Va","r3Vb","r3Vc","r3Vll","r3Vln","r3Ia","r3Ib","r3Ic","r3Iavg",
  "r3Pa","r3Pb","r3Pc","r3Pt","r3Qa","r3Qb","r3Qc","r3Qt",
  "r3Sa","r3Sb","r3Sc","r3St","r3Pfa","r3Pfb","r3Pfc","r3Pft","r3Kwh","r3Hz"
};
static uint16_t* mbU16Field(MeterSlotConfig& c, uint8_t idx) {
  switch (idx) {
    case 0: return &c.r1V;    case 1: return &c.r1A;    case 2: return &c.r1Kw;
    case 3: return &c.r1Kvar; case 4: return &c.r1Kva;  case 5: return &c.r1Pf;
    case 6: return &c.r1Kwh;  case 7: return &c.r1Hz;
    case 8: return &c.r3Va;   case 9: return &c.r3Vb;   case 10: return &c.r3Vc;
    case 11: return &c.r3Vll; case 12: return &c.r3Vln; case 13: return &c.r3Ia;
    case 14: return &c.r3Ib;  case 15: return &c.r3Ic;  case 16: return &c.r3Iavg;
    case 17: return &c.r3Pa;  case 18: return &c.r3Pb;  case 19: return &c.r3Pc;
    case 20: return &c.r3Ptot;case 21: return &c.r3Qa;  case 22: return &c.r3Qb;
    case 23: return &c.r3Qc;  case 24: return &c.r3Qtot;case 25: return &c.r3Sa;
    case 26: return &c.r3Sb;  case 27: return &c.r3Sc;  case 28: return &c.r3Stot;
    case 29: return &c.r3Pfa; case 30: return &c.r3Pfb; case 31: return &c.r3Pfc;
    case 32: return &c.r3Pftot;case 33: return &c.r3Kwh;case 34: return &c.r3Hz;
    default: return nullptr;
  }
}
static constexpr uint8_t kMbU16Count = sizeof(kMbU16Tags) / sizeof(kMbU16Tags[0]);

static void loadModbusConfig() {
  seedMeterSlotDefaults();
  Preferences p; p.begin(kNvsModbus, true);
  if (p.getUChar("init", 0) == 1) {
    for (uint8_t i = 0; i < kMaxMeterSlots; i++) {
      MeterSlotConfig& c = meterSlots[i];
      char key[16];
      snprintf(key, sizeof(key), "s%uen", i);    c.enabled   = p.getBool(key, c.enabled);
      snprintf(key, sizeof(key), "s%ulbl", i);   {
        String lbl = p.getString(key, c.label);
        snprintf(c.label, sizeof(c.label), "%s", lbl.c_str());
      }
      snprintf(key, sizeof(key), "s%uty", i);    c.meterType = (uint8_t)p.getUChar(key, c.meterType);
      snprintf(key, sizeof(key), "s%usl", i);    c.slaveId   = (uint8_t)p.getUChar(key, c.slaveId);
      snprintf(key, sizeof(key), "s%upl", i);    c.pollMs    = p.getUInt(key, c.pollMs);
      snprintf(key, sizeof(key), "s%uph", i);    c.phase     = (uint8_t)p.getUChar(key, c.phase);
      snprintf(key, sizeof(key), "s%utp", i);    {
        String tp = p.getString(key, c.mqttTopic);
        snprintf(c.mqttTopic, sizeof(c.mqttTopic), "%s", tp.c_str());
      }
      snprintf(key, sizeof(key), "s%uts", i);    {
        String ts = p.getString(key, c.mqttSuffix);
        snprintf(c.mqttSuffix, sizeof(c.mqttSuffix), "%s", ts.c_str());
      }
      snprintf(key, sizeof(key), "s%udi", i);    c.dataIntervalSec = p.getUInt(key, c.dataIntervalSec);
      snprintf(key, sizeof(key), "s%uwi", i);    c.whIntervalSec   = p.getUInt(key, c.whIntervalSec);
      for (uint8_t f = 0; f < kMbU16Count; f++) {
        snprintf(key, sizeof(key), "s%u%s", i, kMbU16Tags[f]);
        uint16_t* fp = mbU16Field(c, f);
        *fp = p.getUShort(key, *fp);
      }
    }
    Serial.println("[NVS] Modbus: loaded per-slot config from flash");
  } else {
    Serial.println("[NVS] Modbus: no saved config, using in-RAM slot defaults");
  }
  p.end();
}

static void saveModbusConfig() {
  Preferences p; p.begin(kNvsModbus, false);
  p.putUChar("init", 1);
  for (uint8_t i = 0; i < kMaxMeterSlots; i++) {
    MeterSlotConfig& c = meterSlots[i];
    char key[16];
    snprintf(key, sizeof(key), "s%uen", i);   p.putBool(key, c.enabled);
    snprintf(key, sizeof(key), "s%ulbl", i);  p.putString(key, c.label);
    snprintf(key, sizeof(key), "s%uty", i);   p.putUChar(key, c.meterType);
    snprintf(key, sizeof(key), "s%usl", i);   p.putUChar(key, c.slaveId);
    snprintf(key, sizeof(key), "s%upl", i);   p.putUInt(key, c.pollMs);
    snprintf(key, sizeof(key), "s%uph", i);   p.putUChar(key, c.phase);
    snprintf(key, sizeof(key), "s%utp", i);   p.putString(key, c.mqttTopic);
    snprintf(key, sizeof(key), "s%uts", i);   p.putString(key, c.mqttSuffix);
    snprintf(key, sizeof(key), "s%udi", i);   p.putUInt(key, c.dataIntervalSec);
    snprintf(key, sizeof(key), "s%uwi", i);   p.putUInt(key, c.whIntervalSec);
    for (uint8_t f = 0; f < kMbU16Count; f++) {
      snprintf(key, sizeof(key), "s%u%s", i, kMbU16Tags[f]);
      p.putUShort(key, *mbU16Field(c, f));
    }
  }
  p.end();
}

// ============================================================
// NVS — RS485 bus config (namespace "bus") — single baud+parity shared by
// every meter slot, since Serial2 only has one active UART framing.
// ============================================================
static void loadBusConfig() {
  Preferences p; p.begin(kNvsBus, true);
  busCfg.baud   = p.getUInt("baud", busCfg.baud);
  busCfg.parity = (uint8_t)p.getUChar("parity", busCfg.parity);
  p.end();
  Serial.printf("[NVS] Bus: baud=%lu parity=%u\n", busCfg.baud, busCfg.parity);
}

static void saveBusConfig() {
  Preferences p; p.begin(kNvsBus, false);
  p.putUInt("baud", busCfg.baud);
  p.putUChar("parity", busCfg.parity);
  p.end();
}

// ============================================================
// NVS — Relay config + state (namespace "relay")
// ============================================================
static void loadRelayConfig() {
  Preferences p; p.begin(kNvsRelay, true);
  for (uint8_t i = 0; i < kRelayCount; i++) {
    char key[6]; snprintf(key, sizeof(key), "pin%u", i);
    relayCfg.pin[i] = (uint8_t)p.getUChar(key, kRelayDefaultPins[i]);
  }
  relayCfg.activeHigh       = p.getBool("actHigh", false);
  relayCfg.enabled          = p.getBool("en",       true);
  relayCfg.autoRetryEnabled = p.getBool("arEn",     false);
  relayCfg.autoRetryDelaySec = p.getUShort("arSec", 30);
  relayCfg.currentLimitA    = p.getUShort("iLimA",  0);
  relayCfg.tripDelayMs      = p.getULong("tripMs",  3000);
  for (uint8_t i = 0; i < kRelayCount; i++) {
    char key[6]; snprintf(key, sizeof(key), "req%u", i);
    relayRequestedState[i] = (uint8_t)p.getUChar(key, 0);
  }
  p.end();
  Serial.printf("[NVS] Relay en=%d actHigh=%d pins={%d,%d,%d,%d}\n",
    relayCfg.enabled, relayCfg.activeHigh,
    relayCfg.pin[0], relayCfg.pin[1], relayCfg.pin[2], relayCfg.pin[3]);
}

static void saveRelayConfig() {
  Preferences p; p.begin(kNvsRelay, false);
  for (uint8_t i = 0; i < kRelayCount; i++) {
    char key[6]; snprintf(key, sizeof(key), "pin%u", i);
    p.putUChar(key, relayCfg.pin[i]);
  }
  p.putBool("actHigh",  relayCfg.activeHigh);
  p.putBool("en",       relayCfg.enabled);
  p.putBool("arEn",     relayCfg.autoRetryEnabled);
  p.putUShort("arSec",  relayCfg.autoRetryDelaySec);
  p.putUShort("iLimA",  relayCfg.currentLimitA);
  p.putULong("tripMs",  relayCfg.tripDelayMs);
  p.end();
}

static void saveRelayRequestedStates() {
  Preferences p; p.begin(kNvsRelay, false);
  for (uint8_t i = 0; i < kRelayCount; i++) {
    char key[6]; snprintf(key, sizeof(key), "req%u", i);
    p.putUChar(key, relayRequestedState[i]);
  }
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

static int32_t bytesToLong(const uint8_t* b) {
  // big-endian signed 32-bit: b[0]=MSB b[1] b[2] b[3]=LSB
  uint32_t u = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16)
             | ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
  return (int32_t)u;
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

// Multi-register FC03 (Read Holding Registers) block read, used by the
// Renata AX9L block-read polling path (34/30-register requests). Reuses
// the existing modbusCrc() helper for CRC16 framing. `count` is number of
// 16-bit registers; `out` must be at least count*2 bytes.
static bool modbusReadRegsBlock(uint8_t slaveId, uint16_t regAddr, uint16_t count, uint8_t* out, uint16_t outLen) {
  const uint16_t byteCount = count * 2;
  if (outLen < byteCount) return false;

  uint8_t req[8];
  req[0] = slaveId;
  req[1] = 0x03;
  req[2] = regAddr >> 8;
  req[3] = regAddr & 0xFF;
  req[4] = count >> 8;
  req[5] = count & 0xFF;
  uint16_t crc = modbusCrc(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  while (Serial2.available()) Serial2.read();  // flush
  Serial2.write(req, 8);
  Serial2.flush();

  // expect slaveId + FC + byteCount + data(byteCount) + 2 CRC
  const uint16_t expectedLen = 3 + byteCount + 2;
  uint32_t t = millis();
  while (Serial2.available() < expectedLen && millis() - t < 300);
  if (Serial2.available() < expectedLen) return false;

  static uint8_t resp[3 + 68 + 2];  // big enough for the 34-register block (68 bytes)
  if (expectedLen > sizeof(resp)) return false;
  Serial2.readBytes(resp, expectedLen);

  uint16_t rcrc = modbusCrc(resp, expectedLen - 2);
  if (resp[expectedLen - 2] != (rcrc & 0xFF) || resp[expectedLen - 1] != (rcrc >> 8)) return false;
  if (resp[0] != slaveId || resp[1] != 0x03 || resp[2] != byteCount) return false;

  memcpy(out, &resp[3], byteCount);
  return true;
}

// FC06 "Write Single Register" — used ONLY for parameters explicitly marked
// safe to write in the Renata AX9L manual's "System setting parameter list"
// (CT/PT ratio 0x4801-0x4804, DO switch control 0x480d). Communication
// registers (address/baud/parity, 0x4805-0x480a) are intentionally never
// written by this firmware — a mistake there can silently drop the meter
// off the bus with no way to recover except physical access to the meter's
// own front-panel menu.
static bool modbusWriteSingleReg(uint8_t slaveId, uint16_t regAddr, uint16_t value) {
  uint8_t req[8];
  req[0] = slaveId;
  req[1] = 0x06;
  req[2] = regAddr >> 8;
  req[3] = regAddr & 0xFF;
  req[4] = value >> 8;
  req[5] = value & 0xFF;
  uint16_t crc = modbusCrc(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  while (Serial2.available()) Serial2.read();  // flush
  Serial2.write(req, 8);
  Serial2.flush();

  // Correct FC06 response echoes the request verbatim (8 bytes).
  uint32_t t = millis();
  while (Serial2.available() < 8 && millis() - t < 300);
  if (Serial2.available() < 8) return false;

  uint8_t resp[8];
  Serial2.readBytes(resp, 8);
  for (uint8_t i = 0; i < 8; i++) if (resp[i] != req[i]) return false;
  return true;
}

// Diagnostic single-register read with detailed error reason (for web /api/modbus/ping)
struct ModbusPingResult {
  bool    ok = false;
  uint8_t raw[4] = {0,0,0,0};
  String  error;
};

static ModbusPingResult modbusPing(uint8_t slaveId, uint16_t regAddr) {
  ModbusPingResult r;
  uint8_t req[8];
  req[0] = slaveId;
  req[1] = 0x03;
  req[2] = regAddr >> 8;
  req[3] = regAddr & 0xFF;
  req[4] = 0x00; req[5] = 0x02;
  uint16_t crc = modbusCrc(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  while (Serial2.available()) Serial2.read();
  Serial2.write(req, 8);
  Serial2.flush();

  uint32_t t = millis();
  while (Serial2.available() < 9 && millis() - t < 300);
  int avail = Serial2.available();
  if (avail == 0) { r.error = "Timeout - tidak ada respon sama sekali (cek wiring A/B, GND, baud, slave ID)"; return r; }

  uint8_t resp[9] = {0};
  int n = Serial2.readBytes(resp, avail > 9 ? 9 : avail);
  if (n < 9) {
    r.error = "Respon terlalu pendek (" + String(n) + " byte, harusnya 9) - kemungkinan baud/parity/stopbit salah";
    return r;
  }

  uint16_t rcrc = modbusCrc(resp, 7);
  if (resp[7] != (rcrc & 0xFF) || resp[8] != (rcrc >> 8)) {
    r.error = "CRC mismatch - kemungkinan parity/stopbit tidak cocok dengan meter";
    return r;
  }
  if (resp[0] != slaveId) {
    r.error = "Slave ID di respon beda (dapat " + String(resp[0]) + ", minta " + String(slaveId) + ")";
    return r;
  }
  if (resp[1] == 0x83) {
    r.error = "Modbus exception code " + String(resp[2]) + " (alamat register salah/di luar jangkauan)";
    return r;
  }
  if (resp[1] != 0x03 || resp[2] != 4) {
    r.error = "Format respon tidak sesuai (FC=" + String(resp[1]) + " byteCount=" + String(resp[2]) + ")";
    return r;
  }

  memcpy(r.raw, &resp[3], 4);
  r.ok = true;
  return r;
}

// Decode 4Q Floating Point Power Factor (Schneider PM2xxx format) → signed float -1..1
static float decode4QFpPf(float reg) {
  if (reg > 1.0f)       return  2.0f - reg;  // leading
  else if (reg < -1.0f) return -2.0f - reg;  // leading
  return reg;                                  // lagging or unity
}

// Macro: read 2 holding regs into float; on fail set ok=false and skip rest
// A short inter-request gap is required before each MB_READ — some meters
// (observed on this Schneider PM2120 unit) need a silent interval after
// finishing one FC03 response before they're ready to decode the next
// request; back-to-back requests with zero gap intermittently fail one of
// the ~22 sequential reads in a 3-phase poll, which cascades to "ok=false"
// and discards the entire (otherwise-valid) reading for that cycle.
#define MB_READ(field, reg) \
  delay(15); \
  if (ok && modbusRead2Regs(cfg.slaveId, (reg), raw)) \
    (field) = bytesToFloat(raw); \
  else ok = false;

#define MB_READ4Q(field, reg) \
  delay(15); \
  if (ok && modbusRead2Regs(cfg.slaveId, (reg), raw)) \
    (field) = decode4QFpPf(bytesToFloat(raw)); \
  else ok = false;

// Poll a single Schneider-family FP32 slot (meterType==0). Reads from
// meterSlots[slotIdx], writes into meterResults[slotIdx].
static void pollSchneiderSlot(uint8_t slotIdx, uint32_t now) {
  const MeterSlotConfig& cfg = meterSlots[slotIdx];
  MeterSlotResult& res = meterResults[slotIdx];
  res.lastPollMs = now;

  uint8_t raw[4];
  bool ok = true;

  if (cfg.phase == 1) {
    MeterSlotResult m;
    m.lastPollMs = now;
    MB_READ(m.va,   cfg.r1V)
    MB_READ(m.ia,   cfg.r1A)
    MB_READ(m.ptot, cfg.r1Kw)
    MB_READ(m.qtot, cfg.r1Kvar)
    MB_READ(m.stot, cfg.r1Kva)
    MB_READ4Q(m.pftot, cfg.r1Pf)
    MB_READ(m.kwh,  cfg.r1Kwh)
    MB_READ(m.hz,   cfg.r1Hz)
    if (ok) {
      m.valid = true; m.online = true; m.lastOkMs = now;
      res = m;
      Serial.printf("[MB slot%u 1ph] V=%.1f A=%.3f kW=%.3f PF=%.3f kWh=%.3f Hz=%.2f\n",
        slotIdx, m.va, m.ia, m.ptot, m.pftot, m.kwh, m.hz);
    } else {
      res.valid = false;
      Serial.printf("[MB slot%u 1ph] Poll failed\n", slotIdx);
    }
  } else {
    MeterSlotResult m;
    m.lastPollMs = now;
    // Voltage
    MB_READ(m.va,   cfg.r3Va)
    MB_READ(m.vb,   cfg.r3Vb)
    MB_READ(m.vc,   cfg.r3Vc)
    MB_READ(m.vll,  cfg.r3Vll)
    MB_READ(m.vln,  cfg.r3Vln)
    // Current
    MB_READ(m.ia,   cfg.r3Ia)
    MB_READ(m.ib,   cfg.r3Ib)
    MB_READ(m.ic,   cfg.r3Ic)
    MB_READ(m.iavg, cfg.r3Iavg)
    // Active Power
    MB_READ(m.pa,   cfg.r3Pa)
    MB_READ(m.pb,   cfg.r3Pb)
    MB_READ(m.pc,   cfg.r3Pc)
    MB_READ(m.ptot, cfg.r3Ptot)
    // Reactive Power
    MB_READ(m.qa,   cfg.r3Qa)
    MB_READ(m.qb,   cfg.r3Qb)
    MB_READ(m.qc,   cfg.r3Qc)
    MB_READ(m.qtot, cfg.r3Qtot)
    // Apparent Power
    MB_READ(m.sa,   cfg.r3Sa)
    MB_READ(m.sb,   cfg.r3Sb)
    MB_READ(m.sc,   cfg.r3Sc)
    MB_READ(m.stot, cfg.r3Stot)
    // Power Factor
    MB_READ4Q(m.pfa,  cfg.r3Pfa)
    MB_READ4Q(m.pfb,  cfg.r3Pfb)
    MB_READ4Q(m.pfc,  cfg.r3Pfc)
    MB_READ(m.pftot,  cfg.r3Pftot)
    // Energy + Freq
    MB_READ(m.kwh,  cfg.r3Kwh)
    MB_READ(m.hz,   cfg.r3Hz)
    if (ok) {
      m.valid = true; m.online = true; m.lastOkMs = now;
      res = m;
      Serial.printf("[MB slot%u 3ph] Va=%.1f Vb=%.1f Vc=%.1f Ia=%.3f Ib=%.3f Ic=%.3f Ptot=%.3f kWh=%.3f Hz=%.2f\n",
        slotIdx, m.va, m.vb, m.vc, m.ia, m.ib, m.ic, m.ptot, m.kwh, m.hz);
    } else {
      res.valid = false;
      Serial.printf("[MB slot%u 3ph] Poll failed\n", slotIdx);
    }
  }
}

#undef MB_READ
#undef MB_READ4Q

// Poll a single Renata AX9L slot (meterType==1 3-phase, or meterType==2
// 1-phase) via the fixed INT32 block-read register map (0x4000 count 34,
// 0x4022 count 30) — the register layout is identical for both wiring modes
// per the AX9L datasheet, only the number of physically-connected phases
// differs. Follows the reference implementation exactly (addresses/multipliers).
//
// meterType==2 (1-phase) reads the SAME block (RS485 can't request a sparser
// subset any cheaper — the needed fields span the whole range), but skips
// WRITING phase-B/C fields into the result, leaving them at their default
// (0/NAN) instead of a real-but-meaningless reading from an unwired phase.
static void pollRenataSlot(uint8_t slotIdx, uint32_t now) {
  const MeterSlotConfig& cfg = meterSlots[slotIdx];
  MeterSlotResult& res = meterResults[slotIdx];
  res.lastPollMs = now;

  uint8_t buf1[68];  // 34 regs * 2 bytes (0x4000..0x4021)
  uint8_t buf2[60];  // 30 regs * 2 bytes (0x4022..0x403F)

  bool ok1 = modbusReadRegsBlock(cfg.slaveId, 0x4000, 34, buf1, sizeof(buf1));
  delay(100);  // RS485 bus settling between the two block reads
  bool ok2 = modbusReadRegsBlock(cfg.slaveId, 0x4022, 30, buf2, sizeof(buf2));

  if (!ok1 || !ok2) {
    res.valid = false;
    Serial.printf("[MB slot%u Renata] Poll failed ok1=%d ok2=%d\n", slotIdx, ok1, ok2);
    return;
  }

  auto readVal1 = [&](uint16_t reg, float multiplier) -> float {
    uint16_t offset = (reg - 0x4000) * 2;
    return bytesToLong(&buf1[offset]) * multiplier;
  };
  auto readVal2 = [&](uint16_t reg, float multiplier) -> float {
    uint16_t offset = (reg - 0x4022) * 2;
    return bytesToLong(&buf2[offset]) * multiplier;
  };

  const bool onePhase = (cfg.meterType == 2);

  MeterSlotResult m;
  m.lastPollMs = now;
  m.va  = readVal1(0x4000, 0.1f);
  m.vll = readVal1(0x4006, 0.1f);  // uab (L-L, phase A-B) reused as vll
  m.vln = 0;                       // Renata has no direct L-N avg register
  m.ia  = readVal1(0x400C, 0.001f);
  m.iavg = 0;                      // not present in Renata register map
  m.pa   = readVal1(0x4012, 0.1f) / 1000.0f;  // W -> kW
  m.ptot = readVal1(0x4018, 0.1f) / 1000.0f;
  m.qa   = readVal1(0x401A, 0.1f) / 1000.0f;  // var -> kvar
  m.qtot = readVal1(0x4020, 0.1f) / 1000.0f;
  m.sa   = readVal2(0x4022, 0.1f) / 1000.0f;  // VA -> kVA
  m.stot = readVal2(0x4028, 0.1f) / 1000.0f;
  m.pfa   = readVal2(0x402A, 0.001f);
  m.pftot = readVal2(0x4030, 0.001f);
  m.hz    = readVal2(0x4032, 0.01f);
  m.kwh   = readVal2(0x4034, 0.01f);  // kwh_total

  if (onePhase) {
    // Phase B/C unwired on a 1-phase installation — leave at struct defaults
    // (0) rather than write a real-but-meaningless register value.
    m.vb = 0; m.vc = 0; m.ib = 0; m.ic = 0;
    m.pb = 0; m.pc = 0; m.qb = 0; m.qc = 0; m.sb = 0; m.sc = 0;
    m.pfb = 0; m.pfc = 0;
  } else {
    m.vb  = readVal1(0x4002, 0.1f);
    m.vc  = readVal1(0x4004, 0.1f);
    m.ib  = readVal1(0x400E, 0.001f);
    m.ic  = readVal1(0x4010, 0.001f);
    m.pb   = readVal1(0x4014, 0.1f) / 1000.0f;
    m.pc   = readVal1(0x4016, 0.1f) / 1000.0f;
    m.qb   = readVal1(0x401C, 0.1f) / 1000.0f;
    m.qc   = readVal1(0x401E, 0.1f) / 1000.0f;
    m.sb   = readVal2(0x4024, 0.1f) / 1000.0f;
    m.sc   = readVal2(0x4026, 0.1f) / 1000.0f;
    m.pfb   = readVal2(0x402C, 0.001f);
    m.pfc   = readVal2(0x402E, 0.001f);
  }

  m.valid = true; m.online = true; m.lastOkMs = now;
  res = m;
  Serial.printf("[MB slot%u Renata] Va=%.1f Vb=%.1f Vc=%.1f Ia=%.3f Ib=%.3f Ic=%.3f Ptot=%.3f kWh=%.3f Hz=%.2f\n",
    slotIdx, m.va, m.vb, m.vc, m.ia, m.ib, m.ic, m.ptot, m.kwh, m.hz);
}

// Round-robin poll loop: advances to the next enabled slot each poll
// interval, reconfigures Serial2 for that slot's baud/framing, and
// dispatches to the appropriate meterType handler.
static void handleModbus(uint32_t now) {
  // Count enabled slots; bail out early if none configured.
  bool anyEnabled = false;
  for (uint8_t i = 0; i < kMaxMeterSlots; i++) {
    if (meterSlots[i].enabled) { anyEnabled = true; break; }
  }
  if (!anyEnabled) return;

  // Use the current slot's poll interval to gate timing (simple, shared
  // cadence across slots — good enough for this stage).
  uint32_t pollMs = meterSlots[modbusCurrentSlot].enabled
    ? meterSlots[modbusCurrentSlot].pollMs : 1000;
  if (now - modbusLastPollMs < pollMs) return;
  modbusLastPollMs = now;

  // Advance to next enabled slot (wrap around).
  uint8_t next = modbusCurrentSlot;
  for (uint8_t tries = 0; tries < kMaxMeterSlots; tries++) {
    next = (next + 1) % kMaxMeterSlots;
    if (meterSlots[next].enabled) break;
  }
  modbusCurrentSlot = next;
  if (!meterSlots[modbusCurrentSlot].enabled) return;  // safety guard

  const MeterSlotConfig& cfg = meterSlots[modbusCurrentSlot];
  bool isRenata = (cfg.meterType == 1 || cfg.meterType == 2);
  // Serial2 is configured once for the whole bus (single baud+parity shared
  // by every slot) — see setup() / applyBusSerialConfig(). No per-slot
  // reconfigure needed here anymore.

  if (isRenata) {
    pollRenataSlot(modbusCurrentSlot, now);
  } else {
    pollSchneiderSlot(modbusCurrentSlot, now);
  }
}

// Timestamp in the gist spec's literal format: "YYYY-MM-DD HH:MM:SS".
// Falls back to a zeroed epoch string if NTP hasn't synced yet.
static void gistTimestamp(char* buf, size_t len) {
  struct tm ti;
  if (ntpSynced && getLocalTime(&ti, 0)) {
    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
      ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
  } else {
    snprintf(buf, len, "1970-01-01 00:00:00");
  }
}

// Publishes a raw, absolute-path MQTT topic — bypasses mqttPublish()'s
// <topicPrefix>/<deviceId>/ prefixing, since the hardware-team gist spec
// requires a fixed absolute topic tree independent of device configuration.
static void mqttPublishAbsolute(const String& topic, const String& payload) {
  if (!mqttConnected) return;
  bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), false); // retain=false per spec
  Serial.printf("[MQTT] publish %s (%d bytes) %s\n", topic.c_str(), payload.length(), ok ? "OK" : "FAIL");
  if (ok) { mqttTxUntilMs = millis() + 1000; mqttPostCount++; }
}

// Extracts the trailing integer from mqttSuffix, e.g. "slave_2" -> 2. Per
// the gist spec, the body's "slave_id" field must match this suffix number
// (the ingest worker parses the authoritative slave_id from the TOPIC, but
// the body value must stay consistent with it) — it is NOT the Modbus RTU
// slave address (cfg.slaveId), which is an unrelated, independently-
// configured number. Falls back to cfg.slaveId if suffix has no digits.
static uint32_t gistIdFromSuffix(const MeterSlotConfig& cfg) {
  String s = cfg.mqttSuffix;
  int i = s.length() - 1;
  if (i < 0 || !isDigit(s[i])) return cfg.slaveId;
  int end = i + 1;
  while (i >= 0 && isDigit(s[i])) i--;
  return (uint32_t)s.substring(i + 1, end).toInt();
}

// Publishes one enabled/valid slot's real-time reading to
// "<mqttTopic>/elc_data/<mqttSuffix>" (nested JSON, string-valued electrical
// fields per the gist spec — elc_data is a FIXED middle segment, not
// appended after the user's topic). Payload shape (1-phase vs 3-phase)
// follows cfg.phase (Mode Fasa), independent of how the slot is actually
// read over Modbus — e.g. a Renata AX9L polled via the 3-phase-shaped
// block-read register map can still be configured phase=1 to report as a
// 1-phase meter in this payload.
static void publishElcData(uint8_t i) {
  const MeterSlotConfig& cfg = meterSlots[i];
  const MeterSlotResult& m   = meterResults[i];
  if (!cfg.enabled || !m.valid || cfg.mqttTopic[0] == '\0') return;

  char ts[24];
  gistTimestamp(ts, sizeof(ts));
  uint32_t gistId = gistIdFromSuffix(cfg);

  char buf[512];
  if (cfg.phase == 1) {
    // 1-phase: B/C fields empty strings, totals equal phase-A values (per spec).
    snprintf(buf, sizeof(buf),
      "{\"timestamp\":\"%s\",\"slave_id\":%u,"
      "\"voltage\":{\"ua\":\"%.1f\",\"ub\":\"\",\"uc\":\"\",\"uab\":\"\",\"ubc\":\"\",\"uca\":\"\"},"
      "\"current\":{\"ia\":\"%.3f\",\"ib\":\"\",\"ic\":\"\"},"
      "\"power\":{\"active\":{\"pa\":\"%.1f\",\"pb\":\"\",\"pc\":\"\",\"total\":\"%.1f\"},"
      "\"reactive\":{\"qa\":\"%.1f\",\"qb\":\"\",\"qc\":\"\",\"total\":\"%.1f\"},"
      "\"apparent\":{\"sa\":\"%.1f\",\"sb\":\"\",\"sc\":\"\",\"total\":\"%.1f\"}},"
      "\"power_factor\":{\"pf1\":\"%.3f\",\"pf2\":\"\",\"pf3\":\"\",\"avg\":\"%.3f\"},"
      "\"frequency\":\"%.2f\"}",
      ts, gistId,
      m.va, m.ia,
      m.ptot * 1000.0f, m.ptot * 1000.0f,   // kW -> W
      m.qtot * 1000.0f, m.qtot * 1000.0f,   // kvar -> var
      m.stot * 1000.0f, m.stot * 1000.0f,   // kVA -> VA
      m.pftot, m.pftot,
      m.hz);
  } else {
    snprintf(buf, sizeof(buf),
      "{\"timestamp\":\"%s\",\"slave_id\":%u,"
      "\"voltage\":{\"ua\":\"%.1f\",\"ub\":\"%.1f\",\"uc\":\"%.1f\",\"uab\":\"%.1f\",\"ubc\":\"\",\"uca\":\"\"},"
      "\"current\":{\"ia\":\"%.3f\",\"ib\":\"%.3f\",\"ic\":\"%.3f\"},"
      "\"power\":{\"active\":{\"pa\":\"%.1f\",\"pb\":\"%.1f\",\"pc\":\"%.1f\",\"total\":\"%.1f\"},"
      "\"reactive\":{\"qa\":\"%.1f\",\"qb\":\"%.1f\",\"qc\":\"%.1f\",\"total\":\"%.1f\"},"
      "\"apparent\":{\"sa\":\"%.1f\",\"sb\":\"%.1f\",\"sc\":\"%.1f\",\"total\":\"%.1f\"}},"
      "\"power_factor\":{\"pf1\":\"%.3f\",\"pf2\":\"%.3f\",\"pf3\":\"%.3f\",\"avg\":\"%.3f\"},"
      "\"frequency\":\"%.2f\"}",
      ts, gistId,
      m.va, m.vb, m.vc, m.vll,
      m.ia, m.ib, m.ic,
      m.pa * 1000.0f, m.pb * 1000.0f, m.pc * 1000.0f, m.ptot * 1000.0f,
      m.qa * 1000.0f, m.qb * 1000.0f, m.qc * 1000.0f, m.qtot * 1000.0f,
      m.sa * 1000.0f, m.sb * 1000.0f, m.sc * 1000.0f, m.stot * 1000.0f,
      m.pfa, m.pfb, m.pfc, m.pftot,
      m.hz);
  }
  String dataTopic = String(cfg.mqttTopic) + "/elc_data/" + String(cfg.mqttSuffix);
  mqttPublishAbsolute(dataTopic, buf);
}

// Publishes one enabled/valid slot's accumulated energy to
// "<mqttTopic>/elc_wh/<mqttSuffix>" — "wh" is numeric (not string) per spec.
static void publishElcWh(uint8_t i) {
  const MeterSlotConfig& cfg = meterSlots[i];
  const MeterSlotResult& m   = meterResults[i];
  if (!cfg.enabled || !m.valid || cfg.mqttTopic[0] == '\0') return;

  char ts[24];
  gistTimestamp(ts, sizeof(ts));
  uint32_t gistId = gistIdFromSuffix(cfg);

  char eBuf[80];
  snprintf(eBuf, sizeof(eBuf), "{\"timestamp\":\"%s\",\"slave_id\":%u,\"wh\":%.1f}",
    ts, gistId, m.kwh * 1000.0); // kWh -> Wh
  String whTopic = String(cfg.mqttTopic) + "/elc_wh/" + String(cfg.mqttSuffix);
  mqttPublishAbsolute(whTopic, eBuf);
}

// RTC-scheduled publish, anchored to NTP epoch time (not millis()) so the
// schedule survives reboots and stays aligned to wall-clock time. Interval
// is user-configurable per slot (H/M/S from the web UI, stored as total
// seconds in dataIntervalSec/whIntervalSec) — this is the flexibility this
// scheme exists for: a demo unit wants fast updates (e.g. every few
// seconds) while a production install wants infrequent ones (e.g. every 15
// minutes) to save bandwidth, and that choice shouldn't require a firmware
// change. Each slot is offset by (slotIndex+1) seconds so multiple slots on
// the same interval don't publish in the same tick and collide.
// Requires NTP sync; falls back to the old fixed-interval scheme (every
// kMeterPublishMs) if NTP hasn't synced yet, so publishing still works
// before time sync completes.
static constexpr uint32_t kDefaultDataIntervalSec = 20;  // matches the old 3x/minute cadence
static constexpr uint32_t kDefaultWhIntervalSec    = 60;  // matches the old 1x/minute cadence
static time_t lastPublishedDataEpoch[kMaxMeterSlots] = {0, 0, 0, 0};
static time_t lastPublishedWhEpoch[kMaxMeterSlots]   = {0, 0, 0, 0};

static void publishMeter(uint32_t now) {
  if (!mqttConnected) return;

  struct tm ti;
  if (!ntpSynced || !getLocalTime(&ti, 0)) {
    // No RTC yet — fall back to the old simple interval so data still flows.
    if (now - mqttMeterLastMs < kMeterPublishMs) return;
    mqttMeterLastMs = now;
    for (uint8_t i = 0; i < kMaxMeterSlots; i++) { publishElcData(i); publishElcWh(i); }
    return;
  }

  time_t epoch = time(nullptr);
  for (uint8_t i = 0; i < kMaxMeterSlots; i++) {
    const MeterSlotConfig& cfg = meterSlots[i];
    uint32_t dataIv = cfg.dataIntervalSec > 0 ? cfg.dataIntervalSec : kDefaultDataIntervalSec;
    uint32_t whIv   = cfg.whIntervalSec   > 0 ? cfg.whIntervalSec   : kDefaultWhIntervalSec;
    time_t offset = i + 1;  // stagger slots so same-interval publishes don't collide

    if ((epoch - offset) % dataIv == 0 && lastPublishedDataEpoch[i] != epoch) {
      lastPublishedDataEpoch[i] = epoch;
      publishElcData(i);
    }
    if ((epoch - offset) % whIv == 0 && lastPublishedWhEpoch[i] != epoch) {
      lastPublishedWhEpoch[i] = epoch;
      publishElcWh(i);
    }
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
  if (ok) {
    mqttTxUntilMs = millis() + 1000;
    mqttPostCount++;
  }
}

// ============================================================
// Relay control (4 channels)
// ============================================================
// GPIO output helper — respects activeHigh/Low polarity from relayCfg.
static void setRelayOutput(uint8_t index, uint8_t state) {
  if (index >= kRelayCount) return;
  const bool on = (state == 1);
  const bool outputHigh = relayCfg.activeHigh ? on : !on;
  digitalWrite(relayCfg.pin[index], outputHigh ? HIGH : LOW);
  relayState[index] = state;
}

// Publish per-relay retained state topic per spec:
//   trofis/enms/demo-sems/control/relay_<N>/state  payload "0"/"1"/"2" (raw byte, QoS1, retained)
void publishRelayState(uint8_t index) {
  if (index >= kRelayCount) return;
  if (!mqttConnected) return;
  String topic = String(kRelayMqttBase) + String(index + 1) + "/state";
  char payload[2]; snprintf(payload, sizeof(payload), "%u", relayState[index]);
  bool ok = mqttClient.publish(topic.c_str(), (const uint8_t*)payload, 1, true); // retain=true
  Serial.printf("[Relay] publish %s = %s %s\n", topic.c_str(), payload, ok ? "OK" : "FAIL");
}

static bool canTurnRelayOn(uint8_t index) {
  if (index >= kRelayCount) return false;
  if (!relayCfg.enabled) return false;
  if (relayState[index] == 2 && millis() < relayTripUntilMs[index]) return false;
  return true;
}

// Apply a requested ON/OFF state to a relay: validates against protection
// state, drives the GPIO, persists to NVS, and (optionally) publishes the
// retained MQTT state topic.
void requestRelayState(uint8_t index, uint8_t state, const char* reason, bool notifyMqtt) {
  if (index >= kRelayCount) return;

  if (!relayCfg.enabled) {
    setRelayOutput(index, 0);
    relayRequestedState[index] = 0;
    Serial.println("[Relay] request ignored: relay disabled");
    if (notifyMqtt) publishRelayState(index);
    return;
  }

  if (state == 1 && !canTurnRelayOn(index)) {
    Serial.printf("[Relay] R%u ON blocked: %s\n", index + 1, reason);
    return;
  }

  const uint8_t targetState = (state == 1) ? 1 : 0;
  relayRequestedState[index] = targetState;
  setRelayOutput(index, targetState);
  saveRelayRequestedStates();

  Serial.printf("[Relay] R%u state set: %s reason=%s\n",
    index + 1, targetState == 1 ? "ON" : "OFF", reason);

  if (notifyMqtt) publishRelayState(index);
}

static void tripRelay(uint8_t index, const char* reason, uint32_t nowMs) {
  if (index >= kRelayCount) return;
  relayTripUntilMs[index] = nowMs + (uint32_t)relayCfg.autoRetryDelaySec * 1000UL;
  relayOvercurrentSinceMs[index] = 0;
  relayRequestedState[index] = 0;
  setRelayOutput(index, 2); // 2 = TRIP
  Serial.printf("[Relay] R%u TRIPPED: %s\n", index + 1, reason);
  publishRelayState(index);
}

// Called from loop(): handles auto-retry after trip and (if configured)
// single-meter overcurrent protection. This firmware only has one meter,
// so overcurrent trip applies the shared meter current to all relays.
void updateRelayRuntime(uint32_t nowMs) {
  if (!relayCfg.enabled) {
    for (uint8_t i = 0; i < kRelayCount; i++) {
      if (relayState[i] != 0) setRelayOutput(i, 0);
      relayTripUntilMs[i] = 0;
      relayOvercurrentSinceMs[i] = 0;
    }
    return;
  }

  // 1. Auto-retry for tripped relays
  for (uint8_t i = 0; i < kRelayCount; i++) {
    if (relayState[i] == 2 && relayCfg.autoRetryEnabled && nowMs >= relayTripUntilMs[i]) {
      Serial.printf("[Relay] R%u auto-retry unlocked\n", i + 1);
      setRelayOutput(i, relayRequestedState[i]);
      publishRelayState(i);
    }
  }

  // 2. Overcurrent protection using slot 0 meter current reading
  // STAGE NOTE: temporary simplification — only slot 0 is used for
  // overcurrent protection until multi-slot relay linkage is implemented.
  if (relayCfg.currentLimitA > 0) {
    float cur = NAN;
    if (meterSlots[0].enabled && meterResults[0].valid) {
      cur = (meterSlots[0].phase == 1) ? meterResults[0].ia : meterResults[0].iavg;
    }

    if (!isnan(cur) && cur > relayCfg.currentLimitA) {
      for (uint8_t i = 0; i < kRelayCount; i++) {
        if (relayOvercurrentSinceMs[i] == 0) {
          relayOvercurrentSinceMs[i] = nowMs;
        } else if (nowMs - relayOvercurrentSinceMs[i] >= relayCfg.tripDelayMs) {
          if (relayState[i] != 2) {
            char reasonBuf[48];
            snprintf(reasonBuf, sizeof(reasonBuf), "overcurrent (%.2fA > %uA)", cur, relayCfg.currentLimitA);
            tripRelay(i, reasonBuf, nowMs);
          }
        }
      }
    } else {
      for (uint8_t i = 0; i < kRelayCount; i++) relayOvercurrentSinceMs[i] = 0;
    }
  }
}

// MQTT callback — handles per-relay command topics:
//   trofis/enms/demo-sems/control/relay_<1..4>  payload "0"/"1" (raw byte, QoS1)
static void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  String t(topic);
  if (!t.startsWith(kRelayMqttBase)) return;
  if (t.endsWith("/state")) return; // ignore echo of our own state topic

  String suffix = t.substring(strlen(kRelayMqttBase)); // "N"
  int idx = suffix.toInt();
  if (idx < 1 || idx > kRelayCount) return;

  if (length == 0) return;
  bool on = (payload[0] == '1');
  requestRelayState((uint8_t)(idx - 1), on ? 1 : 0, "mqtt", true);
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
  mqttClient.setCallback(mqttCallback);
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
    // Subscribe to per-relay command topics (QoS1) and republish current
    // retained state so the broker/UI reflects reality right after (re)connect.
    for (uint8_t i = 0; i < kRelayCount; i++) {
      String cmdTopic = String(kRelayMqttBase) + String(i + 1);
      mqttClient.subscribe(cmdTopic.c_str(), 1);
      publishRelayState(i);
    }
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
  bool ok = WiFi.softAP(ssid.c_str(), kApPass);
  if (!ok) {
    // WiFi radio may not be ready immediately after ETH/SPI init (PLL lock,
    // mode switch settling) — retry once after a short delay instead of
    // silently reporting apStarted=true while nothing is actually broadcasting.
    Serial.println("[AP] softAP() failed on first attempt — retrying...");
    delay(300);
    ok = WiFi.softAP(ssid.c_str(), kApPass);
  }
  apStarted = ok;
  if (ok) {
    Serial.printf("[AP] %s  192.168.4.1\n", ssid.c_str());
    oledShow("AP Ready", ssid.c_str(), "192.168.4.1", 6000);
  } else {
    Serial.println("[AP] softAP() FAILED after retry — AP not broadcasting!");
    oledShow("AP FAILED", "Check WiFi radio", "", 6000);
  }
}

void restoreAp() {
  if (apStarted) return;
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  const String ssid = getApSsid();
  apStarted = WiFi.softAP(ssid.c_str(), kApPass);
  Serial.println(apStarted ? "[AP] Restored" : "[AP] Restore FAILED — softAP() returned false");
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

// GPIO4 — short press: next OLED page / move menu cursor; long press: enter menu / select option / exit info mode
static void handleBtn2(uint32_t now) {
  const bool pressed = (digitalRead(kBtn2Pin) == HIGH);
  if (pressed) {
    oledLastActivityMs = now; // any press wakes panel + resets blank timer
    if (btn2PressedMs == 0) btn2PressedMs = now;
    else {
      uint32_t holdTime = now - btn2PressedMs;
      // Jika sedang di dalam mode "View Info" (bukan di dashboard), hold 2s untuk kembali
      if (oledInfoModeActive && !oledInMenu && oledPage != 0) {
        if (holdTime >= 2000) {
          btn2PressedMs = 0;
          oledInfoModeActive = false;
          oledPage = 0;
          oledPageMs = now;
          oledShow("Dashboard", "Returned", "", 1500);
        }
      }
      // Di luar menu (dan bukan View Info mode): hold 3 detik masuk menu
      else if (!oledInMenu) {
        if (holdTime >= 3000) {
          btn2PressedMs = 0;
          oledInMenu = true;
          oledMenuCursor = 0;
        }
      } 
      // Di dalam menu setting: hold 2 detik pilih opsi
      else {
        if (holdTime >= 2000) {
          btn2PressedMs = 0;
          if (oledMenuCursor == 0) {
            oledInMenu = false;
            if (configMode) {
              Preferences p; p.begin(kNvsWifi, false); p.putBool("cfgMode", false); p.end();
              oledShow("Normal Mode", "Rebooting...", "", 3000);
              delay(500); ESP.restart();
            } else {
              enterConfigMode();
            }
          } else if (oledMenuCursor == 1) {
            // View Info: aktifkan oledInfoModeActive dan manual paging
            oledInfoModeActive = true;
            oledInMenu = false;
            oledPage = 1; // start from page 1 (Device Info)
            oledPageMs = now;
            oledInfoIdleMs = now;
            oledShow("Info Mode", "Manual Paging...", "Hold 2s to exit", 3000);
          } else if (oledMenuCursor == 2) {
            oledInMenu = false;
            oledShow("Settings", "Exited", "", 1500);
          }
        }
      }
    }
  } else {
    if (btn2PressedMs > 0) {
      uint32_t pressDuration = now - btn2PressedMs;
      btn2PressedMs = 0;
      if (pressDuration < 2000) {
        // Short press
        if (oledInMenu) {
          // Pindah cursor menu (3 opsi: 0..2)
          oledMenuCursor = (oledMenuCursor + 1) % 3;
        } else if (oledInfoModeActive) {
          // Di dalam view info mode, short press langsung lompat ke halaman info berikutnya
          oledPage = (oledPage + 1) % 5;
          if (oledPage == 0) oledPage = 1; // skip kembali ke dashboard saat manual paging
          oledPageMs = now;
          oledInfoIdleMs = now; // reset idle timeout tiap tombol ditekan
          oledDrawPage(oledPage);
        } else {
          // Di luar info mode/dalam mode dashboard: tap sekali paksa redraw dashboard
          oledPage = 0;
          oledPageMs = now;
          oledDrawPage(0);
        }
      }
    }
  }
}

// ============================================================
// NTP
// ============================================================
void ntpBeginSync() {
  wlog("[NTP] Start sync...");
  configTime(sysTzHour * 3600, 0, kNtpServer);
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

// Helper: get date string "DD-MM-YYYY" or "--/--/----" if not synced
void getDateStr(char* buf, size_t len) {
  if (!ntpSynced) { snprintf(buf, len, "--/--/----"); return; }
  struct tm ti;
  if (getLocalTime(&ti, 0)) {
    snprintf(buf, len, "%02d/%02d/%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
  } else {
    snprintf(buf, len, "--/--/----");
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
  ".pbar{height:8px;background:#e2e8f0;border-radius:99px;overflow:hidden;margin-top:4px}"
  ".pfill{height:100%;background:#0f766e;border-radius:99px}"
  ".pfill.warn{background:#d97706}.pfill.bad{background:#dc2626}"
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
  nav += "<a href=/relay>&#128268; Relay</a>";
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
  html.reserve(4500);
  html += FPSTR(kStyle);
  html += F("<title>SEMS Setup</title></head><body><div class=wrap>"
            "<h1>SEMS AIoT &mdash; Setup</h1>");
  html += getNav();

  // Hardware card
  html += F("<div class=card><div class=card-title>Hardware</div>");
  html += "<div class=row><span class=label>Chip</span><span class=val>ESP32 ";
  html += ESP.getChipModel(); html += " rev"; html += ESP.getChipRevision();
  html += F("</span></div><div class=sep></div>");

  // Flash usage (sketch size / total flash), as a percentage progress bar.
  {
    uint32_t flashTotal = ESP.getFlashChipSize();
    uint32_t flashUsed  = ESP.getSketchSize();
    uint8_t  flashPct   = flashTotal ? (uint8_t)((uint64_t)flashUsed * 100 / flashTotal) : 0;
    const char* flashCls = flashPct >= 90 ? " bad" : (flashPct >= 75 ? " warn" : "");
    html += "<div class=row><span class=label>Flash</span><span class=val>";
    html += flashPct; html += F("% (");
    html += flashUsed / 1024; html += F(" / "); html += flashTotal / 1024;
    html += F(" kB)</span></div><div class=pbar><div class=\"pfill");
    html += flashCls; html += F("\" style=\"width:"); html += flashPct;
    html += F("%\"></div></div>");
  }

  // Free heap, shown as USED percentage (fuller bar = less headroom left).
  {
    uint32_t heapTotal = ESP.getHeapSize();
    uint32_t heapFree  = ESP.getFreeHeap();
    uint32_t heapUsed  = heapTotal > heapFree ? heapTotal - heapFree : 0;
    uint8_t  heapPct   = heapTotal ? (uint8_t)((uint64_t)heapUsed * 100 / heapTotal) : 0;
    const char* heapCls = heapPct >= 90 ? " bad" : (heapPct >= 75 ? " warn" : "");
    html += "<div class=row><span class=label>Heap Used</span><span class=val>";
    html += heapPct; html += F("% (");
    html += heapFree / 1024; html += F(" kB free)</span></div><div class=pbar><div class=\"pfill");
    html += heapCls; html += F("\" style=\"width:"); html += heapPct;
    html += F("%\"></div></div>");
  }
  html += F("<div class=sep></div>");
  html += "<div class=row><span class=label>MAC</span><span class=val style='font-family:monospace;font-size:12px'>";
  html += mac;
  html += F("</span></div><div class=row><span class=label>Firmware</span><span class=val>v" FW_VERSION "</span></div>");
  html += "<div class=row><span class=label>Uptime</span><span class=val>"; html += uptime;
  html += F("</span></div></div>");

  // Config links
  html += F("<div class=card><div class=card-title>Konfigurasi</div>"
            "<a class='btn btn-sm' href=/network style='text-decoration:none'>&#127760; Network &amp; WiFi</a> "
            "<a class='btn btn-sm' href=/mqtt style='text-decoration:none'>&#128236; MQTT</a></div>");

  // Timezone Status Card (Read-only)
  char tzBuf[32];
  snprintf(tzBuf, sizeof(tzBuf), "UTC %s%d", (sysTzHour >= 0) ? "+" : "", sysTzHour);
  html += F("<div class=card><div class=card-title>Waktu & Timezone</div>");
  html += "<div class=row><span class=label>Timezone</span><span class=val>";
  html += tzBuf;
  html += F("</span></div></div>");

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

static void handleTimezoneSave() {
  if (server.hasArg("tz")) {
    sysTzHour = (int8_t)server.arg("tz").toInt();
    Preferences pSys;
    pSys.begin(kNvsWifi, false); // kNvsWifi ("wifi") is proven writable
    pSys.putChar("sys_tz", sysTzHour);
    pSys.end();
    configTime(sysTzHour * 3600, 0, kNtpServer); // update RTC offset
    ntpBeginSync(); // force ntp resync
    wlog("[TZ] Saved timezone to NVS: UTC %s%d", (sysTzHour >= 0) ? "+" : "", sysTzHour);
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing tz\"}");
  }
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

    // Timezone form inside network page (only during AP config mode)
    html += F("<div class=card style='margin-top:12px'><div class=card-title>Waktu & Timezone</div>"
              "<form id=tzForm style='display:flex;gap:8px;align-items:center'>"
              "<label style='margin:0'>UTC Offset:</label>"
              "<select id='tzSelect' name='tz' style='padding:8px;border-radius:8px;border:1px solid #ccc;flex:1'>");
    for (int i = -12; i <= 14; i++) {
      char opt[64];
      snprintf(opt, sizeof(opt), "<option value='%d' %s>UTC %s%d</option>",
               i, (i == sysTzHour) ? "selected" : "", (i >= 0) ? "+" : "", i);
      html += opt;
    }
    html += F("</select>"
              "<button class='btn btn-sm' type=submit>Simpan</button>"
              "</form></div>");
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
  if (configMode) {
    html += F("document.getElementById('tzForm').onsubmit=async(e)=>{"
              "e.preventDefault();"
              "const tz=document.getElementById('tzSelect').value;"
              "let r=await fetch('/api/timezone/save?tz='+tz,{method:'POST'});"
              "if(r.ok)alert('Timezone disimpan & disinkronkan!');"
              "else alert('Gagal menyimpan timezone.');"
              "};");
  }
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
  html.reserve(4200);
  html += FPSTR(kStyle);
  html += F("<title>SEMS Modbus</title>"
            "<style>"
            ".mg{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:8px;margin-top:8px}"
            ".mv{background:#f8fafc;border:1px solid #e2e8f0;border-radius:10px;padding:8px 10px;text-align:center}"
            ".ml{font-size:10px;color:#64748b;margin-bottom:2px}"
            ".mn{font-size:18px;font-weight:700;color:#0f766e;line-height:1.1}"
            ".mu{font-size:10px;color:#94a3b8}"
            ".na{color:#cbd5e1!important}"
            ".slotHead{display:flex;align-items:center;justify-content:space-between;gap:8px}"
            ".slotHead .card-title{margin:0}"
            "details.adv{margin-top:10px}"
            "details.adv summary{cursor:pointer;font-size:12px;color:#0f766e;font-weight:600}"
            "</style>"
            "</head><body><div class=wrap>"
            "<h1>SEMS AIoT &mdash; Modbus</h1>");
  html += getNav();

  if (!configMode) {
    html += F("<div class='card' style='background:#fee2e2;border:1px solid #fca5a5;padding:10px 12px;margin-bottom:12px;"
              "display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px'>"
              "<div>"
                "<div style='font-weight:600;color:#991b1b;font-size:13px'>Normal Mode — Konfigurasi terkunci</div>"
                "<div style='font-size:11px;color:#7f1d1d'>Masuk Config Mode untuk ubah konfigurasi slot meter</div>"
              "</div>"
              "<button class='btn btn-sm btn-danger' type=button onclick='goToConfigMode()'>Config Mode</button>"
              "</div>");
  } else {
    html += F("<div class='card' style='background:#fef9c3;border:1px solid #fde047;padding:12px;margin-bottom:12px;'>"
              "<div style='font-weight:600;color:#854d0e;font-size:14px;margin-bottom:4px;'>Config Mode</div>"
              "<div style='font-size:12px;color:#713f12;'>Perubahan baud rate &amp; tipe meter aktif setelah reboot.</div>"
              "</div>");
  }

  // RS485 bus-wide baud+parity — one shared UART framing for every slot
  // (Config Mode only; takes effect immediately, no reboot needed).
  if (configMode) {
    html += F("<div class=card><div class=card-title>RS485 Bus (semua slot)</div>"
              "<div style='display:grid;grid-template-columns:1fr 1fr;gap:0 16px'>"
              "<div><label>Baud Rate</label><select id=busBd>");
    const uint32_t bauds[] = {1200,2400,4800,9600,19200,38400,115200};
    for (uint32_t b : bauds) {
      html += "<option value="; html += b;
      if (b == busCfg.baud) html += F(" selected");
      html += ">"; html += b; html += F("</option>");
    }
    html += F("</select></div>"
              "<div><label>Parity</label><select id=busPar>"
              "<option value=0"); if (busCfg.parity==0) html += F(" selected"); html += F(">8N1 (No Parity)</option>"
              "<option value=1"); if (busCfg.parity==1) html += F(" selected"); html += F(">8E1 (Even)</option>"
              "<option value=2"); if (busCfg.parity==2) html += F(" selected"); html += F(">8O1 (Odd)</option>"
              "<option value=3"); if (busCfg.parity==3) html += F(" selected"); html += F(">8N2 (No Parity, 2 Stop)</option>"
              "</select></div></div>"
              "<div style='font-size:11px;color:#64748b;margin-top:8px'>Semua meter di bus RS485 ini (Schneider &amp; Renata) harus pakai baud/parity yang sama secara fisik.</div>"
              "<button class=\"btn btn-sm\" style=\"margin-top:10px\" onclick=saveBus()>Simpan Bus Config</button>"
              "<span id=busMsg style=\"margin-left:8px;font-size:12px\"></span>"
              "</div>");
  }

  // Container populated by JS with one card per slot (status + readings,
  // and — only in Config Mode — an inline edit form per slot).
  html += F("<div id=slots><p style='color:#94a3b8;font-size:13px'>Memuat slot...</p></div>");

  // Ping/Test Register card — Config Mode only (needs slave/addr entry).
  if (configMode) {
    html += F("<div class=card><div class=card-title>Test Koneksi (Ping Register)</div>"
              "<div style='font-size:12px;color:#64748b;margin-bottom:8px'>Baca 1 register langsung dari slot terpilih — cek respon RTU tanpa nunggu polling penuh.</div>"
              "<div style='display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px 12px'>"
                "<div><label>Slot</label><select id=pingSlot>"
                  "<option value=0>Slot 1</option><option value=1>Slot 2</option>"
                  "<option value=2>Slot 3</option><option value=3>Slot 4</option></select></div>"
                "<div><label>Slave ID</label><input type=number id=pingSlave min=1 max=247 placeholder='(default slot)'></div>"
                "<div><label>Alamat Register</label><input type=number id=pingAddr min=0 max=65535 value=16384></div>"
              "</div>"
              "<button class='btn btn-sm' style='margin-top:8px' onclick=doPing()>Ping</button>"
              "<div id=pingResult style='margin-top:8px;font-size:13px;font-family:monospace;white-space:pre-wrap'></div>"
              "</div>");
  }

  // ── JavaScript ────────────────────────────────────────────
  html += F("<script>"
  "const CFG=" ); html += (configMode ? "true" : "false"); html += F(";"
  "const P1={r1V:3027,r1A:2999,r1Kw:3053,r1Kvar:3061,r1Kva:3069,r1Pf:3077,r1Kwh:2675,r1Hz:3109};"
  "const P3={r3Va:3027,r3Vb:3029,r3Vc:3031,r3Vll:3025,r3Vln:3035,r3Ia:2999,r3Ib:3001,r3Ic:3003,r3Iavg:3009,"
    "r3Pa:3053,r3Pb:3055,r3Pc:3057,r3Pt:3059,r3Qa:3061,r3Qb:3063,r3Qc:3065,r3Qt:3067,"
    "r3Sa:3069,r3Sb:3071,r3Sc:3073,r3St:3075,r3Pfa:3077,r3Pfb:3079,r3Pfc:3081,r3Pft:3191,r3Kwh:2675,r3Hz:3109};"
  "const PR={r3Va:16384,r3Vb:16386,r3Vc:16388,r3Ia:16396,r3Ib:16398,r3Ic:16400,"
    "r3Pa:16402,r3Pb:16404,r3Pc:16406,r3Pt:16408,r3Qa:16410,r3Qb:16412,r3Qc:16414,r3Qt:16416,"
    "r3Sa:16418,r3Sb:16420,r3Sc:16422,r3St:16424,r3Pfa:16426,r3Pfb:16428,r3Pfc:16430,r3Pft:16432,r3Kwh:16436,r3Hz:16434};"
  "let dirty={};"
  "function eh(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));}"
  // Renders a Jam/Menit/Detik triplet for one publish-interval setting.
  // totalSec (0 = use built-in default) is decomposed into H/M/S for
  // display; hmsToSec(id) recombines them back into seconds before saving.
  "function hmsField(id,label,totalSec,defSec){"
    "const t=(totalSec&&totalSec>0)?totalSec:defSec;"
    "const h=Math.floor(t/3600),m=Math.floor((t%3600)/60),s=t%60;"
    "return '<label>'+label+'</label><div style=\"display:flex;gap:6px;align-items:center\">'"
      "+'<input type=number id='+id+'_h min=0 max=23 value='+h+' style=\"width:60px\"><span style=\"font-size:12px\">jam</span>'"
      "+'<input type=number id='+id+'_m min=0 max=59 value='+m+' style=\"width:60px\"><span style=\"font-size:12px\">menit</span>'"
      "+'<input type=number id='+id+'_s min=0 max=59 value='+s+' style=\"width:60px\"><span style=\"font-size:12px\">detik</span>'"
      "+'</div>';"
  "}"
  "function hmsToSec(id){"
    "const h=gv(id+'_h',0),m=gv(id+'_m',0),s=gv(id+'_s',0);"
    "return h*3600+m*60+s;"
  "}"
  "function row(label,val,unit,na){"
    "return '<div class=mv><div class=ml>'+label+'</div>'"
    "+'<div class=\"mn'+(na?' na':'')+'\">'+val+'</div>'"
    "+'<div class=\"mu'+(na?' na':'')+'\">'+unit+'</div></div>';}"
  "function readingsHtml(d){"
    "const na=!d.valid;const f=(k,n)=>na?'&mdash;':(d[k]||0).toFixed(n);"
    "if(d.phase===1)return '<div class=mg>'"
      "+row('V',f('v',1),'V',na)+row('I',f('a',3),'A',na)+row('P',f('ptot',3),'kW',na)"
      "+row('PF',f('pftot',3),'',na)+row('kWh',f('kwh',3),'',na)+row('Hz',f('hz',2),'',na)+'</div>';"
    "return '<div class=mg>'"
      "+row('Va',f('va',1),'V',na)+row('Vb',f('vb',1),'V',na)+row('Vc',f('vc',1),'V',na)"
      "+row('I avg',f('iavg',3),'A',na)+row('P tot',f('ptot',3),'kW',na)+row('PF tot',f('pftot',3),'',na)"
      "+row('kWh',f('kwh',3),'',na)+row('Hz',f('hz',2),'',na)+'</div>';}"
  "function editFormHtml(i,s){"
    "const en=s.en,mt=s.type,lbl=eh(s.label||'');"
    "let h='<details class=adv id=advReg'+i+'><summary>Register Map Lanjutan (FP32)</summary>'"
      "+'<div style=\"display:flex;gap:8px;flex-wrap:wrap;margin:8px 0\">'"
      "+'<button class=\"btn btn-sm\" type=button onclick=\"applyPreset('+i+',\\'p1_1ph\\')\">1-Phase PM2xxx</button>'"
      "+'<button class=\"btn btn-sm\" type=button onclick=\"applyPreset('+i+',\\'p1_3ph\\')\">3-Phase PM2xxx</button>'"
      "+'<button class=\"btn btn-sm\" type=button onclick=\"applyPreset('+i+',\\'renata\\')\">Renata AX9L</button>'"
      "+'</div>'"
      "+'<div style=\"font-size:11px;color:#64748b\">Preset mengisi alamat register standar (FC03, FP32/INT32, 0-based). Cek dulu dengan Ping sebelum simpan.</div>'"
      "+regGridHtml(i)+'</details>';"
    "let rh='';"
    "const paramFieldsHtml=(fields,note)=>'<div style=\"font-size:11px;color:#64748b;margin:6px 0\">'+note+'</div>'"
      "+'<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:6px 12px\">'"
      "+fields.reduce((acc,v,idx,arr)=>{"
        "if(idx%2)return acc;"
        "return acc+'<div><label>'+arr[idx+1]+'</label><div style=\"display:flex;gap:6px\">'"
          "+'<input type=number id=par'+v+i+' min=0 max=65535 style=\"flex:1\">'"
          "+'<button class=\"btn btn-sm\" type=button onclick=\"paramRead('+i+',\\''+v+'\\')\">Baca</button>'"
          "+'<button class=\"btn btn-sm\" type=button onclick=\"paramWrite('+i+',\\''+v+'\\')\">Tulis</button>'"
          "+'</div></div>';"
      "},'')"
      "+'</div><div id=parMsg'+i+' style=\"margin-top:8px;font-size:12px\"></div>';"
    "if(mt==1||mt==2){"
      "rh='<details class=adv id=advPar'+i+'><summary>Renata AX9L &mdash; CT/PT Ratio</summary>'"
        "+paramFieldsHtml(['pt1','PT1 (0.1kV)','pt2','PT2 (0.1V)','ct1','CT1 (1A)','ct2','CT2 (0.1A)'],"
          "'Baca/tulis langsung ke meter (bukan disimpan di device ini). Slave ID komunikasi TIDAK diubah di sini &mdash; hanya rasio trafo.')"
        "+'</details>';"
    "}else{"
      "rh='<details class=adv id=advPar'+i+'><summary>Schneider PM2xxx &mdash; CT Ratio</summary>'"
        "+paramFieldsHtml(['ctp','CT Primary (A)','cts','CT Secondary (A)'],"
          "'Baca/tulis langsung ke meter (bukan disimpan di device ini). Slave ID/baud/parity komunikasi TIDAK diubah di sini &mdash; hanya rasio trafo arus.')"
        "+'</details>';"
    "}"
    "return '<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:0 16px;margin-top:8px\">'"
      "+'<div class=toggle><input type=checkbox id=en'+i+(en?' checked':'')+'><span>Aktifkan Slot</span></div>'"
      "+'<div><label>Label</label><input type=text id=lbl'+i+' maxlength=15 value=\"'+lbl+'\"></div>'"
      "+'<div><label>Tipe Meter</label><select id=mt'+i+'>'"
        "+'<option value=0'+(mt==0?' selected':'')+'>FP32 (Schneider PM/EM)</option>'"
        "+'<option value=1'+(mt==1?' selected':'')+'>INT32 (Renata AX9L 3P)</option>'"
        "+'<option value=2'+(mt==2?' selected':'')+'>INT32 (Renata AX9L 1P)</option></select></div>'"
      "+'<div><label>Slave ID (1-247)</label><input type=number id=sl'+i+' min=1 max=247 value='+s.slave+'></div>'"
      "+'<div><label>Poll (ms)</label><input type=number id=pl'+i+' min=200 max=60000 value='+s.poll+'></div>'"
      "+'<div><label>Mode Fasa</label><div style=\"display:flex;gap:12px;margin-top:6px\">'"
        "+'<label style=\"display:flex;align-items:center;gap:5px;font-size:13px\"><input type=radio name=ph'+i+' id=ph'+i+'_1 value=1'+(s.phase==1?' checked':'')+'>1-Phase</label>'"
        "+'<label style=\"display:flex;align-items:center;gap:5px;font-size:13px\"><input type=radio name=ph'+i+' id=ph'+i+'_3 value=3'+(s.phase==3?' checked':'')+'>3-Phase</label>'"
      "+'</div></div>'"
      "+'<div><label>MQTT Base Topic</label><input type=text id=tp'+i+' maxlength=39 placeholder=\"trofis/enms/demo-sems\" value=\"'+eh(s.tp||'')+'\"></div>'"
      "+'<div><label>MQTT Suffix (id meter)</label><input type=text id=tsx'+i+' maxlength=15 placeholder=\"slave_1\" value=\"'+eh(s.tsx||'')+'\"></div>'"
      "+'<div style=\"grid-column:1/3;font-size:11px;color:#64748b\">Publish ke: <code>&lt;topic&gt;/elc_data/&lt;suffix&gt;</code> dan <code>&lt;topic&gt;/elc_wh/&lt;suffix&gt;</code></div>'"
      "+'<div style=\"grid-column:1/3\">'+hmsField('di'+i,'Interval elc_data (real-time)',s.di,20)+'</div>'"
      "+'<div style=\"grid-column:1/3\">'+hmsField('wi'+i,'Interval elc_wh (energi)',s.wi,60)+'</div>'"
      "+'</div>'"
      "+h+rh"
      "+'<button class=\"btn btn-sm\" style=\"margin-top:10px\" onclick=saveSlot('+i+')>Simpan Slot '+(i+1)+'</button>'"
      "+'<span id=msg'+i+' style=\"margin-left:8px;font-size:12px\"></span>';}"
  "async function paramRead(i,key){"
    "const msg=document.getElementById('parMsg'+i);msg.className='';msg.textContent='Membaca...';"
    "try{"
      "const r=await fetch('/api/modbus/param?slot='+i+'&key='+key).then(x=>x.json());"
      "if(r.ok){document.getElementById('par'+key+i).value=r.value;msg.className='ok';msg.textContent=key+' = '+r.value+' ('+r.addr+')';}"
      "else{msg.className='err';msg.textContent='Gagal baca: '+(r.error||'unknown');}"
    "}catch(e){msg.className='err';msg.textContent='Error: '+e;}}"
  "async function paramWrite(i,key){"
    "const msg=document.getElementById('parMsg'+i);"
    "const val=parseInt(document.getElementById('par'+key+i).value);"
    "if(isNaN(val)){msg.className='err';msg.textContent='Isi nilai dulu sebelum menulis.';return;}"
    "if(!confirm('Tulis '+key+' = '+val+' ke meter slot '+(i+1)+'?'))return;"
    "msg.className='';msg.textContent='Menulis...';"
    "try{"
      "const r=await fetch('/api/modbus/param',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({slot:i,key:key,value:val})}).then(x=>x.json());"
      "if(r.ok&&r.matches){msg.className='ok';msg.textContent='Tersimpan di meter: '+key+' = '+r.confirmed;}"
      "else if(r.ok){msg.className='err';msg.textContent='Ditulis tapi nilai konfirmasi beda: tertulis '+r.written+', terbaca '+r.confirmed;}"
      "else{msg.className='err';msg.textContent='Gagal tulis: '+(r.error||'unknown');}"
    "}catch(e){msg.className='err';msg.textContent='Error: '+e;}}"
  "const regFields1=[['r1V','V A-N'],['r1A','I A'],['r1Kw','P (kW)'],['r1Kvar','Q (kVAR)'],"
    "['r1Kva','S (kVA)'],['r1Pf','PF'],['r1Kwh','kWh'],['r1Hz','Hz']];"
  "const regFields3=[['r3Va','Va'],['r3Vb','Vb'],['r3Vc','Vc'],['r3Vll','Vll'],['r3Vln','Vln'],"
    "['r3Ia','Ia'],['r3Ib','Ib'],['r3Ic','Ic'],['r3Iavg','Iavg'],"
    "['r3Pa','Pa'],['r3Pb','Pb'],['r3Pc','Pc'],['r3Pt','Ptot'],"
    "['r3Qa','Qa'],['r3Qb','Qb'],['r3Qc','Qc'],['r3Qt','Qtot'],"
    "['r3Sa','Sa'],['r3Sb','Sb'],['r3Sc','Sc'],['r3St','Stot'],"
    "['r3Pfa','PFa'],['r3Pfb','PFb'],['r3Pfc','PFc'],['r3Pft','PFtot'],"
    "['r3Kwh','kWh'],['r3Hz','Hz']];"
  "function regGridHtml(i){"
    "const g=(arr)=>'<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:0 12px\">'"
      "+arr.map(([id,lb])=>'<div><label>'+lb+'</label><input type=number class=reg id='+id+i+' min=0 max=65535></div>').join('')+'</div>';"
    "return g(regFields1)+g(regFields3);}"
  "function fillRegs(i,s){"
    "regFields1.concat(regFields3).forEach(([id])=>{const el=document.getElementById(id+i);if(el&&s[id]!==undefined)el.value=s[id];});}"
  "function applyPreset(i,k){"
    "const map=k==='p1_1ph'?P1:(k==='renata'?PR:P3);"
    "Object.entries(map).forEach(([id,v])=>{const el=document.getElementById(id+i);if(el)el.value=v;});"
    "if(k==='p1_1ph')document.getElementById('ph'+i+'_1').checked=true;"
    "else document.getElementById('ph'+i+'_3').checked=true;"
    "document.getElementById('mt'+i).value=k==='renata'?1:0;"
    "dirty[i]=true;}"
  "function gv(id,def){const el=document.getElementById(id);return el?(parseInt(el.value)||def):def;}"
  "async function saveBus(){"
    "const msg=document.getElementById('busMsg');msg.className='';msg.textContent='Menyimpan...';"
    "const b={baud:parseInt(document.getElementById('busBd').value),"
      "parity:parseInt(document.getElementById('busPar').value)};"
    "const r=await fetch('/api/bus/save',{method:'POST',headers:{'Content-Type':'application/json'},"
      "body:JSON.stringify(b)}).then(x=>x.json());"
    "if(r.ok){msg.className='ok';msg.textContent='Tersimpan! Aktif sekarang.';}"
    "else{msg.className='err';msg.textContent='Error: '+(r.error||'unknown');}}"
  "async function saveSlot(i){"
    "const msg=document.getElementById('msg'+i);msg.className='';msg.textContent='Menyimpan...';"
    "const b={slot:i,en:document.getElementById('en'+i).checked,"
      "lbl:document.getElementById('lbl'+i).value.slice(0,15),"
      "type:parseInt(document.getElementById('mt'+i).value),"
      "slave:gv('sl'+i,1),"
      "poll:gv('pl'+i,1000),phase:document.getElementById('ph'+i+'_3').checked?3:1,"
      "tp:document.getElementById('tp'+i).value.trim().slice(0,39),"
      "tsx:document.getElementById('tsx'+i).value.trim().slice(0,15),"
      "di:hmsToSec('di'+i),wi:hmsToSec('wi'+i)};"
    "b['1V']=gv('r1V'+i,3027);b['1A']=gv('r1A'+i,2999);b['1Kw']=gv('r1Kw'+i,3053);"
    "b['1Kvar']=gv('r1Kvar'+i,3061);b['1Kva']=gv('r1Kva'+i,3069);b['1Pf']=gv('r1Pf'+i,3077);"
    "b['1Kwh']=gv('r1Kwh'+i,2675);b['1Hz']=gv('r1Hz'+i,3109);"
    "b['3Va']=gv('r3Va'+i,3027);b['3Vb']=gv('r3Vb'+i,3029);b['3Vc']=gv('r3Vc'+i,3031);"
    "b['3Vll']=gv('r3Vll'+i,3025);b['3Vln']=gv('r3Vln'+i,3035);"
    "b['3Ia']=gv('r3Ia'+i,2999);b['3Ib']=gv('r3Ib'+i,3001);b['3Ic']=gv('r3Ic'+i,3003);b['3Iavg']=gv('r3Iavg'+i,3009);"
    "b['3Pa']=gv('r3Pa'+i,3053);b['3Pb']=gv('r3Pb'+i,3055);b['3Pc']=gv('r3Pc'+i,3057);b['3Pt']=gv('r3Pt'+i,3059);"
    "b['3Qa']=gv('r3Qa'+i,3061);b['3Qb']=gv('r3Qb'+i,3063);b['3Qc']=gv('r3Qc'+i,3065);b['3Qt']=gv('r3Qt'+i,3067);"
    "b['3Sa']=gv('r3Sa'+i,3069);b['3Sb']=gv('r3Sb'+i,3071);b['3Sc']=gv('r3Sc'+i,3073);b['3St']=gv('r3St'+i,3075);"
    "b['3Pfa']=gv('r3Pfa'+i,3077);b['3Pfb']=gv('r3Pfb'+i,3079);b['3Pfc']=gv('r3Pfc'+i,3081);b['3Pft']=gv('r3Pft'+i,3191);"
    "b['3Kwh']=gv('r3Kwh'+i,2675);b['3Hz']=gv('r3Hz'+i,3109);"
    "const r=await fetch('/api/modbus/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(x=>x.json());"
    "if(r.ok){dirty[i]=false;msg.className='ok';msg.textContent='Tersimpan! Reboot untuk baud rate baru.';setTimeout(()=>load(true),1200);}"
    "else{msg.className='err';msg.textContent='Error: '+(r.error||'unknown');}}"
  "function slotCardHtml(s){"
    "const i=s.i;"
    "const badge=!s.en?'<span class=\"badge\" style=\"background:#e2e8f0;color:#64748b\"><span class=dot></span>Nonaktif</span>'"
      ":(s.online?'<span class=\"badge up\"><span class=dot></span>Online</span>':'<span class=\"badge down\"><span class=dot></span>Offline</span>');"
    "const typeName=s.type==1?'Renata AX9L 3P':(s.type==2?'Renata AX9L 1P':'Schneider FP32');"
    "let h='<div class=card id=slotCard'+i+'><div class=slotHead>'"
      "+'<div class=card-title>Slot '+(i+1)+' &mdash; '+eh(s.label||('Slot '+(i+1)))+'</div>'+badge+'</div>'"
      "+'<div class=row><span class=label>Tipe</span><span class=val>'+typeName+'</span></div>'"
      "+'<div class=row><span class=label>Slave ID</span><span class=val>'+s.slave+'</span></div>'"
      "+(s.en?readingsHtml(s):'<p style=\"color:#94a3b8;font-size:12px;margin-top:6px\">Slot nonaktif</p>');"
    "if(s.en&&(s.type==1||s.type==2)){h+=renataDoHtml(i);}"
    "if(CFG){h+=editFormHtml(i,s);}"
    "h+='</div>';return h;}"
  // DO switch control — usable in both Normal and Config Mode (unlike the
  // CT/PT ratio panel, which is Config-Mode-only via editFormHtml).
  // State lives in renDoState{} (not read back from checkboxes) because the
  // slot card's innerHTML gets replaced wholesale every 3s poll — a
  // checkbox's checked attribute would silently reset to unchecked on every
  // re-render before the async read-back finished, which is the bug this
  // replaced. Buttons render from renDoState so the displayed ON/OFF badge
  // survives re-renders once loaded, and only re-fetches from the meter
  // once per slot (not every poll — avoids extra RS485 traffic).
  "let renDoState={};"
  "function renDoBtn(i,bit,label){"
    "const st=renDoState[i];"
    "const known=st!==undefined;"
    "const on=known&&!!(st&(1<<bit));"
    "return '<span style=\"display:flex;align-items:center;gap:6px\">'"
      "+'<span class=\"badge '+(known?(on?'up':'down'):'')+'\">'+label+': '+(known?(on?'ON':'OFF'):'...')+'</span>'"
      "+'<button class=\"btn btn-sm\" type=button onclick=\"renataDoSet('+i+','+bit+',true)\">ON</button>'"
      "+'<button class=\"btn btn-sm btn-ghost\" type=button onclick=\"renataDoSet('+i+','+bit+',false)\">OFF</button>'"
      "+'</span>';"
  "}"
  "function renataDoHtml(i){"
    "return '<div class=row style=\"margin-top:6px;flex-wrap:wrap;gap:10px\"><span class=label>DO Switch</span>'"
      "+'<span style=\"display:flex;gap:16px;flex-wrap:wrap\">'+renDoBtn(i,0,'DO1')+renDoBtn(i,1,'DO2')+'</span></div>'"
      "+'<div id=renDoMsg'+i+' style=\"font-size:11px;color:#94a3b8\"></div>';}"
  "async function renataDoLoad(i){"
    "try{"
      "const r=await fetch('/api/modbus/param?slot='+i+'&key=do').then(x=>x.json());"
      "if(r.ok){renDoState[i]=r.value;const card=document.getElementById('slotCard'+i);if(card)card.outerHTML=slotCardHtml(lastSlots[i]);}"
    "}catch(e){}}"
  "async function renataDoSet(i,bit,on){"
    "const msg=document.getElementById('renDoMsg'+i);"
    "try{"
      "let val=renDoState[i]||0;"
      "val=on?(val|(1<<bit)):(val&~(1<<bit));"
      "if(msg)msg.textContent='Menulis...';"
      "const r=await fetch('/api/modbus/param',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({slot:i,key:'do',value:val})}).then(x=>x.json());"
      "if(r.ok&&r.matches){"
        "renDoState[i]=r.confirmed;"
        "const card=document.getElementById('slotCard'+i);if(card)card.outerHTML=slotCardHtml(lastSlots[i]);"
        "const m2=document.getElementById('renDoMsg'+i);if(m2)m2.textContent='Tersimpan di meter.';"
      "}else if(msg){"
        "msg.textContent='Gagal: '+(r.error||'nilai tidak sesuai konfirmasi');"
      "}"
    "}catch(e){if(msg)msg.textContent='Error: '+e;}}"
  "let doLoaded={};let lastSlots={};"
  "async function load(force){"
    "const d=await fetch('/api/modbus').then(r=>r.json());"
    "d.slots.forEach(s=>{lastSlots[s.i]=s;});"
    "const cont=document.getElementById('slots');"
    "let anyDirty=false;for(const k in dirty)if(dirty[k])anyDirty=true;"
    "if(!anyDirty||force){"
      "cont.innerHTML=d.slots.map(slotCardHtml).join('');"
      "if(CFG)d.slots.forEach(s=>fillRegs(s.i,s));"
      // Read the DO switch state from the meter once per slot (not on every
      // 3s poll — that would add extra RS485 traffic contending with the
      // regular round-robin read) so the checkboxes reflect reality on load.
      "d.slots.forEach(s=>{if(s.en&&(s.type==1||s.type==2)&&!doLoaded[s.i]){doLoaded[s.i]=true;renataDoLoad(s.i);}});"
    "}else{"
      // only refresh live readings, leave open edit forms untouched
      "d.slots.forEach(s=>{"
        "if(dirty[s.i])return;"
        "const card=document.getElementById('slotCard'+s.i);if(card)card.outerHTML=slotCardHtml(s);"
      "});"
    "}}"
  "if(CFG)document.addEventListener('input',e=>{"
    // Match ANY per-slot field id (config fields en/lbl/mt/sl/bd/pl/ph, every
    // register-map field like r1V0/r3Va1/etc., and HMS interval sub-fields
    // like di0_h/wi1_s where the slot digit is NOT at the very end) so
    // editing any of these also marks that slot dirty and stops the 3s poll
    // from overwriting it mid-edit.
    "const m=(e.target.id||'').match(/(\\d)(?:_[hms])?$/)||(e.target.name||'').match(/^ph(\\d)/);"
    "if(m)dirty[m[1]]=true;"
  "});"
  // <details> open/close fires 'toggle', not 'input' — without this, just
  // expanding "Register Map Lanjutan" (without touching a field first) still
  // gets clobbered by the next 3s poll's innerHTML rebuild, which snaps the
  // <details> back to its default closed state.
  "if(CFG)document.addEventListener('toggle',e=>{"
    "const m=(e.target.id||'').match(/^adv(?:Reg|Par)(\\d)/);"
    "if(m)dirty[m[1]]=true;"
  "},true);"
  "async function doPing(){"
    "const out=document.getElementById('pingResult');out.textContent='Mengirim...';"
    "const addr=gv('pingAddr',16384);const slot=document.getElementById('pingSlot').value;"
    "const slaveEl=document.getElementById('pingSlave');"
    "const q=new URLSearchParams({addr,slot});if(slaveEl.value)q.set('slave',slaveEl.value);"
    "try{"
      "const d=await fetch('/api/modbus/ping?'+q.toString()).then(r=>r.json());"
      "if(d.ok){out.textContent='OK — slave='+d.slave+' addr='+d.addr+'\\n'+"
        "'raw bytes: '+d.raw+'\\nas FP32: '+d.asFloat+'\\nas INT32 raw: '+d.asLong;"
      "}else{out.textContent='GAGAL — slave='+d.slave+' addr='+d.addr+'\\n'+d.error;}"
    "}catch(e){out.textContent='Error request: '+e;}}"
  "load(true);setInterval(()=>load(false),3000);"
  "</script></div></body></html>");
  server.send(200, "text/html", html);
}

static void handleModbusPingApi() {
  uint16_t addr = (uint16_t)server.arg("addr").toInt();
  // Optional "slot" selects which configured slot's slaveId to default to;
  // "slave" (if given) still overrides it directly. Baud/parity are bus-wide
  // (busCfg) — Serial2 is always already configured to match.
  uint8_t slotIdx = server.hasArg("slot") ? (uint8_t)server.arg("slot").toInt() : 0;
  if (slotIdx >= kMaxMeterSlots) slotIdx = 0;
  uint8_t  slave = server.hasArg("slave") ? (uint8_t)server.arg("slave").toInt() : meterSlots[slotIdx].slaveId;
  float    scale = server.hasArg("scale") ? server.arg("scale").toFloat() : 1.0f;

  ModbusPingResult r = modbusPing(slave, addr);
  String body = "{\"ok\":";
  body += r.ok ? "true" : "false";
  body += ",\"slave\":";  body += slave;
  body += ",\"addr\":";   body += addr;
  if (r.ok) {
    char hex[16];
    snprintf(hex, sizeof(hex), "%02X %02X %02X %02X", r.raw[0], r.raw[1], r.raw[2], r.raw[3]);
    body += ",\"raw\":\"";        body += hex; body += "\"";
    body += ",\"asFloat\":";      body += String(bytesToFloat(r.raw), 4);
    body += ",\"asLong\":";       body += bytesToLong(r.raw);
    body += ",\"asLongScaled\":"; body += String(bytesToLong(r.raw) * scale, 4);
  } else {
    body += ",\"error\":\""; body += r.error; body += "\"";
  }
  body += "}";
  server.send(200, "application/json", body);
}

// Per-meter-type config parameters this firmware is willing to read/write —
// CT/PT ratio and (Renata only) DO switch control. Every entry here is
// explicitly listed as R/W in the respective meter's official register
// documentation, and none of them affect communication (Modbus
// address/baud/parity are never touched by this firmware).
//
// Renata AX9L (meterType 1/2): manual section IX "System setting parameter
// list" — single "short" registers, FC06.
// Schneider PM2xxx family (meterType 0): "Public_EM6400_PM2xxx PMC Register
// List" — CT Primary/Secondary at human registers 2030/2031 (Power System
// Configuration 2016 also RWC but not exposed here — changing wiring config
// is not a "safe" write, it can invalidate the whole register map). Register
// numbers in that list are 1-based; this firmware's convention throughout is
// zero-based addressing (register_number - 1), matching r3Va=3027 for human
// register 3028 elsewhere in this file — so 2030/2031 become 2029/2030 here.
// Both are INT16U, unit Ampere, FC06.
struct MeterParam { const char* key; uint16_t addr; };
static const MeterParam kRenataParams[] = {
  {"pt1", 0x4801},  // Voltage ratio PT1, unit 0.1kV
  {"pt2", 0x4802},  // Voltage ratio PT2, unit 0.1V
  {"ct1", 0x4803},  // Current ratio CT1, unit 1A
  {"ct2", 0x4804},  // Current ratio CT2, unit 0.1A
  {"do",  0x480d},  // Remote/DO switch control (Table 6: bit0=DO1, bit1=DO2)
};
static constexpr uint8_t kRenataParamCount = sizeof(kRenataParams) / sizeof(kRenataParams[0]);

static const MeterParam kSchneiderParams[] = {
  {"ctp", 2029},  // CT Primary (human reg 2030), unit A
  {"cts", 2030},  // CT Secondary (human reg 2031), unit A
};
static constexpr uint8_t kSchneiderParamCount = sizeof(kSchneiderParams) / sizeof(kSchneiderParams[0]);

// Resolves a param key to a register address for the given slot's
// meterType. Returns -1 if the key doesn't exist for that meter type.
static int32_t resolveParamAddr(uint8_t meterType, const String& key) {
  if (meterType == 1 || meterType == 2) {
    for (uint8_t i = 0; i < kRenataParamCount; i++)
      if (key == kRenataParams[i].key) return kRenataParams[i].addr;
  } else {
    for (uint8_t i = 0; i < kSchneiderParamCount; i++)
      if (key == kSchneiderParams[i].key) return kSchneiderParams[i].addr;
  }
  return -1;
}

// GET /api/modbus/param?slot=N&key=ct1 — read one parameter's current
// value straight from the meter (not cached; always a live Modbus read).
static void handleMeterParamGet() {
  uint8_t slotIdx = server.hasArg("slot") ? (uint8_t)server.arg("slot").toInt() : 0;
  if (slotIdx >= kMaxMeterSlots) slotIdx = 0;
  String key = server.arg("key");

  int32_t addr = resolveParamAddr(meterSlots[slotIdx].meterType, key);
  if (addr < 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_key\"}"); return; }

  uint8_t slave = meterSlots[slotIdx].slaveId;
  uint8_t raw[2];
  if (!modbusReadRegsBlock(slave, (uint16_t)addr, 1, raw, sizeof(raw))) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"Timeout atau CRC gagal saat membaca dari meter\"}");
    return;
  }
  uint16_t value = ((uint16_t)raw[0] << 8) | raw[1];
  String body = "{\"ok\":true,\"key\":\""; body += key;
  body += "\",\"addr\":\"0x"; body += String(addr, HEX);
  body += "\",\"value\":"; body += value; body += "}";
  server.send(200, "application/json", body);
}

// POST /api/modbus/param — body {"slot":N,"key":"ct1","value":100}
// Writes one parameter via FC06, then reads it back to confirm the meter
// actually accepted the value before reporting success.
static void handleMeterParamSet() {
  String body = server.arg("plain");
  uint8_t slotIdx = (uint8_t)jsonInt(body, "slot", 0);
  if (slotIdx >= kMaxMeterSlots) slotIdx = 0;
  String key = jsonExtract(body, "key");
  uint16_t value = (uint16_t)jsonInt(body, "value", 0);

  int32_t addr = resolveParamAddr(meterSlots[slotIdx].meterType, key);
  if (addr < 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_key\"}"); return; }

  uint8_t slave = meterSlots[slotIdx].slaveId;
  if (!modbusWriteSingleReg(slave, (uint16_t)addr, value)) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"Timeout atau tidak ada respons saat menulis ke meter\"}");
    return;
  }

  // Read back to confirm — a meter can ACK the frame but reject an
  // out-of-range value internally without raising a Modbus exception.
  delay(15);
  uint8_t raw[2];
  bool readOk = modbusReadRegsBlock(slave, (uint16_t)addr, 1, raw, sizeof(raw));
  uint16_t confirmed = readOk ? (((uint16_t)raw[0] << 8) | raw[1]) : 0;

  String resp = "{\"ok\":true,\"key\":\""; resp += key;
  resp += "\",\"written\":"; resp += value;
  resp += ",\"confirmed\":"; resp += readOk ? String(confirmed) : String("null");
  resp += ",\"matches\":"; resp += (readOk && confirmed == value) ? "true" : "false";
  resp += "}";
  server.send(200, "application/json", resp);
}

// Appends one slot's summary object to body: enabled/type/label/online/key
// readings. Used by handleModbusApi() to cover all 4 slots.
static void appendSlotSummary(String& body, uint8_t i) {
  const MeterSlotConfig& c = meterSlots[i];
  const MeterSlotResult& r = meterResults[i];
  // Topic/suffix are user-typed free text — escape quotes/backslashes before
  // embedding in the JSON string literal.
  String tpEsc = c.mqttTopic;
  tpEsc.replace("\\", "\\\\");
  tpEsc.replace("\"", "\\\"");
  String tsEsc = c.mqttSuffix;
  tsEsc.replace("\\", "\\\\");
  tsEsc.replace("\"", "\\\"");
  char buf[420];
  if (c.phase == 1) {
    snprintf(buf, sizeof(buf),
      "{\"i\":%u,\"en\":%s,\"type\":%u,\"label\":\"%s\",\"slave\":%u,\"poll\":%lu,\"phase\":1,\"tp\":\"%s\",\"tsx\":\"%s\""
      ",\"di\":%lu,\"wi\":%lu"
      ",\"online\":%s,\"valid\":%s"
      ",\"v\":%.1f,\"a\":%.3f,\"ptot\":%.3f,\"pftot\":%.3f,\"kwh\":%.3f,\"hz\":%.2f}",
      i, c.enabled ? "true":"false", c.meterType, c.label, c.slaveId, c.pollMs, tpEsc.c_str(), tsEsc.c_str(),
      c.dataIntervalSec, c.whIntervalSec,
      r.online ? "true":"false", r.valid ? "true":"false",
      r.va, r.ia, r.ptot, r.pftot, r.kwh, r.hz);
  } else {
    snprintf(buf, sizeof(buf),
      "{\"i\":%u,\"en\":%s,\"type\":%u,\"label\":\"%s\",\"slave\":%u,\"poll\":%lu,\"phase\":3,\"tp\":\"%s\",\"tsx\":\"%s\""
      ",\"di\":%lu,\"wi\":%lu"
      ",\"online\":%s,\"valid\":%s"
      ",\"va\":%.1f,\"vb\":%.1f,\"vc\":%.1f,\"iavg\":%.3f,\"ptot\":%.3f,\"pftot\":%.3f,\"kwh\":%.3f,\"hz\":%.2f}",
      i, c.enabled ? "true":"false", c.meterType, c.label, c.slaveId, c.pollMs, tpEsc.c_str(), tsEsc.c_str(),
      c.dataIntervalSec, c.whIntervalSec,
      r.online ? "true":"false", r.valid ? "true":"false",
      r.va, r.vb, r.vc, r.iavg, r.ptot, r.pftot, r.kwh, r.hz);
  }
  body += buf;
}

static void handleModbusApi() {
  String body;
  body.reserve(1024);
  // Top-level fields below remain sourced from slot 0 for backward
  // compatibility with the existing /modbus HTML page (slot-0-only for now,
  // multi-slot UI is a later stage). The "slots" array covers all 4 slots.
  const MeterSlotConfig& cfg0 = meterSlots[0];
  const MeterSlotResult& res0 = meterResults[0];
  body  = "{\"ok\":true";
  body += ",\"slave\":";  body += cfg0.slaveId;
  body += ",\"baud\":";   body += busCfg.baud;    // bus-wide, not per-slot
  body += ",\"parity\":"; body += busCfg.parity;
  body += ",\"poll\":";   body += cfg0.pollMs;
  body += ",\"phase\":";  body += cfg0.phase;
  body += ",\"type\":";   body += cfg0.meterType;
  // 1-phase regs
  body += ",\"1V\":";     body += cfg0.r1V;
  body += ",\"1A\":";     body += cfg0.r1A;
  body += ",\"1Kw\":";    body += cfg0.r1Kw;
  body += ",\"1Kvar\":";  body += cfg0.r1Kvar;
  body += ",\"1Kva\":";   body += cfg0.r1Kva;
  body += ",\"1Pf\":";    body += cfg0.r1Pf;
  body += ",\"1Kwh\":";   body += cfg0.r1Kwh;
  body += ",\"1Hz\":";    body += cfg0.r1Hz;
  // 3-phase regs
  body += ",\"3Va\":";    body += cfg0.r3Va;
  body += ",\"3Vb\":";    body += cfg0.r3Vb;
  body += ",\"3Vc\":";    body += cfg0.r3Vc;
  body += ",\"3Vll\":";   body += cfg0.r3Vll;
  body += ",\"3Vln\":";   body += cfg0.r3Vln;
  body += ",\"3Ia\":";    body += cfg0.r3Ia;
  body += ",\"3Ib\":";    body += cfg0.r3Ib;
  body += ",\"3Ic\":";    body += cfg0.r3Ic;
  body += ",\"3Iavg\":";  body += cfg0.r3Iavg;
  body += ",\"3Pa\":";    body += cfg0.r3Pa;
  body += ",\"3Pb\":";    body += cfg0.r3Pb;
  body += ",\"3Pc\":";    body += cfg0.r3Pc;
  body += ",\"3Pt\":";    body += cfg0.r3Ptot;
  body += ",\"3Qa\":";    body += cfg0.r3Qa;
  body += ",\"3Qb\":";    body += cfg0.r3Qb;
  body += ",\"3Qc\":";    body += cfg0.r3Qc;
  body += ",\"3Qt\":";    body += cfg0.r3Qtot;
  body += ",\"3Sa\":";    body += cfg0.r3Sa;
  body += ",\"3Sb\":";    body += cfg0.r3Sb;
  body += ",\"3Sc\":";    body += cfg0.r3Sc;
  body += ",\"3St\":";    body += cfg0.r3Stot;
  body += ",\"3Pfa\":";   body += cfg0.r3Pfa;
  body += ",\"3Pfb\":";   body += cfg0.r3Pfb;
  body += ",\"3Pfc\":";   body += cfg0.r3Pfc;
  body += ",\"3Pft\":";   body += cfg0.r3Pftot;
  body += ",\"3Kwh\":";   body += cfg0.r3Kwh;
  body += ",\"3Hz\":";    body += cfg0.r3Hz;
  // live meter data
  if (cfg0.enabled && cfg0.phase == 1 && res0.valid) {
    char buf[160];
    snprintf(buf, sizeof(buf),
      ",\"valid\":true"
      ",\"v\":%.1f,\"a\":%.3f,\"kw\":%.3f,\"kvar\":%.3f,\"kva\":%.3f,\"pf\":%.3f,\"kwh\":%.3f,\"hz\":%.2f",
      res0.va, res0.ia, res0.ptot, res0.qtot, res0.stot, res0.pftot, res0.kwh, res0.hz);
    body += buf;
  } else if (cfg0.enabled && cfg0.phase == 3 && res0.valid) {
    char buf[320];
    snprintf(buf, sizeof(buf),
      ",\"valid\":true"
      ",\"va\":%.1f,\"vb\":%.1f,\"vc\":%.1f,\"vln\":%.1f"
      ",\"ia\":%.3f,\"ib\":%.3f,\"ic\":%.3f,\"iavg\":%.3f"
      ",\"pa\":%.3f,\"pb\":%.3f,\"pc\":%.3f,\"ptot\":%.3f"
      ",\"qtot\":%.3f,\"stot\":%.3f"
      ",\"pfa\":%.3f,\"pfb\":%.3f,\"pfc\":%.3f,\"pftot\":%.3f"
      ",\"kwh\":%.3f,\"hz\":%.2f",
      res0.va, res0.vb, res0.vc, res0.vln,
      res0.ia, res0.ib, res0.ic, res0.iavg,
      res0.pa, res0.pb, res0.pc, res0.ptot,
      res0.qtot, res0.stot,
      res0.pfa, res0.pfb, res0.pfc, res0.pftot,
      res0.kwh, res0.hz);
    body += buf;
  } else {
    body += ",\"valid\":false";
  }
  // always include phase so JS can render the correct tile layout
  body += ",\"phase\":"; body += cfg0.phase;
  // multi-slot summary array (all 4 slots) — for future multi-slot UI/API
  // consumers; the /modbus HTML page ignores this for now.
  body += ",\"slots\":[";
  for (uint8_t i = 0; i < kMaxMeterSlots; i++) {
    if (i) body += ",";
    appendSlotSummary(body, i);
  }
  body += "]";
  body += "}";
  server.send(200, "application/json", body);
}

static void handleModbusSave() {
  String body = server.arg("plain");
  // Optional "slot" selects which of the 4 slots to write; defaults to 0
  // for backward compatibility with the current (slot-0-only) HTML page.
  uint8_t slotIdx = (uint8_t)jsonInt(body, "slot", 0);
  if (slotIdx >= kMaxMeterSlots) slotIdx = 0;
  uint8_t  slave = (uint8_t)jsonInt(body, "slave", 1);
  uint32_t poll  = (uint32_t)jsonInt(body, "poll",  1000);
  uint8_t  phase = (uint8_t)jsonInt(body, "phase", 1);
  uint8_t  mtype = (uint8_t)jsonInt(body, "type",  0);
  if (slave < 1 || slave > 247) slave = 1;
  if (phase != 1 && phase != 3) phase = 1;
  if (mtype > 2) mtype = 0;  // 0=Schneider FP32, 1=Renata 3P, 2=Renata 1P
  if (poll < 200) poll = 200;

  // Writes into the slot selected by the optional "slot" field (defaults
  // to slot 0 for the current single-slot HTML page).
  MeterSlotConfig& cfg0 = meterSlots[slotIdx];
  cfg0.slaveId   = slave;
  cfg0.pollMs    = poll;
  cfg0.phase     = phase;
  cfg0.meterType = mtype;
  // User-editable MQTT base topic + suffix — empty topic means "don't publish".
  String tp = jsonExtract(body, "tp");
  snprintf(cfg0.mqttTopic, sizeof(cfg0.mqttTopic), "%s", tp.c_str());
  String tsx = jsonExtract(body, "tsx");
  snprintf(cfg0.mqttSuffix, sizeof(cfg0.mqttSuffix), "%s", tsx.c_str());
  // MQTT publish interval, in total seconds (combined from the web UI's H/M/S
  // fields before sending). 0 = use built-in default (see publishMeter()).
  cfg0.dataIntervalSec = (uint32_t)jsonInt(body, "di", 0);
  cfg0.whIntervalSec   = (uint32_t)jsonInt(body, "wi", 0);
  // 1-phase
  cfg0.r1V     = (uint16_t)jsonInt(body, "1V",    3027);
  cfg0.r1A     = (uint16_t)jsonInt(body, "1A",    2999);
  cfg0.r1Kw    = (uint16_t)jsonInt(body, "1Kw",   3053);
  cfg0.r1Kvar  = (uint16_t)jsonInt(body, "1Kvar", 3061);
  cfg0.r1Kva   = (uint16_t)jsonInt(body, "1Kva",  3069);
  cfg0.r1Pf    = (uint16_t)jsonInt(body, "1Pf",   3077);
  cfg0.r1Kwh   = (uint16_t)jsonInt(body, "1Kwh",  2675);
  cfg0.r1Hz    = (uint16_t)jsonInt(body, "1Hz",   3109);
  // 3-phase
  cfg0.r3Va    = (uint16_t)jsonInt(body, "3Va",   3027);
  cfg0.r3Vb    = (uint16_t)jsonInt(body, "3Vb",   3029);
  cfg0.r3Vc    = (uint16_t)jsonInt(body, "3Vc",   3031);
  cfg0.r3Vll   = (uint16_t)jsonInt(body, "3Vll",  3025);
  cfg0.r3Vln   = (uint16_t)jsonInt(body, "3Vln",  3035);
  cfg0.r3Ia    = (uint16_t)jsonInt(body, "3Ia",   2999);
  cfg0.r3Ib    = (uint16_t)jsonInt(body, "3Ib",   3001);
  cfg0.r3Ic    = (uint16_t)jsonInt(body, "3Ic",   3003);
  cfg0.r3Iavg  = (uint16_t)jsonInt(body, "3Iavg", 3009);
  cfg0.r3Pa    = (uint16_t)jsonInt(body, "3Pa",   3053);
  cfg0.r3Pb    = (uint16_t)jsonInt(body, "3Pb",   3055);
  cfg0.r3Pc    = (uint16_t)jsonInt(body, "3Pc",   3057);
  cfg0.r3Ptot  = (uint16_t)jsonInt(body, "3Pt",   3059);
  cfg0.r3Qa    = (uint16_t)jsonInt(body, "3Qa",   3061);
  cfg0.r3Qb    = (uint16_t)jsonInt(body, "3Qb",   3063);
  cfg0.r3Qc    = (uint16_t)jsonInt(body, "3Qc",   3065);
  cfg0.r3Qtot  = (uint16_t)jsonInt(body, "3Qt",   3067);
  cfg0.r3Sa    = (uint16_t)jsonInt(body, "3Sa",   3069);
  cfg0.r3Sb    = (uint16_t)jsonInt(body, "3Sb",   3071);
  cfg0.r3Sc    = (uint16_t)jsonInt(body, "3Sc",   3073);
  cfg0.r3Stot  = (uint16_t)jsonInt(body, "3St",   3075);
  cfg0.r3Pfa   = (uint16_t)jsonInt(body, "3Pfa",  3077);
  cfg0.r3Pfb   = (uint16_t)jsonInt(body, "3Pfb",  3079);
  cfg0.r3Pfc   = (uint16_t)jsonInt(body, "3Pfc",  3081);
  cfg0.r3Pftot = (uint16_t)jsonInt(body, "3Pft",  3191);
  cfg0.r3Kwh   = (uint16_t)jsonInt(body, "3Kwh",  2675);
  cfg0.r3Hz    = (uint16_t)jsonInt(body, "3Hz",   3109);
  // "en" (enabled) and "lbl" (label) are optional — legacy callers that omit
  // them keep the slot enabled with its existing label.
  cfg0.enabled = body.indexOf("\"en\":") >= 0 ? jsonBool(body, "en") : true;
  String lbl = jsonExtract(body, "lbl");
  if (lbl.length()) {
    lbl.toCharArray(cfg0.label, sizeof(cfg0.label));
  }
  saveModbusConfig();
  Serial.printf("[Modbus] Saved: slave=%d poll=%lums phase=%d\n",
    cfg0.slaveId, cfg0.pollMs, cfg0.phase);
  server.send(200, "application/json", "{\"ok\":true}");
}

// Bus-wide baud+parity save — applies to Serial2 immediately (all slots
// share one UART framing, so this takes effect on the very next poll rather
// than requiring a reboot).
static void handleBusSave() {
  String body = server.arg("plain");
  uint32_t baud   = (uint32_t)jsonInt(body, "baud", busCfg.baud);
  uint8_t  parity = (uint8_t)jsonInt(body, "parity", busCfg.parity);
  const uint32_t validBauds[] = {1200,2400,4800,9600,19200,38400,115200};
  bool baudOk = false;
  for (uint32_t vb : validBauds) if (baud == vb) { baudOk = true; break; }
  if (!baudOk) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_baud\"}"); return; }
  if (parity > 3) parity = 1;

  busCfg.baud   = baud;
  busCfg.parity = parity;
  saveBusConfig();
  Serial2.end();
  Serial2.begin(busCfg.baud, busSerialConfig(), kRs485Rx, kRs485Tx);
  Serial.printf("[Bus] Saved: baud=%lu parity=%u\n", busCfg.baud, busCfg.parity);
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
// Web Serial page + API
// ============================================================
static void handleSerialPage() {
  server.send(200, "text/html", F(
    "<!DOCTYPE html><html><head>"
    "<title>SEMS Serial Monitor</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:#0d1117;color:#3fb950;font-family:'Courier New',monospace;height:100vh;display:flex;flex-direction:column}"
    ".bar{background:#161b22;border-bottom:1px solid #30363d;padding:8px 16px;display:flex;align-items:center;gap:10px;flex-shrink:0}"
    ".bar h1{font-size:14px;color:#e6edf3;font-weight:600}"
    ".dot{width:8px;height:8px;border-radius:50%;background:#3fb950}"
    ".dot.off{background:#6e7681}"
    ".spacer{flex:1}"
    ".btn{background:#21262d;border:1px solid #30363d;color:#c9d1d9;padding:4px 12px;border-radius:6px;cursor:pointer;font-size:12px;margin-left:6px}"
    ".btn:hover{background:#30363d}"
    "label{color:#8b949e;font-size:12px;cursor:pointer;margin-left:6px}"
    "#log{flex:1;overflow-y:auto;padding:10px 14px;font-size:12px;line-height:1.6;background:#010409}"
    ".ln{white-space:pre-wrap;word-break:break-all;padding:0 0 1px}"
    ".eth{color:#58a6ff}.wifi{color:#a371f7}.mqtt{color:#d29922}.ok{color:#3fb950}.err{color:#f85149}"
    "</style></head><body>"
    "<div class='bar'>"
    "<div class='dot' id='dot'></div>"
    "<h1>&#128187; SEMS Serial Monitor</h1>"
    "<div class='spacer'></div>"
    "<button class='btn' onclick=\"document.getElementById('log').innerHTML=''\">Clear</button>"
    "<label><input type='checkbox' id='as' checked> Auto-scroll</label>"
    "</div>"
    "<div id='log'></div>"
    "<script>"
    "let seq=0;"
    "const lg=document.getElementById('log'),dot=document.getElementById('dot'),as2=document.getElementById('as');"
    "function cc(l){"
    " if(l.includes('[ETH]')||l.includes('[ETH Sync]'))return l.includes('IP:')||l.includes('success')?'ok':'eth';"
    " if(l.includes('[WiFi]')||l.includes('[STA]'))return 'wifi';"
    " if(l.includes('[MQTT]'))return 'mqtt';"
    " if(l.includes('FAILED')||l.includes('Error')||l.includes('failed'))return 'err';"
    " return '';}"
    "async function poll(){"
    " try{"
    "  const r=await fetch('/api/serial?since='+seq);"
    "  const d=await r.json();"
    "  dot.className='dot';"
    "  if(d.lines&&d.lines.length){"
    "   d.lines.forEach(l=>{"
    "    const div=document.createElement('div');"
    "    div.className='ln '+cc(l);"
    "    div.textContent=l;"
    "    lg.appendChild(div);"
    "   });"
    "   while(lg.children.length>500)lg.removeChild(lg.firstChild);"
    "   if(as2.checked)lg.scrollTop=lg.scrollHeight;"
    "  }"
    "  seq=d.seq;"
    " }catch(e){dot.className='dot off';}"
    " setTimeout(poll,500);"
    "}"
    "poll();"
    "</script></body></html>"
  ));
}

// ============================================================
// Relay web UI + API
// ============================================================
static void handleRelayPage() {
  oledShow("Web UI", "Relay dibuka", server.client().remoteIP().toString().c_str(), 3000);

  String html;
  html.reserve(4200);
  html += FPSTR(kStyle);
  html += F("<title>SEMS Relay</title></head><body><div class=wrap>"
            "<h1>SEMS AIoT &mdash; Relay Control</h1>");
  html += getNav();

  html += F("<div class=card><div class=card-title>Kontrol Relay</div><div id=relays>Loading...</div>"
            "<button class='btn btn-sm' type=button onclick=allSet(1)>Semua ON</button> "
            "<button class='btn btn-sm btn-ghost' type=button onclick=allSet(0)>Semua OFF</button>"
            "</div>");

  if (configMode) {
    html += F("<div class=card><div class=card-title>Pengaturan</div>"
              "<div class=toggle><input type=checkbox id=enabled><span>Relay Aktif</span></div>"
              "<div class=toggle><input type=checkbox id=activeHigh><span>Active-High</span></div>"
              "<div class=toggle><input type=checkbox id=autoRetry><span>Auto-Retry setelah Trip</span></div>"
              "<label>Auto-Retry Delay (detik)</label><input type=number id=arSec min=1 value=30>"
              "<label>Batas Arus (A, 0=nonaktif)</label><input type=number id=iLim min=0 value=0>"
              "<label>Trip Delay (ms)</label><input type=number id=tripMs min=100 value=3000>"
              "<label>GPIO Pin R1</label><input type=number id=pin0 min=0 max=39 value=2>"
              "<label>GPIO Pin R2</label><input type=number id=pin1 min=0 max=39 value=15>"
              "<label>GPIO Pin R3</label><input type=number id=pin2 min=0 max=39 value=14>"
              "<label>GPIO Pin R4</label><input type=number id=pin3 min=0 max=39 value=13>"
              "<button class=btn style='width:100%;margin-top:14px' onclick=saveCfg()>Simpan</button>"
              "<div id=msg></div></div>");
  }

  html += F("<script>"
    "const labels={0:'OFF',1:'ON',2:'TRIP'};"
    "function relayRow(i,st){"
      "return '<div class=row><span class=label>Relay '+(i+1)+'</span>'+"
        "'<span><span class=\"badge '+(st==1?'up':st==2?'down':'')+'\" style=\"margin-right:8px\">'+labels[st]+'</span>'+"
        "'<input type=checkbox '+(st==1?'checked':'')+' onchange=setRelay('+(i+1)+',this.checked?1:0)></span></div>';"
    "}"
    "async function load(){"
      "const d=await fetch('/api/relay/state').then(r=>r.json());"
      "let h='';for(let i=0;i<4;i++)h+=relayRow(i,d.state[i]);"
      "document.getElementById('relays').innerHTML=h;"
      "if(document.getElementById('enabled')){"
        "document.getElementById('enabled').checked=d.enabled;"
        "document.getElementById('activeHigh').checked=d.activeHigh;"
        "document.getElementById('autoRetry').checked=d.autoRetry;"
        "document.getElementById('arSec').value=d.arSec;"
        "document.getElementById('iLim').value=d.iLim;"
        "document.getElementById('tripMs').value=d.tripMs;"
        "document.getElementById('pin0').value=d.pins[0];"
        "document.getElementById('pin1').value=d.pins[1];"
        "document.getElementById('pin2').value=d.pins[2];"
        "document.getElementById('pin3').value=d.pins[3];"
      "}"
    "}"
    "async function setRelay(n,st){"
      "await fetch('/api/relay/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'relay='+n+'&state='+st});"
      "load();"
    "}"
    "async function allSet(st){"
      "await fetch('/api/relay/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
        "body:'relay=all&state='+st});"
      "load();"
    "}"
    "async function saveCfg(){"
      "const msg=document.getElementById('msg');"
      "msg.className='';msg.textContent='Menyimpan...';"
      "const b={enabled:document.getElementById('enabled').checked,"
        "activeHigh:document.getElementById('activeHigh').checked,"
        "autoRetry:document.getElementById('autoRetry').checked,"
        "arSec:parseInt(document.getElementById('arSec').value)||30,"
        "iLim:parseInt(document.getElementById('iLim').value)||0,"
        "tripMs:parseInt(document.getElementById('tripMs').value)||3000,"
        "pin0:parseInt(document.getElementById('pin0').value)||2,"
        "pin1:parseInt(document.getElementById('pin1').value)||15,"
        "pin2:parseInt(document.getElementById('pin2').value)||14,"
        "pin3:parseInt(document.getElementById('pin3').value)||13};"
      "const r=await fetch('/api/relay/config',{method:'POST',"
        "headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(x=>x.json());"
      "if(r.ok){msg.className='ok';msg.textContent='Tersimpan! Reboot untuk pin baru berlaku.';}"
      "else{msg.className='err';msg.textContent='Error: '+(r.error||'unknown');}"
    "}"
    "load();setInterval(load,3000);"
    "</script></div></body></html>");
  server.send(200, "text/html", html);
}

static void handleRelayStateApi() {
  String body;
  body.reserve(320);
  body  = "{\"ok\":true,\"enabled\":"; body += relayCfg.enabled?"true":"false";
  body += ",\"activeHigh\":";          body += relayCfg.activeHigh?"true":"false";
  body += ",\"autoRetry\":";           body += relayCfg.autoRetryEnabled?"true":"false";
  body += ",\"arSec\":";               body += relayCfg.autoRetryDelaySec;
  body += ",\"iLim\":";                body += relayCfg.currentLimitA;
  body += ",\"tripMs\":";              body += relayCfg.tripDelayMs;
  body += ",\"pins\":[";               body += relayCfg.pin[0]; body += ",";
                                        body += relayCfg.pin[1]; body += ",";
                                        body += relayCfg.pin[2]; body += ",";
                                        body += relayCfg.pin[3]; body += "]";
  body += ",\"state\":[";              body += relayState[0]; body += ",";
                                        body += relayState[1]; body += ",";
                                        body += relayState[2]; body += ",";
                                        body += relayState[3]; body += "]";
  body += "}";
  server.send(200, "application/json", body);
}

static void handleRelaySetApi() {
  if (!server.hasArg("relay") || !server.hasArg("state")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_args\"}");
    return;
  }
  String relayArg = server.arg("relay");
  uint8_t st = (server.arg("state").toInt() != 0) ? 1 : 0;

  if (relayArg == "all") {
    for (uint8_t i = 0; i < kRelayCount; i++) requestRelayState(i, st, "web", true);
  } else {
    int r = relayArg.toInt();
    if (r < 1 || r > kRelayCount) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_relay\"}");
      return;
    }
    requestRelayState((uint8_t)(r - 1), st, "web", true);
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleRelayConfigSave() {
  String body = server.arg("plain");
  relayCfg.enabled           = jsonBool(body, "enabled");
  relayCfg.activeHigh        = jsonBool(body, "activeHigh");
  relayCfg.autoRetryEnabled  = jsonBool(body, "autoRetry");
  relayCfg.autoRetryDelaySec = (uint16_t)jsonInt(body, "arSec", 30);
  relayCfg.currentLimitA     = (uint16_t)jsonInt(body, "iLim", 0);
  relayCfg.tripDelayMs       = (uint32_t)jsonInt(body, "tripMs", 3000);
  relayCfg.pin[0]            = (uint8_t)jsonInt(body, "pin0", kRelayDefaultPins[0]);
  relayCfg.pin[1]            = (uint8_t)jsonInt(body, "pin1", kRelayDefaultPins[1]);
  relayCfg.pin[2]            = (uint8_t)jsonInt(body, "pin2", kRelayDefaultPins[2]);
  relayCfg.pin[3]            = (uint8_t)jsonInt(body, "pin3", kRelayDefaultPins[3]);
  saveRelayConfig();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleSerialApi() {
  uint32_t since = 0;
  if (server.hasArg("since")) since = (uint32_t)server.arg("since").toInt();

  // clamp: never go further back than ring buffer holds
  if (wsSeq > kWsLines && since < wsSeq - kWsLines) since = wsSeq - kWsLines;

  String out = "{\"seq\":";
  out += wsSeq;
  out += ",\"lines\":[";
  bool first = true;
  for (uint32_t i = since; i < wsSeq; i++) {
    if (!first) out += ',';
    first = false;
    String line = wsLines[i % kWsLines];
    line.replace("\\", "\\\\");
    line.replace("\"", "\\\"");
    line.replace("\r", "");
    line.replace("\n", " ");
    out += '"'; out += line; out += '"';
  }
  out += "]}";
  server.send(200, "application/json", out);
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
  server.on("/api/modbus/ping", HTTP_GET,  handleModbusPingApi);
  server.on("/api/modbus/param", HTTP_GET,  handleMeterParamGet);
  server.on("/api/modbus/param", HTTP_POST, handleMeterParamSet);
  server.on("/api/bus/save",    HTTP_POST, handleBusSave);
  server.on("/api/scan",        HTTP_POST, handleScanStart);
  server.on("/api/scan/result", HTTP_GET,  handleScanResult);
  server.on("/api/wifi/save",   HTTP_POST, handleWifiSave);
  server.on("/api/wifi/connect-test", HTTP_POST, handleWifiConnectTest);
  server.on("/api/wifi/delete", HTTP_POST, handleWifiDelete);
  server.on("/api/wifi/list",   HTTP_GET,  handleWifiList);
  server.on("/api/wifi/clear",  HTTP_POST, handleWifiClear);
  server.on("/api/mqtt/save",   HTTP_POST, handleMqttSave);
  server.on("/api/timezone/save", HTTP_POST, handleTimezoneSave);
  server.on("/api/reboot",      HTTP_POST, handleReboot);
  server.on("/api/config-mode", HTTP_POST, [](){
    server.send(200,"application/json","{\"ok\":true}");
    enterConfigMode();
  });
  server.on("/update",          HTTP_GET,  handleUpdateGet);
  server.on("/update",          HTTP_POST, handleUpdatePost, handleUpdateUpload);
  server.on("/serial",          HTTP_GET,  handleSerialPage);
  server.on("/api/serial",      HTTP_GET,  handleSerialApi);
  server.on("/relay",           HTTP_GET,  handleRelayPage);
  server.on("/api/relay/state", HTTP_GET,  handleRelayStateApi);
  server.on("/api/relay/set",   HTTP_POST, handleRelaySetApi);
  server.on("/api/relay/config",HTTP_POST, handleRelayConfigSave);
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
  loadBusConfig();
  // Single bus-wide baud+parity shared by every meter slot (Schneider AND
  // Renata alike) — see BusConfig / busSerialConfig().
  Serial2.begin(busCfg.baud, busSerialConfig(), kRs485Rx, kRs485Tx);
  pinMode(kLedPin, OUTPUT);
  pinMode(kBtn2Pin, INPUT);        // Touch TTP223 has external active-high output; no pullup needed

  Wire.begin(22, 21);
  if (oled.begin()) {
    oledReady = true;
    oledLastActivityMs = millis();
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

  // Relay init: load config/requested-states then drive GPIO to match.
  loadRelayConfig();
  for (uint8_t i = 0; i < kRelayCount; i++) {
    pinMode(relayCfg.pin[i], OUTPUT);
    uint8_t initSt = (relayCfg.enabled && relayRequestedState[i] == 1) ? 1 : 0;
    setRelayOutput(i, initSt);
    relayTripUntilMs[i] = 0;
    relayOvercurrentSinceMs[i] = 0;
  }

  // Load timezone config from NVS (using stable kNvsWifi namespace)
  {
    Preferences pSys;
    pSys.begin(kNvsWifi, true);
    sysTzHour = pSys.getChar("sys_tz", 7); // Default to UTC+7
    pSys.end();
  }
  // Apply TZ offset immediately, unconditionally — a soft-reboot (ESP.restart())
  // can leave the internal RTC/system clock holding a stale value from before
  // this boot, which makes handleNtp()'s "tm_year>120 => already synced"
  // heuristic skip calling configTime() entirely, leaving the old (possibly
  // UTC+0) offset in effect forever. Call it here so the correct offset is
  // always in effect from the very first getLocalTime() call.
  configTime(sysTzHour * 3600, 0, kNtpServer);

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

  // Manual hardware reset for W5500 — hold RST low cukup lama
  pinMode(kEthRst, OUTPUT);
  digitalWrite(kEthRst, LOW);
  delay(100);               // was 50ms — longer reset pulse
  digitalWrite(kEthRst, HIGH);
  delay(250);               // was 100ms — beri W5500 waktu PLL lock

  // Init W5500 via native ETH.h using SPIClass in v3.x
  // Gunakan kEthIrq (GPIO27) agar event-driven, bukan polling
  SPI.begin(18, 19, 23, kEthCs);
  if (ETH.begin(ETH_PHY_W5500, 1, kEthCs, kEthIrq, kEthRst, SPI)) {
    wlog("[ETH] W5500 init success");
  } else {
    wlog("[ETH] W5500 init FAILED! — check SPI wiring");
  }

  // ── LAN-first: tunggu physical link dulu (maks 2 detik) ──
  {
    oledShow("LAN", "Checking cable...", "", 2000);
    uint32_t t0 = millis();
    while (!ethLink && (millis() - t0 < 2000)) delay(50);
  }
  if (ethLink) {
    // Kabel ada — tunggu DHCP (maks 6 detik)
    wlog("[Boot] LAN cable detected, waiting DHCP...");
    oledShow("LAN", "DHCP...", "Waiting IP...", 6000);
    uint32_t t0 = millis();
    while (!ethReady && (millis() - t0 < 6000)) delay(50);
    if (ethReady) {
      wlog("[Boot] LAN ready: %s — WiFi as backup", ethIp.c_str());
    } else {
      wlog("[Boot] LAN DHCP timeout — fallback ke WiFi");
    }
  } else {
    wlog("[Boot] No LAN cable — WiFi only");
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
  handleBtn2(now);
  handleNtp(now);
  handleEthSync(now);
  handleModbus(now);
  publishMeter(now);
  publishHealth(now);
  updateRelayRuntime(now);

  // LED heartbeat — skip if GPIO2 is claimed by a relay output (default Relay 1 pin
  // collides with kLedPin; don't let the blink fight the relay's actual state).
  bool ledPinUsedByRelay = false;
  for (uint8_t i = 0; i < kRelayCount; i++) {
    if (relayCfg.pin[i] == kLedPin) { ledPinUsedByRelay = true; break; }
  }
  if (!ledPinUsedByRelay && now - lastBlinkMs >= 500) {
    lastBlinkMs = now;
    ledState = !ledState;
    digitalWrite(kLedPin, ledState);
  }

  updateOled(now);
}
