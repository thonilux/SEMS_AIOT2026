#include "ConfigManager.h"
#include <Preferences.h>
#include <cstring>

// ============================================================================
// CONFIG MANAGER IMPLEMENTATION
// ============================================================================
// Author: Claude (AI Assistant) @ 2026-05-30
// Provides load/save for all config categories with defaults
// Phase: 10 (Web Dashboard and Config API)
// ============================================================================

// ===== DEVICE CONFIG =====
DeviceConfig ConfigManager::loadDeviceConfig() {
  Preferences prefs;
  prefs.begin(NS_DEVICE, true);  // read-only

  DeviceConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  prefs.getString("device_name", cfg.device_name, sizeof(cfg.device_name));
  prefs.getString("hostname", cfg.hostname, sizeof(cfg.hostname));
  prefs.getString("timezone", cfg.timezone, sizeof(cfg.timezone));
  cfg.co2_factor_kg_per_kwh = prefs.getUChar("co2_factor", 0);

  prefs.end();

  // Apply defaults if empty
  if (strlen(cfg.device_name) == 0) {
    strncpy(cfg.device_name, "nocola", sizeof(cfg.device_name) - 1);
  }
  if (strlen(cfg.hostname) == 0) {
    strncpy(cfg.hostname, "sems", sizeof(cfg.hostname) - 1);
  }
  if (strlen(cfg.timezone) == 0) {
    strncpy(cfg.timezone, "WIB-7", sizeof(cfg.timezone) - 1);
  }

  return cfg;
}

bool ConfigManager::saveDeviceConfig(const DeviceConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(NS_DEVICE, false)) return false;

  bool ok = true;
  ok &= prefs.putString("device_name", cfg.device_name) >= 0;
  ok &= prefs.putString("hostname", cfg.hostname) >= 0;
  ok &= prefs.putString("timezone", cfg.timezone) >= 0;
  ok &= prefs.putUChar("co2_factor", cfg.co2_factor_kg_per_kwh) > 0;

  prefs.end();
  return ok;
}

// ===== MQTT CONFIG =====
MqttConfig ConfigManager::loadMqttConfig() {
  Preferences prefs;
  prefs.begin(NS_MQTT, true);

  MqttConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  prefs.getString("host", cfg.host, sizeof(cfg.host));
  cfg.port = prefs.getUShort("port", 1883);
  prefs.getString("username", cfg.username, sizeof(cfg.username));
  prefs.getString("password", cfg.password, sizeof(cfg.password));
  prefs.getString("client_id", cfg.client_id, sizeof(cfg.client_id));
  prefs.getString("base_topic", cfg.base_topic, sizeof(cfg.base_topic));
  cfg.publish_interval_sec = prefs.getUShort("pub_interval", 300);  // default 5 minutes
  cfg.enabled = prefs.getBool("enabled", false);

  prefs.end();

  // Apply defaults if empty
  if (strlen(cfg.host) == 0) {
    strncpy(cfg.host, "localhost", sizeof(cfg.host) - 1);
  }
  if (strlen(cfg.client_id) == 0) {
    strncpy(cfg.client_id, "sems", sizeof(cfg.client_id) - 1);
  }
  if (strlen(cfg.base_topic) == 0) {
    strncpy(cfg.base_topic, "trofis/enms", sizeof(cfg.base_topic) - 1);
  }

  return cfg;
}

bool ConfigManager::saveMqttConfig(const MqttConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(NS_MQTT, false)) return false;

  bool ok = true;
  ok &= prefs.putString("host", cfg.host) >= 0;
  ok &= prefs.putUShort("port", cfg.port) > 0;
  ok &= prefs.putString("username", cfg.username) >= 0;
  ok &= prefs.putString("password", cfg.password) >= 0;
  ok &= prefs.putString("client_id", cfg.client_id) >= 0;
  ok &= prefs.putString("base_topic", cfg.base_topic) >= 0;
  ok &= prefs.putUShort("pub_interval", cfg.publish_interval_sec) > 0;
  ok &= prefs.putBool("enabled", cfg.enabled) > 0;

  prefs.end();
  return ok;
}

