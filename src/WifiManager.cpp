#include "WifiManager.h"
#include "Config.h"
#include "HardwareManager.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>

static Preferences wifiPrefs;
static String lastWifiNotification = "";
static DNSServer dnsServer;
static bool s_dnsStarted = false;
static bool s_apActive = false;

void WifiManager::begin() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASS);
  s_apActive = true;

  // Khởi động DNS Captive Portal cho SoftAP (Trả về 192.168.4.1 cho mọi truy vấn tên miền)
  dnsServer.start(53, "*", WiFi.softAPIP());
  s_dnsStarted = true;
  Serial.printf("🛡️ [CAPTIVE PORTAL] DNS Server đã kích hoạt trên cổng 53 (AP IP: %s)\n",
                WiFi.softAPIP().toString().c_str());

  bool connected = smartConnect();
  if (connected) {
    Serial.printf("\n✅ Wi-Fi đã kết nối thành công! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("\n📡 ESP32 đang chạy chế độ AP (%s, IP: 192.168.4.1)\n", DEFAULT_AP_SSID);
    Serial.println("💡 Vui lòng truy cập http://192.168.4.1 để cấu hình mạng Wi-Fi.");
  }
}

void WifiManager::loadSavedWifiList(JsonDocument& doc) {
  if (!LittleFS.exists(FILE_WIFI_KNOWN)) {
    doc.to<JsonArray>();
    return;
  }
  File file = LittleFS.open(FILE_WIFI_KNOWN, "r");
  if (!file) {
    doc.to<JsonArray>();
    return;
  }
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err || !doc.is<JsonArray>()) {
    doc.to<JsonArray>();
  }
}

void WifiManager::saveSavedWifiList(const JsonDocument& doc) {
  File file = LittleFS.open(FILE_WIFI_KNOWN, "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
}

bool WifiManager::saveOrUpdateWifiNetwork(const String& ssid, const String& password) {
  if (ssid.length() == 0) return false;
  JsonDocument doc;
  loadSavedWifiList(doc);
  JsonArray arr = doc.as<JsonArray>();
  
  bool found = false;
  for (JsonObject item : arr) {
    if (item["ssid"].as<String>() == ssid) {
      if (password.length() > 0) {
        item["password"] = password;
      }
      item["updated_at"] = (uint32_t)millis();
      found = true;
      break;
    }
  }
  if (!found) {
    JsonObject newNet = arr.add<JsonObject>();
    newNet["ssid"] = ssid;
    newNet["password"] = password;
    newNet["created_at"] = (uint32_t)millis();
  }
  saveSavedWifiList(doc);

  // Lưu vào Preferences như kênh dự phòng
  wifiPrefs.begin("wifi", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", password);
  wifiPrefs.end();

  return true;
}

bool WifiManager::deleteSavedWifiNetwork(const String& ssid) {
  JsonDocument doc;
  loadSavedWifiList(doc);
  if (!doc.is<JsonArray>()) return false;
  JsonArray arr = doc.as<JsonArray>();
  
  JsonDocument newDoc;
  JsonArray newArr = newDoc.to<JsonArray>();
  bool found = false;
  for (JsonObject item : arr) {
    if (item["ssid"].as<String>() != ssid) {
      newArr.add(item);
    } else {
      found = true;
    }
  }
  if (found) {
    saveSavedWifiList(newDoc);
  }
  return found;
}

bool WifiManager::smartConnect() {
  Serial.println("\n🌐 [Smart-WiFi] Bắt đầu quét và tự chọn Wi-Fi thông minh...");
  HardwareManager::setLedColor(255, 200, 0); // Vàng đang dò

  JsonDocument savedDoc;
  loadSavedWifiList(savedDoc);
  JsonArray savedArr = savedDoc.as<JsonArray>();

  // Nếu trong LittleFS chưa có mạng nào, thử lấy từ Preferences
  if (savedArr.size() == 0) {
    wifiPrefs.begin("wifi", true);
    String pSsid = wifiPrefs.getString("ssid", "");
    String pPass = wifiPrefs.getString("pass", "");
    wifiPrefs.end();
    if (pSsid.length() > 0) {
      JsonObject obj = savedArr.add<JsonObject>();
      obj["ssid"] = pSsid;
      obj["password"] = pPass;
      saveSavedWifiList(savedDoc);
    }
  }

  if (savedArr.size() == 0) {
    Serial.println("ℹ️ [Smart-WiFi] Chưa có mạng Wi-Fi nào được lưu. Chuyển sang chế độ phát Hotspot AP!");
    return false;
  }

  // Quét mạng 2.4GHz xung quanh
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, true);
  if (n <= 0) {
    Serial.println("⚠️ [Smart-WiFi] Không quét thấy mạng Wi-Fi nào xung quanh!");
    return false;
  }

  struct WifiCandidate {
    String ssid;
    String pass;
    int rssi;
  };
  std::vector<WifiCandidate> candidates;

  for (int i = 0; i < n; i++) {
    String scannedSsid = WiFi.SSID(i);
    int scannedRssi = WiFi.RSSI(i);
    for (JsonObject saved : savedArr) {
      if (saved["ssid"].as<String>() == scannedSsid) {
        WifiCandidate c;
        c.ssid = scannedSsid;
        c.pass = saved["password"].as<String>();
        c.rssi = scannedRssi;
        candidates.push_back(c);
        break;
      }
    }
  }

  WiFi.scanDelete();

  // Sắp xếp theo RSSI giảm dần (mạng mạnh nhất lên đầu)
  std::sort(candidates.begin(), candidates.end(), [](const WifiCandidate& a, const WifiCandidate& b) {
    return a.rssi > b.rssi;
  });

  if (candidates.empty()) {
    Serial.printf("ℹ️ [Smart-WiFi] Tìm thấy %d mạng xung quanh nhưng không khớp mạng đã lưu nào.\n", n);
    return false;
  }

  // Thử kết nối lần lượt các ứng viên theo thứ tự sóng mạnh nhất
  for (const auto& target : candidates) {
    Serial.printf("🚀 [Smart-WiFi] Thử kết nối tới '%s' (Sóng: %d dBm)...\n", target.ssid.c_str(), target.rssi);
    WiFi.disconnect(false);
    delay(100);
    WiFi.begin(target.ssid.c_str(), target.pass.c_str());

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("✅ [Smart-WiFi] Kết nối THÀNH CÔNG tới '%s'! IP: %s\n", target.ssid.c_str(), WiFi.localIP().toString().c_str());
      HardwareManager::setLedColor(0, 255, 120); // Xanh lá
      lastWifiNotification = "Đã kết nối: " + target.ssid + " (" + WiFi.localIP().toString() + ")";
      return true;
    } else {
      Serial.printf("❌ [Smart-WiFi] Không thể kết nối tới '%s'. Thử mạng tiếp theo...\n", target.ssid.c_str());
    }
  }

  return false;
}

