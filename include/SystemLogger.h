#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>

enum LogLevel : uint8_t {
  LOG_LVL_DEBUG = 0,
  LOG_LVL_INFO  = 1,
  LOG_LVL_WARN  = 2,
  LOG_LVL_ERROR = 3,
  LOG_LVL_FATAL = 4
};

struct LogStats {
  uint32_t totalLogged;
  uint32_t totalWrittenBytes;
  uint32_t debugCount;
  uint32_t infoCount;
  uint32_t warnCount;
  uint32_t errorCount;
  uint32_t fatalCount;
  uint32_t bufferDrops;
  uint32_t pendingBytes;
  bool     sdLoggingActive;
  String   currentLogFile;
};

class SystemLogger {
public:
  // Khởi tạo bộ đệm PSRAM 128KB và hệ thống ghi nhật ký
  static void init();

  // Vòng lặp xả bộ đệm xuống thẻ SD và kiểm tra xoay vòng tệp
  static void loop();

  // Ghi nhật ký có định dạng (printf-style)
  static void log(LogLevel level, const char* tag, const char* format, ...) __attribute__((format(printf, 3, 4)));

  // Ghi nhật ký chuỗi thô
  static void logRaw(LogLevel level, const char* tag, const char* message);

  // Ghi nhật ký ảnh chụp trạng thái toàn diện (Snapshot) của hệ thống
  static void logSystemSnapshot();

  // Trạng thái hệ thống ghi log
  static bool isSdReady();
  static size_t getBufferedBytes();
  static LogStats getStats();

  // Xuất log phục vụ Web REST API & Giao diện người dùng
  static String getRecentLogsText(size_t maxLines = 150, LogLevel minLevel = LOG_LVL_DEBUG);
  static void getLogsJson(JsonDocument& doc, size_t maxLines = 100, LogLevel minLevel = LOG_LVL_DEBUG);
  static void getLogFileList(JsonDocument& doc);
  static File openCurrentLogFileForReading();
  static bool clearLogs(bool deleteSdFiles = false);

  // Cấu hình
  static void setLogLevel(LogLevel minLevel);
  static LogLevel getLogLevel();
  static void setEchoToSerial(bool enable);
  static bool isEchoToSerial();

  // Lấy chuỗi định dạng thời gian hiện tại
  static String getFormattedTime();

  // Xả dữ liệu nhật ký đệm xuống thẻ SD ngay lập tức
  static void flush();

private:
  friend void sdLoggerTask(void* param);
  static void flushBufferToSd();
  static void checkLogRotation();
  static void ensureLogDirExists();
  static String resolveActiveLogFilePath();
};

// Các macro tiện ích toàn cục (Sử dụng khắp toàn bộ firmware)
#define LOG_D(tag, fmt, ...) SystemLogger::log(LOG_LVL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) SystemLogger::log(LOG_LVL_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) SystemLogger::log(LOG_LVL_WARN,  tag, fmt, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) SystemLogger::log(LOG_LVL_ERROR, tag, fmt, ##__VA_ARGS__)
#define LOG_F(tag, fmt, ...) SystemLogger::log(LOG_LVL_FATAL, tag, fmt, ##__VA_ARGS__)

