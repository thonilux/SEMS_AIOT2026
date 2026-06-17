// Network-only firmware for SEMS AIoT
// Scope: WiFi STA/AP, W5500 Ethernet DHCP, Web UI (network config + status), MQTT transport test
// Excludes: Modbus, relay, display, protection, history

#include <Arduino.h>
#include <Ethernet.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include "PinMap.h"
#include "ConfigManager.h"

// ============================================================================
// Constants
// ============================================================================
constexpr uint32_t kSerialBaud        = 115200;
constexpr uint32_t kWifiTimeoutMs     = 15000;
constexpr char     kApSsidPrefix[]    = "SEMS-SETUP";
constexpr char     kApPassword[]      = "PM123456";
constexpr char     kNvsNetNs[]        = "network";
constexpr char     kNvsWifiSsid[]     = "wifi_ssid";
constexpr char     kNvsWifiPass[]     = "wifi_pass";

// ============================================================================
// State
// ============================================================================
String    savedSsid, savedPass;
bool      hasSavedWifi       = false;
bool      wifiConnecting     = false;
uint32_t  wifiConnectStartMs = 0;
bool      wifiConnected      = false;
bool      apStarted          = false;
bool      webStarted         = false;
bool      rebootPending      = false;
uint32_t  rebootAtMs         = 0;
uint32_t  lastHeartbeatMs    = 0;
uint32_t  heartbeat          = 0;

// Ethernet
bool      ethLinkUp          = false;
uint32_t  ethLastCheckMs     = 0;

// MQTT
bool      mqttConnected      = false;
bool      mqttConfigured     = false;
uint32_t  mqttLastAttemptMs  = 0;
MqttConfig mqttCfg;
DeviceConfig deviceCfg;

WiFiClient    mqttWifiTransport;
EthernetClient mqttEthTransport;
PubSubClient  mqttWifiClient(mqttWifiTransport);
PubSubClient  mqttEthClient(mqttEthTransport);
PubSubClient* mqtt = &mqttWifiClient;

WebServer server(80);

// ============================================================================
// NVS helpers
// ============================================================================
void loadWifiCreds() {
  Preferences p;
  if (!p.begin(kNvsNetNs, true)) return;
  savedSsid = p.getString(kNvsWifiSsid, "");
  savedPass = p.getString(kNvsWifiPass, "");
  p.end();
  hasSavedWifi = savedSsid.length() > 0;
}

bool saveWifiCreds(const String& ssid, const String& pass) {
  Preferences p;
  if (!p.begin(kNvsNetNs, false)) return false;
  p.putString(kNvsWifiSsid, ssid);
  p.putString(kNvsWifiPass, pass);
  p.end();
  return true;
}

// ============================================================================
// Web UI
// ============================================================================
String apSsid() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s-%02X%02X%02X", kApSsidPrefix, mac[3], mac[4], mac[5]);
  return String(buf);
}

