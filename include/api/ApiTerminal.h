#pragma once
#include <Arduino.h>
#include <WebServer.h>

namespace ApiTerminal {
  // Đăng ký route Terminal CLI
  void registerRoutes(WebServer& server);

  // Bộ thông dịch lệnh CLI dòng lệnh (sudo, mem, info, reboot, periph...) & gợi ý cú pháp
  void handleExec(WebServer& server);
}

