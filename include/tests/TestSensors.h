#pragma once
#include <Arduino.h>

namespace TestSensors {
  // Khởi tạo Cảm biến Nhiệt/Ẩm SHT31 (I2C: Tự động dò cả SDA=8/SCL=9 và SDA=9/SCL=8) & Chạm TTP223 (SIG=7)
  void init();

  // Vòng lặp đọc cảm biến chạm TTP223 & đọc SHT31 định kỳ không chặn
  void loop();

  // Quét toàn bộ Bus I2C (cả chiều thuận 8/9 và chiều đảo 9/8) và in báo cáo chi tiết
  void runFullI2cDiagnostics();

  // Trạng thái & Giá trị đọc mới nhất từ SHT31
  bool isSht31Connected();
  uint8_t getDetectedAddress();
  uint8_t getActiveSdaPin();
  uint8_t getActiveSclPin();
  String getDiagSummary();

  float getTemperatureC();
  float getHumidityPct();
  bool isTouchPressed();

  // In báo cáo trạng thái cảm biến & quét I2C ra Serial Monitor ('t', 'g')
  bool handleSerial(char cmd);
}

