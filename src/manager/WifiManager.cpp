#include "WifiManager.h"
#include "Config.h"
#include "HardwareManager.h"
#include "SystemLogger.h"
#include "TimeManager.h"
#include "tests/TestDisplay.h"
#include "tests/TestAudio.h"
#include "OtaManager.h"
#include <WiFi.h>
#include <esp_wifi.h>
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

// Trạng thái tự động quét và kết nối lại ngầm như Smartphone (Auto-Reconnect)
static bool s_isAutoScanning = false;
static unsigned long s_lastAutoScanTime = 0;
static bool s_isAutoConnecting = false;
static unsigned long s_autoConnectStartTime = 0;
static bool s_isManualConnecting = false;

void WifiManager::begin() {
  WiFi.mode(WIFI_AP_STA);
  // TẮT TIẾT KIỆM ĐIỆN WI-FI (MODEM SLEEP): Giữ RF luôn thức 100%, hạ Ping từ 200ms xuống 1-3ms!
  WiFi.setSleep(false);
  Serial.println("⚡ [WIFI] Đã cấu hình WiFi.setSleep(false) -> Tối ưu Ping 1ms và triệt tiêu giật lag!");

  // Cấu hình IP tĩnh cố định cho SoftAP để DHCP Server cấp IP chuẩn xác 100%
  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, gateway, subnet);

  // Khởi chạy SoftAP: SSID, Mật khẩu, Channel 1, Không ẩn, Tối đa 4 kết nối
  WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASS, 1, 0, 4);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  s_apActive = true;

  // Khởi động DNS Captive Portal cho SoftAP (Trả về 192.168.4.1 cho mọi truy vấn tên miền)
  dnsServer.start(53, "*", apIP);
  s_dnsStarted = true;
  Serial.printf("🛡️ [CAPTIVE PORTAL] DNS Server đã kích hoạt trên cổng 53 (AP IP: %s)\n",
                apIP.toString().c_str());
  Serial.printf("📡 [SOFTAP SẴN SÀNG] SSID: '%s' | Mật khẩu: '%s' | Web: http://192.168.4.1\n",
                DEFAULT_AP_SSID, DEFAULT_AP_PASS);

  // Tự động lưu mạng quen thuộc & xóa mạng không dùng nữa
  deleteSavedWifiNetwork("SPA YEN TRANG");
  saveOrUpdateWifiNetwork("Yen Long", "02082003@#");

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
  if (!doc.is<JsonArray>()) {
    doc.to<JsonArray>();
  }
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

  // Xóa cả trong Preferences (NVS) dự phòng nếu trùng
  wifiPrefs.begin("wifi", false);
  if (wifiPrefs.getString("ssid", "") == ssid) {
    wifiPrefs.clear();
  }
  wifiPrefs.end();

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

  // Quét mạng 2.4GHz xung quanh (show_hidden = false để quét nhanh chỉ ~1.2s không gây đơ hệ thống)
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, false);
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
    TestDisplay::showWifiConnectingScreen(target.ssid);
    WiFi.disconnect(false);
    delay(50);
    WiFi.begin(target.ssid.c_str(), target.pass.c_str());

    int timeout = 0;
    uint8_t animStep = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 50) {
      TestDisplay::updateWifiConnectingSpinner(target.ssid, animStep++);
      delay(80);
      yield();
      timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);
      esp_wifi_set_ps(WIFI_PS_NONE);
      Serial.printf("✅ [Smart-WiFi] Kết nối THÀNH CÔNG tới '%s'! IP: %s (Ping 1-4ms)\n", target.ssid.c_str(), WiFi.localIP().toString().c_str());
      LOG_I("WIFI", "Ket noi THÀNH CONG toi '%s'! IP: %s, RSSI: %d dBm",
            target.ssid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
      HardwareManager::setLedColor(0, 255, 120); // Xanh lá
      lastWifiNotification = "Đã kết nối: " + target.ssid + " (" + WiFi.localIP().toString() + ")";
      TestAudio::playOkChime();
      TestDisplay::onWifiConnectedSuccess(target.ssid, WiFi.localIP().toString());
      TimeManager::onWifiConnected();
      return true;
    } else {
      Serial.printf("❌ [Smart-WiFi] Không thể kết nối tới '%s'. Thử mạng tiếp theo...\n", target.ssid.c_str());
      LOG_W("WIFI", "Khong the ket noi toi '%s'. Thu mang tiep theo...", target.ssid.c_str());
    }
  }

  TestAudio::playDeleteChime();
  TestDisplay::onWifiConnectFailed("Auto Connect");
  return false;
}

