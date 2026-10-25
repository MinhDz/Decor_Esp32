#include "WebRoutes.h"
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
#include "CobLedController.h"
#include "OtaManager.h"
#include "WebStreamManager.h"
#include "WeatherManager.h"
#include "TelemetryManager.h"
#include "TaskProfiler.h"
#include "SystemLogger.h"
#include "MemoryScavenger.h"
#include "WebAuth.h"

#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_system.h>

static WebServer server(80);
static bool uploadAllowed = true;

WebServer& WebRoutes::getServer() {
  return server;
}

// ---------------- REST APIS MẠNG & HỆ THỐNG ----------------

#include "api/ApiSystem.h"
#include "api/ApiTerminal.h"
#include "web/WebInjectedScripts.h"

static void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File file = LittleFS.open("/index.html", "r");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    uint8_t buf[1024];
    while (file.available()) {
      size_t n = file.read(buf, sizeof(buf));
      if (n > 0) server.sendContent((const char*)buf, n);
    }
    file.close();
    server.sendContent_P(SD_CONVERTER_INJECT_SCRIPT);
    server.sendContent_P(MEDIA_PAGE_INJECT_SCRIPT);
    server.sendContent_P(XIAOZHI_CHAT_INJECT_SCRIPT);
    server.sendContent_P(STREAM_PAGE_INJECT_SCRIPT);
    server.sendContent_P(DECOR_PAGE_INJECT_SCRIPT);
    server.sendContent_P(KEYPAD_PAGE_INJECT_SCRIPT);
    server.sendContent_P(TELEMETRY_WEATHER_INJECT_SCRIPT);
    server.sendContent_P(AUTH_FACTORY_INJECT_SCRIPT);
    server.sendContent("");
  } else {
    server.send(404, "text/plain", "Chưa tìm thấy index.html trên LittleFS!");
  }
}

static void handleWifiScan() {
  JsonDocument doc;
  WifiManager::scanNetworks(doc);

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

static void handleGetSavedWifi() {
  JsonDocument doc;
  WifiManager::loadSavedWifiList(doc);
  JsonArray arr = doc.as<JsonArray>();

  JsonDocument resDoc;
  JsonArray resArr = resDoc.to<JsonArray>();
  for (JsonObject item : arr) {
    JsonObject obj = resArr.add<JsonObject>();
    String s = item["ssid"].as<String>();
    obj["ssid"] = s;
    obj["connected"] = (WifiManager::isConnected() && WifiManager::getCurrentSsid() == s);
    obj["has_password"] = (item["password"].as<String>().length() > 0);
  }
  String res;
  serializeJson(resDoc, res);
  server.send(200, "application/json", res);
}

static void handleDeleteSavedWifi() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  String ssid = doc["ssid"] | "";
  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"SSID không hợp lệ\"}");
    return;
  }
  bool deleted = WifiManager::deleteSavedWifiNetwork(ssid);
  if (deleted) {
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Đã xóa Wi-Fi khỏi danh sách!\"}");
  } else {
    server.send(404, "application/json", "{\"error\":\"Không tìm thấy Wi-Fi trong danh sách đã lưu\"}");
  }
}

static void handleWifiConnect() {
  if (!WebAuth::isAuthenticated(server)) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  String ssid = doc["ssid"] | "";
  String pass = doc["password"] | "";
  bool save = doc["save"] | true;

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"Tên Wi-Fi không được để trống\"}");
    return;
  }

  bool ok = WifiManager::connectDirect(ssid, pass, save);
  if (ok) {
    JsonDocument resDoc;
    resDoc["status"] = "ok";
    resDoc["connected"] = true;
    resDoc["ssid"] = ssid;
    resDoc["ip"] = WifiManager::getIp();
    resDoc["message"] = "Đã kết nối Wi-Fi thành công!";
    String res;
    serializeJson(resDoc, res);
    server.send(200, "application/json", res);
  } else {
    server.send(200, "application/json", "{\"status\":\"error\",\"connected\":false,\"message\":\"Không thể kết nối (Sai mật khẩu hoặc sóng yếu)\"}");
  }
}

// ---------------- REST APIS QUẢN LÝ ẢNH ----------------

#include <SD.h>
#include "tests/TestSDCard.h"

static void handleListImages() {
  JsonDocument doc;
  ImageManager::listImages(doc);
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void handleImageUploadRoute() {
  bool forceSd = server.hasArg("target") && (server.arg("target") == "sd");
  ImageManager::handleUpload(server.upload(), uploadAllowed, forceSd);
}

static void handleSdUploadRoute() {
  ImageManager::handleUpload(server.upload(), uploadAllowed, true);
}

static void handleSdFileRoute() {
  if (!server.hasArg("path") || !TestSDCard::isMounted()) {
    server.send(404, "text/plain", "SD file not found");
    return;
  }
  String path = server.arg("path");
  if (!path.startsWith("/")) path = "/" + path;
  if (!SD.exists(path)) {
    server.send(404, "text/plain", "SD file does not exist");
    return;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Cannot open SD file");
    return;
  }
  String contentType = "application/octet-stream";
  String low = path;
  low.toLowerCase();
  if (low.endsWith(".png")) contentType = "image/png";
  else if (low.endsWith(".jpg") || low.endsWith(".jpeg")) contentType = "image/jpeg";
  else if (low.endsWith(".bmp")) contentType = "image/bmp";
  server.streamFile(f, contentType);
  f.close();
}

static void handleDeleteImage() {
  if (!WebAuth::isAuthenticated(server)) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
    return;
  }
  if (!server.hasArg("name")) {
    server.send(400, "application/json", "{\"error\":\"Missing name\"}");
    return;
  }
  String filename = server.arg("name");
  if (ImageManager::deleteImage(filename)) {
    server.send(200, "application/json", "{\"status\":\"success\"}");
  } else {
    server.send(404, "application/json", "{\"error\":\"File not found\"}");
  }
}

static void handleSelectImage() {
  if (!WebAuth::isAuthenticated(server)) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
    return;
  }
  if (!server.hasArg("name")) {
    server.send(400, "application/json", "{\"error\":\"Missing name\"}");
    return;
  }
  String filename = server.arg("name");
  if (!filename.startsWith("sd:") && !filename.startsWith("/")) filename = "/" + filename;
  ImageManager::selectActiveImage(filename);

  // Đồng bộ luôn vào /standby_config.json và đẩy lên màn hình ST7789
  JsonDocument doc;
  if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
    File rFile = LittleFS.open(FILE_STANDBY_CONFIG, "r");
    if (rFile) {
      deserializeJson(doc, rFile);
      rFile.close();
    }
  }
  doc["bg_mode"] = "image";
  doc["bg_image"] = filename;
  File wFile = LittleFS.open(FILE_STANDBY_CONFIG, "w");
  if (wFile) {
    serializeJson(doc, wFile);
    wFile.close();
  }
  TestDisplay::reloadStandbyConfig();

  server.send(200, "application/json", "{\"status\":\"success\"}");
}

