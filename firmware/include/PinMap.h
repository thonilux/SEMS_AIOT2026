#pragma once

#include <Arduino.h>

namespace PinMap {
constexpr uint8_t kConfigButton = 32;
constexpr bool kConfigButtonActiveLow = true;

constexpr uint8_t kBuiltinLed = 2;
constexpr bool kBuiltinLedActiveHigh = true;

constexpr uint8_t kRs485Rx = 16;
constexpr uint8_t kRs485Tx = 17;
}  // namespace PinMap
