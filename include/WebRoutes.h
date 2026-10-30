#pragma once
#include <Arduino.h>
#include <WebServer.h>

class WebRoutes {
public:
  static void begin();
  static void handleClient();
  static WebServer& getServer();

  // Tự phục hồi Web Server và mDNS khi Wi-Fi kết nối hoặc cấp IP mới
  static void onWifiConnected();
  static void recoverServer();

  // Thu hồi bộ nhớ upload JPEG khi nhàn rỗi (Memory Scavenger)
  static size_t trimMemory();
  static bool hasAllocatedUploadBuffer();
};