// ---------------- REST APIS CẤU HÌNH MÀN HÌNH CHỜ ----------------

static void handleGetStandbyConfig() {
  if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
    File file = LittleFS.open(FILE_STANDBY_CONFIG, "r");
    server.streamFile(file, "application/json");
    file.close();
  } else {
    server.send(200, "application/json", "{\"theme\":\"cyberpunk\",\"clock_style\":\"digital\",\"clock_color\":\"#38bdf8\",\"clock_format\":\"24h\",\"show_seconds\":true,\"clock_pos\":\"center\",\"show_date\":true,\"date_format\":\"vi\",\"show_weather\":true,\"temp\":28.5,\"humidity\":65,\"custom_text\":\"Trạm Decor Vũ Trụ ✨\",\"text_color\":\"#94a3b8\",\"bg_mode\":\"gradient\",\"bg_color\":\"#0a0f1d\",\"bg_image\":\"\"}");
  }
}

static void handleSaveStandbyConfig() {
  if (!WebAuth::isAuthenticated(server)) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
    return;
  }
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

  // FlashGuard: Kiểm tra nếu nội dung không đổi so với tệp hiện có -> Bỏ qua ghi đè để bảo vệ Flash
  if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
    File checkFile = LittleFS.open(FILE_STANDBY_CONFIG, "r");
    if (checkFile) {
      String curContent = checkFile.readString();
      checkFile.close();
      if (curContent == body) {
        if (doc["bg_image"].is<const char*>()) {
          String bgImg = doc["bg_image"].as<String>();
          if (bgImg.length() > 0) ImageManager::selectActiveImage(bgImg);
        }
        TestDisplay::reloadStandbyConfig();
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Cấu hình không đổi, FlashGuard đã ngăn ghi đè bảo vệ bộ nhớ Flash!\"}");
        return;
      }
    }
  }

  File file = LittleFS.open(FILE_STANDBY_CONFIG, "w");
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
      ImageManager::selectActiveImage(bgImg);
    }
  }

  // Đẩy cấu hình mới trực tiếp ra màn hình ST7789 rời
  TestDisplay::reloadStandbyConfig();

  Serial.println("✅ Đã cập nhật cấu hình màn hình chờ vào LittleFS & đồng bộ lên màn hình ST7789!");
  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Đã lưu và đẩy cấu hình lên màn hình chờ ST7789!\"}");
}

static void handleScreenStatePost() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  if (doc["mode"].is<const char*>()) {
    String mode = doc["mode"].as<String>();
    TestDisplay::setScreenMode(mode);
  }

  if (doc["emoji"].is<const char*>()) {
    String emoji = doc["emoji"].as<String>();
    String subtitle = doc["subtitle"] | "";
    TestDisplay::setEmojiState(emoji, subtitle);
  }

  if (doc["hud_style"].is<const char*>()) {
    String style = doc["hud_style"].as<String>();
    TestDisplay::setHudStyle(style);
  }

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ---------------- REST APIS ĐIỀU KHIỂN HIỆU ỨNG DECOR & NGUỒN PIN ----------------

static void handleDecorStatus() {
  JsonDocument doc;

  // 1. Cụm Edison Trái & Phải
  JsonObject edL = doc["edison_left"].to<JsonObject>();
  edL["pin"] = PIN_EDISON_TUBE_L;
  edL["mode"] = TestWindmill::getEdisonLeftMode();
  edL["mode_name"] = TestWindmill::getEdisonLeftModeName();
  edL["brightness"] = TestWindmill::getEdisonLeftBrightness();

  JsonObject edR = doc["edison_right"].to<JsonObject>();
  edR["pin"] = PIN_EDISON_TUBE_R;
  edR["mode"] = TestWindmill::getEdisonRightMode();
  edR["mode_name"] = TestWindmill::getEdisonRightModeName();
  edR["brightness"] = TestWindmill::getEdisonRightBrightness();

  JsonArray modesArr = doc["edison_modes"].to<JsonArray>();
  uint8_t totalModes = TestWindmill::getEdisonModeCount();
  for (uint8_t i = 0; i < totalModes; i++) {
    modesArr.add(TestWindmill::getEdisonModeNameByIdx(i));
  }

  // 2. Cối xay gió L298N
  JsonObject wm = doc["windmill"].to<JsonObject>();
  wm["pin"] = PIN_L298N_IN1_MOTOR;
  wm["mode"] = TestWindmill::getMode();
  wm["mode_name"] = TestWindmill::getModeName();
  wm["speed"] = TestWindmill::getMotorSpeed();
  wm["breathing"] = TestWindmill::isBreathing();

  // 3. LED Nước Chảy COB Mặt Lưng
  JsonObject fl = doc["flowing_leds"].to<JsonObject>();
  fl["pin_left"] = PIN_FLOWING_LED_L;
  fl["pin_right"] = PIN_FLOWING_LED_R;
  fl["is_on"] = CobLeds::isOn();
  fl["mode"] = CobLeds::getCurrentMode();
  fl["mode_name"] = CobLeds::getModeName();

  // 4. Pin 18650 & Bo Sạc MH-CD42
  JsonObject bat = doc["battery"].to<JsonObject>();
  bat["pin"] = PIN_BAT_ADC;
  bat["adc_channel"] = "ADC1_CH1";
  bat["voltage"] = serialized(String(TestSensors::getBatteryVoltage(), 2));
  bat["millivolts"] = TestSensors::getBatteryMilliVolts();
  bat["percent"] = TestSensors::getBatteryPercentage();
  bat["charging_pin"] = PIN_CHG_STAT;
  bat["is_charging"] = TestSensors::isCharging();
  bat["charging_source"] = TestSensors::isCharging() ? "Vin MH-CD42 (HIGH - Đang Sạc)" : "Pin Lithium 18650 (Vin LOW)";

  // 5. Cảm biến chạm TTP223
  JsonObject tch = doc["touch"].to<JsonObject>();
  tch["pin"] = PIN_TOUCH_TTP223;
  tch["pressed"] = TestSensors::isTouchPressed();

  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void handleDecorControl() {
  if (!WebAuth::isAuthenticated(server)) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
    return;
  }
  String target = "";
  int mode = -1;
  int brightness = -1;
  int speed = -1;
  String preset = "";
  String action = "";

  if (server.hasArg("plain")) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (!err) {
      if (doc["target"].is<const char*>()) target = doc["target"].as<String>();
      if (doc["mode"].is<int>()) mode = doc["mode"].as<int>();
      if (doc["brightness"].is<int>()) brightness = doc["brightness"].as<int>();
      if (doc["speed"].is<int>()) speed = doc["speed"].as<int>();
      if (doc["preset"].is<const char*>()) preset = doc["preset"].as<String>();
      if (doc["action"].is<const char*>()) action = doc["action"].as<String>();
    }
  }

  if (server.hasArg("target")) target = server.arg("target");
  if (server.hasArg("mode")) mode = server.arg("mode").toInt();
  if (server.hasArg("brightness")) brightness = server.arg("brightness").toInt();
  if (server.hasArg("speed")) speed = server.arg("speed").toInt();
  if (server.hasArg("preset")) preset = server.arg("preset");
  if (server.hasArg("action")) action = server.arg("action");

  if (preset.length() > 0) {
    TestWindmill::applyEffectPreset(preset);
  } else if (target == "edison_left") {
    if (brightness >= 0) TestWindmill::setEdisonLeftBrightness((uint8_t)brightness);
    if (mode >= 0) TestWindmill::setEdisonLeftMode((uint8_t)mode);
  } else if (target == "edison_right") {
    if (brightness >= 0) TestWindmill::setEdisonRightBrightness((uint8_t)brightness);
    if (mode >= 0) TestWindmill::setEdisonRightMode((uint8_t)mode);
  } else if (target == "edison_both" || target == "edison") {
    if (brightness >= 0) TestWindmill::setEdisonBrightness((uint8_t)brightness);
    if (mode >= 0) TestWindmill::setEdisonBothMode((uint8_t)mode);
  } else if (target == "windmill") {
    if (speed >= 0) TestWindmill::setMotorSpeed((uint8_t)speed);
    if (mode >= 0) TestWindmill::setMode((uint8_t)mode);
  } else if (target == "flowing") {
    if (action == "on") CobLeds::turnOn();
    else if (action == "off") CobLeds::turnOff();
    else if (action == "next_mode") CobLeds::nextMode();
    else CobLeds::togglePower();
    TestWindmill::setFlowingLeds(CobLeds::isOn());
  }

  handleDecorStatus();
}

