#pragma once
#include <Arduino.h>
#include "tests/TestDisplay.h"
#include "tests/TestSensors.h"
#include "tests/TestButtons.h"
#include "tests/TestSpeakerMAX98357A.h"
#include "tests/TestAudio.h"
#include "tests/TestWindmill.h"
#include "tests/TestSDCard.h"

// ============================================================================
//              BẢNG BẬT/TẮT CÁC MODULE TEST PHẦN CỨNG RỜI
//  (Đang bật riêng TestSpeakerMAX98357A để kiểm tra độc lập Loa MAX98357A
//   trên Motherboard trước khi ghép chung với Mic INMP441)
// ============================================================================
#define ENABLE_SOLO_MAX98357A   0   // [CHỜ MODULE MỚI] Module Loa I2S MAX98357A
#define ENABLE_TEST_DISPLAY     1   // [ĐANG BẬT] Màn hình ST7789 (4 Chế độ: Chờ / Biểu cảm / PC Status / Kiểm tra DIO & Audio)
#define ENABLE_TEST_BUTTONS     1   // [ĐANG BẬT] Cụm 7 Phím Bấm DIO INPUT_PULLUP (16, 17, 18, 21, 38, 47, 45)
#define ENABLE_TEST_SENSORS     1   // [ĐANG BẬT] Cảm biến SHT31 (SDA=8, SCL=9) & Chạm TTP223 (SIG=7)
#define ENABLE_TEST_AUDIO       1   // [ĐANG BẬT] Thu âm Mic INMP441 (SCK=4, WS=5, SD=6) đã cân chỉnh chuẩn
#define ENABLE_TEST_WINDMILL    0   // [CHỜ NỐI]  L298N Mini (Motor IN1=GPIO 1, LED Edison IN3=GPIO 2)
#define ENABLE_TEST_SDCARD      0   // [CHỜ NỐI]  Thẻ nhớ Micro SD SPI riêng (SCK=39, MISO=40, MOSI=41, CS=42)

namespace ModuleTestRunner {

  inline void printAllHelp() {
    #if ENABLE_SOLO_MAX98357A
      TestSpeakerMAX98357A::printWiringAndDebugGuide();
    #else
      Serial.println("\n==================== BẢNG LỆNH TEST TOÀN BỘ MODULE ====================");
      Serial.println("  [1 / 2 / 3 / 4] : Đổi chế độ Màn hình ST7789 (Chờ / Emoji / PC HUD / Diag)");
      Serial.println("  [s]             : Đổi kiểu hiển thị PC Status HUD (Gauges/Bars/Graph/Matrix)");
      Serial.println("  [p]             : PHÁT LẠI giọng nói vừa thu qua Loa PWM GPIO 15 (Nghe lại Mic)");
      Serial.println("  [t]             : Phát tiếng bíp 1000Hz kiểm tra Loa PWM GPIO 15");
      Serial.println("  [a]             : Phát nhạc hiệu Đố-Mí-Son kiểm tra Loa (GPIO 15)");
      Serial.println("  [v]             : Lưu đoạn thu âm thành file rec_test.wav vào Thẻ nhớ SD");
      Serial.println("  [m]             : Mở Menu Symbian S40");
      Serial.println("  [w]             : Test quay Motor Cối Xay L298N (GPIO 1 PWM: 35% -> 60% -> 85% -> OFF)");
      Serial.println("  [l]             : Test sáng Đèn LED Edison 3V L298N (GPIO 2 PWM + Hiệu ứng thở)");
      Serial.println("  [k]             : Test nhận & đọc/ghi Thẻ nhớ Micro SD rời (GPIO 39,40,41,42)");
      Serial.println("  [h]             : Hiển thị lại bảng hướng dẫn này");
      Serial.println("=======================================================================");
    #endif
  }

  inline void init() {
    Serial.println("\n=======================================================");
    Serial.println("   🧪 KHỞI ĐỘNG HỆ THỐNG MODULE PHẦN CỨNG TRẠM DECOR");
    Serial.println("=======================================================");

    #if ENABLE_TEST_DISPLAY
      TestDisplay::init();
    #endif

    #if ENABLE_SOLO_MAX98357A
      TestSpeakerMAX98357A::init();
    #endif

    #if ENABLE_TEST_SENSORS
      TestSensors::init();
    #endif

    #if ENABLE_TEST_BUTTONS
      TestButtons::init();
    #endif

    #if ENABLE_TEST_AUDIO
      TestAudio::init();
    #endif

    #if ENABLE_TEST_WINDMILL
      TestWindmill::init();
    #endif

    #if ENABLE_TEST_SDCARD
      TestSDCard::init();
    #endif
  }

  inline void loop() {
    #if ENABLE_TEST_DISPLAY
      TestDisplay::loop();
    #endif

    #if ENABLE_SOLO_MAX98357A
      TestSpeakerMAX98357A::loop();
    #endif

    #if ENABLE_TEST_SENSORS
      TestSensors::loop();
    #endif

    #if ENABLE_TEST_BUTTONS
      TestButtons::loop();
    #endif

    #if ENABLE_TEST_AUDIO
      TestAudio::loop();
    #endif

    #if ENABLE_TEST_WINDMILL
      TestWindmill::loop();
    #endif
  }

  inline bool handleSerial(char cmd) {
    #if ENABLE_SOLO_MAX98357A
      if (TestSpeakerMAX98357A::handleSerial(cmd)) return true;
    #endif
    if (TestDisplay::handleSerial(cmd))  return true;
    if (TestSensors::handleSerial(cmd))  return true;
    if (TestButtons::handleSerial(cmd))  return true;
    #if ENABLE_TEST_AUDIO
      if (TestAudio::handleSerial(cmd))  return true;
    #endif
    if (TestWindmill::handleSerial(cmd)) return true;
    if (TestSDCard::handleSerial(cmd))   return true;
    return false;
  }

}
