#include <Arduino.h>
#include "Config.h"
#include "HardwareManager.h"
#include "WifiManager.h"
#include "XiaoZhiClient.h"
#include "ImageManager.h"
#include "WebRoutes.h"
#include "OtaManager.h"
#include "tests/ModuleTestRunner.h"
#include "tests/TestSpeakerMAX98357A.h"
#include "CobLedController.h"
#include "WebStreamManager.h"
#include "WeatherManager.h"
#include "TelemetryManager.h"
#include "TaskProfiler.h"
#include "SystemLogger.h"
#include "TimeManager.h"

void setup() {
  Serial.begin(115200);
  //pinMode(1, OUTPUT);
  
  // Chờ kết nối Serial trong tối đa 3 giây
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 3000)) {
    delay(10);
  }
  delay(500); // Đệm ổn định thêm 0.5s
  Serial.println("-----Bắt đầu khởi động Trạm Decor Vũ Trụ-----");

  // 1. Khởi tạo phần cứng (PSRAM, LittleFS, LED RGB, Tệp cấu hình mặc định)
  HardwareManager::init();

  // 1-Log. Khởi tạo Trình Ghi Nhật Ký Thẻ Nhớ SD & Bộ Đệm Octal PSRAM (Smartphone-Style Syslog)
  SystemLogger::init();

  // 1A. Khởi tạo 2 Thanh LED COB Nước Chảy Sao Băng (GPIO 18 & 38 - Digital HIGH)
  CobLeds::init(true);

  // 1B. Khởi tạo OTA (Xác nhận firmware hợp lệ & hủy cờ rollback)
  OtaManager::init();

  // 1C. Khởi tạo Trình quản lý Stream Đa Phương Tiện (Web Video & Audio)
  WebStreamManager::init();

  // 1D. Cấp phát 1MB PSRAM Circular Buffer lưu Telemetry 48h & Kích hoạt FlashGuard
  TelemetryManager::init();

  // 1E. Khởi tạo Trình quản lý Thời tiết Real-Time (Open-Meteo & Geolocation)
  WeatherManager::init();

  // 1F. Khởi tạo bộ đo đạc hiệu năng vòng lặp chính (Task Profiler)
  TaskProfiler::init();

  // 1G. Khởi tạo Trình quản lý Thời gian Thực (SNTP tức thì GMT+7)
  TimeManager::init();

  // 2. Khởi chạy Hệ thống 4 Màn hình ST7789 + Mic INMP441 + Cảm biến SHT31 + 7 Phím Bấm DIO
  ModuleTestRunner::init();

  // 3. Khởi tạo Wi-Fi (SoftAP cứu hộ & Tự động kết nối Smart Wi-Fi)
  WifiManager::begin();

  // 3B. Khởi động bộ thu Audio UDP Port 12345 (sau khi Wi-Fi đã bắt đầu)
  WebStreamManager::startAudioUdpListener();

  // 4. Khởi tạo Web Server & Toàn bộ REST APIs đồng bộ thời gian thực với Web UI
  WebRoutes::begin();

  // 5. Nếu đã kết nối Wi-Fi, tự động kiểm tra OTA và thông tin kích hoạt XiaoZhi Cloud
  if (WifiManager::isConnected()) {
    TimeManager::onWifiConnected();
    Serial.println("🌐 Đang kiểm tra OTA và lấy thông tin xác thực từ XiaoZhi Cloud...");
    XiaoZhiClient::queryOTA(true);
    WeatherManager::refreshNow();
  }
}

void loop() {
  TaskProfiler::beginLoop();

  // Vòng lặp phát hiện đồng bộ giờ thực tế trên Core 1
  TimeManager::loop();

  // 1. Vòng lặp cập nhật 4 Màn hình ST7789, Mic INMP441, Cảm biến SHT31 & 7 Phím DIO
  TaskProfiler::start(PROF_MODULES);
  ModuleTestRunner::loop();
  TaskProfiler::stop(PROF_MODULES);

  // 1A. Cập nhật chu trình xung đổi chế độ non-blocking cho 2 Thanh LED COB
  TaskProfiler::start(PROF_COB_LEDS);
  CobLeds::update();
  TaskProfiler::stop(PROF_COB_LEDS);

  // 1B. Thu thập dữ liệu Telemetry vào 1MB PSRAM và cập nhật FlashGuard
  TaskProfiler::start(PROF_TELEMETRY);
  TelemetryManager::loop();
  TaskProfiler::stop(PROF_TELEMETRY);

  // 1C. Vòng lặp định kỳ cập nhật Thời tiết Real-time
  TaskProfiler::start(PROF_WEATHER);
  WeatherManager::loop();
  TaskProfiler::stop(PROF_WEATHER);

  // 2. Xử lý DNS Captive Portal & các HTTP request từ giao diện Web
  TaskProfiler::start(PROF_WIFI);
  WifiManager::loop();
  TaskProfiler::stop(PROF_WIFI);

  TaskProfiler::start(PROF_WEB_ROUTES);
  WebRoutes::handleClient();
  TaskProfiler::stop(PROF_WEB_ROUTES);

  TaskProfiler::start(PROF_WEB_STREAM);
  WebStreamManager::loop();
  TaskProfiler::stop(PROF_WEB_STREAM);

  // 2B. Duy trì kết nối WebSocket nền tới XiaoZhi Cloud (giữ Online trên Hub)
  TaskProfiler::start(PROF_XIAOZHI);
  XiaoZhiClient::loopWebSocket();
  TaskProfiler::stop(PROF_XIAOZHI);

  // 3. Cập nhật trạng thái phần cứng, LED RGB, nhịp tim và lệnh Serial
  TaskProfiler::start(PROF_HARDWARE);
  HardwareManager::updateLedStatus();
  HardwareManager::heartbeat();
  HardwareManager::handleSerialCommands();
  TaskProfiler::stop(PROF_HARDWARE);

  // 4. Xả bộ đệm nhật ký hệ thống xuống Thẻ Nhớ SD (Non-blocking Flush & Auto-Rotation)
  TaskProfiler::start(PROF_LOGGER);
  SystemLogger::loop();
  TaskProfiler::stop(PROF_LOGGER);

  TaskProfiler::endLoop();
}