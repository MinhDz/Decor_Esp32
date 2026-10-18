#pragma once
#include <Arduino.h>

namespace TestWindmill {
  // Khởi tạo Module L298N & toàn bộ hệ thống Đèn Decor (Edison L/R, LED Nước Chảy)
  void init();

  // Vòng lặp hiệu ứng đèn thở & dao động công suất (Breathing)
  void loop();

  // 1. Điều chỉnh tốc độ / công suất Motor Cối Xay (GPIO 1)
  void setMotorSpeed(uint8_t speedPct);
  uint8_t getMotorSpeed();

  void setLedBrightness(uint8_t brightPct, bool breathing = false);
  uint8_t getLedBrightness();

  // 2. Chế độ Motor Cối Xay (0: Tắt, 1: 35%, 2: 60%, 3: 85%, 4: Nhịp thở)
  void setMode(uint8_t modeIdx);
  uint8_t getMode();
  const char* getModeName();
  bool isBreathing();

  // 3. Điều khiển cặp LED Ống Edison Sườn Trái (GPIO 47 - PWM Kênh 4)
  // Các chế độ: 
  //   0: Tắt, 1: 25%, 2: 50%, 3: 75%, 4: 100%, 
  //   5: Lò phản ứng & Đồng bộ PC (Reactor & PC HUD Sync)
  //   6: Ngọn nến lung linh (Candle Flicker)
  //   7: Nhịp đập Cyberpunk (Cyber Heartbeat)
  //   8: Sấm sét hồ quang (Electric Lightning)
  //   9: Thở đối xứng lệch pha (Alternating Dual Breath)
  //   10: Dòng năng lượng sạc (Power Flow Pulse)
  void setEdisonLeftMode(uint8_t modeIdx);
  uint8_t getEdisonLeftMode();
  const char* getEdisonLeftModeName();
  void setEdisonLeftBrightness(uint8_t brightPct, bool breathing = false);
  uint8_t getEdisonLeftBrightness();

  // 4. Điều khiển cặp LED Ống Edison Sườn Phải (GPIO 21 - PWM Kênh 5)
  void setEdisonRightMode(uint8_t modeIdx);
  uint8_t getEdisonRightMode();
  const char* getEdisonRightModeName();
  void setEdisonRightBrightness(uint8_t brightPct, bool breathing = false);
  uint8_t getEdisonRightBrightness();

  // Kích hoạt lại chuỗi hiệu ứng khởi động lò phản ứng (sáng từ từ rồi lóe sáng)
  void triggerStartupEffect();
  bool isStartupActive();

  // 5. Điều khiển chung cả 2 cặp LED Edison & Quản lý Preset
  void setEdisonBothMode(uint8_t modeIdx);
  void setEdisonBrightness(uint8_t brightPct);
  uint8_t getEdisonBrightness();
  uint8_t getEdisonModeCount();
  const char* getEdisonModeNameByIdx(uint8_t modeIdx);
  void applyEffectPreset(const String& preset);

  // 6. Điều khiển 2 thanh LED nước chảy sao băng mặt lưng (GPIO 38 & GPIO 18)
  void setFlowingLeds(bool enable);
  bool getFlowingLeds();
  void setFlowingLeft(bool enable);
  bool getFlowingLeft();
  void setFlowingRight(bool enable);
  bool getFlowingRight();

  // Phím tắt Serial Monitor ('w': Cối xay, 'e': Edison Trái, 'r': Edison Phải, 'f': Nước chảy)
  bool handleSerial(char cmd);
}


