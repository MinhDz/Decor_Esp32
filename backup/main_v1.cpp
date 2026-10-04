#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// =========================
// Configuration
// =========================
static const char *AP_SSID = "DecorESP32-Wokwi";
static const char *AP_PASS = "decor1234";
static const uint32_t MAX_UPLOAD_SIZE = 300UL * 1024UL; // 300 KB

WebServer server(80);

// =========================
// App state
// =========================
enum class ScreenMode {
  Standby,
  PCStatus,
  Emoji,
  Image
};

enum class EmojiState {
  Idle,
  LookLeft,
  LookRight,
  Blink,
  Angry,
  Confused
};

struct MockPCStatus {
  uint8_t cpu = 18;
  uint8_t ram = 42;
  uint8_t gpu = 11;
  float temp = 38.5f;
};

ScreenMode currentMode = ScreenMode::Standby;
EmojiState currentEmoji = EmojiState::Idle;
String currentImagePath = "";
MockPCStatus mockPC;
unsigned long lastActivityMs = 0;
unsigned long lastStatusTickMs = 0;

// =========================
// Helpers
// =========================
String modeToString(ScreenMode mode) {
  switch (mode) {
    case ScreenMode::Standby: return "standby";
    case ScreenMode::PCStatus: return "pc";
    case ScreenMode::Emoji: return "emoji";
    case ScreenMode::Image: return "image";
  }
  return "unknown";
}

String emojiToString(EmojiState emoji) {
  switch (emoji) {
    case EmojiState::Idle: return "idle";
    case EmojiState::LookLeft: return "look_left";
    case EmojiState::LookRight: return "look_right";
    case EmojiState::Blink: return "blink";
    case EmojiState::Angry: return "angry";
    case EmojiState::Confused: return "confused";
  }
  return "idle";
}

String htmlEscape(const String &input) {
  String out;
  out.reserve(input.length() + 16);
  for (char c : input) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c; break;
    }
  }
  return out;
}

void touchActivity() {
  lastActivityMs = millis();
}

String fileNameFromUpload(const String &filename) {
  String name = filename;
  name.replace("\\", "/");
  int slash = name.lastIndexOf('/');
  if (slash >= 0) {
    name = name.substring(slash + 1);
  }
  name.trim();
  if (name.length() == 0) {
    name = "image.jpg";
  }
  return name;
}

String contentTypeFromName(const String &name) {
  String lower = name;
  lower.toLowerCase();
  if (lower.endsWith(".png")) return "image/png";
  if (lower.endsWith(".bmp")) return "image/bmp";
  if (lower.endsWith(".gif")) return "image/gif";
  return "image/jpeg";
}

void printState() {
  Serial.println("\n=== Current State ===");
  Serial.printf("Mode: %s\n", modeToString(currentMode).c_str());
  Serial.printf("Emoji: %s\n", emojiToString(currentEmoji).c_str());
  Serial.printf("Image: %s\n", currentImagePath.length() ? currentImagePath.c_str() : "(none)");
  Serial.printf("PC Mock: CPU %u%% | RAM %u%% | GPU %u%% | Temp %.1f C\n",
                mockPC.cpu, mockPC.ram, mockPC.gpu, mockPC.temp);
}

void updateMockPCStatus() {
  unsigned long now = millis();
  if (now - lastStatusTickMs < 2000) {
    return;
  }
  lastStatusTickMs = now;

  mockPC.cpu = 10 + (now / 1000) % 60;
  mockPC.ram = 30 + (now / 1500) % 45;
  mockPC.gpu = 5 + (now / 900) % 70;
  mockPC.temp = 36.0f + ((now / 1000) % 120) / 20.0f;
}

void setMode(ScreenMode mode) {
  currentMode = mode;
  touchActivity();
  printState();
}

void setEmoji(EmojiState emoji) {
  currentEmoji = emoji;
  currentMode = ScreenMode::Emoji;
  touchActivity();
  printState();
}

// =========================
// File system helpers
// =========================
void ensureUploadsDir() {
  if (!LittleFS.exists("/uploads")) {
    LittleFS.mkdir("/uploads");
  }
}

