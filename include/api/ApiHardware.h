#pragma once
#include <Arduino.h>
#include <WebServer.h>

namespace ApiHardware {
  // Đăng ký toàn bộ các route Decor (COB, Motor, Edison), Keypad Remote, Standby Config, PC HUD
  void registerRoutes(WebServer& server);

  void handleGetStandbyConfig(WebServer& server);
  void handleSaveStandbyConfig(WebServer& server);
  void handleScreenStatePost(WebServer& server);
  void handleDecorStatus(WebServer& server);
  void handleDecorControl(WebServer& server);
  void handleKeypadPress(WebServer& server);
  void handleKeypadStatus(WebServer& server);
  void handlePcMetricsGet(WebServer& server);
  void handlePcMetricsPost(WebServer& server);
}

