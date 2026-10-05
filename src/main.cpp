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

  // 1B. Khởi tạo OTA (Xác nhận firmware hợp lệ & hủy cờ rollback)
  OtaManager::init();

  // 2. Khởi chạy Hệ thống 4 Màn hình ST7789 + Mic INMP441 + Cảm biến SHT31 + 7 Phím Bấm DIO
  ModuleTestRunner::init();

  // 3. Khởi tạo Wi-Fi (SoftAP cứu hộ & Tự động kết nối Smart Wi-Fi)
  WifiManager::begin();

  // 4. Khởi tạo Web Server & Toàn bộ REST APIs đồng bộ thời gian thực với Web UI
  WebRoutes::begin();

  // 5. Nếu đã kết nối Wi-Fi, tự động kiểm tra OTA và thông tin kích hoạt XiaoZhi Cloud
  if (WifiManager::isConnected()) {
    Serial.println("🌐 Đang kiểm tra OTA và lấy thông tin xác thực từ XiaoZhi Cloud...");
    XiaoZhiClient::queryOTA(true);
  }
}

void loop() {
  // 1. Vòng lặp cập nhật 4 Màn hình ST7789, Mic INMP441, Cảm biến SHT31 & 7 Phím DIO
  ModuleTestRunner::loop();

  // 2. Xử lý DNS Captive Portal & các HTTP request từ giao diện Web
  WifiManager::loop();
  WebRoutes::handleClient();

  // 2B. Duy trì kết nối WebSocket nền tới XiaoZhi Cloud (giữ Online trên Hub)
  XiaoZhiClient::loopWebSocket();

  // 3. Nhấp nháy đèn LED RGB chỉ báo trạng thái kết nối
  HardwareManager::updateLedStatus();

  // 4. In nhịp tim hệ thống định kỳ 5 giây qua Serial
  HardwareManager::heartbeat();

  // 5. Lắng nghe phím tắt điều khiển nhanh từ Serial Monitor ('1','2','3','4','s','t','b','m','h')
  HardwareManager::handleSerialCommands();
}