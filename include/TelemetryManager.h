#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>

struct __attribute__((packed)) TelemetryRecord {
  uint32_t timestamp;     // Unix epoch time hoặc uptime seconds (4 bytes)
  int16_t  tempC_x10;     // Nhiệt độ x 10, ví dụ 28.5°C = 285 (2 bytes)
  uint8_t  humidity;      // Độ ẩm % (0..100) (1 byte)
  uint8_t  batteryPct;    // Dung lượng pin % (0..100) (1 byte)
  uint8_t  cpuUsage;      // Mức tải CPU % (0..100) (1 byte)
  uint8_t  ramUsage;      // Mức tải RAM / GPU % (0..100) (1 byte)
  uint8_t  weatherCode;   // Mã thời tiết WMO (0..99) (1 byte)
  uint8_t  flags;         // Bit 0: SHT31, Bit 1: Chg, Bit 2: WiFi, Bit 3: PC live (1 byte)
}; // Tổng kích thước chuẩn: 12 bytes

class TelemetryManager {
public:
  // Khởi tạo và cấp phát đúng 1MB PSRAM Circular Buffer (87,381 bản ghi = ~48.5 giờ liên tục)
  static void init();

  // Vòng lặp định kỳ lấy mẫu telemetry & xử lý cơ chế bảo vệ Flash (FlashGuard)
  static void loop();

  // Trạng thái bộ nhớ PSRAM Circular Buffer
  static bool isPsramBufferReady();
  static size_t getCapacity();           // 87,381 bản ghi
  static size_t getRecordCount();        // Số bản ghi hiện có
  static uint32_t getBufferSizeBytes();  // 1,048,576 bytes (1MB)
  static uint32_t getSampleIntervalSec();// 2 giây
  static uint32_t getFlashWritesSaved(); // Số lần ghi Flash đã được bảo vệ/tiết kiệm

  // Ghi nhận 1 mẫu đo đạc thủ công
  static void recordSample();

  // Truy xuất dữ liệu lịch sử phục vụ Web REST API (/api/telemetry/history)
  static void getHistoryJson(JsonDocument& doc, const String& range = "24h", size_t maxPoints = 60);

  // Thống kê Min / Max / Trung bình
  static void getStats(const String& range, float& minT, float& maxT, float& avgT,
                       int& minH, int& maxH, float& avgH);

  // Vẽ biểu đồ xu hướng (Telemetry Line Graph) trực quan lên màn hình ST7789
  static void drawTrendGraphST7789(Adafruit_GFX* gfx, int x, int y, int w, int h,
                                   const String& metric = "temp", const String& range = "24h");

  // --- CƠ CHẾ BẢO VỆ BỘ NHỚ FLASH (FLASHGUARD DEBOUNCER) ---
  // Trì hoãn 2.0 giây trước khi ghi Flash để bảo vệ chip khi kéo thanh trượt / nhấn liên tục
  static void markFlashDirty(void (*saveCallback)());
  static void flushPendingFlashWrites();
};

