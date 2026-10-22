#include "SystemLogger.h"
#include "tests/TestSDCard.h"
#include "tests/TestDisplay.h"
#include "tests/TestSensors.h"
#include "tests/TestAudio.h"
#include "tests/TestWindmill.h"
#include "CobLedController.h"
#include "HardwareManager.h"
#include "WifiManager.h"
#include "WeatherManager.h"
#include "TaskProfiler.h"
#include "Config.h"

#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <esp_timer.h>

// Cấu hình kích thước bộ đệm và tệp log
#define LOG_RING_BUFFER_PSRAM_SIZE  (131072) // 128 KB trong Octal PSRAM (~1500 dòng log)
#define LOG_RING_BUFFER_FALLBACK    (16384)  // 16 KB trong SRAM nếu không có PSRAM
#define LOG_MAX_FILE_SIZE           (524288) // 512 KB mỗi tệp log
#define LOG_MAX_TOTAL_DIR_SIZE      (20971520) // 20 MB giới hạn toàn thư mục /logs
#define LOG_FLUSH_INTERVAL_MS       (2500)   // Xả bộ đệm định kỳ mỗi 2.5 giây
#define LOG_SNAPSHOT_INTERVAL_MS    (600000) // Tự động ghi ảnh chụp trạng thái toàn hệ thống mỗi 10 phút

static char*    s_ringBuffer = nullptr;
static size_t   s_bufferCapacity = 0;
static size_t   s_head = 0;
static size_t   s_historyCount = 0;    // Số byte lịch sử đang lưu trong buffer (0 .. s_bufferCapacity)
static size_t   s_sdFlushTail = 0;     // Vị trí con trỏ đọc phục vụ việc xả dữ liệu xuống thẻ SD
static size_t   s_pendingSdBytes = 0;  // Số byte đang chờ ghi xuống thẻ SD
static uint32_t s_logSeqId = 0;
static portMUX_TYPE s_logMux = portMUX_INITIALIZER_UNLOCKED;

static LogLevel s_minLevel = LOG_LVL_DEBUG;
static bool     s_echoToSerial = true;
static bool     s_needsImmediateFlush = false;

static unsigned long s_lastFlushMs = 0;
static unsigned long s_lastSnapshotMs = 0;
static LogStats s_stats = {0, 0, 0, 0, 0, 0, 0, 0, 0, false, ""};