// ---------------- REST APIS ĐIỀU KHIỂN BÀN PHÍM ẢO SIMULATOR ----------------

static void handleKeypadPress() {
  if (!WebAuth::isAuthenticated(server)) {
    server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
    return;
  }
  int keyIdx = -1;
  String keyStr = "";

  if (server.hasArg("plain")) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (!err) {
      if (doc["key"].is<int>()) {
        keyIdx = doc["key"].as<int>();
      } else if (doc["key"].is<const char*>()) {
        keyStr = doc["key"].as<String>();
      }
    }
  }

  if (server.hasArg("key")) {
    String argKey = server.arg("key");
    if (argKey.length() == 1 && argKey[0] >= '0' && argKey[0] <= '6') {
      keyIdx = argKey.toInt();
    } else {
      keyStr = argKey;
    }
  }

  if (keyStr.length() > 0) {
    keyStr.toLowerCase();
    if (keyStr == "ok" || keyStr == "center") keyIdx = 0;
    else if (keyStr == "up") keyIdx = 1;
    else if (keyStr == "down" || keyStr == "dn") keyIdx = 2;
    else if (keyStr == "left" || keyStr == "lf") keyIdx = 3;
    else if (keyStr == "right" || keyStr == "rt") keyIdx = 4;
    else if (keyStr == "menu" || keyStr == "mn") keyIdx = 5;
    else if (keyStr == "exit" || keyStr == "ex" || keyStr == "back") keyIdx = 6;
    else if (keyStr == "touch") {
      // Giả lập chạm cảm biến TTP223 (đổi hiệu ứng bóng đèn Edison hoặc đánh thức màn hình)
      TestWindmill::setEdisonBothMode((TestWindmill::getEdisonLeftMode() + 1) % TestWindmill::getEdisonModeCount());
      JsonDocument res;
      res["success"] = true;
      res["action"] = "touch";
      res["touch_pressed"] = true;
      res["edison_mode"] = TestWindmill::getEdisonLeftMode();
      res["edison_mode_name"] = TestWindmill::getEdisonLeftModeName();
      res["screen_mode"] = TestDisplay::getScreenModeName();
      res["current_mode_id"] = TestDisplay::getCurrentMode();
      res["screen_title"] = TestDisplay::getCurrentScreenTitle();
      String out;
      serializeJson(res, out);
      server.send(200, "application/json", out);
      return;
    }
  }

  if (keyIdx >= 0 && keyIdx <= 6) {
    TestButtons::injectButtonPress(keyIdx);
  }

  JsonDocument res;
  res["success"] = (keyIdx >= 0 && keyIdx <= 6);
  res["key_index"] = keyIdx;
  res["key_name"] = TestButtons::getLastButtonName();
  res["adc_mv"] = TestButtons::getLastAdcMilliVolts();
  res["adc_voltage"] = TestButtons::getLastAdcVoltage();
  res["adc_raw"] = TestButtons::getLastAdcRaw();
  res["screen_mode"] = TestDisplay::getScreenModeName();
  res["current_mode_id"] = TestDisplay::getCurrentMode();
  res["menu_cursor"] = TestDisplay::getS40MenuCursor();
  res["screen_title"] = TestDisplay::getCurrentScreenTitle();
  res["subtitle"] = TestDisplay::getSubtitle();
  res["hud_style"] = TestDisplay::getHudStyleName();
  res["battery_v"] = TestSensors::getBatteryVoltage();
  res["battery_pct"] = TestSensors::getBatteryPercentage();
  res["is_charging"] = TestSensors::isCharging();
  res["bg_image"] = TestDisplay::getStandbyBgImage();
  res["bg_mode"] = TestDisplay::getStandbyBgMode();
  res["dim_overlay"] = TestDisplay::getStandbyDimOverlay();
  res["clock_color"] = TestDisplay::getStandbyClockColor();
  res["slogan"] = TestDisplay::getStandbyCustomText();
  res["temp"] = TestDisplay::getStandbyTemp();
  res["humidity"] = TestDisplay::getStandbyHumidity();
  bool isWifiConn1 = (WiFi.status() == WL_CONNECTED);
  res["wifi_connected"] = isWifiConn1;
  res["ip"] = WifiManager::getIp();
  res["wifi_rssi"] = WifiManager::getRssi();
  if (isWifiConn1) {
    String s = WifiManager::getCurrentSsid();
    if (s.length() > 8) s = s.substring(0, 8);
    res["wifi_ssid"] = (s.length() > 0) ? s : "STA";
  } else {
    res["wifi_ssid"] = "AP MODE";
  }

  String out;
  serializeJson(res, out);
  server.send(200, "application/json", out);
}

