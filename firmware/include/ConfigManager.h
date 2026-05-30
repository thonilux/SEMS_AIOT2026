#pragma once
#include <Arduino.h>

// ============================================================================
// CONFIG MANAGER - Unified NVS Persistence Layer
// ============================================================================
// Author: Claude (AI Assistant) @ 2026-05-30
// Purpose: Centralize all configuration storage across multiple NVS namespaces
// Scope: Device, Network, MQTT, Modbus, Protection, Display, History, System
// Phase: 10 (Web Dashboard and Config API)
// ============================================================================

struct DeviceConfig {
  char device_name[64];           // Friendly name
  char hostname[64];              // mDNS hostname
  char timezone[32];              // e.g., "WIB-7"
  uint8_t co2_factor_kg_per_kwh;  // CO2 emission factor
};

struct MqttConfig {
  char host[64];
  uint16_t port;
  char username[64];
  char password[64];
  char client_id[64];
  char base_topic[64];
  uint16_t publish_interval_sec;
  bool enabled;
};

struct ModbusConfig {
  uint32_t baudrate;
  uint8_t slave_id;
  uint8_t parity;           // 0=EVEN, 1=ODD, 2=NONE
  uint8_t stop_bits;
  uint16_t poll_interval_ms;
  uint16_t timeout_ms;
  uint8_t retry_count;
  uint8_t meter_profile;    // 0=PM2230, 1=PM1611
};

struct ProtectionConfig {
  bool relay_enabled;
  uint8_t current_limit_a;
  uint32_t trip_delay_ms;
  uint8_t reset_mode;           // 0=MANUAL, 1=AUTO
  bool auto_retry_enabled;
  uint16_t auto_retry_delay_sec;
  bool trip_on_meter_stale;
};

struct DisplayConfig {
  bool enabled;
  uint8_t type;                    // 0=ST7567, 1=SSD1306
  uint8_t i2c_address;
  uint8_t rotation_interval_sec;
  uint8_t brightness;
};

struct HistoryConfig {
  bool enabled;
  uint8_t days_retained;
  uint16_t flush_interval_sec;
};

struct SystemConfig {
  char ntp_server1[64];
  char ntp_server2[64];
  bool debug_enabled;
};

class ConfigManager {
 public:
  static DeviceConfig loadDeviceConfig();
  static bool saveDeviceConfig(const DeviceConfig& cfg);

  static MqttConfig loadMqttConfig();
  static bool saveMqttConfig(const MqttConfig& cfg);

  static ModbusConfig loadModbusConfig();
  static bool saveModbusConfig(const ModbusConfig& cfg);

  static ProtectionConfig loadProtectionConfig();
  static bool saveProtectionConfig(const ProtectionConfig& cfg);

  static DisplayConfig loadDisplayConfig();
  static bool saveDisplayConfig(const DisplayConfig& cfg);

  static HistoryConfig loadHistoryConfig();
  static bool saveHistoryConfig(const HistoryConfig& cfg);

  static SystemConfig loadSystemConfig();
  static bool saveSystemConfig(const SystemConfig& cfg);

 private:
  static constexpr const char* NS_DEVICE = "device";
  static constexpr const char* NS_MQTT = "mqtt";
  static constexpr const char* NS_MODBUS = "modbus";
  static constexpr const char* NS_PROTECTION = "protection";
  static constexpr const char* NS_DISPLAY = "display";
  static constexpr const char* NS_HISTORY = "history";
  static constexpr const char* NS_SYSTEM = "system";
};
