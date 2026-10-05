#pragma once
#include <Arduino.h>

namespace TestWindmill {
  // Khởi tạo Module L298N (GPIO 1 -> IN1, nối chung cả Motor Cối Xay & Vành Đèn LED)
  void init();

  // Vòng lặp hiệu ứng đèn thở & dao động công suất (Breathing)
  void loop();

  // Điều chỉnh tốc độ / công suất Motor & Đèn (0..100%)
  void setMotorSpeed(uint8_t speedPct);
  uint8_t getMotorSpeed();

  void setLedBrightness(uint8_t brightPct, bool breathing = false);
  uint8_t getLedBrightness();

  // Điều khiển 2 LED ống Edison 30mm sườn trạm (GPIO 2 - PWM)
  void setEdisonBrightness(uint8_t brightPct);
  uint8_t getEdisonBrightness();

  // Điều khiển 2 thanh LED nước chảy sao băng mặt lưng (GPIO 18 & GPIO 38)
  void setFlowingLeds(bool enable);
  bool getFlowingLeds();

  // Đặt chế độ công suất chung (0: Tắt, 1: 35%, 2: 60%, 3: 85%, 4: Nhịp thở)
  void setMode(uint8_t modeIdx);
  uint8_t getMode();
  const char* getModeName();
  bool isBreathing();

  // Phím tắt Serial Monitor ('w' hoặc 'l': Chuyển nấc công suất Motor & Đèn)
  bool handleSerial(char cmd);
}