// Bảng tên cấp độ log chuẩn Smartphone
static const char* LEVEL_NAMES[] = { "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };
static const char LEVEL_CHARS[] = { 'D', 'I', 'W', 'E', 'F' };

void SystemLogger::init() {
  if (s_ringBuffer != nullptr) return;

  // Cấp phát trong Octal PSRAM
  if (psramFound()) {
    s_ringBuffer = (char*)ps_malloc(LOG_RING_BUFFER_PSRAM_SIZE);
    if (s_ringBuffer) {
      s_bufferCapacity = LOG_RING_BUFFER_PSRAM_SIZE;
    }
  }

  // Dự phòng SRAM
  if (!s_ringBuffer) {
    s_ringBuffer = (char*)malloc(LOG_RING_BUFFER_FALLBACK);
    if (s_ringBuffer) {
      s_bufferCapacity = LOG_RING_BUFFER_FALLBACK;
    }
  }

  s_head = 0;
  s_historyCount = 0;
  s_sdFlushTail = 0;
  s_pendingSdBytes = 0;
  s_logSeqId = 0;
  s_lastFlushMs = millis();
  s_lastSnapshotMs = millis();

  // Dòng nhật ký khởi động đầu tiên
  log(LOG_LVL_INFO, "SYS", "=======================================================");
  log(LOG_LVL_INFO, "SYS", "🚀 SPACE OS LOG ENGINE KHOI DONG - BO DEM: %u KB (%s)",
      (unsigned)(s_bufferCapacity / 1024), psramFound() ? "8MB Octal PSRAM" : "Internal SRAM");
  log(LOG_LVL_INFO, "SYS", "=======================================================");
}

String SystemLogger::getFormattedTime() {
  time_t now = time(nullptr);
  uint32_t nowMs = millis();

  char timeStr[32];
  if (now >= 1700000000) { // Đã đồng bộ NTP (năm 2023 trở lên)
    struct tm ti;
    localtime_r(&now, &ti);
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d.%03u",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec, (unsigned)(nowMs % 1000));
  } else { // Chưa đồng bộ NTP -> dùng thời gian Uptime
    uint32_t sec = nowMs / 1000;
    uint32_t hr = sec / 3600;
    uint32_t mn = (sec % 3600) / 60;
    uint32_t sc = sec % 60;
    snprintf(timeStr, sizeof(timeStr), "[UP:%02u:%02u:%02u.%03u]",
             (unsigned)hr, (unsigned)mn, (unsigned)sc, (unsigned)(nowMs % 1000));
  }
  return String(timeStr);
}

void SystemLogger::log(LogLevel level, const char* tag, const char* format, ...) {
  if (level < s_minLevel) return;

  char msgBuf[384];
  va_list args;
  va_start(args, format);
  vsnprintf(msgBuf, sizeof(msgBuf), format, args);
  va_end(args);

  logRaw(level, tag, msgBuf);
}

void SystemLogger::logRaw(LogLevel level, const char* tag, const char* message) {
  if (level < s_minLevel) return;

  String timeTag = getFormattedTime();
  char lvlChar = (level <= LOG_LVL_FATAL) ? LEVEL_CHARS[level] : 'I';

  // Định dạng dòng log: TIME [LVL] [TAG] Message\r\n
  char lineBuf[512];
  int lineLen = snprintf(lineBuf, sizeof(lineBuf), "%s [%c] [%s] %s\r\n",
                         timeTag.c_str(), lvlChar, tag ? tag : "SYS", message ? message : "");
  if (lineLen <= 0) return;
  if (lineLen >= (int)sizeof(lineBuf)) lineLen = sizeof(lineBuf) - 1;

  // 1. In ra Serial Monitor
  if (s_echoToSerial) {
    if (level == LOG_LVL_ERROR || level == LOG_LVL_FATAL) {
      Serial.printf("\033[1;31m%s\033[0m", lineBuf); // Đỏ nổi bật
    } else if (level == LOG_LVL_WARN) {
      Serial.printf("\033[1;33m%s\033[0m", lineBuf); // Vàng cảnh báo
    } else if (level == LOG_LVL_DEBUG) {
      Serial.printf("\033[0;36m%s\033[0m", lineBuf); // Xanh nhạt
    } else {
      Serial.print(lineBuf);
    }
  }

  // 2. Đẩy vào Ring Buffer an toàn luồng
  if (s_ringBuffer && s_bufferCapacity > 0) {
    portENTER_CRITICAL(&s_logMux);

    for (int i = 0; i < lineLen; i++) {
      s_ringBuffer[s_head] = lineBuf[i];
      s_head = (s_head + 1) % s_bufferCapacity;

      if (s_historyCount < s_bufferCapacity) {
        s_historyCount++;
      }

      if (s_pendingSdBytes < s_bufferCapacity) {
        s_pendingSdBytes++;
      } else {
        // SD chưa kịp ghi mà buffer đã xoay vòng đủ 128KB: đẩy con trỏ SD tiến lên
        s_sdFlushTail = (s_sdFlushTail + 1) % s_bufferCapacity;
        s_stats.bufferDrops++;
      }
    }

    s_logSeqId++;
    s_stats.totalLogged++;
    if (level == LOG_LVL_DEBUG) s_stats.debugCount++;
    else if (level == LOG_LVL_INFO) s_stats.infoCount++;
    else if (level == LOG_LVL_WARN) s_stats.warnCount++;
    else if (level == LOG_LVL_ERROR) s_stats.errorCount++;
    else if (level == LOG_LVL_FATAL) s_stats.fatalCount++;

    portEXIT_CRITICAL(&s_logMux);
  }

  // Nếu là lỗi nghiêm trọng, kích hoạt xả ngay xuống thẻ nhớ
  if (level >= LOG_LVL_ERROR) {
    s_needsImmediateFlush = true;
  }
}

void SystemLogger::loop() {
  unsigned long now = millis();

  // 1. Tự động ghi ảnh chụp trạng thái toàn hệ thống định kỳ (Snapshot)
  if (now - s_lastSnapshotMs >= LOG_SNAPSHOT_INTERVAL_MS) {
    s_lastSnapshotMs = now;
    logSystemSnapshot();
  }

  // 2. Xả bộ đệm xuống thẻ SD nếu có dữ liệu chờ
  if (s_pendingSdBytes >= 1024 || (now - s_lastFlushMs >= LOG_FLUSH_INTERVAL_MS) || s_needsImmediateFlush) {
    s_lastFlushMs = now;
    s_needsImmediateFlush = false;
    flushBufferToSd();
  }
}

void SystemLogger::ensureLogDirExists() {
  if (!TestSDCard::isMounted()) return;
  if (!SD.exists("/logs")) {
    SD.mkdir("/logs");
  }
}

String SystemLogger::resolveActiveLogFilePath() {
  time_t now = time(nullptr);
  char pathBuf[48];

  if (now >= 1700000000) {
    struct tm ti;
    localtime_r(&now, &ti);
    snprintf(pathBuf, sizeof(pathBuf), "/logs/sys_%04d%02d%02d.log",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  } else {
    snprintf(pathBuf, sizeof(pathBuf), "/logs/sys_boot.log");
  }
  return String(pathBuf);
}

void SystemLogger::checkLogRotation() {
  if (!TestSDCard::isMounted()) return;
  String currentPath = resolveActiveLogFilePath();

  if (SD.exists(currentPath)) {
    File f = SD.open(currentPath, FILE_READ);
    if (f) {
      size_t sz = f.size();
      f.close();

      // Nếu vượt ngưỡng tối đa 512 KB -> xoay vòng tệp sang _01, _02...
      if (sz >= LOG_MAX_FILE_SIZE) {
        for (int i = 1; i <= 20; i++) {
          char rotBuf[64];
          int dotIdx = currentPath.lastIndexOf('.');
          String base = (dotIdx > 0) ? currentPath.substring(0, dotIdx) : currentPath;
          snprintf(rotBuf, sizeof(rotBuf), "%s_%02d.log", base.c_str(), i);
          if (!SD.exists(rotBuf)) {
            SD.rename(currentPath, rotBuf);
            log(LOG_LVL_INFO, "SD", "Tệp log đạt %u KB -> Đã xoay vòng sang %s",
                (unsigned)(sz / 1024), rotBuf);
            break;
          }
        }
      }
    }
  }
}

void SystemLogger::flushBufferToSd() {
  if (!TestSDCard::isMounted() || !s_ringBuffer || s_pendingSdBytes == 0) return;

  ensureLogDirExists();
  checkLogRotation();

  String logPath = resolveActiveLogFilePath();
  File f = SD.open(logPath, FILE_APPEND);
  if (!f) {
    s_stats.sdLoggingActive = false;
    return;
  }

  char chunk[2048];
  size_t bytesToFlush = 0;

  portENTER_CRITICAL(&s_logMux);
  bytesToFlush = (s_pendingSdBytes < sizeof(chunk)) ? s_pendingSdBytes : sizeof(chunk);
  for (size_t i = 0; i < bytesToFlush; i++) {
    chunk[i] = s_ringBuffer[s_sdFlushTail];
    s_sdFlushTail = (s_sdFlushTail + 1) % s_bufferCapacity;
  }
  s_pendingSdBytes -= bytesToFlush;
  portEXIT_CRITICAL(&s_logMux);

  if (bytesToFlush > 0) {
    size_t written = f.write((const uint8_t*)chunk, bytesToFlush);
    f.flush();
    s_stats.totalWrittenBytes += written;
    s_stats.sdLoggingActive = true;
    s_stats.currentLogFile = logPath;
  }
  f.close();
}

void SystemLogger::logSystemSnapshot() {
  uint32_t nowSec = millis() / 1000;
  uint32_t days = nowSec / 86400;
  uint32_t hours = (nowSec % 86400) / 3600;
  uint32_t mins = (nowSec % 3600) / 60;
  uint32_t secs = nowSec % 60;

  bool wifiConn = (WiFi.status() == WL_CONNECTED);
  String wifiMode = (WiFi.getMode() == WIFI_AP) ? "AP MODE" : (wifiConn ? "STA CONNECTED" : "DISCONNECTED");
  String ipStr = wifiConn ? WiFi.localIP().toString() : (WiFi.getMode() == WIFI_AP ? "192.168.4.1" : "0.0.0.0");
  int rssi = wifiConn ? WiFi.RSSI() : -100;
  String ssid = wifiConn ? WifiManager::getCurrentSsid() : "N/A";

  float temp = TestSensors::getTemperatureC();
  float hum = TestSensors::getHumidityPct();
  float vBat = TestSensors::getBatteryVoltage();
  int batPct = TestSensors::getBatteryPercentage();
  bool chg = TestSensors::isCharging();

  uint8_t curMode = TestDisplay::getCurrentMode();
  String modeName = TestDisplay::getScreenModeName();

  log(LOG_LVL_INFO, "SNAPSHOT", "================ [NHAT KY TRANG THAI HE THONG] ================");
  log(LOG_LVL_INFO, "SNAPSHOT", "Thoi gian Uptime : %ud %02uh %02um %02us | Loop FPS: %.1f | Mode: %u (%s)",
      days, hours, mins, secs, TaskProfiler::getFps(), curMode, modeName.c_str());
  log(LOG_LVL_INFO, "SNAPSHOT", "Heap RAM (KB)    : Free=%u | MinFree=%u | MaxBlock=%u | Failures=%u",
      HardwareManager::getFreeHeapKb(), HardwareManager::getMinFreeHeapKb(),
      HardwareManager::getMaxAllocHeapKb(), HardwareManager::getAllocFailures());
  log(LOG_LVL_INFO, "SNAPSHOT", "Octal PSRAM (MB) : Free=%.2f | MinFree=%.2f | MaxBlock=%u KB",
      HardwareManager::getFreePsramMb(), HardwareManager::getMinFreePsramMb(),
      HardwareManager::getMaxAllocPsramKb());
  log(LOG_LVL_INFO, "SNAPSHOT", "Mang Wi-Fi       : %s | SSID: \"%s\" | IP: %s | RSSI: %d dBm",
      wifiMode.c_str(), ssid.c_str(), ipStr.c_str(), rssi);
  log(LOG_LVL_INFO, "SNAPSHOT", "Cam bien & Nguon : SHT31: %.1f*C, %.1f%% | Pin: %.2fV (%d%%) | Sac: %s",
      temp, hum, vBat, batPct, chg ? "DANG SAC" : "PIN");
  log(LOG_LVL_INFO, "SNAPSHOT", "The nho Micro SD : %s | Kich thuoc: %u MB | Da dung: %u MB | File: %d",
      TestSDCard::isMounted() ? "DA KET NOI (OK)" : "CHUA KET NOI",
      (unsigned)TestSDCard::getCardSizeMB(), (unsigned)TestSDCard::getUsedMB(), TestSDCard::getFileCount());
  log(LOG_LVL_INFO, "SNAPSHOT", "Module Ngoai vi  : COB LED: %s (Mode %u) | Coi xay: %u%% | Edison: %u%%",
      CobLeds::isOn() ? "BAT" : "TAT", CobLeds::getCurrentMode(),
      TestWindmill::getMotorSpeed(), TestWindmill::getEdisonBrightness());
  log(LOG_LVL_INFO, "SNAPSHOT", "Nhat ky (Stats)  : Tong=%u | Da ghi SD=%u KB | Loi=%u | Canh bao=%u",
      s_stats.totalLogged, (unsigned)(s_stats.totalWrittenBytes / 1024),
      s_stats.errorCount, s_stats.warnCount);
  char profBuf[160];
  snprintf(profBuf, sizeof(profBuf),
           "Mod:%uus | Cob:%uus | Tele:%uus | Wthr:%uus | WiFi:%uus | Web:%uus | Strm:%uus | XZ:%uus | Hw:%uus | Log:%uus",
           TaskProfiler::getStats(PROF_MODULES).avgUs,
           TaskProfiler::getStats(PROF_COB_LEDS).avgUs,
           TaskProfiler::getStats(PROF_TELEMETRY).avgUs,
           TaskProfiler::getStats(PROF_WEATHER).avgUs,
           TaskProfiler::getStats(PROF_WIFI).avgUs,
           TaskProfiler::getStats(PROF_WEB_ROUTES).avgUs,
           TaskProfiler::getStats(PROF_WEB_STREAM).avgUs,
           TaskProfiler::getStats(PROF_XIAOZHI).avgUs,
           TaskProfiler::getStats(PROF_HARDWARE).avgUs,
           TaskProfiler::getStats(PROF_LOGGER).avgUs);
  log(LOG_LVL_INFO, "SNAPSHOT", "Hieu nang Tasks  : %s", profBuf);

  for (int p = 0; p < PROF_TASK_COUNT; p++) {
    const TaskStats& ts = TaskProfiler::getStats((ProfiledTask)p);
    if (ts.slowCount > 0) {
      log(LOG_LVL_WARN, "PROFILER", "Task '%s' cham >10ms (%u lan, max: %u us)",
          ts.name, ts.slowCount, ts.maxUs);
    }
  }
  TaskProfiler::resetMax();
  log(LOG_LVL_INFO, "SNAPSHOT", "================================================================");
}

bool SystemLogger::isSdReady() {
  return TestSDCard::isMounted();
}

size_t SystemLogger::getBufferedBytes() {
  return s_pendingSdBytes;
}

LogStats SystemLogger::getStats() {
  s_stats.pendingBytes = s_pendingSdBytes;
  return s_stats;
}

String SystemLogger::getRecentLogsText(size_t maxLines, LogLevel minLevel) {
  if (!s_ringBuffer || s_bufferCapacity == 0 || s_historyCount == 0) {
    if (TestSDCard::isMounted()) {
      String p = resolveActiveLogFilePath();
      if (SD.exists(p)) {
        File f = SD.open(p, FILE_READ);
        if (f) {
          size_t sz = f.size();
          size_t rd = (sz > 8192) ? 8192 : sz;
          if (sz > rd) f.seek(sz - rd);
          String content = f.readString();
          f.close();
          return content;
        }
      }
    }
    return "Chua co log.";
  }

  String result = "";
  result.reserve(8192);

  portENTER_CRITICAL(&s_logMux);

  size_t historyStart = (s_head >= s_historyCount) ? (s_head - s_historyCount) : (s_bufferCapacity - (s_historyCount - s_head));
  size_t idx = historyStart;

  char line[512];
  size_t linePos = 0;

  for (size_t i = 0; i < s_historyCount; i++) {
    char c = s_ringBuffer[idx];
    idx = (idx + 1) % s_bufferCapacity;

    if (linePos < sizeof(line) - 1) {
      line[linePos++] = c;
    }

    if (c == '\n') {
      line[linePos] = '\0';
      result += line;
      linePos = 0;
    }
  }

  portEXIT_CRITICAL(&s_logMux);
  return result;
}

void SystemLogger::getLogsJson(JsonDocument& doc, size_t maxLines, LogLevel minLevel) {
  JsonArray arr = doc["logs"].to<JsonArray>();

  // 1. Kiểm tra nếu có dữ liệu trong bộ đệm PSRAM
  if (s_ringBuffer && s_bufferCapacity > 0 && s_historyCount > 0) {
    portENTER_CRITICAL(&s_logMux);

    size_t historyStart = (s_head >= s_historyCount) ? (s_head - s_historyCount) : (s_bufferCapacity - (s_historyCount - s_head));
    size_t idx = historyStart;

    char line[512];
    size_t linePos = 0;
    size_t totalLines = 0;

    // Đếm tổng số dòng
    for (size_t i = 0; i < s_historyCount; i++) {
      if (s_ringBuffer[idx] == '\n') totalLines++;
      idx = (idx + 1) % s_bufferCapacity;
    }

    size_t skipLines = (totalLines > maxLines) ? (totalLines - maxLines) : 0;
    size_t currentLineIdx = 0;

    idx = historyStart;
    for (size_t i = 0; i < s_historyCount; i++) {
      char c = s_ringBuffer[idx];
      idx = (idx + 1) % s_bufferCapacity;

      if (linePos < sizeof(line) - 1) {
        line[linePos++] = c;
      }

      if (c == '\n') {
        while (linePos > 0 && (line[linePos - 1] == '\r' || line[linePos - 1] == '\n')) {
          line[--linePos] = '\0';
        }

        if (currentLineIdx >= skipLines && linePos > 0) {
          bool keep = true;
          if (minLevel > LOG_LVL_DEBUG) {
            if (minLevel == LOG_LVL_INFO && strstr(line, " [D] ")) keep = false;
            else if (minLevel == LOG_LVL_WARN && (strstr(line, " [D] ") || strstr(line, " [I] "))) keep = false;
            else if (minLevel >= LOG_LVL_ERROR && !strstr(line, " [E] ") && !strstr(line, " [F] ")) keep = false;
          }

          if (keep) {
            JsonObject item = arr.add<JsonObject>();
            item["line"] = String(line);
          }
        }
        currentLineIdx++;
        linePos = 0;
      }
    }

    portEXIT_CRITICAL(&s_logMux);
  } else if (TestSDCard::isMounted()) {
    // 2. Dự phòng: nếu bộ đệm PSRAM chưa có log, đọc phần đuôi từ thẻ SD
    String logPath = resolveActiveLogFilePath();
    if (SD.exists(logPath)) {
      File f = SD.open(logPath, FILE_READ);
      if (f) {
        size_t sz = f.size();
        size_t readSz = (sz > 8192) ? 8192 : sz;
        if (sz > readSz) f.seek(sz - readSz);

        while (f.available() && arr.size() < maxLines) {
          String l = f.readStringUntil('\n');
          l.trim();
          if (l.length() > 0) {
            bool keep = true;
            if (minLevel > LOG_LVL_DEBUG) {
              if (minLevel == LOG_LVL_INFO && l.indexOf(" [D] ") >= 0) keep = false;
              else if (minLevel == LOG_LVL_WARN && (l.indexOf(" [D] ") >= 0 || l.indexOf(" [I] ") >= 0)) keep = false;
              else if (minLevel >= LOG_LVL_ERROR && l.indexOf(" [E] ") < 0 && l.indexOf(" [F] ") < 0) keep = false;
            }
            if (keep) {
              JsonObject item = arr.add<JsonObject>();
              item["line"] = l;
            }
          }
        }
        f.close();
      }
    }
  }

  doc["total"] = s_stats.totalLogged;
  doc["pending_bytes"] = s_pendingSdBytes;
  doc["sd_active"] = s_stats.sdLoggingActive;
  doc["file"] = s_stats.currentLogFile.length() > 0 ? s_stats.currentLogFile : resolveActiveLogFilePath();
  doc["last_id"] = s_logSeqId;
}

void SystemLogger::getLogFileList(JsonDocument& doc) {
  JsonArray arr = doc["files"].to<JsonArray>();
  if (!TestSDCard::isMounted()) return;

  File root = SD.open("/logs");
  if (!root || !root.isDirectory()) return;

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      JsonObject item = arr.add<JsonObject>();
      String name = String(file.name());
      if (name.startsWith("/")) name = name.substring(1);
      item["name"] = name;
      item["size"] = (uint32_t)file.size();
    }
    file = root.openNextFile();
  }
  root.close();
}