void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void handleRoot() {
  String page = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>SEMS AIoT — Network</title>"
    "<style>body{font-family:sans-serif;max-width:480px;margin:24px auto;padding:0 16px;background:#111;color:#eee}"
    "h1{font-size:1.2em;margin-bottom:4px}.muted{color:#888;font-size:.85em}"
    ".panel{background:#1e1e1e;border-radius:8px;padding:16px;margin:12px 0}"
    "label{display:block;margin-top:10px;font-size:.85em;color:#aaa}"
    "input{width:100%;box-sizing:border-box;padding:8px;background:#2a2a2a;color:#eee;border:1px solid #444;border-radius:4px;margin-top:4px}"
    ".actions{display:flex;gap:8px;margin-top:12px}"
    "button{flex:1;padding:10px;background:#333;color:#eee;border:1px solid #555;border-radius:6px;cursor:pointer}"
    "button:hover{background:#444}.net{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid #2a2a2a}"
    ".ssid{font-weight:bold}.tag{font-size:.75em;color:#888}.use{flex:none;padding:6px 10px}"
    ".ok{color:#4caf50}.err{color:#f44336}"
    "</style></head><body>");
  page += F("<h1>SEMS AIoT</h1><div class='muted'>Network Setup</div>");

  // ETH status
  page += F("<div class='panel'><div class='muted'>🔌 Ethernet (W5500)</div>");
  page += F("<div id='ethSt' class='muted'>loading...</div></div>");

  // WiFi status
  page += F("<div class='panel'><div class='muted'>📶 WiFi</div>");
  page += F("<div id='wifiSt' class='muted'>loading...</div></div>");

  // MQTT status
  page += F("<div class='panel'><div class='muted'>📡 MQTT</div>");
  page += F("<div id='mqttSt' class='muted'>loading...</div></div>");

  // WiFi scan & save
  page += F("<div class='panel'><div class='muted'>⚙️ WiFi Setup</div>");
  page += F("<div class='actions'><button onclick='scan()'>🔍 Scan</button>"
            "<button onclick='loadSaved()'>📂 Load Saved</button></div>");
  page += F("<div id='scanSt' class='muted' style='margin-top:8px'></div>");
  page += F("<div id='netList' style='margin-top:8px'></div>");
  page += F("<form id='wfForm'>"
            "<label>SSID</label><input id='ssid' name='ssid' maxlength='64' required>"
            "<label>Password</label><input id='pass' name='password' type='password' maxlength='64'>"
            "<div class='actions'>"
            "<button type='submit'>💾 Save &amp; Reboot</button>"
            "<button type='button' onclick='reboot()'>🔄 Reboot</button>"
            "</div></form></div>");

  // MQTT config
  page += F("<div class='panel'><div class='muted'>⚙️ MQTT Setup</div>"
            "<form id='mqForm'>"
            "<label>Host</label><input id='mqHost' maxlength='128'>"
            "<label>Port</label><input id='mqPort' type='number'>"
            "<label>Base Topic</label><input id='mqTopic' maxlength='64'>"
            "<label>Username</label><input id='mqUser' maxlength='64'>"
            "<label>Password</label><input id='mqPass' type='password' maxlength='64'>"
            "<div class='actions'><button type='submit'>💾 Save MQTT</button></div>"
            "<div id='mqSt' class='muted' style='margin-top:8px'></div>"
            "</form></div>");

  page += F("<script>");
  page += F("async function refresh(){"
    "const d=await(await fetch('/api/status',{cache:'no-store'})).json();"
    "document.getElementById('ethSt').innerHTML=d.eth_link"
      "?'<span class=ok>Link UP — IP: '+d.eth_ip+'</span>'"
      ":'<span class=err>Link DOWN</span>';"
    "document.getElementById('wifiSt').innerHTML=d.wifi_connected"
      "?'<span class=ok>Connected — '+d.wifi_ssid+' ('+d.wifi_ip+')</span>'"
      ":'<span class=err>Offline — saved: '+(d.saved_ssid||'none')+'</span>';"
    "document.getElementById('mqttSt').innerHTML=d.mqtt_connected"
      "?'<span class=ok>Connected via '+d.mqtt_transport+'</span>'"
      ":'<span class=err>Disconnected ('+d.mqtt_host+':'+d.mqtt_port+')</span>';"
    "document.getElementById('mqHost').value=d.mqtt_host||'';"
    "document.getElementById('mqPort').value=d.mqtt_port||1883;"
    "document.getElementById('mqTopic').value=d.mqtt_topic||'';"
    "document.getElementById('mqUser').value=d.mqtt_user||'';"
    "}");
  page += F("async function scan(){"
    "document.getElementById('scanSt').textContent='Scanning...';"
    "document.getElementById('netList').innerHTML='';"
    "const d=await(await fetch('/api/wifi/scan',{cache:'no-store'})).json();"
    "if(!d.ok){document.getElementById('scanSt').textContent=d.error||'Failed';return;}"
    "document.getElementById('scanSt').textContent='Found '+d.networks.length+' network(s).';"
    "document.getElementById('netList').innerHTML=d.networks.map(n=>"
      "'<div class=net><div><div class=ssid>'+esc(n.ssid||'(hidden)')+'</div>"
      "<div class=tag>'+n.security+' | ch'+n.channel+' | '+n.rssi+'dBm</div></div>"
      "<button class=use type=button onclick=\"pick(\\''+esc(n.ssid||'')+'\\')\">Use</button></div>'"
    ").join('');}");
  page += F("function pick(s){document.getElementById('ssid').value=s;document.getElementById('pass').focus();}");
  page += F("async function loadSaved(){const d=await(await fetch('/api/status',{cache:'no-store'})).json();"
    "if(d.saved_ssid)document.getElementById('ssid').value=d.saved_ssid;}");
  page += F("document.getElementById('wfForm').onsubmit=async function(e){"
    "e.preventDefault();"
    "const r=await fetch('/api/wifi/save',{method:'POST',"
      "headers:{'Content-Type':'application/json'},"
      "body:JSON.stringify({ssid:document.getElementById('ssid').value,"
        "password:document.getElementById('pass').value})});"
    "const d=await r.json();"
    "if(d.ok){setTimeout(()=>reboot(),800);}else alert(d.error||'Save failed');};");
  page += F("document.getElementById('mqForm').onsubmit=async function(e){"
    "e.preventDefault();"
    "const r=await fetch('/api/mqtt/save',{method:'POST',"
      "headers:{'Content-Type':'application/json'},"
      "body:JSON.stringify({host:document.getElementById('mqHost').value,"
        "port:parseInt(document.getElementById('mqPort').value)||1883,"
        "base_topic:document.getElementById('mqTopic').value,"
        "username:document.getElementById('mqUser').value,"
        "password:document.getElementById('mqPass').value})});"
    "const d=await r.json();"
    "document.getElementById('mqSt').textContent=d.ok?'Saved. Reconnecting...':(d.error||'Failed');"
    "if(d.ok)setTimeout(refresh,2000);};");
  page += F("async function reboot(){await fetch('/api/reboot',{method:'POST'});alert('Rebooting...');}");
  page += F("function esc(s){return String(s).replace(/[&<>\"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[m]));}");
  page += F("refresh();setInterval(refresh,5000);");
  page += F("</script></body></html>");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", page);
}

