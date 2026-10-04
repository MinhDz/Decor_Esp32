#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#ifndef RGB_BUILTIN
  #define RGB_BUILTIN 48
#endif

WebServer server(80);
Preferences prefs;
File uploadFile;
bool uploadAllowed = true;
const int MAX_IMAGE_COUNT = 15;

unsigned long lastLedToggle = 0;
bool ledState = false;
unsigned long lastSerialHeartbeat = 0;

void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(RGB_BUILTIN, r, g, b);
}

void updateLedStatus() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastLedToggle >= 500) {
    lastLedToggle = currentMillis;
    ledState = !ledState;
    if (WiFi.status() == WL_CONNECTED) {
      if (ledState) setLedColor(0, 255, 0);
      else setLedColor(0, 0, 0);
    } else {
      if (ledState) setLedColor(255, 0, 0);
      else setLedColor(0, 0, 0);
    }
  }
}

void initHardware() {
  Serial.println("\n==============================================");
  Serial.println("  TRẠM DECOR VŨ TRỤ - ESP32-S3 N16R8 SYSTEM   ");
  Serial.println("==============================================");
  
  if (psramInit()) Serial.println("✅ PSRAM Status: OK!");
  else Serial.println("❌ PSRAM Status: FAILED!");

  if (LittleFS.begin(true)) Serial.println("✅ LittleFS Status: OK!");
  else Serial.println("❌ LittleFS Status: FAILED!");

  Serial.printf("📡 ESP32 Hardware MAC: %s\n", WiFi.macAddress().c_str());

  if (!LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "w");
    if (file) {
      file.print("{\"endpoint\":\"wss://api.tenclass.net/xiaozhi/v1/\",\"token\":\"\",\"mac\":\"\"}");
      file.close();
    }
  }

  if (!LittleFS.exists("/standby_config.json")) {
    File file = LittleFS.open("/standby_config.json", "w");
    if (file) {
      file.print("{\"theme\":\"cyberpunk\",\"clock_style\":\"digital\",\"clock_color\":\"#38bdf8\",\"clock_format\":\"24h\",\"show_seconds\":true,\"clock_pos\":\"center\",\"show_date\":true,\"date_format\":\"vi\",\"show_weather\":true,\"temp\":28.5,\"humidity\":65,\"custom_text\":\"Trạm Decor Vũ Trụ ✨\",\"text_color\":\"#94a3b8\",\"bg_mode\":\"gradient\",\"bg_color\":\"#0a0f1d\",\"bg_image\":\"\"}");
      file.close();
      Serial.println("✅ Khởi tạo /standby_config.json mặc định thành công!");
    }
  }
}

// ---------------- REST APIs MẠNG ----------------
void handleWifiScan() {
  Serial.println("\n🔍 Bắt đầu quét mạng Wi-Fi 2.4GHz...");
  WiFi.scanDelete(); // Luôn dọn dẹp bộ nhớ scan cũ

  // Nếu chưa kết nối STA, ngắt kết nối ngầm để giải phóng radio cho việc scan
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(false);
  }
  delay(100);

  // Quét đồng bộ cả mạng ẩn (show_hidden = true)
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) {
    Serial.printf("⚠️ Quét lần 1 thất bại (mã: %d), thử lại...\n", n);
    WiFi.scanDelete();
    delay(200);
    n = WiFi.scanNetworks(false, true);
  }

  Serial.printf("🔍 Quét hoàn tất: Tìm thấy %d mạng Wi-Fi\n", n);

  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() > 0) {
        JsonObject obj = array.add<JsonObject>();
        obj["ssid"] = ssid;
        obj["rssi"] = WiFi.RSSI(i);
        obj["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        Serial.printf("   - %s (%d dBm)\n", ssid.c_str(), WiFi.RSSI(i));
      }
    }
  }
  WiFi.scanDelete(); // Giải phóng ngay sau khi duyệt xong

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleWifiConnect() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  String ssid = doc["ssid"] | "";
  String pass = doc["password"] | "";

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Đã lưu Wi-Fi. Đang khởi động lại...\"}");
  delay(1000);
  ESP.restart();
}

