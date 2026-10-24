#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum StrictModeLevel : uint8_t {
  STRICT_MODE_OFF        = 0, // Tắt tự động thu hồi (Chỉ thu hồi khi Heap khẩn cấp hoặc khi bấm Dọn RAM)
  STRICT_MODE_BALANCED   = 1, // Cân bằng (Mặc định): Tự động thu hồi bộ nhớ sau 3 phút nhàn rỗi (180s)
  STRICT_MODE_AGGRESSIVE = 2  // Nghiêm ngặt (Android Low-RAM): Thu hồi sau 60s nhàn rỗi hoặc khi vào AOD/Sleep
};

struct ScavengeReport {
  size_t   heapFreedBytes;
  size_t   psramFreedBytes;
  size_t   totalFreedBytes;
  uint32_t timestamp;
  const char* triggerReason; // "IDLE_TIMEOUT", "MANUAL", "LOW_RAM_ALERT", "AOD_SLEEP_TRIM"
};

class MemoryScavenger {
public:
  static void init();
  static void loop();

  // 1. Cấu hình Chế độ Nghiêm ngặt (Lưu vào NVS Preferences bền vững)
  static void setStrictMode(StrictModeLevel level);
  static StrictModeLevel getStrictMode();
  static const char* getStrictModeName();

  // 2. Kích hoạt Thu Hồi Bộ Nhớ (Scavenge / Trim)
  // forceAll = true: Thu hồi toàn bộ buffer không hoạt động ngay lập tức, bất kể thời gian nhàn rỗi
  static ScavengeReport scavenge(bool forceAll = false, const char* reason = "MANUAL");

  // 3. Thông báo hoạt động ứng dụng để theo dõi thời gian nhàn rỗi (Idle Tracker)
  static void notifyAudioActivity();
  static void notifyStreamActivity();
  static void notifyXiaoZhiActivity();
  static void notifyUploadActivity();

  // 4. Lấy thông số giám sát & thời gian nhàn rỗi
  static uint32_t getAudioIdleSec();
  static uint32_t getStreamIdleSec();
  static uint32_t getXiaoZhiIdleSec();
  static uint32_t getUploadIdleSec();

  static const ScavengeReport& getLastReport();
  static uint32_t getTotalScavengeRuns();
  static uint32_t getCumulativeFreedKb();

  // 5. Kết xuất JSON cho REST API Web
  static void getStatusJson(JsonDocument& doc);
};

