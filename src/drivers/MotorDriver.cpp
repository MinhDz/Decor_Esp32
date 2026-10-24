#include "drivers/MotorDriver.h"
#include "tests/TestDisplay.h"
#include "tests/TestSensors.h"
#include "tests/TestAudio.h"
#include "CobLedController.h"
#include "Config.h"
#include "PcStatsManager.h"
#include <Preferences.h>
#include <math.h>
#include "driver/gpio.h"

namespace MotorDriver {

  static const uint8_t PWM_CH_MOTOR    = 2; // Kênh LEDC PWM cho Motor & Vành đèn LED cối xay (GPIO 1)
  static const uint8_t PWM_CH_EDISON_L = 4; // Kênh LEDC PWM cho cặp LED ống Edison Sườn Trái (GPIO 47)
  static const uint8_t PWM_CH_EDISON_R = 5; // Kênh LEDC PWM cho cặp LED ống Edison Sườn Phải (GPIO 21)
  static const uint32_t PWM_FREQ       = 5000;
  static const uint8_t PWM_RES         = 8; // 0..255

  static bool initialized = false;
  static uint8_t currentModeIdx = 0; // 0: Tắt, 1: 35%, 2: 60%, 3: 85%, 4: Thở
  static uint8_t powerPct = 0;
  static bool breathingActive = false;

  // 14 Chế độ cho từng cặp LED Ống Edison
  static uint8_t edisonModeL = 5; // Mặc định: 5 (Lò phản ứng & PC HUD Sync)
  static uint8_t edisonModeR = 5; // Mặc định: 5 (Lò phản ứng & PC HUD Sync)
  static uint8_t edisonPctL  = 70;
  static uint8_t edisonPctR  = 70;

  static bool flowingLedsActive = false;

  // Caching trạng thái để tối ưu không ghi Flash NVS liên tục khi kéo thanh trượt
  static uint8_t s_savedModeL = 255;
  static uint8_t s_savedModeR = 255;
  static uint8_t s_savedPctL  = 255;
  static uint8_t s_savedPctR  = 255;
  static uint8_t s_savedMotor = 255;

  // Chuỗi hiệu ứng khởi động lò phản ứng (Reactor Ignition Sequence)
  static unsigned long s_startupStartMs = 0;
  static bool s_isStartingUp = true;

  void triggerStartupEffect() {
    s_startupStartMs = millis();
    s_isStartingUp = true;
  }

  bool isStartupActive() {
    return s_isStartingUp;
  }

  static const char* MOTOR_MODE_NAMES[5] = {
    "Tắt (0%)",
    "Êm dịu (35%)",
    "Tiêu chuẩn (60%)",
    "Tối đa (85%)",
    "Nhịp thở (Breathing)"
  };

  static const char* EDISON_MODE_NAMES[14] = {
    "Tắt (0%)",
    "Dịu nhẹ (25%)",
    "Vừa phải (50%)",
    "Sáng rõ (75%)",
    "Tối đa (100%)",
    "Lò phản ứng (Reactor & PC Sync)",
    "Ngọn nến lung linh (Candle Flicker)",
    "Nhịp đập Cyberpunk (Cyber Heartbeat)",
    "Sấm sét hồ quang (Electric Lightning)",
    "Thở đối xứng lệch pha (Alternating Breath)",
    "Dòng năng lượng sạc (Power Flow Pulse)",
    "Nhịp nhạc VU Meter (Stereo Spectrum)",
    "Giọng nói AI (XiaoZhi Voice Reactive)",
    "Tự động thông minh (Smart Fusion)"
  };

