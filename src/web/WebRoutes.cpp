#include "WebRoutes.h"
#include "Config.h"
#include "WifiManager.h"
#include "WebAuth.h"
#include "SystemLogger.h"
#include "tests/TestSDCard.h"
#include "drivers/StorageDriver.h"
#include "drivers/AudioDriver.h"

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
  WebRoutes::markWebActivity();

  // 1. Ưu tiên phục vụ file nén gzip (/index.html.gz, ~132 KB) siêu tốc độ, giải phóng 100% tài nguyên CPU & WiFi
  if (LittleFS.exists("/index.html.gz")) {
    File file = LittleFS.open("/index.html.gz", "r");
    server.sendHeader("Cache-Control", "no-cache");
    server.streamFile(file, "text/html");
    file.close();
    return;
  }

  // 2. Phục vụ index.html trực tiếp bằng streamFile chuẩn non-blocking
  if (LittleFS.exists("/index.html")) {
    File file = LittleFS.open("/index.html", "r");
    server.sendHeader("Cache-Control", "no-cache");
    server.streamFile(file, "text/html");
    file.close();
    return;
  }

  server.send(404, "text/plain", "Chưa tìm thấy index.html trên LittleFS!");
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

  // Tuyến phục vụ tệp âm thanh bản thu mic mới nhất (.wav)
  server.on("/api/audio/latest.wav", HTTP_GET, []() {
    WebRoutes::markWebActivity();
    if (server.client()) {
      AudioDriver::setLastClientIp(server.client().remoteIP().toString());
    }
    if (TestSDCard::isMounted() && StorageDriver::isHealthy() && SD.exists("/recording/rec_latest.wav")) {
      StorageDriver::streamFileSafely(server, "/recording/rec_latest.wav", "audio/wav", true);
      return;
    }
    size_t sc = AudioDriver::getRecordedSampleCount();
    const int16_t* pcm = AudioDriver::getRecordedPcmBuffer();
    if (sc > 0 && pcm) {
      uint32_t sampleRate = 16000;
      uint16_t numChannels = 1;
      uint16_t bitsPerSample = 16;
      uint32_t dataBytes = sc * sizeof(int16_t);
      uint32_t totalChunkSize = 36 + dataBytes;
      uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
      uint16_t blockAlign = numChannels * (bitsPerSample / 8);

      uint8_t header[44];
      memcpy(&header[0], "RIFF", 4);
      header[4] = (uint8_t)(totalChunkSize & 0xFF);
      header[5] = (uint8_t)((totalChunkSize >> 8) & 0xFF);
      header[6] = (uint8_t)((totalChunkSize >> 16) & 0xFF);
      header[7] = (uint8_t)((totalChunkSize >> 24) & 0xFF);
      memcpy(&header[8], "WAVEfmt ", 8);
      header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
      header[20] = 1;  header[21] = 0;
      header[22] = (uint8_t)numChannels; header[23] = 0;
      header[24] = (uint8_t)(sampleRate & 0xFF);
      header[25] = (uint8_t)((sampleRate >> 8) & 0xFF);
      header[26] = (uint8_t)((sampleRate >> 16) & 0xFF);
      header[27] = (uint8_t)((sampleRate >> 24) & 0xFF);
      header[28] = (uint8_t)(byteRate & 0xFF);
      header[29] = (uint8_t)((byteRate >> 8) & 0xFF);
      header[30] = (uint8_t)((byteRate >> 16) & 0xFF);
      header[31] = (uint8_t)((byteRate >> 24) & 0xFF);
      header[32] = (uint8_t)blockAlign; header[33] = 0;
      header[34] = (uint8_t)bitsPerSample; header[35] = 0;
      memcpy(&header[36], "data", 4);
      header[40] = (uint8_t)(dataBytes & 0xFF);
      header[41] = (uint8_t)((dataBytes >> 8) & 0xFF);
      header[42] = (uint8_t)((dataBytes >> 16) & 0xFF);
      header[43] = (uint8_t)((dataBytes >> 24) & 0xFF);

      server.sendHeader("Content-Disposition", "inline; filename=\"rec_latest.wav\"");
      server.sendHeader("Cache-Control", "no-cache");
      WiFiClient client = server.client();
      server.setContentLength(44 + dataBytes);
      server.send(200, "audio/wav", "");
      client.write(header, 44);
      client.write((const uint8_t*)pcm, dataBytes);
      return;
    }
    server.send(404, "text/plain", "Chưa có bản thu âm nào trong bộ nhớ!");
  });

  // Trang web nghe thử Mic INMP441 trực tiếp trên trình duyệt máy tính (Cốc Cốc / Chrome)
  server.on("/mic_test", HTTP_GET, []() {
    WebRoutes::markWebActivity();
    if (server.client()) {
      AudioDriver::setLastClientIp(server.client().remoteIP().toString());
    }
    String latestPath = AudioDriver::getLatestRecordedVoicePath();
    size_t samples = AudioDriver::getRecordedSampleCount();
    float dur = AudioDriver::getRecordedDurationSec();
    int vu = AudioDriver::getRecordedMaxVuPct();
    int peak = AudioDriver::getRecordedPeak();
    if (peak == 0) peak = AudioDriver::getLastMicPeak();

    String html = "<!DOCTYPE html><html lang='vi'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Trạm Decor - Thử Nghiệm Mic INMP441</title>"
      "<style>"
      "body{background:#0d1117;color:#c9d1d9;font-family:-apple-system,sans-serif;margin:0;padding:24px;display:flex;justify-content:center;}"
      ".card{max-width:540px;width:100%;background:#161b22;border:1px solid #30363d;border-radius:12px;padding:22px;box-shadow:0 8px 24px rgba(0,0,0,0.5);}"
      "h2{color:#58a6ff;margin-top:0;font-size:19px;}"
      ".box{background:#0d1117;border:1px solid #238636;border-radius:8px;padding:16px;margin:16px 0;}"
      ".row{display:flex;justify-content:space-between;padding:5px 0;font-size:14px;border-bottom:1px solid #21262d;}"
      ".row:last-child{border-bottom:none;}"
      ".val{color:#f0f6fc;font-weight:bold;}"
      "audio{width:100%;margin-top:14px;border-radius:6px;}"
      ".btn{display:inline-block;background:#238636;color:#fff;padding:10px 18px;border-radius:6px;text-decoration:none;font-weight:600;font-size:14px;margin-top:12px;text-align:center;}"
      ".btn:hover{background:#2ea043;}"
      ".tip{font-size:12px;color:#8b949e;margin-top:16px;line-height:1.5;}"
      "</style></head><body>"
      "<div class='card'>"
      "<h2>🎙️ Trạm Decor - Thử Nghiệm Mic INMP441</h2>"
      "<div class='box'>"
      "<div style='font-size:12px;color:#3fb950;font-weight:bold;margin-bottom:8px;'>BẢN THU ÂM GẦN NHẤT</div>"
      "<div class='row'><span>Tệp SD:</span><span class='val'>" + latestPath + "</span></div>"
      "<div class='row'><span>Thời lượng:</span><span class='val'>" + String(dur, 2) + " giây (" + String((unsigned)samples) + " mẫu)</span></div>"
      "<div class='row'><span>Biên độ đỉnh (Peak):</span><span class='val'>" + String(peak) + "</span></div>"
      "<div class='row'><span>Mức VU tối đa:</span><span class='val'>" + String(vu) + "%</span></div>"
      "<audio controls autoplay src='/api/audio/latest.wav?t=" + String(millis()) + "'></audio>"
      "<div style='display:flex;gap:10px;margin-top:12px;'>"
      "<a class='btn' href='/api/audio/latest.wav' download='rec_latest.wav'>⬇️ Tải tệp WAV</a>"
      "<a class='btn' style='background:#1f6feb;' href='/mic_test'>🔄 Làm mới trang</a>"
      "</div></div>"
      "<div class='tip'>💡 <b>Cách thử nghiệm:</b><br>"
      "1. Trên trạm ESP32 (Chế độ Sóng Âm - Mode 3), <b>Nhấn giữ nút [OK]</b> hoặc <b>Chạm giữ màn hình</b> để bắt đầu thu âm.<br>"
      "2. Nói vào Mic INMP441 (thời lượng tùy thích, không giới hạn giây).<br>"
      "3. <b>Nhả tay ra</b>: Trạm sẽ tự ngắt thu, lưu file <code>rec_&lt;giờ_ngày&gt;.wav</code>, phát qua loa trạm, bắn lên WebSocket cổng 8766 và cập nhật tại trang này!</div>"
      "</div></body></html>";
    server.send(200, "text/html", html);
  });

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

    // 6b. Phục vụ file từ thẻ nhớ SD an toàn với Chunked Non-Blocking Stream
    if (TestSDCard::isMounted() && StorageDriver::isHealthy()) {
      String contentType = "application/octet-stream";
      String low = uri;
      low.toLowerCase();
      if (low.endsWith(".png")) contentType = "image/png";
      else if (low.endsWith(".jpg") || low.endsWith(".jpeg")) contentType = "image/jpeg";
      else if (low.endsWith(".bmp")) contentType = "image/bmp";
      else if (low.endsWith(".wav")) contentType = "audio/wav";
      else if (low.endsWith(".mp3")) contentType = "audio/mpeg";

      if (StorageDriver::streamFileSafely(server, uri, contentType, true)) {
        return;
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

static IPAddress s_lastKnownIp = IPAddress(0, 0, 0, 0);
static volatile bool s_needWakeupRecovery = false;
static unsigned long s_lastWebActivityMs = 0;
static unsigned long s_lastAutoHealCheckMs = 0;

void WebRoutes::markWebActivity() {
  s_lastWebActivityMs = millis();
}

void WebRoutes::onStationWakeup() {
  s_needWakeupRecovery = true;
}

void WebRoutes::onWifiConnected() {
  IPAddress currentIp = WiFi.localIP();
  if (currentIp != s_lastKnownIp && currentIp != IPAddress(0, 0, 0, 0)) {
    s_lastKnownIp = currentIp;
    recoverServer();
  }
}

void WebRoutes::recoverServer() {
  SystemLogger::log(LOG_LVL_INFO, "WEB", "Tai khoi dong WebServer & mDNS (Lam moi socket lwIP)...");
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

  // 1. Nếu có tín hiệu đánh thức trạm từ cảm biến / phím bấm / báo thức: Làm mới socket ngay lập tức!
  if (s_needWakeupRecovery) {
    s_needWakeupRecovery = false;
    SystemLogger::log(LOG_LVL_INFO, "WEB", "Tram duoc danh thuc -> Lam moi WebServer & socket lwIP");
    Serial.println("🌐 [WEB WAKEUP] Trạm được đánh thức -> Tái tạo WebServer & socket lwIP sẵn sàng phục vụ!");
    recoverServer();
  }

  // 2. Giám sát thay đổi IP khi Wi-Fi được cấp phát lại DHCP
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress curIp = WiFi.localIP();
    if (curIp != s_lastKnownIp && curIp != IPAddress(0, 0, 0, 0)) {
      s_lastKnownIp = curIp;
      recoverServer();
    }
  }

  // 3. Cơ chế Tuần tra Tự Chữa Lành Định Kỳ (Auto-Heal Watchdog mỗi 20 phút nhàn rỗi)
  // Ngăn chặn 100% tình trạng rác socket / TIME_WAIT sau nhiều giờ chạy không tải
  if (now - s_lastAutoHealCheckMs >= 60000) {
    s_lastAutoHealCheckMs = now;
    if (s_lastWebActivityMs > 0 && (now - s_lastWebActivityMs >= 1200000)) {
      s_lastWebActivityMs = now;
      recoverServer();
    }
  }

  // 4. Phục vụ request WebServer bình thường (WebServer tự động quản lý timeout và giải phóng socket chuẩn lwIP)
  server.handleClient();
}
