#pragma once
#include <Arduino.h>

// Các bộ HAL Drivers và Space OS Core chuẩn
#include "os/SpaceOS_WindowManager.h"
#include "drivers/DisplayDriver.h"
#include "drivers/SensorDriver.h"
#include "drivers/KeypadDriver.h"
#include "drivers/AudioDriver.h"
#include "drivers/MotorDriver.h"
#include "drivers/StorageDriver.h"
#include "drivers/CobLedDriver.h"
#include "tests/TestSpeakerMAX98357A.h"

// ============================================================================
//              BẢNG CẤU HÌNH BẬT/TẮT CÁC PHÂN HỆ HỆ THỐNG
// ============================================================================
#define ENABLE_SOLO_MAX98357A   0   // Module Loa I2S MAX98357A độc lập (debug)
#define ENABLE_SYSTEM_DISPLAY   1   // Space OS UI & 4 Màn hình ST7789
#define ENABLE_SYSTEM_KEYPAD    1   // Cụm 7 Phím Bấm Keypad ADC / DIO
#define ENABLE_SYSTEM_SENSORS   1   // Cảm biến SHT31, Chạm TTP223, Pin ADC, Sạc MH-CD42
#define ENABLE_SYSTEM_AUDIO     1   // Thu âm Mic INMP441 & Audio Pipeline
#define ENABLE_SYSTEM_MOTOR     1   // Motor Cối Xay & Đèn Edison L298N
#define ENABLE_SYSTEM_STORAGE   0   // Thẻ nhớ Micro SD SPI (StorageDriver tự động khởi tạo khi cắm thẻ)

// Giữ lại các define cũ để tương thích hoàn toàn
#define ENABLE_TEST_DISPLAY     ENABLE_SYSTEM_DISPLAY
#define ENABLE_TEST_BUTTONS     ENABLE_SYSTEM_KEYPAD
#define ENABLE_TEST_SENSORS     ENABLE_SYSTEM_SENSORS
#define ENABLE_TEST_AUDIO       ENABLE_SYSTEM_AUDIO
#define ENABLE_TEST_WINDMILL    ENABLE_SYSTEM_MOTOR
#define ENABLE_TEST_SDCARD      ENABLE_SYSTEM_STORAGE

namespace SystemCoordinator {

  inline void printAllHelp() {
    #if ENABLE_SOLO_MAX98357A
      TestSpeakerMAX98357A::printWiringAndDebugGuide();
    #else
      Serial.println("\n==================== BẢNG LỆNH ĐIỀU PHỐI HỆ THỐNG ====================");
      Serial.println("  [1 / 2 / 3 / 4] : Đổi chế độ Màn hình ST7789 (Chờ / Emoji / PC HUD / Diag)");
      Serial.println("  [s]             : Đổi kiểu hiển thị PC Status HUD (Gauges/Bars/Graph/Matrix)");
      Serial.println("  [p]             : PHÁT LẠI giọng nói vừa thu qua Loa PWM GPIO 15 (Nghe lại Mic)");
      Serial.println("  [t]             : Phát tiếng bíp 1000Hz kiểm tra Loa PWM GPIO 15");
      Serial.println("  [a]             : Phát nhạc hiệu Đố-Mí-Son kiểm tra Loa (GPIO 15)");
      Serial.println("  [v]             : Lưu đoạn thu âm thành file rec_test.wav vào Thẻ nhớ SD");
      Serial.println("  [m]             : Mở Menu Space OS S40 Desktop");
      Serial.println("  [w]             : Test quay Motor Cối Xay L298N (GPIO 1 PWM: 35% -> 60% -> 85% -> OFF)");
      Serial.println("  [l]             : Test sáng Đèn LED Edison 3V L298N (GPIO 2 PWM + Hiệu ứng thở)");
      Serial.println("  [k]             : Thăm dò điện áp & trạng thái Thẻ nhớ Micro SD (GPIO 39,40,41,42)");
      Serial.println("  [h]             : Hiển thị lại bảng hướng dẫn này");
      Serial.println("=======================================================================");
    #endif
  }

  inline void init() {
    Serial.println("\n=======================================================");
    Serial.println("   🚀 KHỞI ĐỘNG HỆ THỐNG PHẦN CỨNG & SPACE OS (COORDINATOR)");
    Serial.println("=======================================================");

    #if ENABLE_SYSTEM_DISPLAY
      TestDisplay::init();
    #endif

    #if ENABLE_SOLO_MAX98357A
      TestSpeakerMAX98357A::init();
    #endif

    #if ENABLE_SYSTEM_SENSORS
      SensorDriver::init();
    #endif

    #if ENABLE_SYSTEM_KEYPAD
      KeypadDriver::init();
    #endif

    #if ENABLE_SYSTEM_AUDIO
      AudioDriver::init();
    #endif

    #if ENABLE_SYSTEM_MOTOR
      MotorDriver::init();
    #endif

    #if ENABLE_SYSTEM_STORAGE
      StorageDriver::init();
    #endif
  }

  inline void loop() {
    #if ENABLE_SYSTEM_DISPLAY
      TestDisplay::loop();
    #endif

    #if ENABLE_SOLO_MAX98357A
      TestSpeakerMAX98357A::loop();
    #endif

    #if ENABLE_SYSTEM_SENSORS
      SensorDriver::loop();
    #endif

    #if ENABLE_SYSTEM_KEYPAD
      KeypadDriver::loop();
    #endif

    #if ENABLE_SYSTEM_AUDIO
      AudioDriver::loop();
    #endif

    #if ENABLE_SYSTEM_MOTOR
      MotorDriver::loop();
    #endif
  }

  inline bool handleSerial(char cmd) {
    #if ENABLE_SOLO_MAX98357A
      if (TestSpeakerMAX98357A::handleSerial(cmd)) return true;
    #endif
    if (TestDisplay::handleSerial(cmd))   return true;
    if (SensorDriver::handleSerial(cmd))  return true;
    if (KeypadDriver::handleSerial(cmd))  return true;
    #if ENABLE_SYSTEM_AUDIO
      if (AudioDriver::handleSerial(cmd)) return true;
    #endif
    if (MotorDriver::handleSerial(cmd))   return true;
    if (StorageDriver::handleSerial(cmd)) return true;
    return false;
  }

} // namespace SystemCoordinator

// Bí danh tương thích ngược cho các module cũ
namespace ModuleTestRunner = SystemCoordinator;