  void saveEdisonPrefs() {
    if (edisonModeL == s_savedModeL && edisonModeR == s_savedModeR &&
        edisonPctL == s_savedPctL && edisonPctR == s_savedPctR &&
        currentModeIdx == s_savedMotor) {
      return;
    }
    Preferences p;
    if (p.begin("edison_cfg", false)) {
      p.putUChar("mode_l", edisonModeL);
      p.putUChar("mode_r", edisonModeR);
      p.putUChar("pct_l", edisonPctL);
      p.putUChar("pct_r", edisonPctR);
      p.putUChar("motor_m", currentModeIdx);
      p.end();
      s_savedModeL = edisonModeL;
      s_savedModeR = edisonModeR;
      s_savedPctL  = edisonPctL;
      s_savedPctR  = edisonPctR;
      s_savedMotor = currentModeIdx;
      Serial.printf("💾 [EDISON NVS] Đã lưu cấu hình: Trái=%d(%s), Phải=%d(%s), Motor=%d\n",
                    edisonModeL, EDISON_MODE_NAMES[edisonModeL % 14],
                    edisonModeR, EDISON_MODE_NAMES[edisonModeR % 14], currentModeIdx);
    }
  }

  void loadEdisonPrefs() {
    Preferences p;
    if (p.begin("edison_cfg", true)) {
      edisonModeL    = p.getUChar("mode_l", 5) % 14;
      edisonModeR    = p.getUChar("mode_r", 5) % 14;
      edisonPctL     = constrain((int)p.getUChar("pct_l", 70), 0, 100);
      edisonPctR     = constrain((int)p.getUChar("pct_r", 70), 0, 100);
      currentModeIdx = p.getUChar("motor_m", 0) % 5;
      p.end();
      s_savedModeL = edisonModeL;
      s_savedModeR = edisonModeR;
      s_savedPctL  = edisonPctL;
      s_savedPctR  = edisonPctR;
      s_savedMotor = currentModeIdx;
      Serial.printf("📖 [EDISON NVS] Đã nạp cấu hình: Trái=%d(%s, %d%%), Phải=%d(%s, %d%%), Motor=%d\n",
                    edisonModeL, EDISON_MODE_NAMES[edisonModeL % 14], edisonPctL,
                    edisonModeR, EDISON_MODE_NAMES[edisonModeR % 14], edisonPctR, currentModeIdx);
    }
  }

  void init() {
    if (initialized) return;
    Serial.println("\n-------------------------------------------------------");
    Serial.println("🎡 [DECOR CONTROLLER] KHỞI TẠO CỐI XAY & HỆ THỐNG ĐÈN TRẠM");
    Serial.printf("   + IN1 Motor Cối Xay & Vành Đèn     : GPIO %d (PWM CH%d %luHz)\n", PIN_L298N_IN1_MOTOR, PWM_CH_MOTOR, PWM_FREQ);
    Serial.printf("   + Cặp LED Ống Edison Sườn Trái    : GPIO %d (PWM CH%d %luHz)\n", PIN_EDISON_TUBE_L, PWM_CH_EDISON_L, PWM_FREQ);
    Serial.printf("   + Cặp LED Ống Edison Sườn Phải    : GPIO %d (PWM CH%d %luHz)\n", PIN_EDISON_TUBE_R, PWM_CH_EDISON_R, PWM_FREQ);
    Serial.printf("   + LED Nước Chảy Lưng (Trái/Phải)  : GPIO %d & GPIO %d (3V COB)\n", PIN_FLOWING_LED_L, PIN_FLOWING_LED_R);
    Serial.println("-------------------------------------------------------");

    // 1. Khởi tạo PWM cho Cối xay gió (GPIO 1)
    pinMode(PIN_L298N_IN1_MOTOR, OUTPUT);
    digitalWrite(PIN_L298N_IN1_MOTOR, LOW);
    ledcSetup(PWM_CH_MOTOR, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_L298N_IN1_MOTOR, PWM_CH_MOTOR);
    ledcWrite(PWM_CH_MOTOR, 0);

    // 2. Khởi tạo PWM cho cặp LED ống Edison Sườn Trái (GPIO 47)
    pinMode(PIN_EDISON_TUBE_L, OUTPUT);
    digitalWrite(PIN_EDISON_TUBE_L, LOW);
    ledcSetup(PWM_CH_EDISON_L, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_EDISON_TUBE_L, PWM_CH_EDISON_L);
    ledcWrite(PWM_CH_EDISON_L, 0);

    // 3. Khởi tạo PWM cho cặp LED ống Edison Sườn Phải (GPIO 21)
    pinMode(PIN_EDISON_TUBE_R, OUTPUT);
    digitalWrite(PIN_EDISON_TUBE_R, LOW);
    ledcSetup(PWM_CH_EDISON_R, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_EDISON_TUBE_R, PWM_CH_EDISON_R);
    ledcWrite(PWM_CH_EDISON_R, 0);

    // 4. Khởi tạo 2 thanh LED nước chảy sao băng qua CobLeds (GPIO 38 & 18 - Digital HIGH)
    CobLeds::init(true);

    // Nạp trạng thái đã lưu từ Flash NVS để không bao giờ bị mất khi tắt nguồn
    loadEdisonPrefs();

    triggerStartupEffect();
    initialized = true;
    Serial.println("✅ [DECOR CONTROLLER] Đã kích hoạt toàn bộ ngoại vi Decor thành công!");
  }

