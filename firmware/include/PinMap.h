#pragma once

#include <Arduino.h>

namespace PinMap {
constexpr uint8_t kConfigButton = 4;
constexpr bool kConfigButtonActiveLow = false;

constexpr uint8_t kBuiltinLed = 2;
constexpr bool kBuiltinLedActiveHigh = true;

constexpr uint8_t kRs485Rx = 16;
constexpr uint8_t kRs485Tx = 17;

constexpr uint8_t kRelayOutput = 13;
constexpr bool kRelayOutputActiveHigh = false; // Active-LOW for 4-channel relay modules (0=ON, 1=OFF)

// W5500 Ethernet (SPI)
constexpr uint8_t kEthCs  = 5;
constexpr uint8_t kEthInt = 27;
constexpr uint8_t kEthRst = 26;
// MOSI=23, MISO=19, SCLK=18 — ESP32 VSPI defaults, wired implicitly

// LCD I2C: SDA=21, SCL=22 — ESP32 I2C defaults, wired implicitly
}  // namespace PinMap