// ===== MODBUS CONFIG =====
ModbusConfig ConfigManager::loadModbusConfig() {
  Preferences prefs;
  prefs.begin(NS_MODBUS, true);

  ModbusConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.baudrate = prefs.getUInt("baudrate", 9600);
  cfg.slave_id[0] = prefs.getUChar("slave_id0", 1);
  cfg.slave_id[1] = prefs.getUChar("slave_id1", 2);
  cfg.slave_id[2] = prefs.getUChar("slave_id2", 3);
  cfg.parity = prefs.getUChar("parity", 2);
  cfg.stop_bits = prefs.getUChar("stop_bits", 1);
  cfg.poll_interval_ms = prefs.getUShort("poll_interval", 1000);
  cfg.timeout_ms = prefs.getUShort("timeout_ms", 1000);
  cfg.retry_count = prefs.getUChar("retry_count", 3);
  cfg.meter_profile = prefs.getUChar("meter_profile", 0);

  prefs.end();
  return cfg;
}

bool ConfigManager::saveModbusConfig(const ModbusConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(NS_MODBUS, false)) return false;

  bool ok = true;
  ok &= prefs.putUInt("baudrate", cfg.baudrate) > 0;
  ok &= prefs.putUChar("slave_id0", cfg.slave_id[0]) > 0;
  ok &= prefs.putUChar("slave_id1", cfg.slave_id[1]) > 0;
  ok &= prefs.putUChar("slave_id2", cfg.slave_id[2]) > 0;
  ok &= prefs.putUChar("parity", cfg.parity) > 0;
  ok &= prefs.putUChar("stop_bits", cfg.stop_bits) > 0;
  ok &= prefs.putUShort("poll_interval", cfg.poll_interval_ms) > 0;
  ok &= prefs.putUShort("timeout_ms", cfg.timeout_ms) > 0;
  ok &= prefs.putUChar("retry_count", cfg.retry_count) > 0;
  ok &= prefs.putUChar("meter_profile", cfg.meter_profile) > 0;

  prefs.end();
  return ok;
}

// ===== MODBUS REGISTER MAPPING (/configmod) =====
// Stored as a single line-based blob string (Preferences has no native
// array/struct support): one entry per line, fields pipe-delimited:
//   field_key|slave_id|function|address|datatype|scale
ModbusMapConfig ConfigManager::loadModbusMapConfig() {
  Preferences prefs;
  prefs.begin(NS_MODMAP, true);
  String blob = prefs.getString("map", "");
  String name = prefs.getString("name", "");
  prefs.end();

  ModbusMapConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.count = 0;
  strncpy(cfg.name, name.c_str(), sizeof(cfg.name) - 1);

  int lineStart = 0;
  const int blobLen = blob.length();
  while (lineStart < blobLen && cfg.count < kModbusMapMaxEntries) {
    int lineEnd = blob.indexOf('\n', lineStart);
    if (lineEnd < 0) lineEnd = blobLen;
    String line = blob.substring(lineStart, lineEnd);
    lineStart = lineEnd + 1;
    line.trim();
    if (line.length() == 0) continue;

    // Split into exactly 6 pipe-delimited fields.
    int fieldStart = 0;
    String fields[6];
    bool malformed = false;
    for (int f = 0; f < 6; f++) {
      int sep = (f < 5) ? line.indexOf('|', fieldStart) : line.length();
      if (sep < 0) {
        malformed = true;
        break;
      }
      fields[f] = line.substring(fieldStart, sep);
      fieldStart = sep + 1;
    }
    if (malformed) continue;

    const uint8_t slaveId = static_cast<uint8_t>(fields[1].toInt());
    const uint8_t function = static_cast<uint8_t>(fields[2].toInt());
    const uint8_t datatype = static_cast<uint8_t>(fields[4].toInt());
    if (slaveId == 0 || slaveId > 247) continue;
    if (function != 3 && function != 4) continue;
    if (datatype > 4) continue;
    if (fields[0].length() == 0) continue;

    ModbusMapEntry& entry = cfg.entries[cfg.count];
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.field_key, fields[0].c_str(), sizeof(entry.field_key) - 1);
    entry.slave_id = slaveId;
    entry.function = function;
    entry.address = static_cast<uint16_t>(fields[3].toInt());
    entry.datatype = datatype;
    entry.scale = fields[5].toFloat();
    if (entry.scale == 0.0f) entry.scale = 1.0f;
    cfg.count++;
  }

  return cfg;
}

