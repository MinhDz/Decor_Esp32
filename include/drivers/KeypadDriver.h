#pragma once
#include <Arduino.h>

namespace KeypadDriver {
  // Khởi tạo Cụm 7 Phím Bấm Thang Điện Trở ADC (Mặc định GPIO 3 - ADC1_CH2, trở kéo lên 10kΩ lên 3.3V)
  // Hỗ trợ bảng thông số:
  //   - Không bấm : Hở mạch (>2.88V, ADC 3900-4095)
  //   - Nút 1 (OK): 0Ω      (<0.13V, ADC 0-100)
  //   - Nút 2 (UP): 1.0kΩ   (0.14V-0.45V, ADC 180-420, thực tế ~0.28V)
  //   - Nút 3 (DN): 2.2kΩ   (0.46V-0.82V, ADC 700-800, thực tế ~0.59V)
  //   - Nút 4 (LF): 4.7kΩ   (0.83V-1.34V, ADC 1250-1380, thực tế ~1.05V)
  //   - Nút 5 (RT): 10kΩ    (1.35V-1.90V, ADC 2000-2100, thực tế ~1.65V)
  //   - Nút 6 (MN): 20kΩ    (1.91V-2.32V, ADC 2750-2880, thực tế ~2.20V)
  //   - Nút 7 (EX): 47kΩ    (2.33V-2.85V, ADC 3300-3450, thực tế ~2.72V)
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
  bool    isOkButtonPhysicallyHeld(); // Kiểm tra phím OK đang được giữ chắc chắn (áp <= 180mV hoặc index 0)

  // In bảng trạng thái & Bật/Tắt stream đo ADC trực tiếp trên Serial ('b')
  bool handleSerial(char cmd);

  // Giả lập bấm phím ảo từ giao diện Web (idx: 0=OK, 1=UP, 2=DOWN, 3=LEFT, 4=RIGHT, 5=MENU, 6=EXIT)
  void injectButtonPress(int idx);
  void injectButtonRelease();
}

// Bí danh tương thích ngược cho các module hiện hữu
namespace TestButtons = KeypadDriver;

