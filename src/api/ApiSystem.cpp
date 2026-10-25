#include "api/ApiSystem.h"
#include "Config.h"
#include "HardwareManager.h"
#include "WifiManager.h"
#include "XiaoZhiClient.h"
#include "ImageManager.h"
#include "PcStatsManager.h"
#include "tests/TestDisplay.h"
#include "tests/TestSensors.h"
#include "tests/TestButtons.h"
#include "tests/TestAudio.h"
#include "tests/TestSDCard.h"
#include "tests/TestWindmill.h"
#include "drivers/CobLedDriver.h"
#include "OtaManager.h"
#include "TelemetryManager.h"
#include "TaskProfiler.h"
#include "SystemLogger.h"
#include "MemoryScavenger.h"
#include "WebAuth.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>
#include <SD.h>
#include <LittleFS.h>
#include <esp_system.h>

namespace ApiSystem {

  static size_t s_localOtaTotalExpected = 0;
  static int s_lastLocalOtaPct = -1;

  static void cloudOtaTask(void* param) {
    String* url = (String*)param;
    OtaManager::startCloudUpdate(*url);
    delete url;
    vTaskDelete(NULL);
  }

  // --- 1. XÁC THỰC BẢO MẬT (AUTH) ---
  void handleAuthStatus(WebServer& server) {
    bool authed = WebAuth::isAuthenticated(server);
    String user = authed ? WebAuth::getUsername() : "";
    server.send(200, "application/json", "{\"authenticated\":" + String(authed ? "true" : "false") + ",\"username\":\"" + user + "\"}");
  }

  void handleAuthLogin(WebServer& server) {
    String u = "";
    String p = "";
    if (server.hasArg("plain")) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      if (!err) {
        u = doc["username"] | "";
        p = doc["password"] | "";
      }
    }
    if (u.length() == 0 && server.hasArg("username")) u = server.arg("username");
    if (p.length() == 0 && server.hasArg("password")) p = server.arg("password");
    u.trim();
    p.trim();