void handleSystemStatus() {
  JsonDocument doc;
  doc["free_heap_kb"] = ESP.getFreeHeap() / 1024;
  doc["free_psram_mb"] = (float)ESP.getFreePsram() / (1024.0 * 1024.0);
  doc["fs_used_mb"] = (float)LittleFS.usedBytes() / (1024.0 * 1024.0);
  doc["fs_total_mb"] = (float)LittleFS.totalBytes() / (1024.0 * 1024.0);
  doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["mac"] = WiFi.macAddress();
  doc["connected"] = (WiFi.status() == WL_CONNECTED);
  
  prefs.begin("sys", true);
  doc["active_img"] = prefs.getString("active_img", "/wallpaper.jpg");
  prefs.end();

  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

// ---------------- REST APIs QUẢN LÝ ẢNH ----------------
int countStoredImages() {
  int count = 0;
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String filename = String(file.name());
    if (filename.endsWith(".jpg") || filename.endsWith(".jpeg") || filename.endsWith(".png") || filename.endsWith(".bmp")) {
      count++;
    }
    file = root.openNextFile();
  }
  return count;
}

void handleListImages() {
  JsonDocument doc;
  JsonArray array = doc["images"].to<JsonArray>();
  int count = 0;
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String filename = String(file.name());
    if (filename.endsWith(".jpg") || filename.endsWith(".jpeg") || filename.endsWith(".png") || filename.endsWith(".bmp")) {
      JsonObject obj = array.add<JsonObject>();
      obj["name"] = filename.startsWith("/") ? filename : "/" + filename;
      obj["size"] = file.size();
      count++;
    }
    file = root.openNextFile();
  }
  doc["count"] = count;
  doc["max"] = MAX_IMAGE_COUNT;
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

void handleImageUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (countStoredImages() >= MAX_IMAGE_COUNT) {
      Serial.println("❌ Đã đạt giới hạn 15 ảnh, từ chối lưu ảnh mới!");
      uploadAllowed = false;
      return;
    }
    uploadAllowed = true;

    // Trích xuất đuôi file (.jpg, .png...)
    String filename = upload.filename;
    int extIdx = filename.lastIndexOf('.');
    String ext = (extIdx != -1) ? filename.substring(extIdx) : ".jpg";

    // Tự động tạo tên file ngắn gọn (ví dụ: /img_48291.jpg) tránh lỗi giới hạn LittleFS
    String newFilename = "/img_" + String(millis() % 100000) + ext;

    Serial.printf("📸 Upload ảnh mới, lưu tên ngắn: %s\n", newFilename.c_str());

    if (LittleFS.exists(newFilename)) LittleFS.remove(newFilename);
    uploadFile = LittleFS.open(newFilename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadAllowed && uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadAllowed && uploadFile) {
      uploadFile.close();
      Serial.println("✅ Đã ghi thành công vào LittleFS!");
    }
  }
}

void handleDeleteImage() {
  if (!server.hasArg("name")) {
    server.send(400, "application/json", "{\"error\":\"Missing name\"}");
    return;
  }
  String filename = server.arg("name");
  if (!filename.startsWith("/")) filename = "/" + filename;
  
  if (LittleFS.exists(filename)) {
    LittleFS.remove(filename);
    server.send(200, "application/json", "{\"status\":\"success\"}");
  } else {
    server.send(404, "application/json", "{\"error\":\"File not found\"}");
  }
}

void handleSelectImage() {
  if (!server.hasArg("name")) {
    server.send(400, "application/json", "{\"error\":\"Missing name\"}");
    return;
  }
  String filename = server.arg("name");
  if (!filename.startsWith("/")) filename = "/" + filename;

  prefs.begin("sys", false);
  prefs.putString("active_img", filename);
  prefs.end();

  server.send(200, "application/json", "{\"status\":\"success\"}");
}

// ---------------- REST APIs CẤU HÌNH MÀN HÌNH CHỜ ----------------
void handleGetStandbyConfig() {
  if (LittleFS.exists("/standby_config.json")) {
    File file = LittleFS.open("/standby_config.json", "r");
    server.streamFile(file, "application/json");
    file.close();
  } else {
    server.send(200, "application/json", "{\"theme\":\"cyberpunk\",\"clock_style\":\"digital\",\"clock_color\":\"#38bdf8\",\"clock_format\":\"24h\",\"show_seconds\":true,\"clock_pos\":\"center\",\"show_date\":true,\"date_format\":\"vi\",\"show_weather\":true,\"temp\":28.5,\"humidity\":65,\"custom_text\":\"Trạm Decor Vũ Trụ ✨\",\"text_color\":\"#94a3b8\",\"bg_mode\":\"gradient\",\"bg_color\":\"#0a0f1d\",\"bg_image\":\"\"}");
  }
}

void handleSaveStandbyConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  File file = LittleFS.open("/standby_config.json", "w");
  if (!file) {
    server.send(500, "application/json", "{\"error\":\"Failed to open config file for write\"}");
    return;
  }
  file.print(body);
  file.close();

  // Đồng bộ ảnh nền nếu có khai báo bg_image
  if (doc["bg_image"].is<const char*>()) {
    String bgImg = doc["bg_image"].as<String>();
    if (bgImg.length() > 0) {
      prefs.begin("sys", false);
      prefs.putString("active_img", bgImg);
      prefs.end();
    }
  }

  Serial.println("✅ Đã cập nhật cấu hình màn hình chờ vào LittleFS!");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Đã lưu cấu hình màn hình chờ thành công!\"}");
}

// ---------------- REST APIs CẤU HÌNH AI XIAOZHI ----------------
String getOrCreateDeviceUuid() {
  prefs.begin("sys", false);
  String uuid = prefs.getString("uuid", "");
  if (uuid.length() < 32) {
    uint8_t u[16];
    esp_fill_random(u, sizeof(u));
    u[6] = (u[6] & 0x0F) | 0x40; // Version 4
    u[8] = (u[8] & 0x3F) | 0x80; // Variant 1
    char buf[37];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
      u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
    uuid = String(buf);
    prefs.putString("uuid", uuid);
    Serial.printf("✨ Đã tạo UUID thiết bị mới: %s\n", uuid.c_str());
  }
  prefs.end();
  return uuid;
}

String queryXiaoZhiOTA(bool printToSerial = true) {
  if (WiFi.status() != WL_CONNECTED) {
    if (printToSerial) {
      Serial.println("⚠️ [XIAOZHI OTA] ESP32 chưa kết nối Wi-Fi, không thể kiểm tra OTA.");
    }
    return "";
  }

  String mac = WiFi.macAddress();
  String uuid = getOrCreateDeviceUuid();

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);

  HTTPClient http;
  http.begin(client, "https://api.tenclass.net/xiaozhi/ota/");
  http.setUserAgent("bread-compact-wifi/2.0.0"); // BẮT BUỘC dùng setUserAgent, addHeader sẽ bị thư viện bỏ qua
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Device-Id", mac);
  http.addHeader("Client-Id", uuid);
  http.addHeader("Activation-Version", "1");

  String reqBody = "{\"version\":2,\"mac_address\":\"" + mac + "\",\"uuid\":\"" + uuid + "\",\"board\":\"bread-compact-wifi\",\"board_type\":\"bread-compact-wifi\",\"flash_size\":16777216,\"psram_size\":8388608,\"chip_model_name\":\"esp32s3\",\"application\":{\"name\":\"xiaozhi\",\"version\":\"2.0.0\"}}";
  int httpCode = http.POST(reqBody);
  String payload = "";

  if (httpCode == HTTP_CODE_OK || httpCode == 200) {
    payload = http.getString();

    if (printToSerial) {
      Serial.println("\n==================================================================");
      Serial.println("🚀 [XIAOZHI OTA] PHẢN HỒI TỪ MÁY CHỦ XIAOZHI (HTTP 200 OK)");
      Serial.println("==================================================================");
      Serial.printf("📡 Device Hardware MAC : %s\n", mac.c_str());
      Serial.printf("🆔 Device Client UUID  : %s\n", uuid.c_str());
      Serial.println("------------------------------------------------------------------");

      JsonDocument resDoc;
      DeserializationError err = deserializeJson(resDoc, payload);
      if (!err) {
        // 1. Kiểm tra mã kích hoạt
        if (resDoc["activation"]["code"].is<const char*>()) {
          const char* code = resDoc["activation"]["code"];
          const char* msg = resDoc["activation"]["message"] | "";
          Serial.println("🔑 TRẠNG THÁI: CHƯA LIÊN KẾT (CẦN THÊM THIẾT BỊ TRÊN XIAOZHI.ME)");
          Serial.printf("👉 MÃ XÁC THỰC (AUTH CODE) : >>>  %s  <<<\n", code);
          if (strlen(msg) > 0) {
            Serial.printf("💬 Hướng dẫn từ XiaoZhi     : %s\n", msg);
          }
          Serial.printf("🌐 Hãy mở https://xiaozhi.me -> Thêm thiết bị -> Nhập MAC [%s] và Mã [%s]\n", mac.c_str(), code);
        } else {
          Serial.println("🎉 TRẠNG THÁI: THIẾT BỊ ĐÃ LIÊN KẾT THÀNH CÔNG VỚI TÀI KHOẢN XIAOZHI!");
        }

        Serial.println("------------------------------------------------------------------");

        // 2. Thông tin WebSocket
        if (resDoc["websocket"]["url"].is<const char*>()) {
          const char* wsUrl = resDoc["websocket"]["url"];
          const char* token = resDoc["websocket"]["token"] | "";
          Serial.printf("🌐 WebSocket Server Endpoint : %s\n", wsUrl);
          Serial.printf("🔑 Token                     : %s\n", (token && strlen(token) > 0) ? token : "(Chưa có)");

          // Tự động lưu Token vào config.json nếu nhận được token
          if (token && strlen(token) > 0) {
            JsonDocument cfgDoc;
            if (LittleFS.exists("/config.json")) {
              File f = LittleFS.open("/config.json", "r");
              deserializeJson(cfgDoc, f);
              f.close();
            }
            cfgDoc["endpoint"] = wsUrl;
            cfgDoc["token"] = token;
            cfgDoc["mac"] = mac;
            cfgDoc["uuid"] = uuid;
            File f = LittleFS.open("/config.json", "w");
            if (f) {
              serializeJson(cfgDoc, f);
              f.close();
              Serial.println("💾 Đã tự động cập nhật Token và Endpoint vào /config.json!");
            }
          }
        }

        // 3. Thông tin MQTT
        if (resDoc["mqtt"]["endpoint"].is<const char*>()) {
          Serial.printf("⚡ MQTT Broker               : %s\n", resDoc["mqtt"]["endpoint"].as<const char*>());
          Serial.printf("📦 MQTT Client ID            : %s\n", resDoc["mqtt"]["client_id"].as<const char*>());
        }

        // 4. Server Time
        if (resDoc["server_time"]["timestamp"].is<long long>()) {
          Serial.printf("⏰ Server Timestamp          : %lld (Timezone Offset: %d phút)\n",
                        resDoc["server_time"]["timestamp"].as<long long>(),
                        resDoc["server_time"]["timezone_offset"].as<int>());
        }
      }

      Serial.println("------------------------------------------------------------------");
      Serial.println("📄 [NỘI DUNG RAW JSON TỪ XIAOZHI]:");
      Serial.println(payload);
      Serial.println("==================================================================\n");
    }
  } else {
    String errPayload = http.getString();
    Serial.printf("❌ [XIAOZHI OTA] Lỗi gọi API (HTTP %d): %s\n", httpCode, errPayload.c_str());
  }

  http.end();
  return payload;
}

