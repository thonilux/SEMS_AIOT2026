#pragma once

#include <Arduino.h>

namespace PinMap {
constexpr uint8_t kConfigButton = 32;
constexpr bool kConfigButtonActiveLow = true;

constexpr uint8_t kBuiltinLed = 2;
constexpr bool kBuiltinLedActiveHigh = true;

constexpr uint8_t kRs485Rx = 16;
constexpr uint8_t kRs485Tx = 17;

constexpr uint8_t kRelayOutput = 25;
constexpr bool kRelayOutputActiveHigh = true;

constexpr uint8_t kDisplayCs = 26;
constexpr uint8_t kDisplayDc = 21;
constexpr uint8_t kDisplayReset = 22;
}  // namespace PinMap
