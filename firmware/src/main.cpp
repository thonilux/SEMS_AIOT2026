#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// --- pins ---
static constexpr uint8_t kLedPin = 2;

// --- OLED ---
// SSD1306 128x64, I2C, SDA=21 SCL=22 (ESP32 default), rotated 180
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R2, U8X8_PIN_NONE);
static bool oledReady = false;

// Status overlay state
static String oledL1, oledL2, oledL3;
static uint32_t oledUntilMs = 0;

// Page rotation state (used after overlay expires)
static uint8_t oledPage = 0;
static uint32_t oledPageMs = 0;
static constexpr uint32_t kPageIntervalMs = 4000;

void oledShow(const char* l1, const char* l2 = "", const char* l3 = "", uint32_t durationMs = 4000) {
  oledL1 = l1;
  oledL2 = l2;
  oledL3 = l3;
  oledUntilMs = millis() + durationMs;
  if (!oledReady) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawStr(0, 12, l1);
  if (l2[0]) oled.drawStr(0, 28, l2);
  if (l3[0]) oled.drawStr(0, 44, l3);
  oled.sendBuffer();
}

void oledDrawPage(uint8_t page) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  switch (page % 2) {
    case 0:
      oled.drawStr(0, 12, "SEMS AIoT");
      oled.drawStr(0, 28, "FW: " FW_VERSION);
      oled.drawStr(0, 44, "Uptime:");
      oled.drawStr(0, 56, (String(millis() / 1000) + "s").c_str());
      break;
    case 1:
      oled.drawStr(0, 12, "Network");
      oled.drawStr(0, 28, "-- no wifi --");
      oled.drawStr(0, 44, "ETH: --");
      break;
  }
  oled.sendBuffer();
}

void updateOled(uint32_t now) {
  if (!oledReady) return;
  // Overlay active — don't rotate
  if (now < oledUntilMs) return;
  // Rotate pages
  if (now - oledPageMs < kPageIntervalMs) return;
  oledPageMs = now;
  oledPage = (oledPage + 1) % 2;
  oledDrawPage(oledPage);
}

// --- LED blink ---
static uint32_t lastBlinkMs = 0;
static bool ledState = false;

void setup() {
  Serial.begin(115200);
  pinMode(kLedPin, OUTPUT);

  // Init OLED
  Wire.begin(21, 22);
  if (oled.begin()) {
    oledReady = true;
    Serial.println("OLED ready");
  } else {
    Serial.println("OLED init failed — check wiring");
  }

  Serial.println("\n=== SEMS AIoT ===");
  Serial.println("FW: " FW_VERSION);
  oledShow("SEMS AIoT", "FW: " FW_VERSION, "Booting...", 5000);
}

void loop() {
  const uint32_t now = millis();

  // LED blink
  if (now - lastBlinkMs >= 500) {
    lastBlinkMs = now;
    ledState = !ledState;
    digitalWrite(kLedPin, ledState);
  }

  updateOled(now);
}
