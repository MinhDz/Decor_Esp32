#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum PowerProfile : uint8_t {
  POWER_PERFORMANCE = 0, // Cực đại: CPU 240MHz, Wi-Fi PS None, SHT31 2.5s, Màn hình sáng 100%, không tự tắt
  POWER_BALANCED    = 1, // Cân bằng (Mặc định khi chạy pin): CPU 160MHz, Wi-Fi MIN_MODEM, SHT31 30s, Loa/Mic auto-sleep, tắt màn hình sau 5 phút
  POWER_SAVER       = 2  // Siêu tiết kiệm: CPU 80MHz, Wi-Fi MIN_MODEM, SHT31 60s, tắt COB, giảm Edison 30%, tắt màn hình sau 2 phút
};

namespace PowerManagementService {
  void init();
  void loop();

  // Đổi cấu hình nguồn (userOverride = true nếu người dùng chủ động chọn qua Web/App/MCP)
  void setProfile(PowerProfile profile, bool userOverride = false);
  PowerProfile getProfile();
  const char* getProfileName();

  // Bật/tắt tự động chuyển chế độ theo nguồn sạc và dung lượng pin
  void setAutoSwitch(bool enabled);
  bool isAutoSwitchEnabled();

  // Trạng thái bão hòa sạc (100% Full Saturation - cắm sạc >= 4.18V liên tục >= 30 phút)
  bool isBatteryFullyCharged();
  uint32_t getFullChargeDurationSec();

  // Chu kỳ đọc cảm biến SHT31 linh hoạt theo Profile
  unsigned long getSensorIntervalMs();

  // Thời gian chờ tắt màn hình tự động (0: không tắt, 300000: 5 phút, 120000: 2 phút)
  unsigned long getScreenSleepTimeoutMs();
  void setScreenSleepTimeoutMs(unsigned long timeoutMs);

  // Báo hiệu có tương tác người dùng (phím bấm, chạm TTP223, API, giọng nói) để reset timer tắt màn hình
  void notifyUserActivity();
  unsigned long getIdleDurationSec();

  // Xử lý khi Wi-Fi vừa kết nối để áp dụng chính sách Wi-Fi Power Save phù hợp
  void onWifiConnected();

  // Xuất trạng thái JSON cho REST API Web & MCP
  void getStatusJson(JsonDocument& doc);
}

