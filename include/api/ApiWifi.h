#pragma once
#include <Arduino.h>
#include <WebServer.h>

namespace ApiWifi {
  // Đăng ký toàn bộ các route Wi-Fi & Thời tiết
  void registerRoutes(WebServer& server);

  void handleWifiScan(WebServer& server);
  void handleGetSavedWifi(WebServer& server);
  void handleDeleteSavedWifi(WebServer& server);
  void handleWifiConnect(WebServer& server);
  void handleGetWeather(WebServer& server);
  void handleRefreshWeather(WebServer& server);
}

