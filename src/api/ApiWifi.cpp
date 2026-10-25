#include "api/ApiWifi.h"
#include "WifiManager.h"
#include "WeatherManager.h"
#include "WebAuth.h"
#include <ArduinoJson.h>

namespace ApiWifi {

  void handleWifiScan(WebServer& server) {
    JsonDocument doc;
    WifiManager::scanNetworks(doc);

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  }

  void handleGetSavedWifi(WebServer& server) {
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

  void handleDeleteSavedWifi(WebServer& server) {
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

  void handleWifiConnect(WebServer& server) {
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

  void handleGetWeather(WebServer& server) {
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

  void handleRefreshWeather(WebServer& server) {
    bool started = WeatherManager::refreshNow();
    JsonDocument doc;
    doc["status"] = started ? "ok" : "busy_or_offline";
    doc["fetching"] = started;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void registerRoutes(WebServer& server) {
    server.on("/api/wifi/scan", HTTP_GET, [&server]() { handleWifiScan(server); });
    server.on("/api/wifi/saved", HTTP_GET, [&server]() { handleGetSavedWifi(server); });
    server.on("/api/wifi/connect", HTTP_POST, [&server]() { handleWifiConnect(server); });
    server.on("/api/wifi/delete", HTTP_POST, [&server]() { handleDeleteSavedWifi(server); });
    server.on("/api/weather", HTTP_GET, [&server]() { handleGetWeather(server); });
    server.on("/api/weather/refresh", HTTP_POST, [&server]() { handleRefreshWeather(server); });
  }

} // namespace ApiWifi

