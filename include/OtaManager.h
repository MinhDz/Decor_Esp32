#pragma once
#include <Arduino.h>
#include <WebServer.h>

#define SYSTEM_DEVICE_NAME    "Trạm Decor Vũ Trụ"
#define SYSTEM_OS_NAME        "Space OS"
#define FIRMWARE_VERSION      "v3.1.1"
#define FIRMWARE_BUILD_DATE   __DATE__ " " __TIME__

struct OtaUpdateInfo {
  bool   hasUpdate;
  String latestVersion;
  String downloadUrl;
  String changelog;
  size_t binSize;
};

class OtaManager {
public:
  // Khởi tạo OTA & hủy cờ rollback khi firmware mới khởi động an toàn
  static void init();

  // Thông tin hệ thống & phân vùng OTA
  static String getDeviceName() { return SYSTEM_DEVICE_NAME; }
  static String getOsName() { return SYSTEM_OS_NAME; }
  static String getRunningPartitionName();
  static String getNextPartitionName();
  static String getFirmwareVersion();
  static String getBuildDateTime();

  // Đăng ký các endpoints cho Web Server (Local OTA & API kiểm tra Cloud OTA)
  static void registerWebRoutes(WebServer& server);

  // Kiểm tra phiên bản mới từ Cloud qua file version.json
  static bool checkCloudUpdate(OtaUpdateInfo& info, const String& manifestUrl = "");

  // Tiến hành tải & nạp Firmware từ xa qua Internet (Cloud HTTPS OTA)
  static bool startCloudUpdate(const String& binUrl);

  // Trạng thái và tiến trình nạp
  static bool isUpdating();
  static int getUpdateProgress();
  static String getUpdateStatus();

  // Vẽ giao diện tiến trình nạp tối giản lên màn hình LCD ST7789
  static void drawMinimalOtaProgress(const String& status, int progressPct);
};

