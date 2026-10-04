#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

WebServer server(80);
Preferences prefs;
File uploadFile;

// Định nghĩa chân LED RGB Onboard cho ESP32-S3
#ifndef RGB_BUILTIN
  #define RGB_BUILTIN 48
#endif

unsigned long lastLedToggle = 0;
bool ledState = false;

// Hàm tiện ích đổi màu LED RGB
void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(RGB_BUILTIN, r, g, b);
}
// Hàm cập nhật trạng thái LED nhấp nháy không gây hoãn
void updateLedStatus() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastLedToggle >= 500) { // Nhấp nháy chu kỳ 0.5 giây
    lastLedToggle = currentMillis;
    ledState = !ledState;

    if (WiFi.status() == WL_CONNECTED) {
      // ✅ Đã kết nối Wi-Fi thành công -> Nhấp nháy XANH LÁ
      if (ledState) setLedColor(0, 255, 0);
      else setLedColor(0, 0, 0);
    } else {
      // ❌ Chưa kết nối Wi-Fi / AP Mode -> Nhấp nháy ĐỎ
      if (ledState) setLedColor(255, 0, 0);
      else setLedColor(0, 0, 0);
    }
  }
}

// 1. Khởi tạo & kiểm tra phần cứng ESP32-S3 N16R8
void initHardware() {
  Serial.println("\n==============================================");
  Serial.println("  TRẠM DECOR VŨ TRỤ - ESP32-S3 N16R8 SYSTEM   ");
  Serial.println("==============================================");
  
  if (psramInit()) {
    Serial.println("✅ PSRAM Status: OK!");
  } else {
    Serial.println("❌ PSRAM Status: FAILED!");
  }

  if (LittleFS.begin(true)) {
    Serial.println("✅ LittleFS Status: OK!");
  } else {
    Serial.println("❌ LittleFS Status: FAILED!");
  }

  if (!LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "w");
    if (file) {
      file.print("{\"endpoint\":\"wss://api.xiaozhi.me/v1/cc\",\"token\":\"\",\"mac\":\"\"}");
      file.close();
    }
  }
}
// void initHardware() {
//   Serial.println("\n==============================================");
//   Serial.println("  TRẠM DECOR VŨ TRỤ - ESP32-S3 N16R8 SYSTEM   ");
//   Serial.println("==============================================");
  
//   // Kiểm tra 8MB OPI PSRAM
//   if (psramInit()) {
//     Serial.println("✅ PSRAM Status: OK!");
//     Serial.printf("   - Tổng dung lượng PSRAM: %d MB (%d bytes)\n", ESP.getPsramSize() / (1024 * 1024), ESP.getPsramSize());
//     Serial.printf("   - PSRAM còn trống      : %d KB\n", ESP.getFreePsram() / 1024);
//   } else {
//     Serial.println("❌ PSRAM Status: FAILED / NOT FOUND!");
//   }

//   // Kiểm tra LittleFS 11.8MB
//   if (LittleFS.begin(true)) {
//     Serial.println("✅ LittleFS Status: OK!");
//     Serial.printf("   - Tổng bộ nhớ Flash : %.2f MB\n", LittleFS.totalBytes() / (1024.0 * 1024.0));
//     Serial.printf("   - Đã sử dụng         : %.2f KB\n", LittleFS.usedBytes() / 1024.0);
//   } else {
//     Serial.println("❌ LittleFS Status: FAILED!");
//   }
//   if (!LittleFS.exists("/config.json")) {
//     File file = LittleFS.open("/config.json", "w");
//     if (file) {
//         file.print("{\"endpoint\":\"wss://api.xiaozhi.me/v1/cc\",\"token\":\"\",\"mac\":\"\"}");
//         file.close();
//         Serial.println("✅ Đã tự động tạo file /config.json mặc định!");
//       }
//   }
//   Serial.println("----------------------------------------------");
// }

// 2. REST API: Quét Wi-Fi
void handleWifiScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  for (int i = 0; i < n; ++i) {
    JsonObject obj = array.add<JsonObject>();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
    obj["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// 3. REST API: Kết nối Wi-Fi
void handleWifiConnect() {
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

  String ssid = doc["ssid"] | "";
  String pass = doc["password"] | "";

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Đã lưu Wi-Fi. Khởi động lại sau 1s...\"}");
  delay(1000);
  ESP.restart();
}

// 4. REST API: Upload Ảnh nền (/wallpaper.jpg)
void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("📸 Bắt đầu nhận ảnh Wallpaper: %s\n", upload.filename.c_str());
    if (LittleFS.exists("/wallpaper.jpg")) {
      LittleFS.remove("/wallpaper.jpg");
    }
    uploadFile = LittleFS.open("/wallpaper.jpg", FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("✅ Đã lưu thành công wallpaper.jpg (%d bytes)\n", upload.totalSize);
    }
  }
}

// 5. REST API: Đọc Cấu hình AI
void handleGetAIConfig() {
  if (LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "r");
    server.streamFile(file, "application/json");
    file.close();
  } else {
    server.send(200, "application/json", "{\"endpoint\":\"wss://api.xiaozhi.me/v1/cc\",\"token\":\"\",\"mac\":\"\"}");
  }
}

// 6. REST API: Lưu Cấu hình AI
void handleSaveAIConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  File file = LittleFS.open("/config.json", "w");
  if (file) {
    file.print(server.arg("plain"));
    file.close();
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Đã lưu cấu hình AI thành công\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Lỗi ghi file config.json\"}");
  }
}

// 7. REST API: Trạng thái Hệ thống
void handleSystemStatus() {
  JsonDocument doc;
  doc["free_heap_kb"] = ESP.getFreeHeap() / 1024;
  doc["free_psram_mb"] = (float)ESP.getFreePsram() / (1024.0 * 1024.0);
  doc["fs_used_mb"] = (float)LittleFS.usedBytes() / (1024.0 * 1024.0);
  doc["fs_total_mb"] = (float)LittleFS.totalBytes() / (1024.0 * 1024.0);
  doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["mode"] = (WiFi.status() == WL_CONNECTED) ? "STA" : "AP";

  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

// 8. Trả về trang Web UI
void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File file = LittleFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "Chưa tìm thấy index.html! Hãy dùng task 'Upload Filesystem Image' trong PlatformIO.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000); // Chờ USB Serial nhận diện

  initHardware();

  prefs.begin("wifi", false); 
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("TramVuTru-Config", "12345678");

  if (ssid != "") {
    // 🟡 Đang chờ kết nối đến Wi-Fi -> Bật sáng VÀNG
    setLedColor(255, 200, 0); 
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("📡 Đang kết nối Wi-Fi '%s'...", ssid.c_str());
    
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      Serial.print(".");
      timeout++;
    }
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ Đã kết nối Wi-Fi thành công!");
  } else {
    Serial.println("⚠️ Chưa kết nối được Wi-Fi, chạy chế độ AP!");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/connect", HTTP_POST, handleWifiConnect);
  server.on("/api/upload/wallpaper", HTTP_POST, []() {
    server.send(200, "application/json", "{\"status\":\"success\"}");
  }, handleFileUpload);
  server.on("/api/config/ai", HTTP_GET, handleGetAIConfig);
  server.on("/api/config/ai", HTTP_POST, handleSaveAIConfig);
  server.on("/api/system/status", HTTP_GET, handleSystemStatus);

  server.begin();
  if (MDNS.begin("tramvutru")) {
    Serial.println("🌐 mDNS ready: http://tramvutru.local");
  }
}

void loop() {
  server.handleClient();
  updateLedStatus(); // Liên tục cập nhật hiệu ứng LED
}