bool WifiManager::connectDirect(const String& ssid, const String& password, bool save) {
  if (ssid.length() == 0) return false;

  String actualPass = password;
  // Nếu mật khẩu để trống, tìm mật khẩu đã lưu
  if (actualPass.length() == 0) {
    JsonDocument sDoc;
    loadSavedWifiList(sDoc);
    for (JsonObject item : sDoc.as<JsonArray>()) {
      if (item["ssid"].as<String>() == ssid) {
        actualPass = item["password"].as<String>();
        break;
      }
    }
  }

  if (save) {
    saveOrUpdateWifiNetwork(ssid, actualPass);
  }

  Serial.printf("🚀 Đang kết nối tới Wi-Fi: %s...\n", ssid.c_str());
  HardwareManager::setLedColor(255, 200, 0); // Vàng đang kết nối

  WiFi.disconnect(false);
  delay(150);
  WiFi.begin(ssid.c_str(), actualPass.c_str());

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    HardwareManager::setLedColor(0, 255, 120); // Xanh lá
    lastWifiNotification = "Đã kết nối: " + ssid + " (" + WiFi.localIP().toString() + ")";
    Serial.printf("✅ Đã kết nối thành công tới %s! IP: %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
    return true;
  } else {
    HardwareManager::setLedColor(255, 50, 50); // Đỏ báo lỗi
    Serial.println("❌ Kết nối thất bại!");
    return false;
  }
}

void WifiManager::scanNetworks(JsonDocument& resDoc) {
  Serial.println("\n🔍 Bắt đầu quét mạng Wi-Fi 2.4GHz...");
  WiFi.scanDelete();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(false);
  }
  delay(100);

  int n = WiFi.scanNetworks(false, true);
  if (n < 0) {
    Serial.printf("⚠️ Quét lần 1 thất bại (mã: %d), thử lại...\n", n);
    WiFi.scanDelete();
    delay(200);
    n = WiFi.scanNetworks(false, true);
  }

  Serial.printf("🔍 Quét hoàn tất: Tìm thấy %d mạng Wi-Fi\n", n);

  JsonDocument savedDoc;
  loadSavedWifiList(savedDoc);
  JsonArray savedArr = savedDoc.as<JsonArray>();

  JsonArray array = resDoc.to<JsonArray>();
  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      String s = WiFi.SSID(i);
      if (s.length() > 0) {
        JsonObject obj = array.add<JsonObject>();
        obj["ssid"] = s;
        obj["rssi"] = WiFi.RSSI(i);
        obj["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

        bool isSaved = false;
        for (JsonObject saved : savedArr) {
          if (saved["ssid"].as<String>() == s) {
            isSaved = true;
            break;
          }
        }
        obj["saved"] = isSaved;
        obj["connected"] = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == s);
        Serial.printf("   - %s (%d dBm)%s%s\n",
                      s.c_str(),
                      WiFi.RSSI(i),
                      isSaved ? " [Đã lưu]" : "",
                      (WiFi.status() == WL_CONNECTED && WiFi.SSID() == s) ? " [Đang kết nối]" : "");
      }
    }
  }
  WiFi.scanDelete();
}

bool WifiManager::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::getCurrentSsid() {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
}

int WifiManager::getRssi() {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
}

String WifiManager::getIp() {
  return (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

String WifiManager::getLastNotification() {
  return lastWifiNotification;
}

void WifiManager::setLastNotification(const String& msg) {
  lastWifiNotification = msg;
}

bool WifiManager::isApActive() {
  return s_apActive;
}

void WifiManager::loop() {
  if (s_dnsStarted) {
    dnsServer.processNextRequest();
  }
}