void handleStatus() {
  String body;
  body.reserve(512);
  body += F("{\"eth_link\":");
  body += ethLinkUp ? F("true") : F("false");
  body += F(",\"eth_ip\":\"");
  body += ethLinkUp ? Ethernet.localIP().toString() : "";
  body += F("\",\"wifi_connected\":");
  body += wifiConnected ? F("true") : F("false");
  body += F(",\"wifi_ssid\":\"");
  body += wifiConnected ? WiFi.SSID() : "";
  body += F("\",\"wifi_ip\":\"");
  body += wifiConnected ? WiFi.localIP().toString() : "";
  body += F("\",\"saved_ssid\":\"");
  body += savedSsid;
  body += F("\",\"mqtt_connected\":");
  body += mqttConnected ? F("true") : F("false");
  body += F(",\"mqtt_transport\":\"");
  body += (mqtt == &mqttEthClient) ? F("ethernet") : F("wifi");
  body += F("\",\"mqtt_host\":\"");
  body += mqttCfg.host;
  body += F("\",\"mqtt_port\":");
  body += mqttCfg.port;
  body += F(",\"mqtt_topic\":\"");
  body += mqttCfg.base_topic;
  body += F("\",\"mqtt_user\":\"");
  body += mqttCfg.username;
  body += F("\"}");
  sendJson(200, body);
}

void handleWifiScan() {
  const int count = WiFi.scanNetworks(false, true);
  String body;
  body.reserve(256 + count * 80);
  body += F("{\"ok\":true,\"networks\":[");
  for (int i = 0; i < count; i++) {
    if (i > 0) body += ',';
    body += F("{\"ssid\":\"");
    body += WiFi.SSID(i);
    body += F("\",\"rssi\":");
    body += WiFi.RSSI(i);
    body += F(",\"channel\":");
    body += WiFi.channel(i);
    body += F(",\"security\":\"");
    body += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? F("open") : F("secured");
    body += F("\"}");
  }
  body += F("]}");
  WiFi.scanDelete();
  sendJson(200, body);
}

void handleWifiSave() {
  if (!server.hasArg("plain")) { sendJson(400, "{\"ok\":false,\"error\":\"no body\"}"); return; }
  String body = server.arg("plain");
  // simple parse — no ArduinoJson
  auto extractStr = [&](const char* key) -> String {
    String k = String("\"") + key + "\":\"";
    int start = body.indexOf(k);
    if (start < 0) return "";
    start += k.length();
    int end = body.indexOf('"', start);
    if (end < 0) return "";
    return body.substring(start, end);
  };
  String ssid = extractStr("ssid");
  String pass = extractStr("password");
  if (ssid.length() == 0) { sendJson(400, "{\"ok\":false,\"error\":\"ssid_required\"}"); return; }
  if (!saveWifiCreds(ssid, pass)) { sendJson(500, "{\"ok\":false,\"error\":\"save_failed\"}"); return; }
  savedSsid = ssid; savedPass = pass; hasSavedWifi = true;
  sendJson(200, "{\"ok\":true}");
  rebootPending = true;
  rebootAtMs = millis() + 1500;
}

