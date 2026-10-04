#include "HardwareManager.h"
#include "Config.h"
#include "XiaoZhiClient.h"
#include <LittleFS.h>
#include <WiFi.h>

static unsigned long lastLedToggle = 0;
static bool ledState = false;
static unsigned long lastSerialHeartbeat = 0;

void HardwareManager::init() {
  Serial.println("\n==============================================");
  Serial.println("  TRẠM DECOR VŨ TRỤ - ESP32-S3 N16R8 SYSTEM   ");
  Serial.println("==============================================");

  if (psramInit()) {
    Serial.printf("✅ PSRAM Status: OK! (Dung lượng: %.1f MB)\n", (float)ESP.getFreePsram() / (1024.0 * 1024.0));
  } else {
    Serial.println("❌ PSRAM Status: FAILED!");
  }

  if (LittleFS.begin(true)) {
    Serial.printf("✅ LittleFS Status: OK! (Dung lượng: %.2f / %.2f MB)\n",
                  (float)LittleFS.usedBytes() / (1024.0 * 1024.0),
                  (float)LittleFS.totalBytes() / (1024.0 * 1024.0));
  } else {
    Serial.println("❌ LittleFS Status: FAILED!");
  }

  Serial.printf("📡 ESP32 Hardware MAC: %s\n", WiFi.macAddress().c_str());

  if (!LittleFS.exists(FILE_CONFIG)) {
    File file = LittleFS.open(FILE_CONFIG, "w");
    if (file) {
      file.print("{\"endpoint\":\"" XIAOZHI_DEFAULT_ENDPOINT "\",\"token\":\"\",\"mac\":\"\"}");
      file.close();
      Serial.println("✅ Khởi tạo /config.json mặc định thành công!");
    }
  }

  if (!LittleFS.exists(FILE_STANDBY_CONFIG)) {
    File file = LittleFS.open(FILE_STANDBY_CONFIG, "w");
    if (file) {
      file.print("{\"theme\":\"cyberpunk\",\"clock_style\":\"digital\",\"clock_color\":\"#38bdf8\",\"clock_format\":\"24h\",\"show_seconds\":true,\"clock_pos\":\"center\",\"show_date\":true,\"date_format\":\"vi\",\"show_weather\":true,\"temp\":28.5,\"humidity\":65,\"custom_text\":\"Trạm Decor Vũ Trụ ✨\",\"text_color\":\"#94a3b8\",\"bg_mode\":\"gradient\",\"bg_color\":\"#0a0f1d\",\"bg_image\":\"\"}");
      file.close();
      Serial.println("✅ Khởi tạo /standby_config.json mặc định thành công!");
    }
  }
}

void HardwareManager::setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(RGB_BUILTIN, r, g, b);
}

void HardwareManager::updateLedStatus() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastLedToggle >= 500) {
    lastLedToggle = currentMillis;
    ledState = !ledState;
    if (WiFi.status() == WL_CONNECTED) {
      if (ledState) setLedColor(0, 255, 0); // Xanh lá khi đã nối mạng
      else setLedColor(0, 0, 0);
    } else {
      if (ledState) setLedColor(255, 0, 0); // Đỏ khi chưa có mạng
      else setLedColor(0, 0, 0);
    }
  }
}

void HardwareManager::heartbeat() {
  unsigned long now = millis();
  if (now - lastSerialHeartbeat >= 5000) {
    lastSerialHeartbeat = now;
    Serial.printf("💓 [Decor ESP32] Uptime: %lu s | RAM: %u KB | IP: %s\n",
                  now / 1000,
                  getFreeHeapKb(),
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "AP Mode (192.168.4.1)");
  }
}

#include "tests/ModuleTestRunner.h"

void HardwareManager::handleSerialCommands() {
  if (Serial.available()) {
    char c = Serial.read();
    if (ModuleTestRunner::handleSerial(c)) {
      return;
    }
    if (c == 'r' || c == 'R') {
      Serial.println("\n🔄 Nhận lệnh 'r': Đang gọi lại kiểm tra XiaoZhi OTA...");
      XiaoZhiClient::queryOTA(true);
    } else if (c == 'h' || c == 'H') {
      Serial.println("\n=======================================================");
      Serial.println("📋 TRẠM DECOR ESP32-S3 (N16R8) - BẢNG ĐIỀU KHIỂN SERIAL");
      Serial.println("=======================================================");
      Serial.printf("📡 MAC Phần Cứng : %s\n", WiFi.macAddress().c_str());
      Serial.printf("🌐 Trạng thái IP : %s\n", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "AP Mode (192.168.4.1)");
      Serial.printf("🧠 Free Heap RAM : %u KB\n", getFreeHeapKb());
      Serial.printf("💾 Free PSRAM    : %.1f MB\n", getFreePsramMb());
      Serial.println("👉 Bấm phím 'r' rồi Enter: Gọi kiểm tra XiaoZhi OTA ngay.");
      Serial.println("=======================================================");
      ModuleTestRunner::printAllHelp();
    }
  }
}

uint32_t HardwareManager::getFreeHeapKb() {
  return ESP.getFreeHeap() / 1024;
}

float HardwareManager::getFreePsramMb() {
  return (float)ESP.getFreePsram() / (1024.0 * 1024.0);
}

float HardwareManager::getFsUsedMb() {
  return (float)LittleFS.usedBytes() / (1024.0 * 1024.0);
}

float HardwareManager::getFsTotalMb() {
  return (float)LittleFS.totalBytes() / (1024.0 * 1024.0);
}