void handleXiaoZhiOTA() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(400, "application/json", "{\"error\":\"ESP32 chưa nối Wi-Fi để truy cập XiaoZhi Cloud trực tiếp\",\"need_wifi\":true}");
    return;
  }
  String payload = queryXiaoZhiOTA(true);
  if (payload.length() > 0) {
    server.send(200, "application/json", payload);
  } else {
    server.send(500, "application/json", "{\"error\":\"Lỗi kết nối máy chủ XiaoZhi\"}");
  }
}

void handleGetAIConfig() {
  JsonDocument doc;
  if (LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "r");
    deserializeJson(doc, file);
    file.close();
  }
  
  if (!doc["endpoint"].is<const char*>() || strlen(doc["endpoint"]) == 0 || String(doc["endpoint"].as<const char*>()).indexOf("api.xiaozhi.me") != -1) {
    doc["endpoint"] = "wss://api.tenclass.net/xiaozhi/v1/";
  }
  if (!doc["token"].is<const char*>()) {
    doc["token"] = "";
  }
  String hwMac = WiFi.macAddress();
  doc["hw_mac"] = hwMac;
  if (!doc["mac"].is<const char*>() || strlen(doc["mac"]) == 0) {
    doc["mac"] = hwMac;
  }
  doc["uuid"] = getOrCreateDeviceUuid();
  
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

void handleSaveAIConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  File file = LittleFS.open("/config.json", "w");
  if (!file) {
    server.send(500, "application/json", "{\"error\":\"Failed to open /config.json for write\"}");
    return;
  }
  file.print(body);
  file.close();

  Serial.println("✅ Đã cập nhật cấu hình XiaoZhi AI vào LittleFS!");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Đã lưu cấu hình XiaoZhi AI thành công!\"}");
}

// Hàm phục vụ trang chủ / (trả về file index.html)
void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File file = LittleFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  } else {
    server.send(404, "text/plain", "Chưa tìm thấy index.html trên LittleFS!");
  }
}

