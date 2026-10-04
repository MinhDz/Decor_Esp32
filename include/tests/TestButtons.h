#pragma once
#include <Arduino.h>

namespace TestButtons {
  // Khởi tạo Cụm 7 Phím Bấm Thang Điện Trở ADC (Mặc định GPIO 3 - ADC1_CH2, trở kéo lên 10kΩ lên 3.3V)
  // Hỗ trợ bảng thông số:
  //   - Không bấm : Hở mạch (>3.00V, ADC 4000-4095)
  //   - Nút 1 (OK): 0Ω      (<0.15V, ADC 0-100)
  //   - Nút 2 (UP): 1.0kΩ   (0.20V-0.45V, ADC 350-420)
  //   - Nút 3 (DN): 2.2kΩ   (0.50V-0.75V, ADC 700-800)
  //   - Nút 4 (LF): 4.7kΩ   (0.90V-1.20V, ADC 1250-1380)
  //   - Nút 5 (RT): 10kΩ    (1.45V-1.85V, ADC 2000-2100)
  //   - Nút 6 (MN): 20kΩ    (2.05V-2.45V, ADC 2750-2880)
  //   - Nút 7 (EX): 47kΩ    (2.55V-2.90V, ADC 3300-3450)
  void init();

  // Vòng lặp quét ADC 16 mẫu + khử dội chuyển tiếp (Settling Filter)
  void loop();

  // Lấy thông tin hiển thị trực tiếp lên màn hình ST7789
  String  getLastButtonName();
  int     getActiveButtonIndex(); // -1: Không bấm, 0:OK, 1:UP, 2:DOWN, 3:LEFT, 4:RIGHT, 5:MENU, 6:EXIT
  float   getLastAdcVoltage();    // Điện áp thực tế (V), ví dụ 1.65V
  int     getLastAdcMilliVolts(); // Điện áp thực tế (mV), ví dụ 1650mV
  int     getLastAdcRaw();        // Giá trị ADC 12-bit (0..4095)
  uint8_t getAdcPin();            // Chân GPIO ADC đang dùng (mặc định GPIO 3)

  // In bảng trạng thái & Bật/Tắt stream đo ADC trực tiếp trên Serial ('b')
  bool handleSerial(char cmd);
}

#if defined(TEST_AUDIO_H_INCLUDED) && !defined(REAL_DISPLAY_DRIVER_CPP)
  #define TestDisplay TestDisplay_LegacyShadow
#endif