void handleMqttSave() {
  if (!server.hasArg("plain")) { sendJson(400, "{\"ok\":false,\"error\":\"no body\"}"); return; }
  String body = server.arg("plain");
  auto extractStr = [&](const char* key) -> String {
    String k = String("\"") + key + "\":\"";
    int start = body.indexOf(k);
    if (start < 0) return "";
    start += k.length();
    int end = body.indexOf('"', start);
    if (end < 0) return "";
    return body.substring(start, end);
  };
  auto extractInt = [&](const char* key) -> int {
    String k = String("\"") + key + "\":";
    int start = body.indexOf(k);
    if (start < 0) return -1;
    start += k.length();
    int end = start;
    while (end < (int)body.length() && isdigit(body[end])) end++;
    return body.substring(start, end).toInt();
  };
  strlcpy(mqttCfg.host,       extractStr("host").c_str(),       sizeof(mqttCfg.host));
  strlcpy(mqttCfg.base_topic, extractStr("base_topic").c_str(), sizeof(mqttCfg.base_topic));
  strlcpy(mqttCfg.username,   extractStr("username").c_str(),   sizeof(mqttCfg.username));
  strlcpy(mqttCfg.password,   extractStr("password").c_str(),   sizeof(mqttCfg.password));
  int port = extractInt("port");
  if (port > 0) mqttCfg.port = port;
  ConfigManager::saveMqttConfig(mqttCfg);
  mqttConfigured = false;  // force reconfigure
  (*mqtt).disconnect();
  mqttConnected = false;
  sendJson(200, "{\"ok\":true}");
}

void handleReboot() {
  sendJson(200, "{\"ok\":true}");
  rebootPending = true;
  rebootAtMs = millis() + 800;
}

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void startWebServer() {
  if (webStarted) return;
  server.on("/",               HTTP_GET,  handleRoot);
  server.on("/api/status",     HTTP_GET,  handleStatus);
  server.on("/api/wifi/scan",  HTTP_GET,  handleWifiScan);
  server.on("/api/wifi/save",  HTTP_POST, handleWifiSave);
  server.on("/api/mqtt/save",  HTTP_POST, handleMqttSave);
  server.on("/api/reboot",     HTTP_POST, handleReboot);
  server.onNotFound(handleNotFound);
  server.begin();
  webStarted = true;
  Serial.println("Web server started");
}

// ============================================================================
// WiFi lifecycle
// ============================================================================
void startAp() {
  if (apStarted) { startWebServer(); return; }
  WiFi.softAP(apSsid().c_str(), kApPassword);
  apStarted = true;
  Serial.print("AP: "); Serial.print(apSsid());
  Serial.print(" pass="); Serial.println(kApPassword);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  startWebServer();
}

void handleWifiLifecycle(uint32_t nowMs) {
  if (wifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting = false;
      wifiConnected = true;
      Serial.print("WiFi connected: "); Serial.println(WiFi.localIP());
      startWebServer();
    } else if (nowMs - wifiConnectStartMs >= kWifiTimeoutMs) {
      wifiConnecting = false;
      Serial.println("WiFi timeout — starting AP");
      startAp();
    }
  } else if (wifiConnected) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      Serial.println("WiFi lost — starting AP");
      startAp();
    }
  }
}

// ============================================================================
// Ethernet lifecycle
// ============================================================================
void initEthernet() {
  uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint64_t chipId = ESP.getEfuseMac();
  mac[2] = (chipId >> 32) & 0xFF;
  mac[3] = (chipId >> 24) & 0xFF;
  mac[4] = (chipId >> 16) & 0xFF;
  mac[5] = (chipId >>  8) & 0xFF;

  pinMode(PinMap::kEthRst, OUTPUT);
  digitalWrite(PinMap::kEthRst, LOW); delay(20);
  digitalWrite(PinMap::kEthRst, HIGH); delay(50);

  Ethernet.init(PinMap::kEthCs);
  if (Ethernet.begin(mac, 8000) == 0) {
    Serial.println("ETH: DHCP failed");
  } else {
    ethLinkUp = true;
    Serial.print("ETH: IP="); Serial.println(Ethernet.localIP());
    startWebServer();
  }
}

