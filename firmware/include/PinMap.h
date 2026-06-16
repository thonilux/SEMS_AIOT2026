#pragma once

#include <Arduino.h>

namespace PinMap {
constexpr uint8_t kConfigButton = 32;
constexpr bool kConfigButtonActiveLow = true;

constexpr uint8_t kBuiltinLed = 2;
constexpr bool kBuiltinLedActiveHigh = true;

constexpr uint8_t kRs485Rx = 16;
constexpr uint8_t kRs485Tx = 17;

// Relay header pin 3 (GPIO13), active-LOW
constexpr uint8_t kRelayOutput = 13;
constexpr bool kRelayOutputActiveHigh = false;

// LCD I2C (hardware I2C: SDA=GPIO21, SCL=GPIO22 — ESP32 default)
constexpr uint8_t kDisplaySda = 21;
constexpr uint8_t kDisplayScl = 22;

// W5500 Ethernet (SPI)
constexpr uint8_t kEthernetCs  = 5;
constexpr uint8_t kEthernetMosi = 23;
constexpr uint8_t kEthernetMiso = 19;
constexpr uint8_t kEthernetSck  = 18;
constexpr uint8_t kEthernetInt  = 27;
constexpr uint8_t kEthernetRst  = 26;
}  // namespace PinMap