static void handleKeypadStatus() {
  JsonDocument res;
  res["active_btn_idx"] = TestButtons::getActiveButtonIndex();
  res["last_button_str"] = TestButtons::getLastButtonName();
  res["adc_voltage"] = TestButtons::getLastAdcVoltage();
  res["adc_mv"] = TestButtons::getLastAdcMilliVolts();
  res["adc_raw"] = TestButtons::getLastAdcRaw();
  res["screen_mode"] = TestDisplay::getScreenModeName();
  res["current_mode_id"] = TestDisplay::getCurrentMode();
  res["menu_cursor"] = TestDisplay::getS40MenuCursor();
  res["screen_title"] = TestDisplay::getCurrentScreenTitle();
  res["subtitle"] = TestDisplay::getSubtitle();
  res["hud_style"] = TestDisplay::getHudStyleName();
  res["battery_v"] = TestSensors::getBatteryVoltage();
  res["battery_pct"] = TestSensors::getBatteryPercentage();
  res["is_charging"] = TestSensors::isCharging();
  res["bg_image"] = TestDisplay::getStandbyBgImage();
  res["bg_mode"] = TestDisplay::getStandbyBgMode();
  res["dim_overlay"] = TestDisplay::getStandbyDimOverlay();
  res["clock_color"] = TestDisplay::getStandbyClockColor();
  res["slogan"] = TestDisplay::getStandbyCustomText();
  res["temp"] = TestDisplay::getStandbyTemp();
  res["humidity"] = TestDisplay::getStandbyHumidity();
  bool isWifiConn2 = (WiFi.status() == WL_CONNECTED);
  res["wifi_connected"] = isWifiConn2;
  res["ip"] = WifiManager::getIp();
  res["wifi_rssi"] = WifiManager::getRssi();
  if (isWifiConn2) {
    String s = WifiManager::getCurrentSsid();
    if (s.length() > 8) s = s.substring(0, 8);
    res["wifi_ssid"] = (s.length() > 0) ? s : "STA";
  } else {
    res["wifi_ssid"] = "AP MODE";
  }

  String out;
  serializeJson(res, out);
  server.send(200, "application/json", out);
}

// ---------------- REST APIS CẤU HÌNH XIAOZHI AI ----------------

static void handleGetAIConfig() {
  JsonDocument doc;
  XiaoZhiClient::getAIConfig(doc);
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void handleSaveAIConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  bool ok = XiaoZhiClient::saveAIConfig(server.arg("plain"));
  if (ok) {
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Đã lưu cấu hình XiaoZhi AI thành công!\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Failed to save AI config\"}");
  }
}

static void handleXiaoZhiOTA() {
  if (!WifiManager::isConnected()) {
    server.send(400, "application/json", "{\"error\":\"ESP32 chưa nối Wi-Fi để truy cập XiaoZhi Cloud trực tiếp\",\"need_wifi\":true}");
    return;
  }
  String payload = XiaoZhiClient::queryOTA(true);
  if (payload.length() > 0) {
    server.send(200, "application/json", payload);
  } else {
    server.send(500, "application/json", "{\"error\":\"Lỗi kết nối máy chủ XiaoZhi\"}");
  }
}

static void handleXiaoZhiUnbind() {
  XiaoZhiClient::unbindDevice();
  if (WifiManager::isConnected()) {
    XiaoZhiClient::queryOTA(true);
  }
  JsonDocument res;
  res["status"] = "ok";
  res["bound"] = XiaoZhiClient::isDeviceBound();
  res["mac"] = WiFi.macAddress();
  res["otp"] = XiaoZhiClient::getLastAuthCode();
  String out;
  serializeJson(res, out);
  server.send(200, "application/json", out);
}

static void handleXiaoZhiChatStatus() {
  String stateStr = "idle";
  String userText = "";
  String aiReply = "";
  String emotion = "happy";
  uint8_t eyeState = 0;
  int vol = 80;
  int mic = 0;
  uint32_t turnId = 0;

  TestDisplay::getXiaoZhiChatState(stateStr, userText, aiReply, emotion, eyeState, vol, mic, turnId);

  JsonDocument doc;
  doc["state"] = stateStr;
  doc["user_text"] = userText;
  doc["ai_reply"] = aiReply;
  doc["emotion"] = emotion;
  doc["eye_state"] = eyeState;
  doc["volume_pct"] = vol;
  doc["mic_level"] = mic;
  doc["turn_id"] = turnId;
  doc["bound"] = XiaoZhiClient::isDeviceBound();
  doc["ws_connected"] = XiaoZhiClient::isWebSocketConnected();
  doc["mac"] = WiFi.macAddress();
  doc["otp"] = XiaoZhiClient::getLastAuthCode();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

static void handleXiaoZhiAction() {
  String action = server.hasArg("action") ? server.arg("action") : "";
  int volume = -1;

  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    if (doc["action"].is<const char*>()) action = doc["action"].as<String>();
    if (doc["volume"].is<int>()) volume = doc["volume"].as<int>();
  }

  if (action == "listen" || action == "toggle_listen") {
    TestDisplay::triggerXiaoZhiListeningFromWeb();
  } else if (action == "stop" || action == "send") {
    TestDisplay::stopXiaoZhiListeningFromWeb();
  } else if (action == "volume" || action == "set_volume") {
    if (volume >= 0 && volume <= 100) {
      TestAudio::setXiaoZhiVolumePct(volume);
    }
  }

  handleXiaoZhiChatStatus();
}

static void handleLittleFsUpload() {
  server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Uploaded to LittleFS\"}");
}

