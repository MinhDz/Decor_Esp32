#pragma once
#include <Arduino.h>
#include <WebServer.h>

namespace ApiTerminal {
  // Bộ thông dịch lệnh CLI dòng lệnh (sudo, mem, info, reboot, periph...) & gợi ý cú pháp
  void handleExec(WebServer& server);
}

