#pragma once
#include <Arduino.h>

namespace TestSDCard {

  struct SdPinDiag {
    uint8_t gpio;
    const char* shieldPin; // "D5", "D6", "D7", "D8"
    const char* spiRole;   // "SCK", "MISO", "MOSI", "CS"
    uint8_t valPullUp;     // Mức logic khi bật INPUT_PULLUP (1 = ~3.3V, 0 = 0.0V)
    uint8_t valPullDown;   // Mức logic khi bật INPUT_PULLDOWN (1 = ~3.3V, 0 = 0.0V)
    uint8_t valActiveSpi;  // Mức logic thực tế khi giữ bus SPI nghỉ
    float estVoltage;      // Điện áp ước lượng từ trạng thái mạch (0.00V, 1.65V, 3.30V)
    const char* stateDesc; // "3.3V (Pull-Up)", "Float->3.3V(PU)", "0.0V (Pull-Down/GND)"
  };

  // Khởi tạo & Kiểm tra Shield Thẻ nhớ Micro SD (Wemos D1 Mini Shield: D5/SCK=42, D6/MISO=41, D7/MOSI=40, D8/CS=39)
  bool init();

  // Vòng lặp kiểm tra tự động định kỳ khi ở màn hình Test SD (Mode 9)
  void loop();

  // Quét điện áp/trạng thái điện học của 4 chân (42, 41, 40, 39) và gửi lệnh SPI CMD0 thô
  void runFullElectricalAndSpiProbe();

  // Trạng thái thẻ nhớ & Dữ liệu chẩn đoán để hiển thị lên màn hình ST7789
  bool isMounted();
  uint32_t getCardSizeMB();
  uint32_t getTotalMB();
  uint32_t getUsedMB();
  uint32_t getFreeMB();
  int getFileCount();
  String getCardTypeName();
  String getStatusSummary();
  String getCmd0RawHexStr();
  uint8_t getCmd0R1Byte();
  String getMatchedPinMapDesc();
  String getFirstFileNames();
  uint32_t getScanCount();
  const SdPinDiag* getPinDiags(); // Mảng 4 phần tử: D5(42), D6(41), D7(40), D8(39)

  // Các hàm phục vụ Ứng dụng "Bộ nhớ" (Memory Manager) trong Menu Symbian S40:
  int listSdFiles(String outNames[], size_t outSizes[], bool outIsDir[], int maxItems);
  String readSdFilePreview(const String& path);
  bool copySdJpgToLittleFS(const String& sdPath);
  bool formatSdCard();

  // Thực hiện bài kiểm tra đọc/ghi tệp và liệt kê danh sách tệp trên thẻ Micro SD ('k')
  bool handleSerial(char cmd);
}