  // ============================================================================
  // 1. ĐIỀU KHIỂN MOTOR CỐI XAY GIÓ (GPIO 1)
  // ============================================================================
  void setMotorSpeed(uint8_t speedPct) {
    if (!initialized) init();
    uint8_t oldPct = powerPct;
    powerPct = constrain(speedPct, 0, 100);
    breathingActive = false;
    uint8_t targetDuty = (uint8_t)((powerPct * 255) / 100);
    uint8_t currentDuty = (uint8_t)((oldPct * 255) / 100);

    // Khởi động mềm (Soft Start): Tăng mượt từ 0 lên targetDuty trong 200ms để chống sụt áp nguồn MD-CH02
    if (targetDuty > currentDuty && currentDuty == 0) {
      for (int step = 1; step <= 8; step++) {
        uint8_t stepDuty = (targetDuty * step) / 8;
        ledcWrite(PWM_CH_MOTOR, stepDuty);
        delay(25); // 8 bước x 25ms = 200ms
      }
    } else {
      ledcWrite(PWM_CH_MOTOR, targetDuty);
    }

    saveEdisonPrefs();
    Serial.printf("🎡 [CỐI XAY - GPIO %d] Công suất Motor & Vành Đèn: %u%% (Duty: %u/255)\n",
                  PIN_L298N_IN1_MOTOR, powerPct, targetDuty);
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

  void setMode(uint8_t modeIdx) {
    currentModeIdx = modeIdx % 5;
    if (currentModeIdx == 0) {
      setMotorSpeed(0);
      breathingActive = false;
    } else if (currentModeIdx == 1) {
      setMotorSpeed(35);
    } else if (currentModeIdx == 2) {
      setMotorSpeed(60);
    } else if (currentModeIdx == 3) {
      setMotorSpeed(85);
    } else if (currentModeIdx == 4) {
      setLedBrightness(75, true);
    }
    saveEdisonPrefs();
  }

  uint8_t getMode() {
    return currentModeIdx;
  }

  const char* getModeName() {
    return MOTOR_MODE_NAMES[currentModeIdx % 5];
  }

  bool isBreathing() {
    return breathingActive;
  }

  // ============================================================================
  // 2. ĐIỀU KHIỂN CẶP LED ỐNG EDISON SƯỜN TRÁI (GPIO 47 - CH4)
  // ============================================================================
  void setEdisonLeftMode(uint8_t modeIdx) {
    if (!initialized) init();
    edisonModeL = modeIdx % 14;
    if (edisonModeL == 0) {
      edisonPctL = 0;
      ledcWrite(PWM_CH_EDISON_L, 0);
    } else if (edisonModeL == 1) {
      edisonPctL = 25;
      ledcWrite(PWM_CH_EDISON_L, 64);
    } else if (edisonModeL == 2) {
      edisonPctL = 50;
      ledcWrite(PWM_CH_EDISON_L, 128);
    } else if (edisonModeL == 3) {
      edisonPctL = 75;
      ledcWrite(PWM_CH_EDISON_L, 191);
    } else if (edisonModeL == 4) {
      edisonPctL = 100;
      ledcWrite(PWM_CH_EDISON_L, 255);
    } else {
      edisonPctL = 70;
      // Hiệu ứng động được băm xung mượt trong loop()
    }
    saveEdisonPrefs();
    Serial.printf("💡 [EDISON TRÁI - GPIO %d] Chế độ: %s\n", PIN_EDISON_TUBE_L, getEdisonLeftModeName());
  }

  uint8_t getEdisonLeftMode() {
    return edisonModeL;
  }

  const char* getEdisonLeftModeName() {
    return EDISON_MODE_NAMES[edisonModeL % 14];
  }

  void setEdisonLeftBrightness(uint8_t brightPct, bool breathing) {
    if (!initialized) init();
    brightPct = constrain(brightPct, 0, 100);
    if (breathing) {
      setEdisonLeftMode(5);
    } else if (brightPct == 0) {
      setEdisonLeftMode(0);
    } else if (brightPct <= 35) {
      setEdisonLeftMode(1);
    } else if (brightPct <= 65) {
      setEdisonLeftMode(2);
    } else if (brightPct <= 85) {
      setEdisonLeftMode(3);
    } else {
      setEdisonLeftMode(4);
    }
  }

  uint8_t getEdisonLeftBrightness() {
    return edisonPctL;
  }

  // ============================================================================
  // 3. ĐIỀU KHIỂN CẶP LED ỐNG EDISON SƯỜN PHẢI (GPIO 21 - CH5)
  // ============================================================================
  void setEdisonRightMode(uint8_t modeIdx) {
    if (!initialized) init();
    edisonModeR = modeIdx % 14;
    if (edisonModeR == 0) {
      edisonPctR = 0;
      ledcWrite(PWM_CH_EDISON_R, 0);
    } else if (edisonModeR == 1) {
      edisonPctR = 25;
      ledcWrite(PWM_CH_EDISON_R, 64);
    } else if (edisonModeR == 2) {
      edisonPctR = 50;
      ledcWrite(PWM_CH_EDISON_R, 128);
    } else if (edisonModeR == 3) {
      edisonPctR = 75;
      ledcWrite(PWM_CH_EDISON_R, 191);
    } else if (edisonModeR == 4) {
      edisonPctR = 100;
      ledcWrite(PWM_CH_EDISON_R, 255);
    } else {
      edisonPctR = 70;
      // Hiệu ứng động được băm xung mượt trong loop()
    }
    saveEdisonPrefs();
    Serial.printf("💡 [EDISON PHẢI - GPIO %d] Chế độ: %s\n", PIN_EDISON_TUBE_R, getEdisonRightModeName());
  }

  uint8_t getEdisonRightMode() {
    return edisonModeR;
  }

  const char* getEdisonRightModeName() {
    return EDISON_MODE_NAMES[edisonModeR % 14];
  }

  void setEdisonRightBrightness(uint8_t brightPct, bool breathing) {
    if (!initialized) init();
    brightPct = constrain(brightPct, 0, 100);
    if (breathing) {
      setEdisonRightMode(5);
    } else if (brightPct == 0) {
      setEdisonRightMode(0);
    } else if (brightPct <= 35) {
      setEdisonRightMode(1);
    } else if (brightPct <= 65) {
      setEdisonRightMode(2);
    } else if (brightPct <= 85) {
      setEdisonRightMode(3);
    } else {
      setEdisonRightMode(4);
    }
  }

  uint8_t getEdisonRightBrightness() {
    return edisonPctR;
  }

  // ============================================================================
  // 4. ĐIỀU KHIỂN CHUNG CẢ 2 CẶP LED EDISON & PRESET
  // ============================================================================
  void setEdisonBothMode(uint8_t modeIdx) {
    setEdisonLeftMode(modeIdx);
    setEdisonRightMode(modeIdx);
  }

  void setEdisonBrightness(uint8_t brightPct) {
    setEdisonLeftBrightness(brightPct, false);
    setEdisonRightBrightness(brightPct, false);
  }

  uint8_t getEdisonBrightness() {
    return (uint8_t)((edisonPctL + edisonPctR) / 2);
  }

  uint8_t getEdisonModeCount() {
    return 14;
  }

  const char* getEdisonModeNameByIdx(uint8_t modeIdx) {
    return EDISON_MODE_NAMES[modeIdx % 14];
  }

  void applyEffectPreset(const String& preset) {
    String p = preset;
    p.toLowerCase();
    p.trim();
    if (p == "reactor" || p == "pc") {
      setEdisonBothMode(5);
    } else if (p == "candle" || p == "flame") {
      setEdisonBothMode(6);
    } else if (p == "heartbeat" || p == "pulse") {
      setEdisonBothMode(7);
    } else if (p == "lightning" || p == "storm") {
      setEdisonBothMode(8);
    } else if (p == "breath" || p == "dual_breath") {
      setEdisonBothMode(9);
    } else if (p == "charging" || p == "power") {
      setEdisonBothMode(10);
    } else if (p == "vumeter" || p == "vu" || p == "music") {
      setEdisonBothMode(11);
    } else if (p == "voice" || p == "ai" || p == "speech" || p == "xiaozhi") {
      setEdisonBothMode(12);
    } else if (p == "smart" || p == "auto" || p == "fusion") {
      setEdisonBothMode(13);
    } else if (p == "max" || p == "100") {
      setEdisonBothMode(4);
    } else if (p == "off" || p == "0") {
      setEdisonBothMode(0);
    } else if (p == "soft" || p == "25") {
      setEdisonBothMode(1);
    } else if (p == "medium" || p == "50") {
      setEdisonBothMode(2);
    } else if (p == "bright" || p == "75") {
      setEdisonBothMode(3);
    }
  }

  // ============================================================================
  // 5. ĐIỀU KHIỂN 2 THANH LED NƯỚC CHẢY MẶT LƯNG (GPIO 38 & GPIO 18)
  // ============================================================================
  void setFlowingLeds(bool enable) {
    if (!initialized) init();
    flowingLedsActive = enable;
    if (enable) CobLeds::turnOn();
    else CobLeds::turnOff();
    Serial.printf("🌊 [FLOWING LED - GPIO %d & %d] 2 Thanh LED Nước Chảy COB: %s (Mode: %s)\n",
                  PIN_FLOWING_LED_L, PIN_FLOWING_LED_R, enable ? "BẬT" : "TẮT", CobLeds::getModeName());
  }

  bool getFlowingLeds() {
    return CobLeds::isOn();
  }

  void setFlowingLeft(bool enable) {
    if (enable) cobLedLeft.turnOn();
    else cobLedLeft.turnOff();
  }

  bool getFlowingLeft() {
    return cobLedLeft.isOn();
  }

  void setFlowingRight(bool enable) {
    if (enable) cobLedRight.turnOn();
    else cobLedRight.turnOff();
  }

  bool getFlowingRight() {
    return cobLedRight.isOn();
  }

  // ============================================================================
  // HÀM TÍNH TOÁN CÔNG SUẤT BĂM XUNG (DUTY CYCLE 0..255) CHO 11 HIỆU ỨNG EDISON
  // ============================================================================
  static uint8_t computeDynamicEdisonDuty(uint8_t mode, bool isLeft, unsigned long now) {
    if (mode == 0) return 0;
    if (mode == 1) return 64;
    if (mode == 2) return 128;
    if (mode == 3) return 191;
    if (mode == 4) return 255;

    // Mode 5: Lò phản ứng & Đồng bộ PC (Reactor & PC HUD Sync)
    if (mode == 5) {
      bool pcLive = PcStatsManager::isLive();
      bool isPcHud = (TestDisplay::getScreenModeName() == "pchud");

      if (pcLive || isPcHud) {
        const PcMetrics& m = PcStatsManager::getMetrics();
        if (isLeft) {
          float sysLoad = constrain((m.cpu.usage + m.ram.usage) * 0.5f, 0.0f, 100.0f);
          float periodL = 3200.0f - (sysLoad / 100.0f) * 2350.0f;
          float phaseL = (float)(now % (unsigned long)periodL) / periodL * 2.0f * M_PI;
          float pulseL = (sinf(phaseL) + 1.0f) * 0.5f;
          float minDutyL = 25.0f + (sysLoad / 100.0f) * 125.0f;
          float maxDutyL = 70.0f + (sysLoad / 100.0f) * 185.0f;
          return (uint8_t)constrain(minDutyL + pulseL * (maxDutyL - minDutyL), 0.0f, 255.0f);
        } else {
          float netSpeedKb = m.net.dlSpeedKb + m.net.ulSpeedKb;
          float netScore = constrain(netSpeedKb / 5000.0f, 0.0f, 1.0f) * 100.0f;
          float diskSpeed = m.disk.readSpeed + m.disk.writeSpeed;
          float diskScore = constrain(diskSpeed / 20.0f, 0.0f, 1.0f) * 100.0f;
          float ioLoad = constrain(max(netScore, diskScore), 0.0f, 100.0f);
          float periodR = 3000.0f - (ioLoad / 100.0f) * 2200.0f;
          float phaseR = (float)((now + 200) % (unsigned long)periodR) / periodR * 2.0f * M_PI;
          float pulseR = (sinf(phaseR) + 1.0f) * 0.5f;
          float ripple = 0.0f;
          if (ioLoad > 10.0f) {
            float ripPhase = (float)(now % 120) / 120.0f * 2.0f * M_PI;
            ripple = sinf(ripPhase) * (ioLoad / 100.0f) * 28.0f;
          }
          float minDutyR = 25.0f + (ioLoad / 100.0f) * 125.0f;
          float maxDutyR = 70.0f + (ioLoad / 100.0f) * 185.0f;
          return (uint8_t)constrain(minDutyR + pulseR * (maxDutyR - minDutyR) + ripple, 0.0f, 255.0f);
        }
      } else {
        // Plasma wave mặc định
        unsigned long offset = isLeft ? 0 : 450;
        float phase = ((now + offset) % 3200) / 3200.0f * 2.0f * M_PI;
        float wave = (sinf(phase) + 0.15f * sinf(3.0f * phase) + 1.15f) / 2.3f;
        return (uint8_t)(35 + wave * 185);
      }
    }

    // Mode 6: Ngọn nến lung linh (Candle / Flame Flicker)
    if (mode == 6) {
      unsigned long t = now + (isLeft ? 0 : 777);
      float f1 = sinf(t * 0.007f);
      float f2 = sinf(t * 0.019f + 1.2f);
      float f3 = sinf(t * 0.037f + 2.5f);
      float flicker = (f1 * 0.5f + f2 * 0.3f + f3 * 0.2f);
      float gust = (sinf(t * 0.0013f) > 0.82f) ? -35.0f : 0.0f;
      float base = 150.0f + flicker * 55.0f + gust;
      return (uint8_t)constrain(base, 30.0f, 255.0f);
    }

    // Mode 7: Nhịp đập Cyberpunk (Cyber Heartbeat - nhịp kép lub-dub)
    if (mode == 7) {
      unsigned long cycle = (now + (isLeft ? 0 : 180)) % 1100;
      if (cycle < 160) {
        float p = (float)cycle / 160.0f;
        return (uint8_t)(35 + sinf(p * M_PI) * 205);
      } else if (cycle < 250) {
        return 35;
      } else if (cycle < 390) {
        float p = (float)(cycle - 250) / 140.0f;
        return (uint8_t)(35 + sinf(p * M_PI) * 155);
      } else {
        return 30;
      }
    }

    // Mode 8: Sấm sét hồ quang (Electric Lightning)
    if (mode == 8) {
      unsigned long cycle = (now + (isLeft ? 0 : 1200)) % 3200;
      if (cycle < 350) {
        uint16_t sub = cycle % 90;
        if (sub < 25) return 255;
        if (sub < 45) return 40;
        if (sub < 70) return 220;
        return 50;
      } else {
        return 22;
      }
    }

    // Mode 9: Thở đối xứng lệch pha (Alternating Dual Breath)
    if (mode == 9) {
      unsigned long offset = isLeft ? 0 : 1800; // Lệch pha 180 độ (chu kỳ 3600ms)
      float phase = ((now + offset) % 3600) / 3600.0f * 2.0f * M_PI;
      float wave = (sinf(phase) + 1.0f) * 0.5f;
      return (uint8_t)(25 + wave * 220);
    }

    // Mode 10: Dòng năng lượng sạc (Power Flow Pulse)
    if (mode == 10) {
      unsigned long period = TestSensors::isCharging() ? 900 : 1400;
      unsigned long offset = isLeft ? 0 : 250;
      unsigned long t = (now + offset) % period;
      float norm = (float)t / (float)period;
      float pulse = (norm < 0.70f) ? (norm / 0.70f) : (1.0f - ((norm - 0.70f) / 0.30f));
      return (uint8_t)(30 + pulse * 220);
    }

    // Mode 11: Nhịp nhạc VU Meter (Stereo Spectrum: Trái = Bass, Phải = Treble)
    if (mode == 11) {
      uint8_t bass = 0, treble = 0;
      TestAudio::getAudioSpectrumLevels(bass, treble);
      uint8_t level = isLeft ? bass : treble;

      float targetDuty = 0.0f;
      if (level <= 3) {
        // Khi không có tiếng nhạc hoặc đoạn nghỉ giữa bài: Đèn thở nhẹ dịu (warm filament idle glow)
        float idleWave = (sinf((float)now * 0.0022f + (isLeft ? 0.0f : 1.6f)) + 1.0f) * 0.5f;
        targetDuty = 20.0f + idleWave * 26.0f;
      } else {
        // Có nhạc: Giữ ánh sáng đáy 24 + công suất bùng nổ theo nhịp Bass/Treble
        targetDuty = 24.0f + (level / 100.0f) * 231.0f;
      }

      // Mô phỏng quán tính sợi đốt bóng đèn chân không cổ điển (Fast Attack / Smooth Thermal Decay)
      static float s_smoothDutyL = 25.0f;
      static float s_smoothDutyR = 25.0f;
      float& smooth = isLeft ? s_smoothDutyL : s_smoothDutyR;
      if (targetDuty > smooth) {
        smooth = smooth * 0.22f + targetDuty * 0.78f; // Nhịp bốc nhanh tức thì (<15ms)
      } else {
        smooth = smooth * 0.86f + targetDuty * 0.14f; // Sợi đốt tản nhiệt êm (~120ms)
      }
      return (uint8_t)constrain(smooth, 0.0f, 255.0f);
    }

    // Mode 12: Giọng nói AI (XiaoZhi Voice Reactive: Đồng bộ nhịp phát âm TTS)
    if (mode == 12) {
      if (TestAudio::isVoiceStreamActive()) {
        uint8_t energy = TestAudio::getVoiceStreamEnergy();
        float baseVoice = (float)energy / 100.0f;
        // Hiệu ứng vi sóng rung âm tiết tiếng nói (Syllable ripple Flutter ~10Hz)
        float flutter = sinf((float)now * 0.048f + (isLeft ? 0.0f : 0.85f)) * 0.18f;
        float combined = constrain(baseVoice + flutter, 0.0f, 1.0f);
        float targetDuty = 28.0f + combined * 227.0f;

        static float s_voiceSmoothL = 28.0f;
        static float s_voiceSmoothR = 28.0f;
        float& vSmooth = isLeft ? s_voiceSmoothL : s_voiceSmoothR;
        if (targetDuty > vSmooth) {
          vSmooth = vSmooth * 0.30f + targetDuty * 0.70f;
        } else {
          vSmooth = vSmooth * 0.82f + targetDuty * 0.18f;
        }
        return (uint8_t)constrain(vSmooth, 0.0f, 255.0f);
      } else {
        // Trạng thái AI đang lắng nghe / chờ: Nhịp thở ấm êm dịu
        float pulse = (sinf((float)now * 0.0028f + (isLeft ? 0.0f : 0.75f)) + 1.0f) * 0.5f;
        return (uint8_t)(25.0f + pulse * 45.0f);
      }
    }

    // Mode 13: Tự động thông minh (Smart Fusion: Tự đổi theo AI / Nhạc / Chờ)
    if (mode == 13) {
      if (TestAudio::isVoiceStreamActive()) {
        return computeDynamicEdisonDuty(12, isLeft, now);
      } else if (TestAudio::isMusicPlaying()) {
        return computeDynamicEdisonDuty(11, isLeft, now);
      } else {
        return computeDynamicEdisonDuty(5, isLeft, now); // Lò phản ứng & Trạm không gian
      }
    }

    return 70;
  }

  // ============================================================================
  // 6. VÒNG LẶP HIỆU ỨNG LÒ PHẢN ỨNG & ĐỒNG BỘ PC STATUS (REACTOR ENERGY TUBES)
  // ============================================================================
  void loop() {
    if (!initialized) return;

    unsigned long now = millis();

    // 0. HIỆU ỨNG KHỞI ĐỘNG (STARTUP IGNITION SEQUENCE) TRONG 2.5 GIÂY ĐẦU
    if (s_isStartingUp) {
      unsigned long elapsed = now - s_startupStartMs;
      if (elapsed < 2500) {
        uint8_t duty = 0;
        if (elapsed < 1600) {
          float t = (float)elapsed / 1600.0f;
          duty = (uint8_t)(t * t * 210.0f);
        } else if (elapsed < 2000) {
          float t = (float)(elapsed - 1600) / 400.0f;
          duty = (uint8_t)(210.0f + sinf(t * M_PI) * 45.0f);
        } else {
          float t = (float)(elapsed - 2000) / 500.0f;
          duty = (uint8_t)(180.0f - t * 105.0f);
        }
        ledcWrite(PWM_CH_EDISON_L, duty);
        ledcWrite(PWM_CH_EDISON_R, duty);
        return;
      } else {
        s_isStartingUp = false;
        Serial.println("⚡ [EDISON TUBES] Khởi động hoàn tất! Kích hoạt hiệu ứng Đèn Edison.");
      }
    }

    // 1. Nhịp thở cho Motor cối xay gió
    if (breathingActive) {
      float phase = (now % 3500) / 3500.0f * 2.0f * M_PI;
      float norm = (sinf(phase) + 1.0f) * 0.5f;
      uint8_t dutyMotor = (uint8_t)(30 + norm * 180);
      ledcWrite(PWM_CH_MOTOR, dutyMotor);
    }

    // 2. HIỆU ỨNG ÁNH SÁNG ĐỘNG CHO 2 CỤM LED EDISON (KHI MODE >= 5)
    bool touchActive = TestSensors::isTouchPressed();
    if (edisonModeL >= 5) {
      uint8_t dutyL = computeDynamicEdisonDuty(edisonModeL, true, now);
      if (touchActive) {
        dutyL = (uint8_t)min(255, (int)dutyL + 45); // Phản hồi xúc giác tức thời khi chạm vỏ đồng!
      }
      ledcWrite(PWM_CH_EDISON_L, dutyL);
    }

    if (edisonModeR >= 5) {
      uint8_t dutyR = computeDynamicEdisonDuty(edisonModeR, false, now);
      if (touchActive) {
        dutyR = (uint8_t)min(255, (int)dutyR + 45); // Phản hồi xúc giác tức thời khi chạm vỏ đồng!
      }
      ledcWrite(PWM_CH_EDISON_R, dutyR);
    }
  }

  // ============================================================================
  // 7. PHÍM TẮT SERIAL MONITOR
  // ============================================================================
  bool handleSerial(char cmd) {
    if (cmd == 'w' || cmd == 'W') {
      currentModeIdx = (currentModeIdx + 1) % 5;
      setMode(currentModeIdx);
      Serial.printf("👉 [DECOR] Cối xay: %s\n", getModeName());
      return true;
    } else if (cmd == 'e' || cmd == 'E') {
      edisonModeL = (edisonModeL + 1) % 14;
      setEdisonLeftMode(edisonModeL);
      Serial.printf("👉 [DECOR] Edison Trái (GPIO %d): %s\n", PIN_EDISON_TUBE_L, getEdisonLeftModeName());
      return true;
    } else if (cmd == 'r' || cmd == 'R') {
      edisonModeR = (edisonModeR + 1) % 14;
      setEdisonRightMode(edisonModeR);
      Serial.printf("👉 [DECOR] Edison Phải (GPIO %d): %s\n", PIN_EDISON_TUBE_R, getEdisonRightModeName());
      return true;
    } else if (cmd == 'f' || cmd == 'F') {
      CobLeds::nextMode();
      Serial.printf("👉 [DECOR] LED Nước chảy COB: %s\n", CobLeds::getModeName());
      return true;
    }
    return false;
  }

} // namespace TestWindmill


