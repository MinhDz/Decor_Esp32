#pragma once
#include <Arduino.h>
#include <SPI.h>

namespace TouchDriver {

  enum TouchEventType {
    TOUCH_NONE = 0,
    TOUCH_DOWN,
    TOUCH_MOVE,
    TOUCH_UP,
    TOUCH_CLICK,
    TOUCH_SWIPE_UP,
    TOUCH_SWIPE_DOWN,
    TOUCH_SWIPE_LEFT,
    TOUCH_SWIPE_RIGHT,
    TOUCH_LONG_PRESS
  };

  struct TouchPoint {
    int16_t x;        // Tọa độ màn hình (0 .. 239)
    int16_t y;        // Tọa độ màn hình (0 .. 319)
    int16_t rawX;     // Tọa độ ADC thô (0 .. 4095)
    int16_t rawY;     // Tọa độ ADC thô (0 .. 4095)
    int16_t pressure; // Áp lực nhấn Z (0 .. 4095)
    bool isTouched;   // Trạng thái đang chạm
    int16_t startX;   // Tọa độ lúc bắt đầu ấn xuống (0 .. 239)
    int16_t startY;   // Tọa độ lúc bắt đầu ấn xuống (0 .. 319)
    uint32_t durationMs; // Thời gian giữ ngón tay (ms)
  };

  // Khởi tạo phần cứng XPT2046
  void init(SPIClass* spiBus, int8_t csPin = 45);

  // Vòng lặp quét cảm ứng định kỳ (gọi trong loop)
  void loop();

  // Kiểm tra có đang chạm vào màn hình hay không
  bool isTouched();

  // Lấy tọa độ chạm hiện tại
  TouchPoint getPoint();

  // Rút sự kiện chạm (Touch Event)
  bool getTouchEvent(TouchEventType& outEvt, TouchPoint& outPt);

  // Cấu hình hiệu chuẩn tọa độ
  void setCalibration(int16_t minX, int16_t maxX, int16_t minY, int16_t maxY,
                      bool swapXY = false, bool invertX = true, bool invertY = true);
  void getCalibration(int16_t& minX, int16_t& maxX, int16_t& minY, int16_t& maxY,
                      bool& swapXY, bool& invertX, bool& invertY);
  void loadConfig();
  void saveConfig();

  // Bật/tắt in log cảm ứng qua Serial
  void setDebugLog(bool enable);
  bool isDebugLog();
}
