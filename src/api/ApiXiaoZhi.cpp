#include "api/ApiXiaoZhi.h"
#include "XiaoZhiClient.h"
#include "WifiManager.h"
#include "tests/TestDisplay.h"
#include "tests/TestAudio.h"
#include <ArduinoJson.h>
#include <WiFi.h>

namespace ApiXiaoZhi {

  void handleGetAIConfig(WebServer& server) {
    JsonDocument doc;
    XiaoZhiClient::getAIConfig(doc);
    String res;
    serializeJson(doc, res);
    server.send(200, "application/json", res);
  }

  void handleSaveAIConfig(WebServer& server) {
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

  void handleXiaoZhiOTA(WebServer& server) {
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

  void handleXiaoZhiUnbind(WebServer& server) {
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

  void handleXiaoZhiChatStatus(WebServer& server) {
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

  void handleXiaoZhiAction(WebServer& server) {
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

    handleXiaoZhiChatStatus(server);
  }

  void registerRoutes(WebServer& server) {
    server.on("/api/config/ai", HTTP_GET, [&server]() { handleGetAIConfig(server); });
    server.on("/api/config/ai", HTTP_POST, [&server]() { handleSaveAIConfig(server); });
    server.on("/api/xiaozhi/ota", HTTP_GET, [&server]() { handleXiaoZhiOTA(server); });
    server.on("/api/xiaozhi/ota", HTTP_POST, [&server]() { handleXiaoZhiOTA(server); });
    server.on("/api/xiaozhi/unbind", HTTP_GET, [&server]() { handleXiaoZhiUnbind(server); });
    server.on("/api/xiaozhi/unbind", HTTP_POST, [&server]() { handleXiaoZhiUnbind(server); });
    server.on("/api/xiaozhi/chat_status", HTTP_GET, [&server]() { handleXiaoZhiChatStatus(server); });
    server.on("/api/xiaozhi/action", HTTP_POST, [&server]() { handleXiaoZhiAction(server); });
  }

} // namespace ApiXiaoZhi