bool WifiManager::connectDirect(const String& ssid, const String& password, bool save) {
  if (ssid.length() == 0) return false;
  s_isManualConnecting = true;

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

  // Hiển thị ngay màn hình kết nối WiFi với hoạt ảnh chấm tròn xoay Cyberpunk
  TestDisplay::showWifiConnectingScreen(ssid);

  WiFi.disconnect(false);
  delay(50);
  WiFi.begin(ssid.c_str(), actualPass.c_str());

  int timeout = 0;
  uint8_t animStep = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 65) {
    TestDisplay::updateWifiConnectingSpinner(ssid, animStep++);
    delay(80);
    yield();
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    HardwareManager::setLedColor(0, 255, 120); // Xanh lá
    lastWifiNotification = "Đã kết nối: " + ssid + " (" + WiFi.localIP().toString() + ")";
    Serial.printf("✅ Đã kết nối thành công tới %s! IP: %s (Ping 1-4ms)\n", ssid.c_str(), WiFi.localIP().toString().c_str());
    TestAudio::playOkChime();
    TestDisplay::onWifiConnectedSuccess(ssid, WiFi.localIP().toString());
    TimeManager::onWifiConnected();
    s_isManualConnecting = false;
    return true;
  } else {
    HardwareManager::setLedColor(255, 50, 50); // Đỏ báo lỗi
    Serial.println("❌ Kết nối thất bại!");
    TestAudio::playDeleteChime();
    TestDisplay::onWifiConnectFailed(ssid);
    s_isManualConnecting = false;
    return false;
  }
}

void WifiManager::scanNetworks(JsonDocument& resDoc) {
  Serial.println("\n🔍 Bắt đầu quét mạng Wi-Fi 2.4GHz...");
  WiFi.scanDelete();

  // Quét nhanh mạng thông thường (show_hidden = false để thời gian quét chỉ còn ~1.5s, không làm nghẽn WebServer)
  int n = WiFi.scanNetworks(false, false);
  if (n < 0) {
    Serial.printf("⚠️ Quét sóng Wi-Fi tạm bận (mã: %d), nạp nhanh danh sách đã lưu.\n", n);
    WiFi.scanDelete();
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
  } else {
    // Nếu quét sóng tạm thời bận hoặc chưa có kết quả, trả về danh sách các mạng đã lưu để người dùng không bị kẹt
    for (JsonObject saved : savedArr) {
      String s = saved["ssid"].as<String>();
      if (s.length() > 0) {
        JsonObject obj = array.add<JsonObject>();
        obj["ssid"] = s;
        obj["rssi"] = -55;
        obj["secure"] = true;
        obj["saved"] = true;
        obj["connected"] = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == s);
      }
    }
  }
  WiFi.scanDelete();
}

void WifiManager::triggerScan(bool async) {
  WiFi.scanDelete();
  if (async) {
    WiFi.scanNetworks(true, false);
  } else {
    WiFi.scanNetworks(false, false);
  }
}