File SystemLogger::openCurrentLogFileForReading() {
  if (TestSDCard::isMounted()) {
    String p = resolveActiveLogFilePath();
    if (SD.exists(p)) {
      return SD.open(p, FILE_READ);
    }
  }
  return File();
}

bool SystemLogger::clearLogs(bool deleteSdFiles) {
  portENTER_CRITICAL(&s_logMux);
  s_head = 0;
  s_historyCount = 0;
  s_sdFlushTail = 0;
  s_pendingSdBytes = 0;
  s_stats.bufferDrops = 0;
  portEXIT_CRITICAL(&s_logMux);

  if (deleteSdFiles && TestSDCard::isMounted()) {
    File root = SD.open("/logs");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        String p = "/logs/" + String(file.name());
        if (p.startsWith("//")) p = p.substring(1);
        file.close();
        SD.remove(p);
        file = root.openNextFile();
      }
      root.close();
    }
  }

  log(LOG_LVL_INFO, "SYS", "Da xoa toan bo nhat ky hoat dong theo yeu cau");
  return true;
}

void SystemLogger::setLogLevel(LogLevel minLevel) {
  s_minLevel = minLevel;
}

LogLevel SystemLogger::getLogLevel() {
  return s_minLevel;
}

void SystemLogger::setEchoToSerial(bool enable) {
  s_echoToSerial = enable;
}

bool SystemLogger::isEchoToSerial() {
  return s_echoToSerial;
}