bool ConfigManager::saveModbusMapConfig(const ModbusMapConfig& cfg) {
  if (cfg.count > kModbusMapMaxEntries) return false;

  String blob;
  blob.reserve(cfg.count * 64 + 16);
  for (uint8_t i = 0; i < cfg.count; i++) {
    const ModbusMapEntry& entry = cfg.entries[i];
    if (entry.slave_id == 0 || entry.slave_id > 247) continue;
    if (entry.function != 3 && entry.function != 4) continue;
    if (entry.datatype > 4) continue;

    // Sanitize field_key: strip the delimiter characters so a stray '|' or
    // '\n' in a custom key can't corrupt the blob format.
    String key = String(entry.field_key);
    key.replace("|", "_");
    key.replace("\n", "_");
    key.replace("\r", "_");
    if (key.length() == 0) continue;
    if (key.length() > 39) key = key.substring(0, 39);

    blob += key;
    blob += '|';
    blob += String(entry.slave_id);
    blob += '|';
    blob += String(entry.function);
    blob += '|';
    blob += String(entry.address);
    blob += '|';
    blob += String(entry.datatype);
    blob += '|';
    blob += String(entry.scale, 6);
    blob += '\n';
  }

  String name = String(cfg.name);
  name.trim();
  if (name.length() > 39) name = name.substring(0, 39);

  Preferences prefs;
  if (!prefs.begin(NS_MODMAP, false)) return false;
  bool ok = prefs.putString("map", blob) >= 0;
  ok &= prefs.putString("name", name) >= 0;
  prefs.end();
  return ok;
}

// ===== PROTECTION CONFIG =====
ProtectionConfig ConfigManager::loadProtectionConfig() {
  Preferences prefs;
  prefs.begin(NS_PROTECTION, true);

  ProtectionConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.relay_enabled = prefs.getBool("relay_enabled", true);
  cfg.relay_pin[0] = prefs.getUChar("r_pin0", 2);
  cfg.relay_pin[1] = prefs.getUChar("r_pin1", 15);
  cfg.relay_pin[2] = prefs.getUChar("r_pin2", 14);
  cfg.relay_pin[3] = prefs.getUChar("r_pin3", 13);
  cfg.relay_slave_id[0] = prefs.getUChar("r_sid0", 1);
  cfg.relay_slave_id[1] = prefs.getUChar("r_sid1", 2);
  cfg.relay_slave_id[2] = prefs.getUChar("r_sid2", 3);
  cfg.relay_slave_id[3] = prefs.getUChar("r_sid3", 4);
  cfg.current_limit_a = prefs.getUChar("curr_limit", 16);
  cfg.trip_delay_ms = prefs.getUInt("trip_delay_ms", 1000);
  cfg.reset_mode = prefs.getUChar("reset_mode", 0);
  cfg.auto_retry_enabled = prefs.getBool("auto_retry_en", false);
  cfg.auto_retry_delay_sec = prefs.getUShort("auto_retry_del", 300);
  cfg.trip_on_meter_stale = prefs.getBool("trip_on_stale", false);

  prefs.end();
  return cfg;
}

bool ConfigManager::saveProtectionConfig(const ProtectionConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(NS_PROTECTION, false)) return false;

  bool ok = true;
  ok &= prefs.putBool("relay_enabled", cfg.relay_enabled) > 0;
  ok &= prefs.putUChar("r_pin0", cfg.relay_pin[0]) > 0;
  ok &= prefs.putUChar("r_pin1", cfg.relay_pin[1]) > 0;
  ok &= prefs.putUChar("r_pin2", cfg.relay_pin[2]) > 0;
  ok &= prefs.putUChar("r_pin3", cfg.relay_pin[3]) > 0;
  ok &= prefs.putUChar("r_sid0", cfg.relay_slave_id[0]) > 0;
  ok &= prefs.putUChar("r_sid1", cfg.relay_slave_id[1]) > 0;
  ok &= prefs.putUChar("r_sid2", cfg.relay_slave_id[2]) > 0;
  ok &= prefs.putUChar("r_sid3", cfg.relay_slave_id[3]) > 0;
  ok &= prefs.putUChar("curr_limit", cfg.current_limit_a) > 0;
  ok &= prefs.putUInt("trip_delay_ms", cfg.trip_delay_ms) > 0;
  ok &= prefs.putUChar("reset_mode", cfg.reset_mode) > 0;
  ok &= prefs.putBool("auto_retry_en", cfg.auto_retry_enabled) > 0;
  ok &= prefs.putUShort("auto_retry_del", cfg.auto_retry_delay_sec) > 0;
  ok &= prefs.putBool("trip_on_stale", cfg.trip_on_meter_stale) > 0;

  prefs.end();
  return ok;
}