static void handleLittleFsUploadChunk() {
  HTTPUpload& upload = server.upload();
  static File f;
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    Serial.printf("📁 [LITTLEFS UPLOAD] Bắt đầu ghi file: %s\n", filename.c_str());
    if (LittleFS.exists(filename)) LittleFS.remove(filename);
    f = LittleFS.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (f) f.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (f) {
      f.close();
      Serial.printf("✅ [LITTLEFS UPLOAD] Ghi hoàn tất: %u bytes\n", upload.totalSize);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (f) f.close();
    Serial.println("⚠️ [LITTLEFS UPLOAD] Hủy tải lên LittleFS");
  }
}

// ---------------- REST APIS THÔNG SỐ PC HUD ----------------

static void handlePcMetricsGet() {
  JsonDocument doc;
  PcStatsManager::serializeMetrics(doc);
  String res;
  serializeJson(doc, res);
  server.send(200, "application/json", res);
}

static void handlePcMetricsPost() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  bool ok = PcStatsManager::updateMetrics(server.arg("plain"));
  if (ok) {
    server.send(200, "application/json", "{\"status\":\"ok\",\"is_live\":true}");
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
  }
}

// ================================================================
//     REST APIS ĐA PHƯƠNG TIỆN & THƯ MỤC /Musics TRÊN THẺ NHỚ SD
// ================================================================

static File mediaUploadFile;
static String lastUploadedMusicName = "";
static bool mediaUploadSuccess = false;

static void handleMediaStatus() {
  String trackName;
  int trackIdx = 0, trackCount = 0, volumePct = 75;
  bool playing = true, fromSd = false;
  uint8_t visMode = 0, repeatMode = 0;
  uint16_t curSec = 0, totalSec = 180;

  TestDisplay::getMediaPlayerState(trackName, trackIdx, trackCount, playing,
                                   visMode, repeatMode, curSec, totalSec, volumePct, fromSd);

  JsonDocument doc;
  doc["playing"]     = playing;
  doc["track_idx"]   = trackIdx;
  doc["track_name"]  = trackName;
  doc["track_count"] = trackCount;
  doc["cur_sec"]     = curSec;
  doc["total_sec"]   = totalSec;
  doc["volume"]      = volumePct;
  doc["vis_mode"]    = visMode;
  doc["repeat_mode"] = repeatMode;
  doc["from_sd"]     = fromSd;
  doc["sd_mounted"]  = TestSDCard::isMounted();
  doc["mic_level"]   = TestAudio::getLastMicLevelPct();
  doc["ui_theme"]    = TestDisplay::getUiThemeIdx();

  JsonArray specArr = doc["spectrum"].to<JsonArray>();
  const uint8_t* spec16 = TestAudio::getMicSpectrum16();
  for (int i = 0; i < 16; i++) {
    specArr.add(spec16 ? spec16[i] : 0);
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleMediaList() {
  String names[24];
  uint16_t durs[24];
  bool fromSd[24];
  size_t sizes[24];
  int cnt = TestDisplay::getMediaPlaylistItems(names, durs, fromSd, sizes, 24);

  JsonDocument doc;
  doc["sd_mounted"]  = TestSDCard::isMounted();
  doc["sd_total_mb"] = (uint32_t)TestSDCard::getTotalMB();
  doc["sd_used_mb"]  = (uint32_t)TestSDCard::getUsedMB();

  JsonArray arr = doc["tracks"].to<JsonArray>();
  for (int i = 0; i < cnt; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["index"]    = i;
    obj["name"]     = names[i];
    obj["duration"] = durs[i];
    obj["from_sd"]  = fromSd[i];
    obj["size_kb"]  = (uint32_t)(sizes[i] / 1024);
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleMediaControl() {
  if (server.hasArg("plain")) {
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain"))) {
      // 1. Nếu trình duyệt Web gửi phổ âm thanh thực FFT (spectrum[16]) từ file MP3/WAV đang phát
      if (doc["spectrum"].is<JsonArray>()) {
        JsonArray sp = doc["spectrum"].as<JsonArray>();
        uint8_t bands[16] = {0};
        for (int i = 0; i < 16 && i < (int)sp.size(); i++) {
          bands[i] = (uint8_t)constrain(sp[i].as<int>(), 0, 100);
        }
        int lv = doc["mic_level"] | 25;
        TestAudio::setExternalAudioSpectrum(bands, lv);
      }

      // 2. Nếu có lệnh điều khiển trình phát (play, pause, toggle, next, prev, select, seek, volume, vis, repeat, open_on_esp)
      if (!doc["action"].isNull()) {
        String action = doc["action"].as<String>();
        int val       = doc["value"] | 0;
        String name   = doc["name"] | "";
        TestDisplay::controlMediaPlayer(action, val, name);
      }
    }
  }
  handleMediaStatus();
}

static void handleMediaUploadChunk() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    mediaUploadSuccess = false;
    lastUploadedMusicName = upload.filename;
    int sl = lastUploadedMusicName.lastIndexOf('/');
    if (sl >= 0) lastUploadedMusicName = lastUploadedMusicName.substring(sl + 1);
    int bsl = lastUploadedMusicName.lastIndexOf('\\');
    if (bsl >= 0) lastUploadedMusicName = lastUploadedMusicName.substring(bsl + 1);

    if (TestSDCard::isMounted()) {
      if (!SD.exists("/Musics")) {
        SD.mkdir("/Musics");
      }
      String fullPath = "/Musics/" + lastUploadedMusicName;
      if (SD.exists(fullPath)) SD.remove(fullPath);
      mediaUploadFile = SD.open(fullPath, FILE_WRITE);
      Serial.printf("🎵 [SD /Musics] Đang nhận file nhạc: %s\n", fullPath.c_str());
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (mediaUploadFile && upload.currentSize > 0) {
      mediaUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (mediaUploadFile) {
      mediaUploadFile.close();
      mediaUploadSuccess = true;
      Serial.printf("✅ [SD /Musics] Đã lưu xong: /Musics/%s (%u bytes)\n",
                    lastUploadedMusicName.c_str(), (unsigned)upload.totalSize);
      TestDisplay::controlMediaPlayer("select", 0, lastUploadedMusicName);
    }
  }
}

static void handleMediaDelete() {
  String name = server.arg("name");
  int sl = name.lastIndexOf('/');
  if (sl >= 0) name = name.substring(sl + 1);
  if (name.length() > 0 && TestSDCard::isMounted()) {
    String p1 = "/Musics/" + name;
    String p2 = "/" + name;
    bool removed = false;
    if (SD.exists(p1)) removed = SD.remove(p1);
    else if (SD.exists(p2)) removed = SD.remove(p2);
    TestDisplay::controlMediaPlayer("refresh", 0);
    if (removed) {
      server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Đã xóa bài hát khỏi thư mục /Musics trên Thẻ SD!\"}");
      return;
    }
  }
  server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Không tìm thấy tệp nhạc trên Thẻ SD!\"}");
}

static void handleMediaStream() {
  String name = server.arg("name");
  int sl = name.lastIndexOf('/');
  if (sl >= 0) name = name.substring(sl + 1);
  if (name.length() > 0 && TestSDCard::isMounted()) {
    String path = "/Musics/" + name;
    if (!SD.exists(path)) path = "/" + name;
    if (SD.exists(path)) {
      File f = SD.open(path, FILE_READ);
      if (f) {
        String low = name;
        low.toLowerCase();
        String mime = "audio/mpeg";
        if (low.endsWith(".wav")) mime = "audio/wav";
        else if (low.endsWith(".ogg")) mime = "audio/ogg";
        else if (low.endsWith(".flac")) mime = "audio/flac";
        server.streamFile(f, mime);
        f.close();
        return;
      }
    }
  }
  server.send(404, "text/plain", "Music file not found on SD");
}

// ---------------- REST APIS STREAM ĐA PHƯƠNG TIỆN (VIDEO & AUDIO) ----------------

static uint8_t* s_streamJpegUploadBuf = nullptr;
static size_t s_streamJpegUploadLen = 0;
static const size_t MAX_STREAM_JPEG_UPLOAD = 64 * 1024;
static uint32_t s_logVideoFrameCount = 0;
static uint32_t s_logAudioPcmChunks = 0;

bool WebRoutes::hasAllocatedUploadBuffer() {
  return (s_streamJpegUploadBuf != nullptr);
}

size_t WebRoutes::trimMemory() {
  if (s_streamJpegUploadBuf == nullptr) return 0;
  if (WebStreamManager::isVideoStreaming()) return 0;

  size_t freed = MAX_STREAM_JPEG_UPLOAD;
  free(s_streamJpegUploadBuf);
  s_streamJpegUploadBuf = nullptr;
  s_streamJpegUploadLen = 0;
  Serial.printf("🧹 [TRIM WEB] Đã giải phóng bộ đệm upload video (%u KB)\n", (unsigned int)(freed / 1024));
  return freed;
}

static void handleStreamVideoUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (!s_streamJpegUploadBuf) {
      if (psramFound()) s_streamJpegUploadBuf = (uint8_t*)ps_malloc(MAX_STREAM_JPEG_UPLOAD);
      if (!s_streamJpegUploadBuf) s_streamJpegUploadBuf = (uint8_t*)malloc(MAX_STREAM_JPEG_UPLOAD);
    }
    s_streamJpegUploadLen = 0;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (s_streamJpegUploadBuf && s_streamJpegUploadLen + upload.currentSize <= MAX_STREAM_JPEG_UPLOAD) {
      memcpy(s_streamJpegUploadBuf + s_streamJpegUploadLen, upload.buf, upload.currentSize);
      s_streamJpegUploadLen += upload.currentSize;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (s_streamJpegUploadBuf && s_streamJpegUploadLen > 100) {
      s_logVideoFrameCount++;
      if (s_logVideoFrameCount == 1) {
        Serial.printf("🎬 [STREAM VIDEO] Bắt đầu nhận frame JPEG (%u bytes)\n", (unsigned)s_streamJpegUploadLen);
      } else if (s_logVideoFrameCount % 30 == 0) {
        Serial.printf("🎬 [STREAM VIDEO] Đang hiển thị mượt mà: frame #%u (%u bytes)\n",
                      (unsigned)s_logVideoFrameCount, (unsigned)s_streamJpegUploadLen);
      }
      WebStreamManager::queueVideoFrame(s_streamJpegUploadBuf, s_streamJpegUploadLen);
    }
  }
}