String listFilesHtml() {
  String html;
  html.reserve(1024);
  html += "<ul>";

  File root = LittleFS.open("/uploads");
  if (!root || !root.isDirectory()) {
    html += "<li>Chưa có thư mục uploads</li>";
    html += "</ul>";
    return html;
  }

  File file = root.openNextFile();
  bool hasFile = false;
  while (file) {
    hasFile = true;
    String path = String("/uploads/") + file.name();
    html += "<li>";
    html += htmlEscape(path);
    html += " (";
    html += String(file.size());
    html += " bytes) ";
    html += "<a href=\"/download?file=" + htmlEscape(path) + "\">Tải về</a> ";
    html += "<a href=\"/delete?file=" + htmlEscape(path) + "\">Xóa</a>";
    html += "</li>";
    file = root.openNextFile();
  }

  if (!hasFile) {
    html += "<li>Chưa có ảnh nào</li>";
  }

  html += "</ul>";
  return html;
}

bool safePathFromQuery(String path) {
  path.replace("\\", "/");
  if (!path.startsWith("/uploads/")) {
    return false;
  }
  if (path.indexOf("..") >= 0) {
    return false;
  }
  return true;
}

// =========================
// Web handlers
// =========================
String renderIndexPage() {
  String html;
  html.reserve(6000);
  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Decor ESP32</title>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:920px;margin:24px auto;padding:0 16px;line-height:1.5}h1{margin-bottom:0}.card{border:1px solid #ddd;border-radius:12px;padding:16px;margin:16px 0;background:#fafafa}button,a,input,select{font-size:16px}input[type=file]{width:100%}button{padding:8px 14px;margin:4px 4px 4px 0}code{background:#eee;padding:2px 6px;border-radius:6px}</style>";
  html += "</head><body>";
  html += "<h1>DIY Decor ESP32</h1>";
  html += "<p>Giai đoạn mô phỏng trên Wokwi - upload ảnh, chọn màn hình, test luồng cơ bản.</p>";

  html += "<div class='card'><h2>Trạng thái hiện tại</h2>";
  html += "<p><b>Mode:</b> " + modeToString(currentMode) + "</p>";
  html += "<p><b>Emoji:</b> " + emojiToString(currentEmoji) + "</p>";
  html += "<p><b>Ảnh hiện tại:</b> " + htmlEscape(currentImagePath.length() ? currentImagePath : "(none)") + "</p>";
  html += "<p><b>Mock PC:</b> CPU " + String(mockPC.cpu) + "%, RAM " + String(mockPC.ram) + "%, GPU " + String(mockPC.gpu) + "%, Temp " + String(mockPC.temp, 1) + "C</p>";
  html += "<p><a href='/api/state'>Xem JSON trạng thái</a></p>";
  html += "</div>";

  html += "<div class='card'><h2>Upload ảnh</h2>";
  html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
  html += "<input type='file' name='image' accept='image/*' required><br><br>";
  html += "<button type='submit'>Tải lên</button>";
  html += "</form>";
  html += "<p>Giới hạn upload hiện tại: khoảng 300 KB.</p>";
  html += "</div>";

  html += "<div class='card'><h2>Chuyển màn hình</h2>";
  html += "<a href='/setmode?mode=standby'><button>Standby</button></a>";
  html += "<a href='/setmode?mode=pc'><button>PC Status</button></a>";
  html += "<a href='/setmode?mode=emoji'><button>Emoji</button></a>";
  html += "<a href='/setmode?mode=image'><button>Image</button></a>";
  html += "</div>";

  html += "<div class='card'><h2>Emoji</h2>";
  html += "<a href='/setemoji?e=idle'><button>Idle</button></a>";
  html += "<a href='/setemoji?e=left'><button>Liếc trái</button></a>";
  html += "<a href='/setemoji?e=right'><button>Liếc phải</button></a>";
  html += "<a href='/setemoji?e=blink'><button>Nháy 2 mắt</button></a>";
  html += "<a href='/setemoji?e=angry'><button>Giận</button></a>";
  html += "<a href='/setemoji?e=confused'><button>Bối rối @@</button></a>";
  html += "</div>";

  html += "<div class='card'><h2>File đã tải lên</h2>";
  html += listFilesHtml();
  html += "</div>";

  html += "</body></html>";
  return html;
}

void handleRoot() {
  touchActivity();
  server.send(200, "text/html; charset=utf-8", renderIndexPage());
}

void handleStateApi() {
  JsonDocument doc;
  doc["mode"] = modeToString(currentMode);
  doc["emoji"] = emojiToString(currentEmoji);
  doc["image"] = currentImagePath;
  doc["uptime_ms"] = millis();
  doc["mock_pc"]["cpu"] = mockPC.cpu;
  doc["mock_pc"]["ram"] = mockPC.ram;
  doc["mock_pc"]["gpu"] = mockPC.gpu;
}