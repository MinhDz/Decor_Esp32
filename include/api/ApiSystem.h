#pragma once
#include <Arduino.h>
#include <WebServer.h>

namespace ApiSystem {
  // Đăng ký toàn bộ các route Hệ thống, Xác thực, OTA, Logs, Memory, Telemetry
  void registerRoutes(WebServer& server);

  // Các hàm xử lý chi tiết (hỗ trợ kiểm thử hoặc điều phối)
  void handleAuthStatus(WebServer& server);
  void handleAuthLogin(WebServer& server);
  void handleAuthLogout(WebServer& server);
  void handleAuthChangePassword(WebServer& server);
  void handleFactoryReset(WebServer& server);
  void handleSystemStatus(WebServer& server);
  void handleSystemInfo(WebServer& server);
  void handleOtaCheck(WebServer& server);
  void handleOtaStart(WebServer& server);
  void handleOtaStatus(WebServer& server);
  void handleLocalUpdate(WebServer& server);
  void handleLocalUpdateUpload(WebServer& server);
  void handleGetLogs(WebServer& server);
  void handleDownloadLogs(WebServer& server);
  void handleGetLogStats(WebServer& server);
  void handleGetLogFiles(WebServer& server);
  void handlePostLogSnapshot(WebServer& server);
  void handlePostLogClear(WebServer& server);
  void handleGetTelemetryHistory(WebServer& server);
  void handleGetTelemetryStats(WebServer& server);
  void handleMemoryStatus(WebServer& server);
  void handleMemoryScavenge(WebServer& server);
  void handleMemoryConfig(WebServer& server);
  void handleAudioPlayTune(WebServer& server);
}

