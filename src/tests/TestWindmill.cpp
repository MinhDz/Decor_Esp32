#include "tests/TestWindmill.h"
#include "tests/TestDisplay.h"
#include "Config.h"
#include <math.h>

namespace TestWindmill {

  static const uint8_t PWM_CH_MOTOR = 2; // Kênh LEDC PWM cho Motor & Vành đèn LED (GPIO 1)
  static const uint32_t PWM_FREQ    = 5000;
  static const uint8_t PWM_RES      = 8; // 0..255

  static bool initialized = false;
  static uint8_t currentModeIdx = 0; // 0: Tắt, 1: 35%, 2: 60%, 3: 85%, 4: Thở
  static uint8_t powerPct = 0;
  static bool breathingActive = false;

  static const char* MODE_NAMES[5] = {
    "Tắt (0%)",
    "Êm dịu (35%)",
    "Tiêu chuẩn (60%)",
    "Tối đa (85%)",
    "Nhịp thở (Breathing)"
  };

  void init() {
    if (initialized) return;
    Serial.println("\n-------------------------------------------------------");
    Serial.println("🎡 [L298N DECOR] KHỞI TẠO MODULE CẦU H L298N (GPIO 1 -> IN1)");
    Serial.printf("   + IN1 (Motor Cối Xay & Vành Đèn LED) : GPIO %d (PWM %luHz)\n", PIN_L298N_IN1_MOTOR, PWM_FREQ);
    Serial.println("   + Lưu ý: Cả Motor và Vành Đèn được nối chung vào kênh IN1.");
    Serial.println("-------------------------------------------------------");

    ledcSetup(PWM_CH_MOTOR, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_L298N_IN1_MOTOR, PWM_CH_MOTOR);
    ledcWrite(PWM_CH_MOTOR, 0);

    initialized = true;
    Serial.println("✅ [L298N DECOR] Đã kích hoạt PWM! Gõ 'w' hoặc 'l' trên Serial để test.");
  }

  void setMotorSpeed(uint8_t speedPct) {
    if (!initialized) init();
    powerPct = constrain(speedPct, 0, 100);
    breathingActive = false;
    uint8_t duty = (uint8_t)((powerPct * 255) / 100);
    ledcWrite(PWM_CH_MOTOR, duty);
    Serial.printf("🎡 [L298N - GPIO %d] Công suất Motor & Vành Đèn: %u%% (Duty: %u/255)\n",
                  PIN_L298N_IN1_MOTOR, powerPct, duty);
  }

  uint8_t getMotorSpeed() {
    return powerPct;
  }

  void setLedBrightness(uint8_t brightPct, bool breathing) {
    if (!initialized) init();
    powerPct = constrain(brightPct, 0, 100);
    breathingActive = breathing;
    if (!breathing) {
      uint8_t duty = (uint8_t)((powerPct * 255) / 100);
      ledcWrite(PWM_CH_MOTOR, duty);
    }
    Serial.printf("💡 [L298N - GPIO %d] Vành Đèn & Motor: %u%% | Thở: %s\n",
                  PIN_L298N_IN1_MOTOR, powerPct, breathing ? "BẬT" : "TẮT");
  }

  uint8_t getLedBrightness() {
    return powerPct;
  }

  void setMode(uint8_t modeIdx) {
    currentModeIdx = modeIdx % 5;
    if (currentModeIdx == 0) {
      setMotorSpeed(0);
    } else if (currentModeIdx == 1) {
      setMotorSpeed(35);
    } else if (currentModeIdx == 2) {
      setMotorSpeed(60);
    } else if (currentModeIdx == 3) {
      setMotorSpeed(85);
    } else if (currentModeIdx == 4) {
      setLedBrightness(75, true);
    }
  }

  uint8_t getMode() {
    return currentModeIdx;
  }

  const char* getModeName() {
    return MODE_NAMES[currentModeIdx % 5];
  }

  bool isBreathing() {
    return breathingActive;
  }

  void loop() {
    if (!initialized || !breathingActive) return;
    // Hiệu ứng nhịp thở nhấp nháy êm dịu (Sinusoidal Breathing)
    float phase = (millis() % 3500) / 3500.0f * 2.0f * M_PI;
    float norm = (sinf(phase) + 1.0f) * 0.5f; // 0.0 -> 1.0
    // Dao động mượt từ mức duty 30 (~12%) đến 210 (~82%)
    uint8_t duty = (uint8_t)(30 + norm * 180);
    ledcWrite(PWM_CH_MOTOR, duty);
  }

  bool handleSerial(char cmd) {
    if (cmd == 'w' || cmd == 'W' || cmd == 'l' || cmd == 'L') {
      currentModeIdx = (currentModeIdx + 1) % 5;
      setMode(currentModeIdx);
      String sub = "L298N (GPIO 1): " + String(getModeName());
      TestDisplay::setEmojiState("happy", sub);
      Serial.printf("👉 [L298N] Chuyển chế độ: %s\n", getModeName());
      return true;
    }
    return false;
  }

}

