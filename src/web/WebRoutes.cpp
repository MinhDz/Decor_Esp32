#include "WebRoutes.h"
#include "Config.h"
#include "WifiManager.h"
#include "WebAuth.h"
#include "tests/TestSDCard.h"
#include "drivers/StorageDriver.h"

// Các bộ điều khiển Controller REST API theo miền tính năng
#include "api/ApiSystem.h"
#include "api/ApiWifi.h"
#include "api/ApiXiaoZhi.h"
#include "api/ApiHardware.h"
#include "api/ApiMedia.h"
#include "api/ApiTerminal.h"
#include "web/WebInjectedScripts.h"

#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>

static WebServer server(80);

WebServer& WebRoutes::getServer() {
  return server;
}

bool WebRoutes::hasAllocatedUploadBuffer() {
  return ApiMedia::hasAllocatedUploadBuffer();
}

size_t WebRoutes::trimMemory() {
  return ApiMedia::trimMemory();
}

// ---------------- PHỤC VỤ TRANG GIAO DIỆN CHỦ & SCRIPT INJECTION ----------------

static void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    File file = LittleFS.open("/index.html", "r");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.sendHeader("Connection", "close");
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
    server.sendContent_P(EMOJI_STYLE_INJECT_SCRIPT);
    server.sendContent("");
  } else {
    server.send(404, "text/plain", "Chưa tìm thấy index.html trên LittleFS!");
  }
}

// ---------------- KHỞI TẠO BỘ ĐIỀU HƯỚNG WEB & ĐĂNG KÝ CONTROLLERS ----------------

void WebRoutes::begin() {
  // 1. Thu thập các Headers xác thực (Cookie, Authorization, X-Auth-Token)
  const char* headerkeys[] = {"Cookie", "Authorization", "X-Auth-Token"};
  server.collectHeaders(headerkeys, 3);

  // 2. Khởi tạo hệ thống xác thực WebAuth
  WebAuth::init();

  // 3. Phục vụ trang chủ "/"
  server.on("/", HTTP_GET, handleRoot);

  // 4. Đăng ký các module REST API Controllers chuyên biệt
  ApiSystem::registerRoutes(server);
  ApiWifi::registerRoutes(server);
  ApiXiaoZhi::registerRoutes(server);
  ApiHardware::registerRoutes(server);
  ApiMedia::registerRoutes(server);
  ApiTerminal::registerRoutes(server);

  // 5. Captive Portal Probes cho iOS / Android / Windows (Tự động mở trang Cấu hình Wi-Fi)
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

  // 6. Phục vụ file tĩnh từ cả Thẻ nhớ SD (/sd_images/...) và LittleFS
  server.serveStatic("/", LittleFS, "/");
  server.onNotFound([handleCaptiveRedirect]() {
    String uri = server.uri();

    // 6a. Kiểm tra nếu là Captive Portal Probes từ các hệ điều hành điện thoại
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

    // 6b. Phục vụ file từ thẻ nhớ SD an toàn với Mutex
    if (TestSDCard::isMounted() && StorageDriver::isHealthy()) {
      if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
        if (SD.exists(uri)) {
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
            StorageDriver::notifyIoSuccess();
            StorageDriver::unlock();
            return;
          }
        }
        StorageDriver::unlock();
      }
    }

    // 6c. Phục vụ file từ LittleFS
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

    // 6d. Chuyển hướng Captive Portal nếu người dùng truy cập domain bất kỳ khi kết nối SoftAP
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

static unsigned long s_lastClientActiveMs = 0;
static bool s_clientWasActive = false;
static IPAddress s_lastKnownIp = IPAddress(0, 0, 0, 0);
static unsigned long s_lastPeriodicHealthCheckMs = 0;

void WebRoutes::onWifiConnected() {
  IPAddress currentIp = WiFi.localIP();
  if (currentIp != s_lastKnownIp && currentIp != IPAddress(0, 0, 0, 0)) {
    s_lastKnownIp = currentIp;
    recoverServer();
  }
}

void WebRoutes::recoverServer() {
  Serial.println("🌐 [WEB WATCHDOG] Tái khởi động WebServer & mDNS để làm mới socket lwIP...");
  server.close();
  server.begin();
  MDNS.end();
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("🌐 mDNS ready: http://%s.local\n", MDNS_HOSTNAME);
  }
}

void WebRoutes::handleClient() {
  unsigned long now = millis();

  // 1. Giám sát thay đổi IP khi Wi-Fi được cấp phát lại
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress curIp = WiFi.localIP();
    if (curIp != s_lastKnownIp && curIp != IPAddress(0, 0, 0, 0)) {
      s_lastKnownIp = curIp;
      recoverServer();
    }
  }

  // 2. Client Watchdog: Diệt các kết nối client bị zombie / treo quá 4.5 giây
  WiFiClient c = server.client();
  if (c && c.connected()) {
    if (!s_clientWasActive) {
      s_clientWasActive = true;
      s_lastClientActiveMs = now;
    } else if (now - s_lastClientActiveMs > 4500) {
      Serial.println("⚠️ [WEB WATCHDOG] Client chiếm giữ kết nối quá 4.5s -> Cưỡng bức giải phóng socket!");
      c.stop();
      s_clientWasActive = false;
    }
  } else {
    s_clientWasActive = false;
  }

  // 3. Phục vụ request WebServer bình thường
  server.handleClient();

  // 4. Tự động làm mới socket mỗi 30 phút khi trạm nhàn rỗi (không có client đang kết nối)
  // để loại bỏ mọi trạng thái treo ngầm hoặc backlog đầy của lwIP
  if (now - s_lastPeriodicHealthCheckMs > 30 * 60 * 1000) {
    s_lastPeriodicHealthCheckMs = now;
    WiFiClient current = server.client();
    if (!current || !current.connected()) {
      server.close();
      server.begin();
    }
  }
}