    if (WebAuth::verify(u, p)) {
      String token = WebAuth::generateNewSessionToken();
      server.sendHeader("Set-Cookie", "spaceos_token=" + token + "; Path=/; Max-Age=2592000; SameSite=Lax");
      server.send(200, "application/json", "{\"status\":\"ok\",\"token\":\"" + token + "\",\"username\":\"" + u + "\"}");
    } else {
      server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Sai tên đăng nhập hoặc mật khẩu!\"}");
    }
  }

  void handleAuthLogout(WebServer& server) {
    WebAuth::clearSession();
    server.sendHeader("Set-Cookie", "spaceos_token=; Path=/; Max-Age=0; SameSite=Lax");
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }

  void handleAuthChangePassword(WebServer& server) {
    if (!WebAuth::isAuthenticated(server)) {
      server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Phiên làm việc hết hạn hoặc chưa đăng nhập!\"}");
      return;
    }
    String oldPass = "";
    String newPass = "";
    String newUser = "";
    if (server.hasArg("plain")) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, server.arg("plain"));
      oldPass = doc["old_password"] | "";
      newPass = doc["new_password"] | "";
      newUser = doc["username"] | "";
    }
    if (oldPass.length() == 0 && server.hasArg("old_password")) oldPass = server.arg("old_password");
    if (newPass.length() == 0 && server.hasArg("new_password")) newPass = server.arg("new_password");
    if (newUser.length() == 0 && server.hasArg("username")) newUser = server.arg("username");
    oldPass.trim();
    newPass.trim();
    newUser.trim();

    if (newPass.length() < 4) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Mật khẩu mới tối thiểu 4 ký tự!\"}");
      return;
    }
    if (!WebAuth::verify(WebAuth::getUsername(), oldPass)) {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Mật khẩu hiện tại không đúng!\"}");
      return;
    }

    if (newUser.length() > 0) {
      WebAuth::changeCredentials(newUser, newPass);
    } else {
      WebAuth::changePassword(oldPass, newPass);
    }
    String token = WebAuth::getSessionToken();
    server.sendHeader("Set-Cookie", "spaceos_token=" + token + "; Path=/; Max-Age=2592000; SameSite=Lax");
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Đổi mật khẩu thành công!\",\"token\":\"" + token + "\"}");
  }

  // --- 2. KHÔI PHỤC CÀI ĐẶT BAN ĐẦU (FACTORY RESET) ---
  void handleFactoryReset(WebServer& server) {
    if (!WebAuth::isAuthenticated(server)) {
      server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Chưa đăng nhập quyền quản trị!\"}");
      return;
    }
    TestDisplay::performFactoryReset();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Đã khôi phục cài đặt ban đầu và chuyển sang giao diện MÈO GOME CHIBI!\"}");
  }

  // --- 3. TRẠNG THÁI & THÔNG TIN HỆ THỐNG ---
  void handleSystemStatus(WebServer& server) {
    JsonDocument doc;
    doc["device_name"] = OtaManager::getDeviceName();
    doc["os_name"] = OtaManager::getOsName();
    doc["firmware_version"] = OtaManager::getFirmwareVersion();
    doc["build_date"] = OtaManager::getBuildDateTime();
    doc["running_partition"] = OtaManager::getRunningPartitionName();
    doc["next_partition"] = OtaManager::getNextPartitionName();
    doc["free_heap_kb"] = HardwareManager::getFreeHeapKb();
    doc["free_psram_mb"] = HardwareManager::getFreePsramMb();
    doc["fs_used_mb"] = HardwareManager::getFsUsedMb();
    doc["fs_total_mb"] = HardwareManager::getFsTotalMb();
    doc["ip"] = WifiManager::getIp();
    doc["mac"] = WiFi.macAddress();
    doc["connected"] = WifiManager::isConnected();
    doc["current_ssid"] = WifiManager::getCurrentSsid();
    doc["wifi_rssi"] = WifiManager::getRssi();
    doc["wifi_notification"] = WifiManager::getLastNotification();
    doc["active_img"] = ImageManager::getActiveImage();
    doc["screen_mode"] = TestDisplay::getScreenModeName();
    doc["emoji_state"] = TestDisplay::getEmojiStateName();
    doc["emoji_subtitle"] = TestDisplay::getSubtitle();
    doc["hud_style"] = TestDisplay::getHudStyleName();
    doc["emoji_style"] = TestDisplay::getEmojiStyle();
    doc["emoji_style_name"] = TestDisplay::getEmojiStyleName();
    doc["sht31_online"] = TestSensors::isSht31Connected();
    doc["temperature_c"] = serialized(String(TestSensors::getTemperatureC(), 1));
    doc["humidity_pct"] = (int)roundf(TestSensors::getHumidityPct());
    doc["touch_pressed"] = TestSensors::isTouchPressed();
    doc["last_button"] = TestButtons::getLastButtonName();
    doc["battery_v"] = serialized(String(TestSensors::getBatteryVoltage(), 2));
    doc["battery_mv"] = TestSensors::getBatteryMilliVolts();
    doc["battery_pct"] = TestSensors::getBatteryPercentage();
    doc["is_charging"] = TestSensors::isCharging();
    doc["edison_l_mode"] = TestWindmill::getEdisonLeftMode();
    doc["edison_l_name"] = TestWindmill::getEdisonLeftModeName();
    doc["edison_l_pct"] = TestWindmill::getEdisonLeftBrightness();
    doc["edison_r_mode"] = TestWindmill::getEdisonRightMode();
    doc["edison_r_name"] = TestWindmill::getEdisonRightModeName();
    doc["edison_r_pct"] = TestWindmill::getEdisonRightBrightness();
    doc["windmill_mode"] = TestWindmill::getMode();
    doc["windmill_name"] = TestWindmill::getModeName();
    doc["windmill_speed"] = TestWindmill::getMotorSpeed();
    doc["flowing_leds_on"] = CobLeds::isOn();
    doc["flowing_leds_mode"] = CobLeds::getModeName();
    doc["xiaozhi_bound"] = XiaoZhiClient::isDeviceBound();
    doc["xiaozhi_ws_connected"] = XiaoZhiClient::isWebSocketConnected();

    String res;
    serializeJson(doc, res);
    server.send(200, "application/json", res);
  }

  void handleSystemInfo(WebServer& server) {
    JsonDocument doc;
    doc["device_name"]        = OtaManager::getDeviceName();
    doc["os_name"]            = OtaManager::getOsName();
    doc["firmware_version"]   = OtaManager::getFirmwareVersion();
    doc["build_date"]         = OtaManager::getBuildDateTime();
    doc["running_partition"]  = OtaManager::getRunningPartitionName();
    doc["next_partition"]     = OtaManager::getNextPartitionName();
    doc["chip_model"]         = ESP.getChipModel();
    doc["chip_revision"]      = ESP.getChipRevision();
    doc["cpu_freq_mhz"]       = ESP.getCpuFreqMHz();
    doc["flash_size_bytes"]   = ESP.getFlashChipSize();
    doc["free_heap_bytes"]    = ESP.getFreeHeap();
    doc["min_free_heap_bytes"]= ESP.getMinFreeHeap();
    doc["max_alloc_heap_bytes"]= ESP.getMaxAllocHeap();
    doc["total_heap_bytes"]   = ESP.getHeapSize();
    doc["free_psram_bytes"]   = ESP.getFreePsram();
    doc["min_free_psram_bytes"]= ESP.getMinFreePsram();
    doc["max_alloc_psram_bytes"]= ESP.getMaxAllocPsram();
    doc["total_psram_bytes"]  = ESP.getPsramSize();
    doc["alloc_failures"]     = HardwareManager::getAllocFailures();
    doc["loop_last_us"]       = TaskProfiler::getLoopLastUs();
    doc["loop_avg_us"]        = TaskProfiler::getLoopAvgUs();
    doc["loop_max_us"]        = TaskProfiler::getLoopMaxUs();
    doc["loop_fps"]           = TaskProfiler::getFps();
    doc["wifi_connected"]     = (WiFi.status() == WL_CONNECTED);
    doc["wifi_ssid"]          = WiFi.SSID();
    doc["wifi_rssi"]          = WiFi.RSSI();
    doc["wifi_ip"]            = WiFi.localIP().toString();
    doc["uptime_seconds"]     = millis() / 1000UL;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  }

  // --- 4. CẬP NHẬT PHẦN MỀM TỪ XA (OTA) ---
  void handleOtaCheck(WebServer& server) {
    String customUrl = server.hasArg("url") ? server.arg("url") : "";
    OtaUpdateInfo info;
    bool ok = OtaManager::checkCloudUpdate(info, customUrl);

    JsonDocument doc;
    doc["success"]          = ok;
    doc["current_version"]  = OtaManager::getFirmwareVersion();
    doc["has_update"]       = info.hasUpdate;
    doc["latest_version"]   = info.latestVersion;
    doc["bin_url"]          = info.downloadUrl;
    doc["changelog"]        = info.changelog;
    doc["size"]             = info.binSize;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  }

  void handleOtaStart(WebServer& server) {
    if (!WebAuth::isAuthenticated(server)) {
      server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
      return;
    }
    if (OtaManager::isUpdating()) {
      server.send(409, "application/json", "{\"error\":\"Already updating\"}");
      return;
    }

    String binUrl = "";
    if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      binUrl = doc["bin_url"] | "";
    } else if (server.hasArg("bin_url")) {
      binUrl = server.arg("bin_url");
    }

    if (binUrl.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"Missing bin_url\"}");
      return;
    }

    OtaManager::setUpdating(true);
    server.send(200, "application/json", "{\"status\":\"updating\"}");

    String* urlCopy = new String(binUrl);
    xTaskCreatePinnedToCore(cloudOtaTask, "cloud_ota_task", 10240, urlCopy, 1, NULL, 0);
  }

  void handleOtaStatus(WebServer& server) {
    JsonDocument doc;
    doc["updating"] = OtaManager::isUpdating();
    doc["progress"] = OtaManager::getUpdateProgress();
    doc["status"]   = OtaManager::getUpdateStatus();
    doc["version"]  = OtaManager::getFirmwareVersion();

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  }

  void handleLocalUpdate(WebServer& server) {
    if (!WebAuth::isAuthenticated(server)) {
      server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
      return;
    }
    server.sendHeader("Connection", "close");
    if (!Update.hasError()) {
      server.send(200, "text/html",
        "<html><body style='font-family:sans-serif;text-align:center;padding:50px;background:#0f172a;color:#fff;'>"
        "<h2>✅ CẬP NHẬT FIRMWARE THÀNH CÔNG!</h2>"
        "<p>Hệ thống đang khởi động lại vào firmware mới... Vui lòng đợi 5 giây rồi tải lại trang.</p>"
        "<script>setTimeout(function(){ window.location.href='/'; }, 6000);</script>"
        "</body></html>");
      delay(1500);
      ESP.restart();
    } else {
      OtaManager::setUpdating(false);
      server.send(500, "text/html",
        "<html><body style='font-family:sans-serif;text-align:center;padding:50px;background:#0f172a;color:#fff;'>"
        "<h2>❌ CẬP NHẬT THẤT BẠI!</h2>"
        "<p>Lỗi: " + String(Update.errorString()) + "</p>"
        "<p><a href='/' style='color:#38bdf8;'>Quay lại trang chủ</a></p>"
        "</body></html>");
    }
  }

  void handleLocalUpdateUpload(WebServer& server) {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      OtaManager::setUpdating(true);
      s_lastLocalOtaPct = -1;

      if (server.hasArg("size")) {
        s_localOtaTotalExpected = (size_t)server.arg("size").toInt();
      } else {
        s_localOtaTotalExpected = 0;
      }

      Serial.printf("⬆️ [WEB OTA] Bắt đầu nhận file: %s (Kỳ vọng: %u bytes)\n",
                    upload.filename.c_str(), (unsigned)s_localOtaTotalExpected);
      OtaManager::drawMinimalOtaProgress("Nhan file qua Web...", 0);

      if (!Update.begin(s_localOtaTotalExpected > 0 ? s_localOtaTotalExpected : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        Update.printError(Serial);
        OtaManager::drawMinimalOtaProgress("Loi khoi tao Flash!", 0);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }

      if (s_localOtaTotalExpected > 0) {
        int pct = (int)((upload.totalSize * 100ULL) / s_localOtaTotalExpected);
        pct = constrain(pct, 0, 99);
        if (pct != s_lastLocalOtaPct) {
          s_lastLocalOtaPct = pct;
          char statusMsg[48];
          snprintf(statusMsg, sizeof(statusMsg), "Dang ghi Flash (%u KB)...", (unsigned)(upload.totalSize / 1024));
          OtaManager::drawMinimalOtaProgress(statusMsg, pct);
        }
      } else {
        static unsigned long s_lastKbUpdateMs = 0;
        if (millis() - s_lastKbUpdateMs >= 250) {
          s_lastKbUpdateMs = millis();
          char statusMsg[48];
          snprintf(statusMsg, sizeof(statusMsg), "Da nhan %u KB...", (unsigned)(upload.totalSize / 1024));
          OtaManager::drawMinimalOtaProgress(statusMsg, 50);
        }
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("✅ [WEB OTA] Nhận file xong: %u bytes! Đang khởi động lại...\n", upload.totalSize);
        OtaManager::drawMinimalOtaProgress("Hoan tat! Dang reboot...", 100);
      } else {
        Update.printError(Serial);
        OtaManager::drawMinimalOtaProgress("Loi ghi Flash!", 0);
        OtaManager::setUpdating(false);
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.end();
      Serial.println("⚠️ [WEB OTA] Quá trình tải lên bị hủy!");
      OtaManager::drawMinimalOtaProgress("Da huy tai len!", 0);
      OtaManager::setUpdating(false);
    }
  }

  // --- 5. NHẬT KÝ HỆ THỐNG & SYSLOG ---
  void handleGetLogs(WebServer& server) {
    String fmt = server.hasArg("format") ? server.arg("format") : "json";
    size_t limit = server.hasArg("limit") ? (size_t)server.arg("limit").toInt() : 120;
    if (limit == 0 || limit > 500) limit = 120;

    String lvlStr = server.hasArg("level") ? server.arg("level") : "all";
    LogLevel minLvl = LOG_LVL_DEBUG;
    if (lvlStr == "info") minLvl = LOG_LVL_INFO;
    else if (lvlStr == "warn") minLvl = LOG_LVL_WARN;
    else if (lvlStr == "error") minLvl = LOG_LVL_ERROR;

    if (fmt == "text") {
      String txt = SystemLogger::getRecentLogsText(limit, minLvl);
      server.send(200, "text/plain; charset=utf-8", txt);
    } else {
      JsonDocument doc;
      SystemLogger::getLogsJson(doc, limit, minLvl);
      String out;
      serializeJson(doc, out);
      server.send(200, "application/json", out);
    }
  }

  void handleDownloadLogs(WebServer& server) {
    File logFile = SystemLogger::openCurrentLogFileForReading();
    if (logFile) {
      server.sendHeader("Content-Disposition", "attachment; filename=\"decor_esp32_syslog.log\"");
      server.streamFile(logFile, "text/plain");
      logFile.close();
    } else {
      String txt = SystemLogger::getRecentLogsText(1000, LOG_LVL_DEBUG);
      server.sendHeader("Content-Disposition", "attachment; filename=\"decor_esp32_ram_syslog.txt\"");
      server.send(200, "text/plain; charset=utf-8", txt);
    }
  }

  void handleGetLogStats(WebServer& server) {
    LogStats st = SystemLogger::getStats();
    JsonDocument doc;
    doc["total"] = st.totalLogged;
    doc["written_bytes"] = st.totalWrittenBytes;
    doc["written_kb"] = (uint32_t)(st.totalWrittenBytes / 1024);
    doc["debug"] = st.debugCount;
    doc["info"] = st.infoCount;
    doc["warn"] = st.warnCount;
    doc["error"] = st.errorCount;
    doc["fatal"] = st.fatalCount;
    doc["buffer_drops"] = st.bufferDrops;
    doc["pending_bytes"] = st.pendingBytes;
    doc["sd_active"] = st.sdLoggingActive;
    doc["current_file"] = st.currentLogFile;
    doc["sd_ready"] = SystemLogger::isSdReady();

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void handleGetLogFiles(WebServer& server) {
    JsonDocument doc;
    SystemLogger::getLogFileList(doc);
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void handlePostLogSnapshot(WebServer& server) {
    SystemLogger::logSystemSnapshot();
    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Snapshot da duoc ghi vao nhat ky he thong";
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void handlePostLogClear(WebServer& server) {
    bool deleteSd = server.hasArg("delete_sd") && (server.arg("delete_sd") == "1" || server.arg("delete_sd") == "true");
    bool ok = SystemLogger::clearLogs(deleteSd);
    JsonDocument doc;
    doc["success"] = ok;
    doc["deleted_sd"] = deleteSd;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  // --- 6. TELEMETRY 48H TRÊN PSRAM ---
  void handleGetTelemetryHistory(WebServer& server) {
    String range = server.hasArg("range") ? server.arg("range") : "24h";
    size_t maxPoints = server.hasArg("points") ? server.arg("points").toInt() : 60;
    if (maxPoints < 10) maxPoints = 10;
    if (maxPoints > 200) maxPoints = 200;

    JsonDocument doc;
    TelemetryManager::getHistoryJson(doc, range, maxPoints);

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void handleGetTelemetryStats(WebServer& server) {
    String range = server.hasArg("range") ? server.arg("range") : "24h";
    float minT = 0, maxT = 0, avgT = 0;
    int minH = 0, maxH = 0;
    float avgH = 0;
    TelemetryManager::getStats(range, minT, maxT, avgT, minH, maxH, avgH);

    JsonDocument doc;
    doc["status"] = "ok";
    doc["range"] = range;
    doc["minTemp"] = minT;
    doc["maxTemp"] = maxT;
    doc["avgTemp"] = avgT;
    doc["minHumidity"] = minH;
    doc["maxHumidity"] = maxH;
    doc["avgHumidity"] = avgH;
    doc["psramRecords"] = (uint32_t)TelemetryManager::getRecordCount();
    doc["psramCapacity"] = (uint32_t)TelemetryManager::getCapacity();
    doc["flashWritesSaved"] = TelemetryManager::getFlashWritesSaved();

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  // --- 7. GIÁM SÁT & DỌN DẸP RAM (MEMORY SCAVENGER) ---
  void handleMemoryStatus(WebServer& server) {
    JsonDocument doc;
    MemoryScavenger::getStatusJson(doc);
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void handleMemoryScavenge(WebServer& server) {
    ScavengeReport rep = MemoryScavenger::scavenge(true, "WEB_API");
    JsonDocument doc;
    doc["status"] = "ok";
    doc["freed_bytes"] = rep.totalFreedBytes;
    doc["freed_kb"] = rep.totalFreedBytes / 1024;
    doc["heap_freed"] = rep.heapFreedBytes;
    doc["psram_freed"] = rep.psramFreedBytes;
    doc["reason"] = rep.triggerReason;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["free_psram"] = ESP.getFreePsram();
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void handleMemoryConfig(WebServer& server) {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    if (doc.containsKey("strict_mode")) {
      uint8_t mode = doc["strict_mode"].as<uint8_t>();
      if (mode > 2) mode = 1;
      MemoryScavenger::setStrictMode((StrictModeLevel)mode);
    }
    JsonDocument resp;
    resp["status"] = "ok";
    resp["strict_mode"] = (uint8_t)MemoryScavenger::getStrictMode();
    resp["mode_name"] = MemoryScavenger::getStrictModeName();
    String out;
    serializeJson(resp, out);
    server.send(200, "application/json", out);
  }

  // --- 8. PHÁT THỬ ÂM THANH SYMBIAN S40 ---
  void handleAudioPlayTune(WebServer& server) {
    String tune = server.hasArg("tune") ? server.arg("tune") : "nokia";
    if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("tune")) tune = doc["tune"].as<String>();
    }
    if (tune == "nokia" || tune == "1")       TestAudio::playNokiaTune();
    else if (tune == "ok")                     TestAudio::playOkChime();
    else if (tune == "delete" || tune == "del") TestAudio::playDeleteChime();
    else if (tune == "sms" || tune == "2")     TestAudio::playSmsSpecialTone();
    else if (tune == "beep" || tune == "0")    TestAudio::playKeyBeep();
    else if (tune == "chime" || tune == "3")   TestAudio::playStartupChime();
    else TestAudio::playAlarmTuneStep(tune.toInt(), 0);

    JsonDocument res;
    res["success"] = true;
    res["played"] = tune;
    String out;
    serializeJson(res, out);
    server.send(200, "application/json", out);
  }

  // --- ĐĂNG KÝ ROUTE VÀO WEBSERVER ---
  void registerRoutes(WebServer& server) {
    server.on("/api/auth/status", HTTP_GET, [&server]() { handleAuthStatus(server); });
    server.on("/api/auth/login", HTTP_POST, [&server]() { handleAuthLogin(server); });
    server.on("/api/auth/logout", HTTP_POST, [&server]() { handleAuthLogout(server); });
    server.on("/api/auth/change_password", HTTP_POST, [&server]() { handleAuthChangePassword(server); });
    server.on("/api/system/factory_reset", HTTP_POST, [&server]() { handleFactoryReset(server); });

    server.on("/api/system/status", HTTP_GET, [&server]() { handleSystemStatus(server); });
    server.on("/api/status", HTTP_GET, [&server]() { handleSystemStatus(server); });
    server.on("/api/system_info", HTTP_GET, [&server]() { handleSystemInfo(server); });

    server.on("/api/ota_check", HTTP_GET, [&server]() { handleOtaCheck(server); });
    server.on("/api/ota_start", HTTP_POST, [&server]() { handleOtaStart(server); });
    server.on("/api/ota_status", HTTP_GET, [&server]() { handleOtaStatus(server); });
    server.on("/update", HTTP_POST, [&server]() { handleLocalUpdate(server); }, [&server]() { handleLocalUpdateUpload(server); });

    server.on("/api/logs", HTTP_GET, [&server]() { handleGetLogs(server); });
    server.on("/api/logs/download", HTTP_GET, [&server]() { handleDownloadLogs(server); });
    server.on("/api/logs/stats", HTTP_GET, [&server]() { handleGetLogStats(server); });
    server.on("/api/logs/files", HTTP_GET, [&server]() { handleGetLogFiles(server); });
    server.on("/api/logs/snapshot", HTTP_POST, [&server]() { handlePostLogSnapshot(server); });
    server.on("/api/logs/clear", HTTP_POST, [&server]() { handlePostLogClear(server); });

    server.on("/api/telemetry/history", HTTP_GET, [&server]() { handleGetTelemetryHistory(server); });
    server.on("/api/telemetry/stats", HTTP_GET, [&server]() { handleGetTelemetryStats(server); });

    server.on("/api/memory/status", HTTP_GET, [&server]() { handleMemoryStatus(server); });
    server.on("/api/memory/scavenge", HTTP_POST, [&server]() { handleMemoryScavenge(server); });
    server.on("/api/memory/config", HTTP_POST, [&server]() { handleMemoryConfig(server); });

    server.on("/api/audio/play_tune", HTTP_POST, [&server]() { handleAudioPlayTune(server); });
  }

} // namespace ApiSystem

