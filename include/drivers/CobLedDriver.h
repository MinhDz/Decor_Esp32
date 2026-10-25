#pragma once
#include <Arduino.h>
#include "Config.h"

// ============================================================================
//   BỘ ĐIỀU KHIỂN ĐÈN THANH NƯỚC CHẢY COB SAO BĂNG (GPIO 18 & GPIO 38)
//   HAL Driver chuẩn cho hệ thống LED COB mặt lưng (Hardware Abstraction Layer).
//   Đặc tính: Thanh LED tích hợp sẵn IC tạo xung hiệu ứng nội bộ (3V, <20mA).
//   Xuất mức HIGH thuần túy (Không dùng PWM) để tránh làm IC bị treo ở Mode 2.
//   Đổi chế độ bằng cách ngắt nguồn LOW tạm thời (300ms) rồi xuất lại HIGH.
// ============================================================================

#ifndef PIN_COB_LED_R
  #define PIN_COB_LED_R PIN_FLOWING_LED_R // GPIO 18 (Thanh mặt lưng bên Phải)
#endif

#ifndef PIN_COB_LED_L
  #define PIN_COB_LED_L PIN_FLOWING_LED_L // GPIO 38 (Thanh mặt lưng bên Trái)
#endif

#define COB_PULSE_LOW_MS 300 // Thời gian ngắt nguồn tối ưu để IC nhận lệnh đổi chế độ

class CobLedController {
private:
  uint8_t pin;
  bool isPoweredOn;
  bool isTriggering;
  unsigned long pulseStartTime;

public:
  CobLedController(uint8_t targetPin);

  void begin(bool powerOn = true);
  void triggerNextMode();
  void update();
  void turnOff();
  void turnOn();

  bool isOn() const { return isPoweredOn; }
  uint8_t getPin() const { return pin; }
};

// 2 Đối tượng điều khiển độc lập cho 2 thanh LED mặt lưng
extern CobLedController cobLedRight; // GPIO 18 (Thanh Phải / Xanh dương)
extern CobLedController cobLedLeft;  // GPIO 38 (Thanh Trái / Đỏ)

// Namespace quản lý tập trung toàn bộ hệ thống COB LED (HAL Driver)
namespace CobLeds {
  void init(bool powerOn = true);
  void update();
  
  void turnOn();
  void turnOff();
  void togglePower();
  bool isOn();

  void nextMode();
  void prevMode();
  void triggerNextMode();

  uint8_t getCurrentMode();
  void setCurrentMode(uint8_t mode);
  const char* getModeName();
}

// Bí danh tương thích theo quy chuẩn đặt tên HAL Driver
namespace CobLedDriver = CobLeds;
using CobLed = CobLedController;

