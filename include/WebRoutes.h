#pragma once
#include <Arduino.h>
#include <WebServer.h>

class WebRoutes {
public:
  static void begin();
  static void handleClient();
  static WebServer& getServer();

  // Thu hồi bộ nhớ upload JPEG khi nhàn rỗi (Memory Scavenger)
  static size_t trimMemory();
  static bool hasAllocatedUploadBuffer();
};

