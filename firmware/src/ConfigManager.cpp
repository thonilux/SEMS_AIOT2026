#include "ConfigManager.h"
#include <Preferences.h>
#include <cstring>

namespace {
template <typename Writer>
bool saveWithRecovery(const char* ns, Writer writer) {
  Preferences prefs;
  if (!prefs.begin(ns, false)) {
    return false;
  }

  bool ok = writer(prefs);
  if (ok) {
    prefs.end();
    return true;
  }

  prefs.clear();
  ok = writer(prefs);
  prefs.end();
  return ok;
}

bool putOptionalString(Preferences& prefs, const char* key, const char* value) {
  if (!value || value[0] == '\0') {
    if (prefs.isKey(key)) {
      return prefs.remove(key);
    }
    return true;
  }

  return prefs.putString(key, value) > 0;
}
}  // namespace

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
    strncpy(cfg.device_name, "PM1611-Device", sizeof(cfg.device_name) - 1);
  }
  if (strlen(cfg.hostname) == 0) {
    strncpy(cfg.hostname, "pm1611", sizeof(cfg.hostname) - 1);
  }
  if (strlen(cfg.timezone) == 0) {
    strncpy(cfg.timezone, "WIB-7", sizeof(cfg.timezone) - 1);
  }

  return cfg;
}

bool ConfigManager::saveDeviceConfig(const DeviceConfig& cfg) {
  return saveWithRecovery(NS_DEVICE, [&](Preferences& prefs) {
    bool ok = true;
    ok &= putOptionalString(prefs, "device_name", cfg.device_name);
    ok &= putOptionalString(prefs, "hostname", cfg.hostname);
    ok &= putOptionalString(prefs, "timezone", cfg.timezone);
    ok &= prefs.putUChar("co2_factor", cfg.co2_factor_kg_per_kwh) > 0;
    return ok;
  });
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
  cfg.publish_interval_sec = prefs.getUShort("pub_int_sec", 5);
  cfg.enabled = prefs.getBool("enabled", false);

  prefs.end();

  // Apply defaults if empty
  if (strlen(cfg.host) == 0) {
    strncpy(cfg.host, "localhost", sizeof(cfg.host) - 1);
  }
  if (strlen(cfg.client_id) == 0) {
    strncpy(cfg.client_id, "pm1611", sizeof(cfg.client_id) - 1);
  }
  if (strlen(cfg.base_topic) == 0) {
    strncpy(cfg.base_topic, "pm1611", sizeof(cfg.base_topic) - 1);
  }

  return cfg;
}

bool ConfigManager::saveMqttConfig(const MqttConfig& cfg) {
  return saveWithRecovery(NS_MQTT, [&](Preferences& prefs) {
    bool ok = true;
    ok &= putOptionalString(prefs, "host", cfg.host);
    ok &= prefs.putUShort("port", cfg.port) > 0;
    ok &= putOptionalString(prefs, "username", cfg.username);
    ok &= putOptionalString(prefs, "password", cfg.password);
    ok &= putOptionalString(prefs, "client_id", cfg.client_id);
    ok &= putOptionalString(prefs, "base_topic", cfg.base_topic);
    ok &= prefs.putUShort("pub_int_sec", cfg.publish_interval_sec) > 0;
    ok &= prefs.putBool("enabled", cfg.enabled) > 0;
    return ok;
  });
}

// ===== MODBUS CONFIG =====
ModbusConfig ConfigManager::loadModbusConfig() {
  Preferences prefs;
  prefs.begin(NS_MODBUS, true);

  ModbusConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.baudrate = prefs.getUInt("baudrate", 19200);
  cfg.slave_id = prefs.getUChar("slave", 1);
  cfg.parity = prefs.getUChar("parity", 0);
  cfg.stop_bits = prefs.getUChar("stopbits", 1);
  cfg.poll_interval_ms = prefs.getUShort("poll_ms", 1000);
  cfg.timeout_ms = prefs.getUShort("timeout_ms", 1000);
  cfg.retry_count = prefs.getUChar("retry_cnt", 3);
  cfg.meter_profile = prefs.getUChar("profile", 0);

  prefs.end();
  return cfg;
}

bool ConfigManager::saveModbusConfig(const ModbusConfig& cfg) {
  return saveWithRecovery(NS_MODBUS, [&](Preferences& prefs) {
    bool ok = true;
    ok &= prefs.putUInt("baudrate", cfg.baudrate) > 0;
    ok &= prefs.putUChar("slave", cfg.slave_id) > 0;
    ok &= prefs.putUChar("parity", cfg.parity) > 0;
    ok &= prefs.putUChar("stopbits", cfg.stop_bits) > 0;
    ok &= prefs.putUShort("poll_ms", cfg.poll_interval_ms) > 0;
    ok &= prefs.putUShort("timeout_ms", cfg.timeout_ms) > 0;
    ok &= prefs.putUChar("retry_cnt", cfg.retry_count) > 0;
    ok &= prefs.putUChar("profile", cfg.meter_profile) > 0;
    return ok;
  });
}