static void handleStreamVideoFinish() {
  if (server.hasArg("plain")) {
    const String& body = server.arg("plain");
    if (body.length() > 100) {
      s_logVideoFrameCount++;
      if (s_logVideoFrameCount == 1 || s_logVideoFrameCount % 30 == 0) {
        Serial.printf("🎬 [STREAM VIDEO (RAW)] Frame #%u (%u bytes)\n", (unsigned)s_logVideoFrameCount, (unsigned)body.length());
      }
      WebStreamManager::queueVideoFrame((const uint8_t*)body.c_str(), body.length());
    }
  }
  server.send(200, "text/plain", "1");
}

static int16_t s_audioCarryByte = -1;

static void handleStreamAudioUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    s_audioCarryByte = -1;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (upload.currentSize == 0) return;

    const uint8_t* p = upload.buf;
    size_t len = upload.currentSize;
    static int16_t s_alignedBuf[1024];
    size_t samplesInBuf = 0;

    // 1. Nếu gói trước bị lẻ 1 byte, ghép với byte đầu tiên của gói này tạo thành sample 16-bit
    if (s_audioCarryByte >= 0 && len > 0) {
      int16_t sample = (int16_t)(((uint16_t)(*p) << 8) | (uint8_t)s_audioCarryByte);
      s_alignedBuf[samplesInBuf++] = sample;
      p++;
      len--;
      s_audioCarryByte = -1;
    }

    // 2. Chuyển đổi các cặp byte 16-bit an toàn
    size_t pairs = len / 2;
    for (size_t i = 0; i < pairs; i++) {
      int16_t sample = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
      p += 2;
      s_alignedBuf[samplesInBuf++] = sample;
      if (samplesInBuf >= 1024) {
        s_logAudioPcmChunks++;
        if (s_logAudioPcmChunks == 1 || s_logAudioPcmChunks % 50 == 0) {
          Serial.printf("🎵 [STREAM AUDIO] Đang nạp PCM 16kHz vào RingBuffer (Chunk #%u, %u mẫu)\n",
                        (unsigned)s_logAudioPcmChunks, (unsigned)samplesInBuf);
        }
        WebStreamManager::playAudioPcmChunk(s_alignedBuf, samplesInBuf);
        samplesInBuf = 0;
      }
    }

    if (samplesInBuf > 0) {
      s_logAudioPcmChunks++;
      if (s_logAudioPcmChunks == 1 || s_logAudioPcmChunks % 50 == 0) {
        Serial.printf("🎵 [STREAM AUDIO] Đang nạp PCM 16kHz vào RingBuffer (Chunk #%u, %u mẫu)\n",
                      (unsigned)s_logAudioPcmChunks, (unsigned)samplesInBuf);
      }
      WebStreamManager::playAudioPcmChunk(s_alignedBuf, samplesInBuf);
      samplesInBuf = 0;
    }

    // 3. Nếu còn thừa 1 byte lẻ ở cuối gói, lưu lại cho gói tiếp theo
    if (len % 2 != 0) {
      s_audioCarryByte = *p;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    s_audioCarryByte = -1;
  }
}