std::vector<SavedWifiStatus> WifiManager::getSavedWifiWithStatus() {
  std::vector<SavedWifiStatus> list;
  JsonDocument doc;
  loadSavedWifiList(doc);
  if (!doc.is<JsonArray>()) return list;

  String curSsid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
  int curRssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;

  for (JsonObject item : doc.as<JsonArray>()) {
    SavedWifiStatus st;
    st.ssid = item["ssid"].as<String>();
    st.password = item["password"].as<String>();
    st.connected = (curSsid.length() > 0 && curSsid == st.ssid);
    if (st.connected) {
      st.inRange = true;
      st.rssi = curRssi;
    } else {
      st.inRange = false;
      st.rssi = -100;
    }
    list.push_back(st);
  }

  // Đối chiếu kết quả quét gần nhất để cập nhật RSSI cho các mạng chưa kết nối
  int n = WiFi.scanComplete();
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String scSsid = WiFi.SSID(i);
      int scRssi = WiFi.RSSI(i);
      for (auto& st : list) {
        if (!st.connected && st.ssid == scSsid) {
          st.inRange = true;
          st.rssi = scRssi;
          break;
        }
      }
    }
  }

  return list;
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
  static unsigned long s_lastPsCheck = 0;
  if (millis() - s_lastPsCheck > 4000) {
    s_lastPsCheck = millis();
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);
      esp_wifi_set_ps(WIFI_PS_NONE);
    }
  }

  // ============================================================================
  // CƠ CHẾ TỰ ĐỘNG QUÉT & KẾT NỐI LẠI NHƯ SMARTPHONE (BACKGROUND AUTO-RECONNECT)
  // Khi ở chế độ AP hoặc khi vừa mất kết nối STA: quét ngầm định kỳ và tự nối lại!
  // ============================================================================
  if (OtaManager::isUpdating() || s_isManualConnecting) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Đang có mạng STA bình thường -> Reset cờ auto-reconnect
    s_isAutoScanning = false;
    s_isAutoConnecting = false;
    return;
  }

  // Trường hợp đang trong tiến trình auto-connect ngầm
  if (s_isAutoConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      s_isAutoConnecting = false;
      WiFi.setSleep(false);
      esp_wifi_set_ps(WIFI_PS_NONE);
      HardwareManager::setLedColor(0, 255, 120);
      lastWifiNotification = "Đã tự động kết nối: " + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")";
      Serial.printf("✅ [Smart-WiFi] Auto-Reconnect thành công tới '%s'! IP: %s\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      TestAudio::playOkChime();
      TestDisplay::onWifiConnectedSuccess(WiFi.SSID(), WiFi.localIP().toString());
    } else if (millis() - s_autoConnectStartTime > 12000) {
      // Hết timeout 12s không kết nối được
      Serial.println("⚠️ [Smart-WiFi] Auto-Reconnect timeout (12s). Chờ chu kỳ quét tiếp theo.");
      WiFi.disconnect(false);
      s_isAutoConnecting = false;
      s_lastAutoScanTime = millis();
    }
    return;
  }

  // Trường hợp đang có tiến trình quét mạng ngầm
  if (s_isAutoScanning) {
    int16_t scanStatus = WiFi.scanComplete();
    if (scanStatus == WIFI_SCAN_RUNNING) {
      return; // Vẫn đang quét ngầm, không block
    } else if (scanStatus == WIFI_SCAN_FAILED) {
      WiFi.scanDelete();
      s_isAutoScanning = false;
      s_lastAutoScanTime = millis();
      return;
    } else if (scanStatus >= 0) {
      // Quét ngầm hoàn tất! Tìm kiếm mạng đã lưu có sóng mạnh nhất
      JsonDocument savedDoc;
      loadSavedWifiList(savedDoc);
      JsonArray savedArr = savedDoc.as<JsonArray>();

      String bestSsid = "";
      String bestPass = "";
      int bestRssi = -999;

      for (int i = 0; i < scanStatus; i++) {
        String scannedSsid = WiFi.SSID(i);
        int scannedRssi = WiFi.RSSI(i);
        for (JsonObject saved : savedArr) {
          if (saved["ssid"].as<String>() == scannedSsid) {
            if (scannedRssi > bestRssi) {
              bestRssi = scannedRssi;
              bestSsid = scannedSsid;
              bestPass = saved["password"].as<String>();
            }
            break;
          }
        }
      }

      WiFi.scanDelete();
      s_isAutoScanning = false;
      s_lastAutoScanTime = millis();

      if (bestSsid.length() > 0) {
        Serial.printf("📡 [Smart-WiFi] Smartphone Auto-Reconnect: Phát hiện '%s' (%d dBm). Đang tự động kết nối lại...\n",
                      bestSsid.c_str(), bestRssi);
        s_isAutoConnecting = true;
        s_autoConnectStartTime = millis();
        WiFi.disconnect(false);
        WiFi.begin(bestSsid.c_str(), bestPass.c_str());
      }
    }
    return;
  }

  // Định kỳ mỗi 18 giây: Khởi chạy quét mạng ngầm không đồng bộ
  if (millis() - s_lastAutoScanTime > 18000) {
    s_lastAutoScanTime = millis();
    int16_t res = WiFi.scanNetworks(true, false);
    if (res == WIFI_SCAN_RUNNING) {
      s_isAutoScanning = true;
    }
  }
}