void ConfigManager::loadRelayStates(uint8_t states[4]) {
  Preferences prefs;
  prefs.begin(NS_PROTECTION, true);
  states[0] = prefs.getUChar("r_st0", 0);
  states[1] = prefs.getUChar("r_st1", 0);
  states[2] = prefs.getUChar("r_st2", 0);
  states[3] = prefs.getUChar("r_st3", 0);
  prefs.end();
}

bool ConfigManager::saveRelayStates(const uint8_t states[4]) {
  Preferences prefs;
  if (!prefs.begin(NS_PROTECTION, false)) return false;
  prefs.putUChar("r_st0", states[0]);
  prefs.putUChar("r_st1", states[1]);
  prefs.putUChar("r_st2", states[2]);
  prefs.putUChar("r_st3", states[3]);
  prefs.end();
  return true;
}

// ===== DISPLAY CONFIG =====
DisplayConfig ConfigManager::loadDisplayConfig() {
  Preferences prefs;
  prefs.begin(NS_DISPLAY, true);

  DisplayConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.enabled = prefs.getBool("enabled", true);
  cfg.type = prefs.getUChar("type", 1);
  cfg.i2c_address = prefs.getUChar("i2c_address", 0x3C);
  cfg.rotation_interval_sec = prefs.getUChar("rot_interval", 5);
  cfg.brightness = prefs.getUChar("brightness", 200);

  prefs.end();
  return cfg;
}

bool ConfigManager::saveDisplayConfig(const DisplayConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(NS_DISPLAY, false)) return false;

  bool ok = true;
  ok &= prefs.putBool("enabled", cfg.enabled) > 0;
  ok &= prefs.putUChar("type", cfg.type) > 0;
  ok &= prefs.putUChar("i2c_address", cfg.i2c_address) > 0;
  ok &= prefs.putUChar("rot_interval", cfg.rotation_interval_sec) > 0;
  ok &= prefs.putUChar("brightness", cfg.brightness) > 0;

  prefs.end();
  return ok;
}

// ===== HISTORY CONFIG =====
HistoryConfig ConfigManager::loadHistoryConfig() {
  Preferences prefs;
  prefs.begin(NS_HISTORY, true);

  HistoryConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.enabled = prefs.getBool("enabled", true);
  cfg.days_retained = prefs.getUChar("days_retained", 7);
  cfg.flush_interval_sec = prefs.getUShort("flush_interval", 3600);

  prefs.end();
  return cfg;
}

bool ConfigManager::saveHistoryConfig(const HistoryConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(NS_HISTORY, false)) return false;

  bool ok = true;
  ok &= prefs.putBool("enabled", cfg.enabled) > 0;
  ok &= prefs.putUChar("days_retained", cfg.days_retained) > 0;
  ok &= prefs.putUShort("flush_interval", cfg.flush_interval_sec) > 0;

  prefs.end();
  return ok;
}

// ===== SYSTEM CONFIG =====
SystemConfig ConfigManager::loadSystemConfig() {
  Preferences prefs;
  prefs.begin(NS_SYSTEM, true);

  SystemConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  prefs.getString("ntp_server1", cfg.ntp_server1, sizeof(cfg.ntp_server1));
  prefs.getString("ntp_server2", cfg.ntp_server2, sizeof(cfg.ntp_server2));
  cfg.debug_enabled = prefs.getBool("debug_enabled", false);

  prefs.end();

  // Apply defaults if empty
  if (strlen(cfg.ntp_server1) == 0) {
    strncpy(cfg.ntp_server1, "pool.ntp.org", sizeof(cfg.ntp_server1) - 1);
  }
  if (strlen(cfg.ntp_server2) == 0) {
    strncpy(cfg.ntp_server2, "time.google.com", sizeof(cfg.ntp_server2) - 1);
  }

  return cfg;
}

bool ConfigManager::saveSystemConfig(const SystemConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(NS_SYSTEM, false)) return false;

  bool ok = true;
  ok &= prefs.putString("ntp_server1", cfg.ntp_server1) >= 0;
  ok &= prefs.putString("ntp_server2", cfg.ntp_server2) >= 0;
  ok &= prefs.putBool("debug_enabled", cfg.debug_enabled) > 0;

  prefs.end();
  return ok;
}
