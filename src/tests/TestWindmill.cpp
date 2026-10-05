#include "tests/TestWindmill.h"
#include "tests/TestDisplay.h"
#include "Config.h"
#include <math.h>

namespace TestWindmill {

  static const uint8_t PWM_CH_MOTOR  = 2; // Kênh LEDC PWM cho Motor & Vành đèn LED cối xay (GPIO 1)
  static const uint8_t PWM_CH_EDISON = 4; // Kênh LEDC PWM cho 2 LED ống Edison sườn trạm (GPIO 2)
  static const uint32_t PWM_FREQ     = 5000;
  static const uint8_t PWM_RES       = 8; // 0..255

  static bool initialized = false;
  static uint8_t currentModeIdx = 0; // 0: Tắt, 1: 35%, 2: 60%, 3: 85%, 4: Thở
  static uint8_t powerPct = 0;
  static uint8_t edisonPct = 0;
  static bool flowingLedsActive = false;
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
    Serial.println("🎡 [DECOR CONTROLLER] KHỞI TẠO CỐI XAY & HỆ THỐNG ĐÈN TRẠM");
    Serial.printf("   + IN1 (Motor Cối Xay & Vành Đèn LED) : GPIO %d (PWM %luHz)\n", PIN_L298N_IN1_MOTOR, PWM_FREQ);
    Serial.printf("   + Ống LED Edison Sườn (Trở 47Ω)      : GPIO %d (PWM %luHz)\n", PIN_EDISON_TUBE_LED, PWM_FREQ);
    Serial.printf("   + LED Nước Chảy Mặt Lưng (Trái/Phải) : GPIO %d & GPIO %d (3V COB)\n", PIN_FLOWING_LED_L, PIN_FLOWING_LED_R);
    Serial.println("-------------------------------------------------------");

    // 1. Khởi tạo PWM cho Cối xay gió (GPIO 1)
    ledcSetup(PWM_CH_MOTOR, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_L298N_IN1_MOTOR, PWM_CH_MOTOR);
    ledcWrite(PWM_CH_MOTOR, 0);

    // 2. Khởi tạo PWM cho 2 LED ống Edison sườn trạm (GPIO 2)
    ledcSetup(PWM_CH_EDISON, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_EDISON_TUBE_LED, PWM_CH_EDISON);
    ledcWrite(PWM_CH_EDISON, 0);

    // 3. Khởi tạo GPIO cho 2 thanh LED nước chảy sao băng mặt lưng (GPIO 18 & GPIO 38)
    pinMode(PIN_FLOWING_LED_L, OUTPUT);
    pinMode(PIN_FLOWING_LED_R, OUTPUT);
    digitalWrite(PIN_FLOWING_LED_L, LOW);
    digitalWrite(PIN_FLOWING_LED_R, LOW);

    initialized = true;
    Serial.println("✅ [DECOR CONTROLLER] Đã kích hoạt toàn bộ ngoại vi Decor! Gõ 'w' hoặc 'l' trên Serial để test.");
  }

  void setMotorSpeed(uint8_t speedPct) {
    if (!initialized) init();
    powerPct = constrain(speedPct, 0, 100);
    breathingActive = false;
    uint8_t duty = (uint8_t)((powerPct * 255) / 100);
    ledcWrite(PWM_CH_MOTOR, duty);
    Serial.printf("🎡 [CỐI XAY - GPIO %d] Công suất Motor & Vành Đèn: %u%% (Duty: %u/255)\n",
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
    Serial.printf("💡 [CỐI XAY - GPIO %d] Vành Đèn & Motor: %u%% | Thở: %s\n",
                  PIN_L298N_IN1_MOTOR, powerPct, breathing ? "BẬT" : "TẮT");
  }

  uint8_t getLedBrightness() {
    return powerPct;
  }

  void setEdisonBrightness(uint8_t brightPct) {
    if (!initialized) init();
    edisonPct = constrain(brightPct, 0, 100);
    uint8_t duty = (uint8_t)((edisonPct * 255) / 100);
    ledcWrite(PWM_CH_EDISON, duty);
    Serial.printf("💡 [EDISON - GPIO %d] Độ sáng 2 ống LED Edison: %u%% (Duty: %u/255)\n",
                  PIN_EDISON_TUBE_LED, edisonPct, duty);
  }

  uint8_t getEdisonBrightness() {
    return edisonPct;
  }

  void setFlowingLeds(bool enable) {
    if (!initialized) init();
    flowingLedsActive = enable;
    digitalWrite(PIN_FLOWING_LED_L, enable ? HIGH : LOW);
    digitalWrite(PIN_FLOWING_LED_R, enable ? HIGH : LOW);
    Serial.printf("🌊 [FLOWING LED - GPIO %d & %d] 2 Thanh LED Nước Chảy Lưng: %s\n",
                  PIN_FLOWING_LED_L, PIN_FLOWING_LED_R, enable ? "BẬT" : "TẮT");
  }

  bool getFlowingLeds() {
    return flowingLedsActive;
  }

  void setMode(uint8_t modeIdx) {
    currentModeIdx = modeIdx % 5;
    if (currentModeIdx == 0) {
      setMotorSpeed(0);
      setEdisonBrightness(0);
      setFlowingLeds(false);
    } else if (currentModeIdx == 1) {
      setMotorSpeed(35);
      setEdisonBrightness(40);
      setFlowingLeds(true);
    } else if (currentModeIdx == 2) {
      setMotorSpeed(60);
      setEdisonBrightness(70);
      setFlowingLeds(true);
    } else if (currentModeIdx == 3) {
      setMotorSpeed(85);
      setEdisonBrightness(100);
      setFlowingLeds(true);
    } else if (currentModeIdx == 4) {
      setLedBrightness(75, true);
      setFlowingLeds(true);
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
    // Dao động mượt từ mức duty 30 (~12%) đến 210 (~82%) cho Motor và LED Edison
    uint8_t dutyMotor = (uint8_t)(30 + norm * 180);
    uint8_t dutyEdison = (uint8_t)(20 + norm * 200);
    ledcWrite(PWM_CH_MOTOR, dutyMotor);
    ledcWrite(PWM_CH_EDISON, dutyEdison);
  }

  bool handleSerial(char cmd) {
    if (cmd == 'w' || cmd == 'W' || cmd == 'l' || cmd == 'L') {
      currentModeIdx = (currentModeIdx + 1) % 5;
      setMode(currentModeIdx);
      String sub = "Decor Mode: " + String(getModeName());
      TestDisplay::setEmojiState("happy", sub);
      Serial.printf("👉 [DECOR] Chuyển chế độ: %s\n", getModeName());
      return true;
    }
    return false;
  }

}