void updateEthernetRuntime(uint32_t nowMs) {
  if (nowMs - ethLastCheckMs < 5000) return;
  ethLastCheckMs = nowMs;

  if (Ethernet.linkStatus() == LinkOFF) {
    if (ethLinkUp) { ethLinkUp = false; Serial.println("ETH: link down"); }
    return;
  }
  if (!ethLinkUp) {
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint64_t chipId = ESP.getEfuseMac();
    mac[2] = (chipId >> 32) & 0xFF; mac[3] = (chipId >> 24) & 0xFF;
    mac[4] = (chipId >> 16) & 0xFF; mac[5] = (chipId >>  8) & 0xFF;
    if (Ethernet.begin(mac, 8000) != 0) {
      ethLinkUp = true;
      Serial.print("ETH: link up IP="); Serial.println(Ethernet.localIP());
      startWebServer();
    }
  } else {
    Ethernet.maintain();
  }
}

// ============================================================================
// MQTT
// ============================================================================
void mqttCallback(char*, uint8_t*, unsigned int) {}

void updateMqtt(uint32_t nowMs) {
  if (!mqttCfg.enabled || mqttCfg.host[0] == '\0') return;
  const bool netOk = ethLinkUp || wifiConnected;
  if (!netOk) { mqttConnected = false; return; }

  // Pick best transport
  PubSubClient* preferred = ethLinkUp ? &mqttEthClient : &mqttWifiClient;
  if (mqtt != preferred) {
    if ((*mqtt).connected()) (*mqtt).disconnect();
    mqtt = preferred;
    mqttConfigured = false;
    mqttConnected = false;
  }

  if (!mqttConfigured) {
    (*mqtt).setServer(mqttCfg.host, mqttCfg.port);
    (*mqtt).setBufferSize(512);
    (*mqtt).setKeepAlive(30);
    (*mqtt).setCallback(mqttCallback);
    mqttConfigured = true;
  }

  if ((*mqtt).connected()) {
    mqttConnected = true;
    (*mqtt).loop();
    return;
  }

  mqttConnected = false;
  if (nowMs - mqttLastAttemptMs < 5000) return;
  mqttLastAttemptMs = nowMs;

  Serial.print("MQTT connecting to "); Serial.print(mqttCfg.host);
  Serial.print(":"); Serial.println(mqttCfg.port);

  bool ok;
  String clientId = String("sems-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqttCfg.username[0]) {
    ok = (*mqtt).connect(clientId.c_str(), mqttCfg.username, mqttCfg.password);
  } else {
    ok = (*mqtt).connect(clientId.c_str());
  }

  if (ok) {
    mqttConnected = true;
    Serial.print("MQTT connected via ");
    Serial.println(mqtt == &mqttEthClient ? "Ethernet" : "WiFi");
    // Publish a hello
    String topic = String(mqttCfg.base_topic) + "/net/hello";
    String payload = "{\"transport\":\"" + String(mqtt == &mqttEthClient ? "eth" : "wifi") + "\"}";
    (*mqtt).publish(topic.c_str(), payload.c_str());
  } else {
    Serial.print("MQTT failed, state="); Serial.println((*mqtt).state());
  }
}

// ============================================================================
// Setup & Loop
// ============================================================================
void setup() {
  Serial.begin(kSerialBaud);
  delay(300);
  Serial.println("\n=== SEMS AIoT — Network Test Firmware ===");

  deviceCfg = ConfigManager::loadDeviceConfig();
  mqttCfg   = ConfigManager::loadMqttConfig();

  loadWifiCreds();
  initEthernet();

  if (hasSavedWifi) {
    Serial.print("WiFi connecting to: "); Serial.println(savedSsid);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    wifiConnecting = true;
    wifiConnectStartMs = millis();
  } else {
    Serial.println("No saved WiFi — starting AP");
    startAp();
  }
}

void loop() {
  const uint32_t now = millis();

  handleWifiLifecycle(now);
  updateEthernetRuntime(now);
  updateMqtt(now);

  if (webStarted) server.handleClient();

  if (rebootPending && now >= rebootAtMs) {
    Serial.println("Rebooting...");
    ESP.restart();
  }

  if (now - lastHeartbeatMs >= 3000) {
    lastHeartbeatMs = now;
    heartbeat++;
    Serial.printf("hb=%u eth=%s wifi=%s mqtt=%s heap=%u\n",
      heartbeat,
      ethLinkUp    ? Ethernet.localIP().toString().c_str() : "down",
      wifiConnected ? WiFi.localIP().toString().c_str()    : "down",
      mqttConnected ? "up" : "down",
      ESP.getFreeHeap());
  }
}
