#include <Arduino.h>

static constexpr uint8_t kLedPin = 2;
static constexpr uint32_t kBlinkMs = 500;

static uint32_t lastBlinkMs = 0;
static bool ledState = false;

void setup() {
  Serial.begin(115200);
  pinMode(kLedPin, OUTPUT);
  Serial.println("\n\n=== SEMS AIoT ===");
  Serial.print("FW: ");
  Serial.println(FW_VERSION);
  Serial.println("Step 1: boot OK");
}

void loop() {
  const uint32_t now = millis();
  if (now - lastBlinkMs >= kBlinkMs) {
    lastBlinkMs = now;
    ledState = !ledState;
    digitalWrite(kLedPin, ledState);
  }
}