void setup() {
  Serial.begin(115200);
  // Vòng lặp chờ máy tính kết nối cổng Serial (tối đa 3 giây)
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 3000)) {
    delay(10);
  }
  delay(500); // Đệm ổn định thêm 0.5s
  Serial.println("-----Bắt đầu khởi động-----");

  initHardware();

  prefs.begin("wifi", true); 
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("TramVuTru-Config", "12345678");

  if (ssid != "") {
    setLedColor(255, 200, 0); 
    WiFi.begin(ssid.c_str(), pass.c_str());
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      timeout++;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ Wi-Fi đã kết nối thành công! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.println("🌐 Đang kiểm tra OTA và lấy thông tin xác thực từ XiaoZhi Cloud...");
    queryXiaoZhiOTA(true);
  } else {
    Serial.println("\n📡 ESP32 đang chạy chế độ AP (TramVuTru-Config, IP: 192.168.4.1)");
    Serial.println("💡 Sau khi nối Wi-Fi thành công, ESP32 sẽ tự động gọi XiaoZhi OTA và in thông tin lên Serial.");
  }

  // 1. Phục vụ trang chủ "/" bằng handler đọc file index.html
  server.on("/", HTTP_GET, handleRoot);

  // 2. Khai báo các REST API Mạng & Hệ thống
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/connect", HTTP_POST, handleWifiConnect);
  server.on("/api/system/status", HTTP_GET, handleSystemStatus);

  // Routes Quản Lý Ảnh
  server.on("/api/images", HTTP_GET, handleListImages);
  server.on("/api/images/upload", HTTP_POST, []() {
    if (uploadAllowed) {
      server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Tải ảnh lên thành công!\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Đã đạt giới hạn tối đa 15 ảnh. Vui lòng xóa bớt ảnh cũ!\"}");
    }
  }, handleImageUpload);
  server.on("/api/images/delete", HTTP_POST, handleDeleteImage);
  server.on("/api/images/select", HTTP_POST, handleSelectImage);

  // Routes Cấu hình Màn hình chờ
  server.on("/api/screen/standby", HTTP_GET, handleGetStandbyConfig);
  server.on("/api/screen/standby", HTTP_POST, handleSaveStandbyConfig);

  // Routes Cấu hình XiaoZhi AI
  server.on("/api/config/ai", HTTP_GET, handleGetAIConfig);
  server.on("/api/config/ai", HTTP_POST, handleSaveAIConfig);
  server.on("/api/xiaozhi/ota", HTTP_GET, handleXiaoZhiOTA);
  server.on("/api/xiaozhi/ota", HTTP_POST, handleXiaoZhiOTA);

  // 3. Phục vụ tất cả các file tĩnh khác trên LittleFS (ảnh, v.v.)
  server.serveStatic("/", LittleFS, "/");

  server.begin();
  if (MDNS.begin("tramvutru")) {
    Serial.println("🌐 mDNS ready: http://tramvutru.local");
  }
}

void loop() {
  server.handleClient();
  updateLedStatus();

  // In nhịp tim (Heartbeat) định kỳ 5 giây để kiểm tra cổng Serial luôn thông suốt
  unsigned long now = millis();
  if (now - lastSerialHeartbeat >= 5000) {
    lastSerialHeartbeat = now;
    Serial.printf("💓 [Decor ESP32] Uptime: %lu s | RAM: %u KB | IP: %s\n",
                  now / 1000,
                  ESP.getFreeHeap() / 1024,
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "AP Mode (192.168.4.1)");
  }

  // Nhận lệnh tương tác nhanh qua bàn phím trên Serial Monitor
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      Serial.println("\n🔄 Nhận lệnh 'r': Đang gọi lại kiểm tra XiaoZhi OTA...");
      queryXiaoZhiOTA(true);
    } else if (c == 'h' || c == 'H' || c == '\n' || c == '\r') {
      Serial.println("\n=======================================================");
      Serial.println("📋 TRẠM DECOR ESP32-S3 (N16R8) - BẢNG ĐIỀU KHIỂN SERIAL");
      Serial.println("=======================================================");
      Serial.printf("📡 MAC Phần Cứng : %s\n", WiFi.macAddress().c_str());
      Serial.printf("🌐 Trạng thái IP : %s\n", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "AP Mode (192.168.4.1)");
      Serial.printf("🧠 Free Heap RAM : %u KB\n", ESP.getFreeHeap() / 1024);
      Serial.printf("💾 Free PSRAM    : %.1f MB\n", (float)ESP.getFreePsram() / (1024.0 * 1024.0));
      Serial.println("👉 Bấm phím 'r' rồi Enter: Gọi kiểm tra XiaoZhi OTA ngay.");
      Serial.println("=======================================================\n");
    }
  }
}