// ===== PROTECTION CONFIG =====
ProtectionConfig ConfigManager::loadProtectionConfig() {
  Preferences prefs;
  prefs.begin(NS_PROTECTION, true);

  ProtectionConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.relay_enabled = prefs.getBool("relay_en", false);
  cfg.current_limit_a = prefs.getUChar("curr_lim", 16);
  cfg.trip_delay_ms = prefs.getUInt("trip_dly", 1000);
  cfg.reset_mode = prefs.getUChar("reset_md", 0);
  cfg.auto_retry_enabled = prefs.getBool("auto_retry_en", false);
  cfg.auto_retry_delay_sec = prefs.getUShort("auto_retry_dly", 300);
  cfg.trip_on_meter_stale = prefs.getBool("trip_stale", true);

  prefs.end();
  return cfg;
}

bool ConfigManager::saveProtectionConfig(const ProtectionConfig& cfg) {
  return saveWithRecovery(NS_PROTECTION, [&](Preferences& prefs) {
    bool ok = true;
    ok &= prefs.putBool("relay_en", cfg.relay_enabled) > 0;
    ok &= prefs.putUChar("curr_lim", cfg.current_limit_a) > 0;
    ok &= prefs.putUInt("trip_dly", cfg.trip_delay_ms) > 0;
    ok &= prefs.putUChar("reset_md", cfg.reset_mode) > 0;
    ok &= prefs.putBool("auto_retry_en", cfg.auto_retry_enabled) > 0;
    ok &= prefs.putUShort("auto_retry_dly", cfg.auto_retry_delay_sec) > 0;
    ok &= prefs.putBool("trip_stale", cfg.trip_on_meter_stale) > 0;
    return ok;
  });
}

// ===== DISPLAY CONFIG =====
DisplayConfig ConfigManager::loadDisplayConfig() {
  Preferences prefs;
  prefs.begin(NS_DISPLAY, true);

  DisplayConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.enabled = prefs.getBool("enabled", false);
  cfg.type = prefs.getUChar("type", 0);
  cfg.i2c_address = prefs.getUChar("i2c_addr", 0x3C);
  cfg.rotation_interval_sec = prefs.getUChar("rot_int_sec", 5);
  cfg.brightness = prefs.getUChar("brightness", 200);

  prefs.end();
  return cfg;
}

bool ConfigManager::saveDisplayConfig(const DisplayConfig& cfg) {
  return saveWithRecovery(NS_DISPLAY, [&](Preferences& prefs) {
    bool ok = true;
    ok &= prefs.putBool("enabled", cfg.enabled) > 0;
    ok &= prefs.putUChar("type", cfg.type) > 0;
    ok &= prefs.putUChar("i2c_addr", cfg.i2c_address) > 0;
    ok &= prefs.putUChar("rot_int_sec", cfg.rotation_interval_sec) > 0;
    ok &= prefs.putUChar("brightness", cfg.brightness) > 0;
    return ok;
  });
}

// ===== HISTORY CONFIG =====
HistoryConfig ConfigManager::loadHistoryConfig() {
  Preferences prefs;
  prefs.begin(NS_HISTORY, true);

  HistoryConfig cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.enabled = prefs.getBool("enabled", true);
  cfg.days_retained = prefs.getUChar("days_retained", 7);
  cfg.flush_interval_sec = prefs.getUShort("flush_int_sec", 3600);

  prefs.end();
  return cfg;
}

bool ConfigManager::saveHistoryConfig(const HistoryConfig& cfg) {
  return saveWithRecovery(NS_HISTORY, [&](Preferences& prefs) {
    bool ok = true;
    ok &= prefs.putBool("enabled", cfg.enabled) > 0;
    ok &= prefs.putUChar("days_retained", cfg.days_retained) > 0;
    ok &= prefs.putUShort("flush_int_sec", cfg.flush_interval_sec) > 0;
    return ok;
  });
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
  return saveWithRecovery(NS_SYSTEM, [&](Preferences& prefs) {
    bool ok = true;
    ok &= putOptionalString(prefs, "ntp_server1", cfg.ntp_server1);
    ok &= putOptionalString(prefs, "ntp_server2", cfg.ntp_server2);
    ok &= prefs.putBool("debug_enabled", cfg.debug_enabled) > 0;
    return ok;
  });
}
