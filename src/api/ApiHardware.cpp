#include "api/ApiHardware.h"
#include "BuiltinEmojis.h"
#include "Config.h"
#include "tests/TestDisplay.h"
#include "tests/TestSensors.h"
#include "tests/TestButtons.h"
#include "tests/TestWindmill.h"
#include "drivers/CobLedDriver.h"
#include "ImageManager.h"
#include "PcStatsManager.h"
#include "WifiManager.h"
#include "WebAuth.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

namespace ApiHardware {

  void handleGetStandbyConfig(WebServer& server) {
    if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
      File file = LittleFS.open(FILE_STANDBY_CONFIG, "r");
      server.streamFile(file, "application/json");
      file.close();
    } else {
      server.send(200, "application/json", "{\"theme\":\"cyberpunk\",\"clock_style\":\"digital\",\"clock_color\":\"#38bdf8\",\"clock_format\":\"24h\",\"show_seconds\":true,\"clock_pos\":\"center\",\"show_date\":true,\"date_format\":\"vi\",\"show_weather\":true,\"temp\":28.5,\"humidity\":65,\"custom_text\":\"Trạm Decor Vũ Trụ ✨\",\"text_color\":\"#94a3b8\",\"bg_mode\":\"gradient\",\"bg_color\":\"#0a0f1d\",\"bg_image\":\"\"}");
    }
  }

  void handleSaveStandbyConfig(WebServer& server) {
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

    if (doc["bg_image"].is<const char*>()) {
      String bgImg = doc["bg_image"].as<String>();
      if (bgImg.length() > 0) {
        ImageManager::selectActiveImage(bgImg);
      }
    }

    TestDisplay::reloadStandbyConfig();
    Serial.println("✅ Đã cập nhật cấu hình màn hình chờ vào LittleFS & đồng bộ lên màn hình ST7789!");
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Đã lưu và đẩy cấu hình lên màn hình chờ ST7789!\"}");
  }

  void handleScreenStatePost(WebServer& server) {
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

    if (doc["emoji_style"].is<int>()) {
      TestDisplay::setEmojiStyle((uint8_t)doc["emoji_style"].as<int>());
    } else if (doc["emoji_style"].is<const char*>()) {
      String st = doc["emoji_style"].as<String>();
      st.toLowerCase();
      if (st == "cute_girl" || st == "chibi") TestDisplay::setEmojiStyle(1);
      else if (st == "doi_mat" || st == "robot") TestDisplay::setEmojiStyle(2);
      else TestDisplay::setEmojiStyle(0);
    }

    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }

  void handleDecorStatus(WebServer& server) {
    JsonDocument doc;

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

    JsonObject wm = doc["windmill"].to<JsonObject>();
    wm["pin"] = PIN_L298N_IN1_MOTOR;
    wm["mode"] = TestWindmill::getMode();
    wm["mode_name"] = TestWindmill::getModeName();
    wm["speed"] = TestWindmill::getMotorSpeed();
    wm["breathing"] = TestWindmill::isBreathing();

    JsonObject fl = doc["flowing_leds"].to<JsonObject>();
    fl["pin_left"] = PIN_FLOWING_LED_L;
    fl["pin_right"] = PIN_FLOWING_LED_R;
    fl["is_on"] = CobLeds::isOn();
    fl["mode"] = CobLeds::getCurrentMode();
    fl["mode_name"] = CobLeds::getModeName();

    JsonObject bat = doc["battery"].to<JsonObject>();
    bat["pin"] = PIN_BAT_ADC;
    bat["adc_channel"] = "ADC1_CH1";
    bat["voltage"] = serialized(String(TestSensors::getBatteryVoltage(), 2));
    bat["millivolts"] = TestSensors::getBatteryMilliVolts();
    bat["percent"] = TestSensors::getBatteryPercentage();
    bat["charging_pin"] = PIN_CHG_STAT;
    bat["is_charging"] = TestSensors::isCharging();
    bat["charging_source"] = TestSensors::isCharging() ? "Vin MH-CD42 (HIGH - Đang Sạc)" : "Pin Lithium 18650 (Vin LOW)";

    JsonObject tch = doc["touch"].to<JsonObject>();
    tch["pin"] = PIN_TOUCH_TTP223;
    tch["pressed"] = TestSensors::isTouchPressed();

    String res;
    serializeJson(doc, res);
    server.send(200, "application/json", res);
  }

  void handleDecorControl(WebServer& server) {
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

    handleDecorStatus(server);
  }

  void handleKeypadPress(WebServer& server) {
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
    res["emoji_style"] = TestDisplay::getEmojiStyle();
    res["emoji_style_name"] = TestDisplay::getEmojiStyleName();
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

  void handleKeypadStatus(WebServer& server) {
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

  void handlePcMetricsGet(WebServer& server) {
    JsonDocument doc;
    PcStatsManager::serializeMetrics(doc);
    String res;
    serializeJson(doc, res);
    server.send(200, "application/json", res);
  }

  void handlePcMetricsPost(WebServer& server) {
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

  void registerRoutes(WebServer& server) {
    server.on("/api/screen/standby", HTTP_GET, [&server]() { handleGetStandbyConfig(server); });
    server.on("/api/screen/standby", HTTP_POST, [&server]() { handleSaveStandbyConfig(server); });
    server.on("/api/screen/state", HTTP_POST, [&server]() { handleScreenStatePost(server); });

    server.on("/api/decor/status", HTTP_GET, [&server]() { handleDecorStatus(server); });
    server.on("/api/decor/control", HTTP_POST, [&server]() { handleDecorControl(server); });

    server.on("/api/keypad/press", HTTP_POST, [&server]() { handleKeypadPress(server); });
    server.on("/api/keypad/status", HTTP_GET, [&server]() { handleKeypadStatus(server); });

    server.on("/api/pc/metrics", HTTP_GET, [&server]() { handlePcMetricsGet(server); });
    server.on("/api/pc/metrics", HTTP_POST, [&server]() { handlePcMetricsPost(server); });

    server.on("/api/emoji_preview", HTTP_GET, [&server]() {
      int style = server.hasArg("style") ? server.arg("style").toInt() : 1;
      String name = server.hasArg("name") ? server.arg("name") : "happy";
      size_t len = 0;
      const uint8_t* data = nullptr;
      if (style == 1) {
        data = BuiltinEmojis::getCuteGirlJpg(name, len);
      } else {
        data = BuiltinEmojis::getDoiMatJpg(name, len);
      }
      if (data && len > 0) {
        server.sendHeader("Content-Type", "image/jpeg");
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.send_P(200, "image/jpeg", (const char*)data, len);
      } else {
        server.send(404, "text/plain", "Not found");
      }
    });
  }

} // namespace ApiHardware

