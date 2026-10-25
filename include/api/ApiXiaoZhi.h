#pragma once
#include <Arduino.h>
#include <WebServer.h>

namespace ApiXiaoZhi {
  // Đăng ký toàn bộ các route điều khiển trợ lý ảo XiaoZhi AI
  void registerRoutes(WebServer& server);

  void handleGetAIConfig(WebServer& server);
  void handleSaveAIConfig(WebServer& server);
  void handleXiaoZhiOTA(WebServer& server);
  void handleXiaoZhiUnbind(WebServer& server);
  void handleXiaoZhiChatStatus(WebServer& server);
  void handleXiaoZhiAction(WebServer& server);
}