static void handleStreamAudioFinish() {
  server.send(200, "text/plain", "1");
}

static void handleStreamStartUrl() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  String type = doc["type"] | "audio";
  String url = doc["url"] | "";
  bool ok = WebStreamManager::startUrlStream(type, url);
  server.send(ok ? 200 : 500, "application/json", ok ? "{\"status\":\"ok\"}" : "{\"error\":\"Failed to start URL stream\"}");
}

static void handleStreamStop() {
  Serial.println("🛑 [STREAM] Đã nhận lệnh dừng toàn bộ luồng stream từ Web / Transcoder!");
  s_logVideoFrameCount = 0;
  s_logAudioPcmChunks = 0;
  WebStreamManager::stopAll();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleStreamStatus() {
  JsonDocument doc;
  WebStreamManager::getStatusJson(doc);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// 6d. REST APIs Thời tiết Real-Time & Telemetry (1MB PSRAM Circular Buffer)
static void handleGetWeather() {
  const WeatherData& w = WeatherManager::getData();
  JsonDocument doc;
  doc["status"] = "ok";
  doc["isAvailable"] = WeatherManager::isAvailable();
  doc["city"] = w.cityName;
  doc["lat"] = w.latitude;
  doc["lon"] = w.longitude;
  doc["temp"] = w.outdoorTemp;
  doc["humidity"] = w.outdoorHumidity;
  doc["condition"] = w.conditionText;
  doc["weatherCode"] = w.weatherCode;
  doc["windSpeed"] = w.windSpeed;
  doc["iconType"] = (int)w.iconType;
  doc["iconStr"] = WeatherManager::getIconTypeString();
  doc["lastUpdateEpoch"] = w.lastUpdateEpoch;
  doc["lastFetchMs"] = w.lastFetchMs;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleRefreshWeather() {
  bool started = WeatherManager::refreshNow();
  JsonDocument doc;
  doc["status"] = started ? "ok" : "busy_or_offline";
  doc["fetching"] = started;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleTerminalExec() {
  ApiTerminal::handleExec(server);
}

void WebRoutes::begin() {
  // Thu thập các Headers xác thực (Cookie, Authorization, X-Auth-Token)
  const char* headerkeys[] = {"Cookie", "Authorization", "X-Auth-Token"};
  server.collectHeaders(headerkeys, 3);

  // Khởi tạo hệ thống xác thực WebAuth
  WebAuth::init();

  // 1. Phục vụ trang chủ "/"
  server.on("/", HTTP_GET, handleRoot);

  // Đăng ký toàn bộ REST APIs Hệ Thống, Auth, OTA, Logs, Memory, Telemetry
  ApiSystem::registerRoutes(server);
  server.on("/api/terminal/exec", HTTP_POST, handleTerminalExec);


  server.on("/api/weather", HTTP_GET, handleGetWeather);
  server.on("/api/weather/refresh", HTTP_POST, handleRefreshWeather);

  // 2. Các REST API Mạng & Hệ thống
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/saved", HTTP_GET, handleGetSavedWifi);
  server.on("/api/wifi/connect", HTTP_POST, handleWifiConnect);
  server.on("/api/wifi/delete", HTTP_POST, handleDeleteSavedWifi);
  server.on("/api/decor/status", HTTP_GET, handleDecorStatus);
  server.on("/api/decor/control", HTTP_POST, handleDecorControl);
  server.on("/api/keypad/press", HTTP_POST, handleKeypadPress);
  server.on("/api/keypad/status", HTTP_GET, handleKeypadStatus);

  // 3. Các REST API Quản Lý Ảnh (Hỗ trợ cả Thẻ nhớ SD /sd_images + /sd_rgb565 và LittleFS)
  server.on("/api/images", HTTP_GET, handleListImages);
  server.on("/api/sd/file", HTTP_GET, handleSdFileRoute);
  auto finishUploadSync = []() {
    if (uploadAllowed) {
      String activeImg = ImageManager::getActiveImage();
      if (activeImg.length() > 0) {
        JsonDocument doc;
        if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
          File rFile = LittleFS.open(FILE_STANDBY_CONFIG, "r");
          if (rFile) {
            deserializeJson(doc, rFile);
            rFile.close();
          }
        }
        doc["bg_mode"] = "image";
        doc["bg_image"] = activeImg;
        File wFile = LittleFS.open(FILE_STANDBY_CONFIG, "w");
        if (wFile) {
          serializeJson(doc, wFile);
          wFile.close();
        }
      }
      TestDisplay::reloadStandbyConfig();
      server.send(200, "application/json", "{\"status\":\"success\",\"active_img\":\"" + activeImg + "\",\"message\":\"Đã convert & lưu ảnh vào Thẻ nhớ SD (/sd_images + /sd_rgb565) và đẩy lên màn hình ST7789!\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Đã đạt giới hạn tối đa 15 ảnh trên LittleFS. Hãy bật lưu sang Thẻ nhớ SD hoặc xóa bớt ảnh cũ!\"}");
    }
  };
  server.on("/api/images/upload", HTTP_POST, finishUploadSync, handleImageUploadRoute);
  server.on("/api/sd/upload", HTTP_POST, finishUploadSync, handleSdUploadRoute);
  server.on("/api/images/delete", HTTP_POST, handleDeleteImage);
  server.on("/api/images/select", HTTP_POST, handleSelectImage);

  // 4. Các REST API Cấu hình Màn hình chờ & Chuyển đổi Chế độ Màn hình ST7789
  server.on("/api/screen/standby", HTTP_GET, handleGetStandbyConfig);
  server.on("/api/screen/standby", HTTP_POST, handleSaveStandbyConfig);
  server.on("/api/screen/state", HTTP_POST, handleScreenStatePost);

  // 4b. Các REST API Đa Phương Tiện & Trình Phát Nhạc (/Musics trên Thẻ SD)
  server.on("/api/media/status", HTTP_GET, handleMediaStatus);
  server.on("/api/media/list", HTTP_GET, handleMediaList);
  server.on("/api/media/control", HTTP_POST, handleMediaControl);
  server.on("/api/media/upload", HTTP_POST, []() {
    if (mediaUploadSuccess) {
      server.send(200, "application/json", "{\"status\":\"ok\",\"file\":\"" + lastUploadedMusicName + "\",\"message\":\"Đã lưu vào /Musics trên Thẻ SD!\"}");
    } else {
      server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Không thể ghi vào Thẻ nhớ SD (/Musics)!\"}");
    }
  }, handleMediaUploadChunk);
  server.on("/api/media/delete", HTTP_POST, handleMediaDelete);
  server.on("/api/media/stream", HTTP_GET, handleMediaStream);

  // 5. Các REST API Cấu hình XiaoZhi AI & OTA
  server.on("/api/config/ai", HTTP_GET, handleGetAIConfig);
  server.on("/api/config/ai", HTTP_POST, handleSaveAIConfig);
  server.on("/api/xiaozhi/ota", HTTP_GET, handleXiaoZhiOTA);
  server.on("/api/xiaozhi/ota", HTTP_POST, handleXiaoZhiOTA);
  server.on("/api/xiaozhi/unbind", HTTP_GET, handleXiaoZhiUnbind);
  server.on("/api/xiaozhi/unbind", HTTP_POST, handleXiaoZhiUnbind);
  server.on("/api/xiaozhi/chat_status", HTTP_GET, handleXiaoZhiChatStatus);
  server.on("/api/xiaozhi/action", HTTP_POST, handleXiaoZhiAction);
  server.on("/api/littlefs/upload", HTTP_POST, handleLittleFsUpload, handleLittleFsUploadChunk);

  // 6. Các REST API Thông số PC (PC Status HUD)
  server.on("/api/pc/metrics", HTTP_GET, handlePcMetricsGet);
  server.on("/api/pc/metrics", HTTP_POST, handlePcMetricsPost);

  // 6b. Các REST API Cập Nhật Firmware OTA & Thông Tin Hệ Thống



  // 6b3. REST APIs Stream Đa Phương Tiện (Web Browser / URL Video & Audio Stream)
  server.on("/api/stream/video_frame", HTTP_POST, handleStreamVideoFinish, handleStreamVideoUpload);
  server.on("/api/stream/audio_pcm", HTTP_POST, handleStreamAudioFinish, handleStreamAudioUpload);
  server.on("/api/stream/start_url", HTTP_POST, handleStreamStartUrl);
  server.on("/api/stream/stop", HTTP_POST, handleStreamStop);
  server.on("/api/stream/status", HTTP_GET, handleStreamStatus);

  // 6b4. REST APIs Nhật Ký Hệ Thống & Thẻ Nhớ SD (Smartphone-Style Syslog)

  // 6c. Captive Portal Probes cho iOS / Android / Windows (Tự động mở trang Cấu hình Wi-Fi)
  auto handleCaptiveRedirect = []() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  };
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect); // Apple iOS / macOS
  server.on("/generate_204", HTTP_GET, handleCaptiveRedirect);        // Google Android
  server.on("/gen_204", HTTP_GET, handleCaptiveRedirect);             // Chrome / Android
  server.on("/ncsi.txt", HTTP_GET, handleCaptiveRedirect);            // Windows
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);     // Windows
  server.on("/canonical.html", HTTP_GET, handleCaptiveRedirect);

  // 7. Phục vụ file tĩnh từ cả Thẻ nhớ SD (/sd_images/...) và LittleFS
  server.serveStatic("/", LittleFS, "/");
  server.onNotFound([handleCaptiveRedirect]() {
    String uri = server.uri();

    // 1. Kiểm tra nếu là Captive Portal Probes từ các hệ điều hành điện thoại
    if (uri.indexOf("hotspot-detect") >= 0 ||
        uri.indexOf("generate_204") >= 0 ||
        uri.indexOf("gen_204") >= 0 ||
        uri.indexOf("ncsi.txt") >= 0 ||
        uri.indexOf("connecttest") >= 0 ||
        uri.indexOf("canonical") >= 0 ||
        uri.indexOf("mobile/status") >= 0) {
      handleCaptiveRedirect();
      return;
    }

    if (uri.startsWith("/api/")) {
      server.send(404, "application/json", "{\"error\":\"API Not Found\"}");
      return;
    }

    if (uri.startsWith("/sd:")) uri = uri.substring(4);
    else if (uri.startsWith("sd:")) uri = uri.substring(3);
    if (!uri.startsWith("/")) uri = "/" + uri;

    if (TestSDCard::isMounted() && SD.exists(uri)) {
      File f = SD.open(uri, FILE_READ);
      if (f) {
        String contentType = "application/octet-stream";
        String low = uri;
        low.toLowerCase();
        if (low.endsWith(".png")) contentType = "image/png";
        else if (low.endsWith(".jpg") || low.endsWith(".jpeg")) contentType = "image/jpeg";
        else if (low.endsWith(".bmp")) contentType = "image/bmp";
        server.streamFile(f, contentType);
        f.close();
        return;
      }
    }
    if (LittleFS.exists(uri)) {
      File f = LittleFS.open(uri, "r");
      if (f) {
        String contentType = "application/octet-stream";
        String low = uri;
        low.toLowerCase();
        if (low.endsWith(".png")) contentType = "image/png";
        else if (low.endsWith(".jpg") || low.endsWith(".jpeg")) contentType = "image/jpeg";
        server.streamFile(f, contentType);
        f.close();
        return;
      }
    }

    // 2. Chuyển hướng Captive Portal nếu người dùng truy cập domain bất kỳ khi kết nối SoftAP
    String host = server.hostHeader();
    int colonIdx = host.indexOf(':');
    String hostNoPort = (colonIdx > 0) ? host.substring(0, colonIdx) : host;

    if (WifiManager::isApActive() && hostNoPort.length() > 0 &&
        hostNoPort != "192.168.4.1" &&
        (WiFi.status() != WL_CONNECTED || hostNoPort != WiFi.localIP().toString()) &&
        hostNoPort != (String(MDNS_HOSTNAME) + ".local")) {
      handleCaptiveRedirect();
      return;
    }

    server.send(404, "text/plain", "Not Found: " + uri);
  });

  server.begin();
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("🌐 mDNS ready: http://%s.local\n", MDNS_HOSTNAME);
  }
}

void WebRoutes::handleClient() {
  server.handleClient();
}

