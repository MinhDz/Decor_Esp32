#include "OtaManager.h"
#include "tests/TestDisplay.h"
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static bool s_isUpdating = false;
static int s_progressPct = 0;
static String s_statusMsg = "Sẵn sàng";

void OtaManager::init() {
  // Xác nhận phân vùng đang chạy hợp lệ và hủy cờ rollback nếu khởi động tốt
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (running && esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      Serial.println("🛡️ [OTA] Firmware mới đang chạy xác thực lần đầu. Đang hủy cờ rollback...");
      esp_ota_mark_app_valid_cancel_rollback();
      Serial.println("✅ [OTA] Firmware đã được xác nhận hoạt động ổn định!");
    }
  }

  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  Serial.printf("🚀 [OTA INIT] Phân vùng đang chạy: %s | Phân vùng dự phòng: %s\n",
                running ? running->label : "chưa rõ",
                next ? next->label : "chưa rõ");
  Serial.printf("   + Phiên bản Firmware: %s | Ngày build: %s\n", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
}

String OtaManager::getRunningPartitionName() {
  const esp_partition_t *p = esp_ota_get_running_partition();
  return p ? String(p->label) : "factory";
}

String OtaManager::getNextPartitionName() {
  const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
  return p ? String(p->label) : "none";
}

String OtaManager::getFirmwareVersion() {
  return FIRMWARE_VERSION;
}

String OtaManager::getBuildDateTime() {
  return FIRMWARE_BUILD_DATE;
}

bool OtaManager::isUpdating() {
  return s_isUpdating;
}

int OtaManager::getUpdateProgress() {
  return s_progressPct;
}

String OtaManager::getUpdateStatus() {
  return s_statusMsg;
}

void OtaManager::drawMinimalOtaProgress(const String& status, int progressPct) {
  s_statusMsg = status;
  s_progressPct = progressPct;
  TestDisplay::drawOtaProgressScreen(status.c_str(), progressPct);
}

bool OtaManager::checkCloudUpdate(OtaUpdateInfo& info, const String& manifestUrl) {
  info.hasUpdate = false;
  info.latestVersion = "";
  info.downloadUrl = "";
  info.changelog = "";
  info.binSize = 0;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ [OTA CHECK] Wi-Fi chưa kết nối!");
    return false;
  }

  String url = manifestUrl;
  if (url.length() == 0) {
    // URL manifest mặc định kiểm tra phiên bản trên GitHub của MinhDz
    url = "https://raw.githubusercontent.com/MinhDz/Decor_Esp32/main/release/version.json";
  }

  Serial.printf("🔍 [OTA CHECK] Đang kiểm tra bản cập nhật từ: %s\n", url.c_str());

  WiFiClientSecure client;
  client.setInsecure(); // Bỏ qua kiểm tra SSL cert để linh hoạt mọi server HTTPS
  HTTPClient http;
  http.setTimeout(8000);

  if (!http.begin(client, url)) {
    Serial.println("❌ [OTA CHECK] Không thể kết nối HTTP!");
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("⚠️ [OTA CHECK] Máy chủ trả về mã HTTP: %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("❌ [OTA CHECK] Lỗi giải mã JSON: %s\n", err.c_str());
    return false;
  }

  info.latestVersion = doc["version"] | "";
  info.downloadUrl   = doc["bin_url"] | "";
  info.changelog     = doc["changelog"] | "";
  info.binSize       = doc["size"] | 0;

  if (info.latestVersion.length() > 0 && info.latestVersion != FIRMWARE_VERSION) {
    info.hasUpdate = true;
    Serial.printf("🎉 [OTA CHECK] Có bản nâng cấp mới: %s (Hiện tại: %s)!\n",
                  info.latestVersion.c_str(), FIRMWARE_VERSION);
  } else {
    Serial.printf("✅ [OTA CHECK] Hệ thống đã ở bản mới nhất (%s).\n", FIRMWARE_VERSION);
  }

  return true;
}

bool OtaManager::startCloudUpdate(const String& binUrl) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ [CLOUD OTA] Wi-Fi chưa kết nối!");
    return false;
  }
  if (binUrl.length() == 0) {
    Serial.println("❌ [CLOUD OTA] Đường dẫn binUrl trống!");
    return false;
  }

  s_isUpdating = true;
  drawMinimalOtaProgress("Ket noi may chu...", 0);
  Serial.printf("⬇️ [CLOUD OTA] Đang tải firmware từ: %s\n", binUrl.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);

  if (!http.begin(client, binUrl)) {
    drawMinimalOtaProgress("Loi ket noi HTTPS!", 0);
    s_isUpdating = false;
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    drawMinimalOtaProgress(String("Loi HTTP ") + httpCode, 0);
    http.end();
    s_isUpdating = false;
    return false;
  }

  int totalLen = http.getSize();
  Serial.printf("📦 [CLOUD OTA] Kích thước firmware: %d bytes\n", totalLen);

  if (!Update.begin(totalLen > 0 ? totalLen : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    drawMinimalOtaProgress("Loi khoi tao Flash!", 0);
    Update.printError(Serial);
    http.end();
    s_isUpdating = false;
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buff[2048];
  int written = 0;
  int lastPct = -1;

  while (http.connected() && (totalLen > 0 ? (written < totalLen) : true)) {
    size_t size = stream->available();
    if (size > 0) {
      int c = stream->readBytes(buff, min((size_t)size, sizeof(buff)));
      if (c > 0) {
        Update.write(buff, c);
        written += c;
        int pct = (totalLen > 0) ? (written * 100 / totalLen) : 50;
        if (pct != lastPct) {
          lastPct = pct;
          drawMinimalOtaProgress("Dang tai & nap Flash...", pct);
        }
      }
    } else {
      delay(1);
    }
  }

  http.end();

  if (Update.end(true)) {
    Serial.println("✅ [CLOUD OTA] Nạp firmware thành công 100%!");
    drawMinimalOtaProgress("Hoan tat! Dang reboot...", 100);
    delay(2000);
    ESP.restart();
    return true;
  } else {
    Serial.printf("❌ [CLOUD OTA] Nạp thất bại: %s\n", Update.errorString());
    drawMinimalOtaProgress("Loi nap Flash!", 0);
    s_isUpdating = false;
    delay(3000);
    return false;
  }
}

void OtaManager::registerWebRoutes(WebServer& server) {
  (void)server;
}


