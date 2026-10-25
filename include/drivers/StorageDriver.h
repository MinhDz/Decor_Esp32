#pragma once
#include <Arduino.h>

namespace StorageDriver {

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

  // ============================================================================
  // HỆ THỐNG GIÁM SÁT SỨC KHỎE THẺ NHỚ & CƠ CHẾ BẢO VỆ NGẮT MẠCH (CIRCUIT BREAKER)
  // Xử lý các lỗi: Không có thẻ, rút thẻ đột ngột khi đang ghi, hỏng do tuổi thọ flash,
  // xung dòng làm chết thẻ, bad blocks, hoặc lỗi filesystem FAT.
  // ============================================================================
  enum SdCardHealth : uint8_t {
    SD_HEALTH_OK = 0,          // Hoạt động bình thường
    SD_HEALTH_NO_CARD = 1,     // Không có thẻ nhớ
    SD_HEALTH_UNMOUNTED = 2,   // Thẻ có mặt nhưng chưa mount được FAT
    SD_HEALTH_WRITE_ERROR = 3, // Lỗi ghi dữ liệu (Flash bad block / Write protect / Hết vòng đời)
    SD_HEALTH_IO_ERROR = 4,    // Lỗi giao tiếp SPI / Thẻ bị rút đột ngột
    SD_HEALTH_CORRUPTED = 5,   // Hệ thống tệp FAT bị lỗi
    SD_HEALTH_OVERCURRENT = 6  // Cảnh báo sụt áp / xung dòng làm tê liệt bus SPI
  };

  struct SdHealthStats {
    SdCardHealth health;
    uint32_t writeErrors;
    uint32_t readErrors;
    uint32_t consecutiveErrors;
    uint32_t successfulOps;
    unsigned long lastErrorMs;
    unsigned long lastProbeMs;
    bool circuitBreakerTripped;
  };

  // Kiểm tra sức khỏe & trạng thái sẵn sàng
  bool isHealthy();
  SdCardHealth getHealthState();
  const char* getHealthStateStr();
  SdHealthStats getHealthStats();

  // Báo cáo lỗi I/O để kích hoạt cơ chế ngắt mạch bảo vệ
  void notifyIoError(bool isWrite = false, const char* context = nullptr);
  void notifyIoSuccess();

  // Thử kiểm tra sự hiện diện vật lý nhanh (Fast Low-Overhead CMD0 Probe)
  bool probeCardPresent();

  // Ngắt kết nối an toàn giải phóng tài nguyên SPI
  void unmountSafe();

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

// Bí danh tương thích ngược cho các module hiện hữu
namespace TestSDCard = StorageDriver;

