#include "tests/TestDisplay.h"
#include "tests/TestSensors.h"
#include "tests/TestAudio.h"
#include "tests/TestButtons.h"
#include "Config.h"
#include "WifiManager.h"
#include "PcStatsManager.h"
#include "ImageManager.h"
#include <SPI.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ILI9341.h>
#include <TJpg_Decoder.h>

// Bảng màu 16-bit RGB565 chuẩn Cyberpunk
#define C_BLACK       0x0000
#define C_WHITE       0xFFFF
#define C_RED         0xF800
#define C_GREEN       0x07E0
#define C_BLUE        0x001F
#define C_CYAN        0x07FF
#define C_MAGENTA     0xF81F
#define C_YELLOW      0xFFE0
#define C_ORANGE      0xFD20
#define C_DARK_BG     0x0842  // #0a0f1d
#define C_CARD_BG     0x10A4  // #111827
#define C_CARD_BORDER 0x2188  // #1e293b
#define C_NEON_BLUE   0x3DFE  // #38bdf8 RGB565
#define C_NEON_CYAN   0x3DF7  // #38bdf8
#define C_NEON_GREEN  0x15EC  // #10b981
#define C_NEON_PINK   0xF292  // #f43f5e
#define C_NEON_AMBER  0xFBE1  // #f59e0b
#define C_NEON_PURPLE 0xA2B7  // #a855f7
#define C_SLATE       0x9534  // #94a3b8
#define C_GRAY        0x52AA

namespace TestDisplay {

  static SPIClass* spiBus = nullptr;
  static Adafruit_ST7789* tft7789 = nullptr;
  static Adafruit_ILI9341* tft9341 = nullptr;
  static Adafruit_SPITFT* tft = nullptr;

  static bool useST7789 = true;
  // Màn hình TFT 2.4" ST7789 PCB đỏ V1.3 dùng phân cực màu chuẩn (invertDisplay = false)
  // giúp nền đen sâu, màu chữ Cyan chuẩn và ảnh JPEG đúng màu tự nhiên 100%
  static bool inverted = false;
  static uint8_t rotation = 0;      // 0: Dọc (240x320)

  // Chế độ hiển thị chính:
  // 0: Màn Hình Chờ (Standby Clock + Wallpaper - MẶC ĐỊNH)
  // 1: Biểu Cảm XiaoZhi AI (Emoji + Phụ đề)
  // 2: PC Status HUD (Thông số máy tính + Cảnh báo Offline)
  // 3: Bảng Diagnostic Phần Cứng (SHT31 + Máy Hiện Sóng INMP441 + 7-Key ADC)
  // 4: Giao diện Điều Khiển Symbian S40 (Menu Lưới Biểu Tượng)
  // 5: App Symbian - Hình Nền (Cài đặt Màn Hình Chờ đồng bộ Web UI)
  // 6: App Symbian - Cài Đặt (Độ sáng PWM GPIO 7, Thời gian sáng, Âm thanh MAX98357A)
  // 7: App Symbian - Thư Viện (Xem ảnh JPEG trên LittleFS & Đặt làm Hình nền)
  // 8: App Symbian - About (Thông tin Phần mềm & Thông số Phần cứng ESP32-S3)
  static uint8_t currentMode = 0;
  static uint8_t hudStyle = 1; // Mặc định 1: Gauges, 0: Bars, 2: Graph, 3: Matrix

  // Trạng thái bộ điều khiển Symbian S40 & Đèn nền PWM GPIO 7
  static int s40MenuCursor = 0;       // 0: Hình nền, 1: Cài đặt, 2: Thông số, 3: Thư viện, 4: About, 5: Test DIO
  static int s40WallpaperCursor = 0;  // Con trỏ trong App Hình nền (0..9)
  static int s40SettingsCursor = 0;   // Con trỏ trong App Cài đặt (0..6)
  static int s40GalleryIndex = 0;     // Chỉ số ảnh đang xem trong App Thư viện
  static int s40AboutPage = 0;        // Trang thông tin trong App About (0: Phần mềm/Mạng, 1: Phần cứng/Bộ nhớ)
  static String s40ToastMsg = "";     // Thông báo nổi ngắn (Toast) khi lưu cài đặt
  static unsigned long s40ToastExpireMs = 0;

  // Cấu hình Độ sáng màn hình (PWM trên GPIO 7) & Thời gian tắt màn hình
  static int screenBrightnessPct = 100; // 10% -> 100%
  static int screenTimeoutIdx = 0;      // 0: Luôn sáng, 1: 15s, 2: 30s, 3: 1p, 4: 5p, 5: 10p
  static const uint16_t TIMEOUT_SECONDS_LIST[6] = { 0, 15, 30, 60, 300, 600 };
  static const char* TIMEOUT_LABELS[6] = { "Luon sang", "15 giay", "30 giay", "1 phut", "5 phut", "10 phut" };
  static unsigned long lastUserActivityMs = 0;
  static bool isScreenSleeping = false;
  static bool keyBeepEnabled = true;
  static int micSensitivityMode = 1; // 0: Thap, 1: Tieu chuan, 2: Cao
  static bool lastPcLiveState = false;

  // Cấu hình Màn Hình Chờ đọc từ /standby_config.json (đồng bộ 100% với Web UI)
  struct StandbyConfig {
    String theme       = "cyberpunk";
    String clockStyle  = "digital";
    uint16_t clockColor = C_NEON_CYAN;
    String clockFormat = "24h";
    bool showSeconds   = true;
    String clockPos    = "center"; // "top", "center", "bottom"
    bool showDate      = true;
    String dateFormat  = "vi";
    bool showWeather   = true;
    float temp         = 28.5f;
    int humidity       = 65;
    String customText  = "Tram Decor Vu Tru *";
    uint16_t textColor = C_WHITE;
    uint16_t bgColor   = C_DARK_BG;
    String bgMode      = "image";  // "image", "gradient-cyber", "gradient-nebula", "gradient-sunset", "solid-black"
    String bgImage     = "";
    int dimOverlay     = 35;       // Độ tối lớp phủ nền (0..85%)
  };

  static StandbyConfig stCfg;
  static bool ntpConfigured = false;
  static unsigned long lastSecondTick = 0;
  static unsigned long lastAnimTick = 0;
  static int lastDrawnMinute = -1;
  static bool lastWifiState = false;
  static uint8_t eyeState = 0;
  static unsigned long lastExternalEmojiSync = 0;
  static bool isAutoBlinkActive = false;
  static String customSubtitle = "Xin chao! Toi la tro ly ao XiaoZhi tren Tram Decor Vu Tru!";
  static uint8_t graphHistoryCpu[20] = {0};
  static uint8_t graphHistoryGpu[20] = {0};

  String getEmojiStateName() {
    const char* names[] = {
      "idle", "blink", "happy", "left", "right", "up", "down",
      "angry", "confused", "wink-left", "wink-right", "sleepy"
    };
    return names[eyeState % 12];
  }

  // Bộ đệm lưu lát cắt nền phía sau đồng hồ (240 x 44 px = 21KB) giúp vẽ chữ nổi trong suốt trên nền ảnh JPEG mà không nháy hình
  static const int CLOCK_STRIP_W = 240;
  static const int CLOCK_STRIP_H = 44;
  static uint16_t* clockBgBuf = nullptr;
  static GFXcanvas16* clockCanvas = nullptr;
  static int activeClockStripY = 112;

  // Chuyển mã màu Hex "#RRGGBB" sang 16-bit RGB565
  static uint16_t hexToRgb565(const String& hexStr, uint16_t fallback = C_NEON_CYAN) {
    String s = hexStr;
    s.trim();
    if (s.startsWith("#")) s = s.substring(1);
    if (s.length() != 6) return fallback;
    long val = strtol(s.c_str(), nullptr, 16);
    uint8_t r = (val >> 16) & 0xFF;
    uint8_t g = (val >> 8) & 0xFF;
    uint8_t b = val & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  // Làm tối một pixel RGB565 theo tỷ lệ keep256 (0 = đen tuyền, 255 = giữ nguyên)
  static inline uint16_t dimRgb565(uint16_t col, uint16_t keep256) {
    uint16_t r = ((col >> 11) & 0x1F);
    uint16_t g = ((col >> 5) & 0x3F);
    uint16_t b = (col & 0x1F);
    r = (r * keep256) >> 8;
    g = (g * keep256) >> 8;
    b = (b * keep256) >> 8;
    return (r << 11) | (g << 5) | b;
  }

  // Nội suy tuyến tính giữa 2 màu RGB565 (t: 0..255) để vẽ nền Gradient mượt mà
  static uint16_t lerpRgb565(uint16_t c1, uint16_t c2, uint8_t t) {
    uint16_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    uint16_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
    uint16_t r = r1 + (((int)r2 - (int)r1) * t) / 255;
    uint16_t g = g1 + (((int)g2 - (int)g1) * t) / 255;
    uint16_t b = b1 + (((int)b2 - (int)b1) * t) / 255;
    return (r << 11) | (g << 5) | b;
  }

  // Kiểm tra xem tọa độ (x, y) có nằm trong các khung kính mờ (Frosted Glass Pills) giống Web UI hay không
  static inline bool isInsideGlassPill(int x, int y) {
    // 1. Pill Wi-Fi góc trên trái (x: 8..92, y: 8..25)
    if (y >= 8 && y <= 25 && x >= 8 && x <= 92) return true;
    // 2. Pill ESP32-S3 góc trên phải (x: 164..231, y: 8..25)
    if (y >= 8 && y <= 25 && x >= 164 && x <= 231) return true;
    // 3. Pill Thời tiết ở đáy (x: 42..198, y: 262..283)
    if (stCfg.showWeather && y >= 262 && y <= 283 && x >= 42 && x <= 198) return true;
    return false;
  }

  // Chuyển chuỗi tiếng Việt UTF-8 có dấu sang ASCII không dấu để hiển thị sạch đẹp trên font GFX
  static String toCleanAscii(const String& input) {
    String s = input;
    const char* from[] = {
      "à","á","ạ","ả","ã","â","ầ","ấ","ậ","ẩ","ẫ","ă","ằ","ắ","ặ","ẳ","ẵ",
      "À","Á","Ạ","Ả","Ã","Â","Ầ","Ấ","Ậ","Ẩ","Ẫ","Ă","Ằ","Ắ","Ặ","Ẳ","Ẵ",
      "è","é","ẹ","ẻ","ẽ","ê","ề","ế","ệ","ể","ễ",
      "È","É","Ẹ","Ẻ","Ẽ","Ê","Ề","Ế","Ệ","Ể","Ễ",
      "ì","í","ị","ỉ","ĩ","Ì","Í","Ị","Ỉ","Ĩ",
      "ò","ó","ọ","ỏ","õ","ô","ồ","ố","ộ","ổ","ỗ","ơ","ờ","ớ","ợ","ở","ỡ",
      "Ò","Ó","Ọ","Ỏ","Õ","Ô","Ồ","Ố","Ộ","Ổ","Ỗ","Ơ","Ờ","Ớ","Ợ","Ở","Ỡ",
      "ù","ú","ụ","ủ","ũ","ư","ừ","ứ","ự","ử","ữ",
      "Ù","Ú","Ụ","Ủ","Ũ","Ư","Ừ","Ứ","Ự","Ử","Ữ",
      "ỳ","ý","ỵ","ỷ","ỹ","Ỳ","Ý","Ỵ","Ỷ","Ỹ",
      "đ","Đ","✨","🌡️","💧","⏰","😊","📊"
    };
    const char* to[] = {
      "a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a",
      "A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A",
      "e","e","e","e","e","e","e","e","e","e","e",
      "E","E","E","E","E","E","E","E","E","E","E",
      "i","i","i","i","i","I","I","I","I","I",
      "o","o","o","o","o","o","o","o","o","o","o","o","o","o","o","o","o",
      "O","O","O","O","O","O","O","O","O","O","O","O","O","O","O","O","O",
      "u","u","u","u","u","u","u","u","u","u","u",
      "U","U","U","U","U","U","U","U","U","U","U",
      "y","y","y","y","y","Y","Y","Y","Y","Y",
      "d","D","*","","","", "", ""
    };
    const int count = sizeof(from) / sizeof(from[0]);
    for (int i = 0; i < count; i++) {
      s.replace(from[i], to[i]);
    }
    String out = "";
    for (unsigned int i = 0; i < s.length(); i++) {
      uint8_t c = (uint8_t)s[i];
      if (c >= 32 && c <= 126) out += (char)c;
    }
    out.trim();
    return out;
  }

  // Tìm đường dẫn file ảnh nền thực tế đang lưu trong LittleFS
  static String resolveWallpaperPath() {
    // 1. Ưu tiên ảnh khai báo trong /standby_config.json
    if (stCfg.bgImage.length() > 0) {
      String p = stCfg.bgImage.startsWith("/") ? stCfg.bgImage : ("/" + stCfg.bgImage);
      if (LittleFS.exists(p)) return p;
    }
    // 2. Tiếp theo kiểm tra ảnh đang active trong ImageManager (Preferences)
    String active = ImageManager::getActiveImage();
    if (active.length() > 0) {
      if (!active.startsWith("/")) active = "/" + active;
      if (LittleFS.exists(active)) return active;
    }
    // 3. Quét LittleFS tìm ảnh .jpg / .jpeg mới nhất mà người dùng đã tải lên
    String found = "";
    File root = LittleFS.open("/");
    if (root) {
      File file = root.openNextFile();
      while (file) {
        String fname = String(file.name());
        if (!fname.startsWith("/")) fname = "/" + fname;
        if (fname.endsWith(".jpg") || fname.endsWith(".jpeg")) {
          found = fname;
        }
        file = root.openNextFile();
      }
    }
    return found;
  }

  // Đọc cấu hình từ /standby_config.json trên LittleFS
  static void loadConfigFromFile() {
    if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
      File f = LittleFS.open(FILE_STANDBY_CONFIG, "r");
      if (f) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (!err) {
          stCfg.theme       = doc["theme"] | "cyberpunk";
          stCfg.clockStyle  = doc["clock_style"] | "digital";
          stCfg.clockColor  = hexToRgb565(doc["clock_color"] | "#38bdf8", C_NEON_CYAN);
          stCfg.clockFormat = doc["clock_format"] | "24h";
          stCfg.showSeconds = doc["show_seconds"] | true;
          stCfg.clockPos    = doc["clock_pos"] | "center";
          stCfg.showDate    = doc["show_date"] | true;
          stCfg.dateFormat  = doc["date_format"] | "vi";
          stCfg.showWeather = doc["show_weather"] | true;
          stCfg.temp        = doc["temp"] | 28.5f;
          stCfg.humidity    = doc["humidity"] | 65;
          stCfg.customText  = toCleanAscii(doc["custom_text"] | "Tram Decor Vu Tru *");
          if (stCfg.customText.length() == 0) stCfg.customText = "Tram Decor Vu Tru *";
          stCfg.textColor   = hexToRgb565(doc["text_color"] | "#f8fafc", C_WHITE);
          stCfg.bgColor     = hexToRgb565(doc["bg_color"] | "#0a0f1d", C_DARK_BG);
          stCfg.bgMode      = doc["bg_mode"] | "image";
          stCfg.bgImage     = doc["bg_image"] | "";
          stCfg.dimOverlay  = doc["dim_overlay"] | 35;
          if (stCfg.bgColor == C_WHITE) stCfg.bgColor = C_DARK_BG;
        }
      }
    }
    // Tự động đồng bộ nếu đã có ảnh tải lên trong LittleFS
    String validImg = resolveWallpaperPath();
    if (validImg.length() > 0 && (stCfg.bgMode == "image" || stCfg.bgImage.length() > 0)) {
      stCfg.bgImage = validImg;
      stCfg.bgMode = "image";
    }
  }

  // Chuyển màu RGB565 sang mã Hex "#RRGGBB" để lưu xuống /standby_config.json
  static String rgb565ToHex(uint16_t col) {
    uint8_t r = ((col >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((col >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (col & 0x1F) * 255 / 31;
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return String(buf);
  }

  // Lưu cấu hình Màn Hình Chờ hiện tại xuống /standby_config.json (đồng bộ 2 chiều với Web UI)
  static void saveConfigToFile() {
    JsonDocument doc;
    if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
      File rf = LittleFS.open(FILE_STANDBY_CONFIG, "r");
      if (rf) {
        deserializeJson(doc, rf);
        rf.close();
      }
    }
    doc["theme"]        = stCfg.theme;
    doc["clock_style"]  = stCfg.clockStyle;
    doc["clock_color"]  = rgb565ToHex(stCfg.clockColor);
    doc["clock_format"] = stCfg.clockFormat;
    doc["show_seconds"] = stCfg.showSeconds;
    doc["clock_pos"]    = stCfg.clockPos;
    doc["show_date"]    = stCfg.showDate;
    doc["date_format"]  = stCfg.dateFormat;
    doc["show_weather"] = stCfg.showWeather;
    doc["temp"]         = stCfg.temp;
    doc["humidity"]     = stCfg.humidity;
    doc["custom_text"]  = stCfg.customText;
    doc["bg_mode"]      = stCfg.bgMode;
    doc["bg_image"]     = stCfg.bgImage;
    doc["dim_overlay"]  = stCfg.dimOverlay;

    File wf = LittleFS.open(FILE_STANDBY_CONFIG, "w");
    if (wf) {
      serializeJson(doc, wf);
      wf.close();
    }
    if (stCfg.bgImage.length() > 0) {
      String cleanName = stCfg.bgImage;
      if (cleanName.startsWith("/")) cleanName = cleanName.substring(1);
      ImageManager::selectActiveImage(cleanName);
    }
  }

  // Lấy danh sách tất cả file ảnh .jpg/.jpeg trong LittleFS cho App Thư Viện & App Hình Nền
  static int getStoredJpgList(String outNames[], size_t outSizes[], int maxItems) {
    int count = 0;
    File root = LittleFS.open("/");
    if (!root) return 0;
    File file = root.openNextFile();
    while (file && count < maxItems) {
      String fname = String(file.name());
      if (!fname.startsWith("/")) fname = "/" + fname;
      String lower = fname;
      lower.toLowerCase();
      if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
        outNames[count] = fname;
        if (outSizes) outSizes[count] = file.size();
        count++;
      }
      file = root.openNextFile();
    }
    return count;
  }

  // Điều chỉnh độ sáng đèn nền màn hình bằng xung PWM trên chân GPIO 7 (PIN_TFT_BL)
  void setBacklightBrightness(int pct) {
    screenBrightnessPct = constrain(pct, 10, 100);
    if (!isScreenSleeping) {
      uint32_t duty = (uint32_t)((screenBrightnessPct * 255) / 100);
      ledcWrite(0, duty);
    }
  }

  int getBacklightBrightness() {
    return screenBrightnessPct;
  }

  // Lấy thời gian hiện tại (từ NTP nếu đã đồng bộ, hoặc đếm từ mốc mặc định)
  static void getCurrentDateTime(int& hour, int& minute, int& second, int& wday, int& day, int& month, int& year) {
    struct tm timeinfo;
    if (ntpConfigured && getLocalTime(&timeinfo, 5)) {
      hour   = timeinfo.tm_hour;
      minute = timeinfo.tm_min;
      second = timeinfo.tm_sec;
      wday   = timeinfo.tm_wday; // 0 = CN, 1 = T2 ... 6 = T7
      day    = timeinfo.tm_mday;
      month  = timeinfo.tm_mon + 1;
      year   = timeinfo.tm_year + 1900;
      return;
    }
    unsigned long totalSec = (millis() / 1000) + (0 * 3600 + 35 * 60);
    second = totalSec % 60;
    minute = (totalSec / 60) % 60;
    hour   = (totalSec / 3600) % 24;
    wday   = 5; // Thứ Sáu
    day    = 25;
    month  = 9;
    year   = 2026;
  }

  // Tính tọa độ Y của dải Đồng Hồ (theo đúng vị trí trên Web UI: Top / Center / Bottom)
  static int getStandbyClockStripY() {
    if (stCfg.clockPos == "top") return 50;
    if (stCfg.clockPos == "bottom") return 182;
    return 112; // "center" mặc định
  }

  // Callback giải mã từng khối MCU của ảnh JPEG từ LittleFS và vẽ lên ST7789
  static bool jpgRenderCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!tft || y >= tft->height() || x >= tft->width()) return false;

    uint16_t baseKeep = (uint16_t)((100 - constrain(stCfg.dimOverlay, 0, 85)) * 255 / 100);
    uint16_t pillKeep = (baseKeep * 95) >> 8; // Kính mờ tối hơn cho các pill hiển thị thông tin

    for (uint16_t row = 0; row < h; row++) {
      int py = y + row;
      if (py < 0 || py >= 320) continue;
      bool inClockStrip = (clockBgBuf != nullptr && py >= activeClockStripY && py < activeClockStripY + CLOCK_STRIP_H);

      for (uint16_t col = 0; col < w; col++) {
        int px = x + col;
        if (px < 0 || px >= 240) continue;
        uint32_t idx = row * w + col;
        uint16_t keep = isInsideGlassPill(px, py) ? pillKeep : baseKeep;
        uint16_t dimmed = dimRgb565(bitmap[idx], keep);
        bitmap[idx] = dimmed;

        if (inClockStrip) {
          clockBgBuf[(py - activeClockStripY) * CLOCK_STRIP_W + px] = dimmed;
        }
      }
    }

    tft->drawRGBBitmap(x, y, bitmap, w, h);
    return true;
  }

  // Vẽ nền Màn Hình Chờ (Ảnh JPEG từ LittleFS hoặc Gradient chuẩn Web UI)
  static void renderStandbyBackground() {
    if (!tft) return;
    activeClockStripY = getStandbyClockStripY();

    if (!clockBgBuf) {
      if (psramFound()) {
        clockBgBuf = (uint16_t*)ps_malloc(CLOCK_STRIP_W * CLOCK_STRIP_H * sizeof(uint16_t));
      }
      if (!clockBgBuf) {
        clockBgBuf = (uint16_t*)malloc(CLOCK_STRIP_W * CLOCK_STRIP_H * sizeof(uint16_t));
      }
    }
    if (!clockCanvas) {
      clockCanvas = new GFXcanvas16(CLOCK_STRIP_W, CLOCK_STRIP_H);
    }

    String imgPath = resolveWallpaperPath();
    bool renderedJpg = false;

    // 1. Nếu có ảnh JPEG hợp lệ và đang ở chế độ Ảnh nền (hoặc có ảnh được chọn)
    if (imgPath.length() > 0 && (stCfg.bgMode == "image" || stCfg.bgImage.length() > 0)) {
      uint16_t jw = 0, jh = 0;
      TJpgDec.setCallback(jpgRenderCallback);
      TJpgDec.setSwapBytes(false);

      if (TJpgDec.getFsJpgSize(&jw, &jh, imgPath.c_str(), LittleFS) == JDR_OK && jw > 0 && jh > 0) {
        uint8_t scale = 1;
        if (jw >= 960 || jh >= 1280) scale = 4;
        else if (jw >= 480 || jh >= 640) scale = 2; // Tự động co ảnh Retina 480x640 về chuẩn 240x320
        TJpgDec.setJpgScale(scale);

        int drawW = jw / scale;
        int drawH = jh / scale;
        int offsetX = (240 - drawW) / 2;
        int offsetY = (320 - drawH) / 2;
        if (offsetX > 0 || offsetY > 0) tft->fillScreen(C_BLACK);

        Serial.printf("🖼️ [ST7789] Đang vẽ ảnh nền LittleFS: %s (%dx%d -> scale 1/%d)\n", imgPath.c_str(), jw, jh, scale);
        if (TJpgDec.drawFsJpg(max(0, offsetX), max(0, offsetY), imgPath.c_str(), LittleFS) == JDR_OK) {
          renderedJpg = true;
        }
      }
    }

    // 2. Fallback: Vẽ dải màu Gradient dọc 320 dòng giống hệt Web UI nếu không dùng ảnh
    if (!renderedJpg) {
      uint16_t cTop = hexToRgb565("#0b132b"), cMid = hexToRgb565("#1c2541"), cBot = hexToRgb565("#3a0ca3");
      if (stCfg.bgMode == "gradient-nebula") {
        cTop = hexToRgb565("#2b0938"); cMid = hexToRgb565("#0f172a"); cBot = hexToRgb565("#1e1b4b");
      } else if (stCfg.bgMode == "gradient-sunset") {
        cTop = hexToRgb565("#3d1308"); cMid = hexToRgb565("#1e1b4b"); cBot = hexToRgb565("#0f172a");
      } else if (stCfg.bgMode == "solid-black") {
        cTop = C_BLACK; cMid = C_BLACK; cBot = C_BLACK;
      }

      uint16_t baseKeep = (uint16_t)((100 - constrain(stCfg.dimOverlay, 0, 85)) * 255 / 100);
      uint16_t pillKeep = (baseKeep * 95) >> 8;
      uint16_t lineBuf[240];

      for (int y = 0; y < 320; y++) {
        uint16_t baseCol = (y < 160)
          ? lerpRgb565(cTop, cMid, (uint8_t)((y * 255) / 160))
          : lerpRgb565(cMid, cBot, (uint8_t)(((y - 160) * 255) / 160));

        uint16_t normalCol = dimRgb565(baseCol, baseKeep);
        uint16_t glassCol  = dimRgb565(baseCol, pillKeep);
        bool inClockStrip  = (clockBgBuf != nullptr && y >= activeClockStripY && y < activeClockStripY + CLOCK_STRIP_H);

        for (int x = 0; x < 240; x++) {
          uint16_t pxCol = isInsideGlassPill(x, y) ? glassCol : normalCol;
          lineBuf[x] = pxCol;
          if (inClockStrip) {
            clockBgBuf[(y - activeClockStripY) * CLOCK_STRIP_W + x] = pxCol;
          }
        }
        tft->drawRGBBitmap(0, y, lineBuf, 240, 1);
      }
    }
  }

  // Vẽ chữ có bóng đổ đen (Drop Shadow) trong suốt trên nền ảnh Wallpaper
  static void drawShadowText(int x, int y, const String& text, uint16_t color, uint8_t size = 1) {
    if (!tft) return;
    tft->setTextSize(size);
    tft->setTextColor(C_BLACK);
    tft->setCursor(x + 1, y + 1);
    tft->print(text);
    tft->setCursor(x + 1, y);
    tft->print(text);
    tft->setTextColor(color);
    tft->setCursor(x, y);
    tft->print(text);
  }

  // Vẽ thanh trạng thái đặc cho các màn hình Emoji / PC HUD / Diagnostic
  static void drawTopStatusBar() {
    if (!tft) return;
    int w = tft->width();
    tft->fillRect(0, 0, w, 24, C_CARD_BG);
    tft->drawFastHLine(0, 24, w, C_CARD_BORDER);

    bool connected = (WiFi.status() == WL_CONNECTED);
    uint16_t dotColor = connected ? C_NEON_GREEN : C_NEON_AMBER;
    tft->fillCircle(10, 12, 4, dotColor);

    tft->setTextSize(1);
    tft->setTextColor(C_WHITE, C_CARD_BG);
    tft->setCursor(18, 8);
    if (connected) {
      String ssid = toCleanAscii(WifiManager::getCurrentSsid());
      if (ssid.length() > 14) ssid = ssid.substring(0, 14);
      tft->printf("WiFi: %-14s", ssid.c_str());
    } else {
      tft->print("AP: TramVuTru-Config");
    }

    tft->drawRect(w - 38, 6, 26, 12, C_NEON_GREEN);
    tft->fillRect(w - 12, 9, 3, 6, C_NEON_GREEN);
    tft->fillRect(w - 36, 8, 22, 8, C_NEON_GREEN);
    tft->setTextColor(C_BLACK, C_NEON_GREEN);
    tft->setCursor(w - 34, 8);
    tft->print("5V");
  }

  // Vẽ đôi mắt robot XiaoZhi đồng bộ 100% với 12 trạng thái mắt tròn phát sáng trên Web UI
  static void drawXiaoZhiEyesBox(int boxX, int boxY, int boxW, int boxH, uint8_t state, uint8_t scaleMode) {
    if (!tft) return;
    int cx = boxX + boxW / 2;
    int cy = boxY + boxH / 2;

    if (scaleMode == 2) {
      // Màn hình Emoji chính: nền đen tuyền giống hệt khung màn hình ảo trên Web UI
      tft->fillRect(boxX, boxY, boxW, boxH, C_BLACK);
    } else {
      tft->fillRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 6, C_BLACK);
      tft->drawRoundRect(boxX, boxY, boxW, boxH, 6, C_NEON_CYAN);
    }

    int radius = (scaleMode == 2) ? 29 : 18;
    int gap    = (scaleMode == 2) ? 32 : 18;
    int leftCx  = cx - gap / 2 - radius;
    int rightCx = cx + gap / 2 + radius;
    int eyeCy   = cy;
    int pupR    = (scaleMode == 2) ? 6 : 4;

    uint16_t mainCyan = 0x5FFF; // #58ffff sáng rực như Web UI
    uint16_t haloCyan = C_NEON_CYAN;

    auto drawGlowingCircleEye = [&](int ex, int ey, int r, uint16_t col, int pupDx, int pupDy) {
      tft->drawCircle(ex, ey, r + 2, haloCyan);
      tft->drawCircle(ex, ey, r + 1, col);
      tft->fillCircle(ex, ey, r, col);
      tft->fillCircle(ex + pupDx, ey + pupDy, pupR, C_WHITE);
    };

    switch (state) {
      case 0: // IDLE (Bình thường - 2 mắt tròn phát sáng)
        drawGlowingCircleEye(leftCx,  eyeCy, radius, mainCyan,  6, -6);
        drawGlowingCircleEye(rightCx, eyeCy, radius, mainCyan,  6, -6);
        break;

      case 1: // BLINK (Nháy mắt - -)
        tft->fillRoundRect(leftCx - radius,  eyeCy - 4, radius * 2, 8, 4, mainCyan);
        tft->fillRoundRect(rightCx - radius, eyeCy - 4, radius * 2, 8, 4, mainCyan);
        break;

      case 2: // HAPPY (Vui vẻ ^ ^)
        drawGlowingCircleEye(leftCx,  eyeCy + 4, radius, C_NEON_GREEN, 0, -8);
        drawGlowingCircleEye(rightCx, eyeCy + 4, radius, C_NEON_GREEN, 0, -8);
        tft->fillCircle(leftCx,  eyeCy + radius / 2 + 6, radius, C_BLACK);
        tft->fillCircle(rightCx, eyeCy + radius / 2 + 6, radius, C_BLACK);
        break;

      case 3: // LOOK LEFT (Liếc Trái)
        drawGlowingCircleEye(leftCx - 12,  eyeCy, radius, mainCyan, -10, -4);
        drawGlowingCircleEye(rightCx - 12, eyeCy, radius, mainCyan, -10, -4);
        break;

      case 4: // LOOK RIGHT (Liếc Phải)
        drawGlowingCircleEye(leftCx + 12,  eyeCy, radius, mainCyan, 10, -4);
        drawGlowingCircleEye(rightCx + 12, eyeCy, radius, mainCyan, 10, -4);
        break;

      case 5: // LOOK UP (Nhìn Lên)
        drawGlowingCircleEye(leftCx,  eyeCy - 12, radius, mainCyan, 0, -11);
        drawGlowingCircleEye(rightCx, eyeCy - 12, radius, mainCyan, 0, -11);
        break;

      case 6: // LOOK DOWN (Nhìn Xuống)
        drawGlowingCircleEye(leftCx,  eyeCy + 12, radius, mainCyan, 0, 11);
        drawGlowingCircleEye(rightCx, eyeCy + 12, radius, mainCyan, 0, 11);
        break;

      case 7: // ANGRY (Giận >_<)
        tft->fillCircle(leftCx,  eyeCy, radius, C_NEON_PINK);
        tft->fillCircle(rightCx, eyeCy, radius, C_NEON_PINK);
        for (int i = -radius; i <= radius; i++) {
          int cutLeftY  = eyeCy - radius / 2 + (i + radius) / 3;
          int cutRightY = eyeCy - radius / 2 + (radius - i) / 3;
          tft->drawFastVLine(leftCx + i,  eyeCy - radius - 2, max(0, cutLeftY - (eyeCy - radius - 2)), C_BLACK);
          tft->drawFastVLine(rightCx + i, eyeCy - radius - 2, max(0, cutRightY - (eyeCy - radius - 2)), C_BLACK);
        }
        break;

      case 8: // CONFUSED (Bối rối @@)
        for (int r = radius; r >= 6; r -= 7) {
          tft->drawCircle(leftCx,  eyeCy, r, (r % 2 == 0) ? C_NEON_AMBER : C_WHITE);
          tft->drawCircle(rightCx, eyeCy, r, (r % 2 == 0) ? C_NEON_AMBER : C_WHITE);
        }
        tft->fillCircle(leftCx,  eyeCy, 4, C_NEON_AMBER);
        tft->fillCircle(rightCx, eyeCy, 4, C_NEON_AMBER);
        break;

      case 9: // WINK LEFT (Nháy mắt trái)
        tft->fillRoundRect(leftCx - radius, eyeCy - 4, radius * 2, 8, 4, mainCyan);
        drawGlowingCircleEye(rightCx, eyeCy, radius, mainCyan, 6, -6);
        break;

      case 10: // WINK RIGHT (Nháy mắt phải)
        drawGlowingCircleEye(leftCx, eyeCy, radius, mainCyan, 6, -6);
        tft->fillRoundRect(rightCx - radius, eyeCy - 4, radius * 2, 8, 4, mainCyan);
        break;

      case 11: // SLEEPY (Buồn ngủ)
      default:
        drawGlowingCircleEye(leftCx,  eyeCy + 4, radius, C_NEON_PURPLE, 0, 4);
        drawGlowingCircleEye(rightCx, eyeCy + 4, radius, C_NEON_PURPLE, 0, 4);
        tft->fillRect(leftCx - radius - 2,  eyeCy - radius - 2, radius * 2 + 4, radius + 2, C_BLACK);
        tft->fillRect(rightCx - radius - 2, eyeCy - radius - 2, radius * 2 + 4, radius + 2, C_BLACK);
        tft->drawFastHLine(leftCx - radius,  eyeCy, radius * 2, C_NEON_CYAN);
        tft->drawFastHLine(rightCx - radius, eyeCy, radius * 2, C_NEON_CYAN);
        break;
    }
  }

  // Cập nhật Đồng hồ số nổi trong suốt trên nền ảnh JPEG / Gradient (Không vẽ hộp đen che ảnh & không nháy hình)
  static void updateStandbyClockDigits(bool forceFullClock) {
    if (!tft || currentMode != 0 || !clockCanvas) return;

    int hour, minute, second, wday, day, month, year;
    getCurrentDateTime(hour, minute, second, wday, day, month, year);

    bool is12h = (stCfg.clockFormat == "12h");
    bool isPM = (hour >= 12);
    int dispHour = hour;
    if (is12h) {
      dispHour = hour % 12;
      if (dispHour == 0) dispHour = 12;
    }

    char hmBuf[8];
    if (second % 2 == 0) {
      snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", dispHour, minute);
    } else {
      snprintf(hmBuf, sizeof(hmBuf), "%02d %02d", dispHour, minute);
    }

    // Khôi phục lát cắt ảnh nền 240x44 phía sau đồng hồ vào bộ đệm canvas
    if (clockBgBuf) {
      memcpy(clockCanvas->getBuffer(), clockBgBuf, CLOCK_STRIP_W * CLOCK_STRIP_H * sizeof(uint16_t));
    } else {
      clockCanvas->fillScreen(stCfg.bgColor);
    }

    int hmWidth = 5 * 24; // 120px (size 4)
    int secWidth = stCfg.showSeconds ? (3 * 12) : (is12h ? 28 : 0);
    int totalW = hmWidth + secWidth;
    int startX = (CLOCK_STRIP_W - totalW) / 2;
    int textY = 6;

    // 1. Vẽ bóng đổ đen đậm (Drop Shadow 2px) cho chữ HH:MM
    clockCanvas->setTextSize(4);
    clockCanvas->setTextColor(C_BLACK);
    clockCanvas->setCursor(startX + 2, textY + 2);
    clockCanvas->print(hmBuf);
    clockCanvas->setCursor(startX + 1, textY + 1);
    clockCanvas->print(hmBuf);

    // 2. Vẽ chữ HH:MM chính với màu clockColor rực rỡ
    clockCanvas->setTextColor(stCfg.clockColor);
    clockCanvas->setCursor(startX, textY);
    clockCanvas->print(hmBuf);

    // 3. Vẽ phần Giây (:SS) và AM/PM
    int secX = startX + hmWidth + 3;
    if (is12h) {
      clockCanvas->setTextSize(1);
      clockCanvas->setTextColor(C_BLACK);
      clockCanvas->setCursor(secX + 1, textY + 3);
      clockCanvas->print(isPM ? "PM" : "AM");
      clockCanvas->setTextColor(C_NEON_AMBER);
      clockCanvas->setCursor(secX, textY + 2);
      clockCanvas->print(isPM ? "PM" : "AM");
    }
    if (stCfg.showSeconds) {
      char secBuf[6];
      snprintf(secBuf, sizeof(secBuf), ":%02d", second);
      clockCanvas->setTextSize(2);
      clockCanvas->setTextColor(C_BLACK);
      clockCanvas->setCursor(secX + 1, textY + 15);
      clockCanvas->print(secBuf);
      clockCanvas->setTextColor(C_WHITE);
      clockCanvas->setCursor(secX, textY + 14);
      clockCanvas->print(secBuf);
    }

    // Đẩy duy nhất dải 240x44 ra màn hình SPI -> Mượt 100%, giữ nguyên ảnh nền phía dưới!
    tft->drawRGBBitmap(0, activeClockStripY, clockCanvas->getBuffer(), CLOCK_STRIP_W, CLOCK_STRIP_H);
  }

  // Vẽ toàn bộ bố cục Màn Hình Chờ đồng bộ 100% với màn hình ảo trên Web UI (240x320 Portrait)
  static void drawStandbyScreenFull() {
    if (!tft || isScreenSleeping) return;
    int w = tft->width();

    // Bước 1: Vẽ toàn bộ Ảnh nền JPEG từ LittleFS (hoặc Gradient) + làm mờ sẵn các ô kính mờ
    renderStandbyBackground();

    // Bước 2: Vẽ Top Bar (2 viên thuốc kính mờ: Trái = Wi-Fi/STA, Phải = ESP32-S3)
    bool connected = (WiFi.status() == WL_CONNECTED);
    tft->drawRoundRect(8, 8, 84, 18, 8, 0x39E7);
    tft->fillCircle(17, 17, 3, connected ? C_NEON_GREEN : C_NEON_AMBER);
    String wifiTag = "AP MODE";
    if (connected) {
      String s = toCleanAscii(WifiManager::getCurrentSsid());
      if (s.length() > 8) s = s.substring(0, 8);
      wifiTag = s.length() > 0 ? s : "STA";
    }
    drawShadowText(25, 13, wifiTag, C_WHITE, 1);

    tft->drawRoundRect(164, 8, 68, 18, 8, 0x39E7);
    drawShadowText(174, 13, "ESP32-S3", C_NEON_CYAN, 1);

    // Bước 3: Vẽ Đồng hồ số trung tâm (nổi trong suốt trên ảnh nền) + Ngày tháng bên dưới
    lastDrawnMinute = -1;
    updateStandbyClockDigits(true);

    if (stCfg.showDate) {
      int hour, minute, second, wday, day, month, year;
      getCurrentDateTime(hour, minute, second, wday, day, month, year);
      char dateBuf[32];
      const char* viDays[] = { "Chu Nhat", "Thu Hai", "Thu Ba", "Thu Tu", "Thu Nam", "Thu Sau", "Thu Bay" };
      const char* enDays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
      if (stCfg.dateFormat == "en") {
        snprintf(dateBuf, sizeof(dateBuf), "%s, %02d/%02d/%04d", enDays[wday % 7], day, month, year);
      } else if (stCfg.dateFormat == "num") {
        snprintf(dateBuf, sizeof(dateBuf), "%02d / %02d / %04d", day, month, year);
      } else {
        snprintf(dateBuf, sizeof(dateBuf), "%s, %02d/%02d/%04d", viDays[wday % 7], day, month, year);
      }
      int dateX = (w - strlen(dateBuf) * 6) / 2;
      drawShadowText(max(8, dateX), activeClockStripY + CLOCK_STRIP_H + 4, String(dateBuf), C_WHITE, 1);
    }

    // Bước 4: Vẽ Footer (Viên thuốc kính mờ Thời tiết + Lời chào Slogan ở đáy màn hình giống hệt Web UI)
    if (stCfg.showWeather) {
      tft->drawRoundRect(42, 262, 156, 22, 10, 0x39E7);
      char wBuf[28];
      snprintf(wBuf, sizeof(wBuf), "%.1foC  |  %d%%", stCfg.temp, stCfg.humidity);
      int wx = (w - strlen(wBuf) * 6) / 2;
      drawShadowText(max(46, wx), 269, String(wBuf), C_WHITE, 1);
    }

    String msg = stCfg.customText;
    if (msg.length() > 34) msg = msg.substring(0, 34);
    int msgX = (w - msg.length() * 6) / 2;
    drawShadowText(max(8, msgX), 292, msg, stCfg.textColor, 1);
  }

  // Vẽ hộp Phụ đề XiaoZhi ở phần dưới màn hình Emoji
  static void drawEmojiSubtitleBox() {
    if (!tft) return;
    int w = tft->width();
    int h = tft->height();
    int subY = 220;
    int subH = h - subY - 8;

    tft->fillRoundRect(10, subY, w - 20, subH, 8, C_CARD_BG);
    tft->drawRoundRect(10, subY, w - 20, subH, 8, 0x2188);

    // Badge "• XIAOZHI AI" giống hệt trên Web UI
    tft->fillCircle(20, subY + 13, 3, C_NEON_CYAN);
    tft->setTextSize(1);
    tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
    tft->setCursor(28, subY + 9);
    tft->print("XIAOZHI AI");

    String cleanSub = toCleanAscii(customSubtitle);
    if (cleanSub.length() == 0) {
      cleanSub = "Xin chao! Minh la XiaoZhi * Hay cham bieu cam hoac tro chuyen cung minh nhe!";
    }
    tft->setTextColor(C_WHITE, C_CARD_BG);
    for (int line = 0; line < 3; line++) {
      int startIdx = line * 33;
      if (startIdx >= (int)cleanSub.length()) break;
      String part = cleanSub.substring(startIdx, min((int)cleanSub.length(), startIdx + 33));
      tft->setCursor(16, subY + 26 + line * 14);
      tft->print(part);
    }
  }

  // Vẽ Chế độ 1: Giao diện Biểu Cảm XiaoZhi Toàn Màn Hình (70% Mắt + 30% Phụ đề)
  static void drawFullEmojiScreen() {
    if (!tft) return;
    int w = tft->width();

    tft->fillScreen(C_BLACK);
    drawTopStatusBar();

    // Phần trên (70%): Đôi mắt tròn phát sáng đồng bộ 100% với Web UI
    drawXiaoZhiEyesBox(8, 28, w - 16, 186, eyeState, 2);

    // Phần dưới (30%): Hộp phụ đề 3 dòng
    drawEmojiSubtitleBox();
  }

  // Vẽ Chế độ 2: Giao diện PC Status HUD (4 Phong cách: Bars, Gauges, Graph, Matrix + Báo Offline)
  static void drawPcHudScreen(bool fullRedraw) {
    if (!tft) return;
    int w = tft->width();
    int h = tft->height();

    const PcMetrics& m = PcStatsManager::getMetrics();
    bool live = PcStatsManager::isLive();

    // Nếu trạng thái kết nối PC vừa thay đổi (Online <-> Offline) -> Vẽ lại toàn màn hình
    if (live != lastPcLiveState) {
      lastPcLiveState = live;
      fullRedraw = true;
    }

    if (fullRedraw) {
      tft->fillScreen(C_DARK_BG);
      drawTopStatusBar();
    }

    // Header HUD
    tft->fillRect(8, 28, w - 16, 20, C_CARD_BG);
    tft->drawRect(8, 28, w - 16, 20, live ? C_NEON_GREEN : C_NEON_PINK);
    tft->setTextSize(1);
    tft->setTextColor(live ? C_NEON_GREEN : C_NEON_PINK, C_CARD_BG);
    tft->setCursor(14, 34);
    const char* styleNames[] = { "BARS", "GAUGES", "GRAPH", "MATRIX" };
    tft->printf("THONG SO PC // %-6s [%s]", styleNames[hudStyle % 4], live ? "ONLINE" : "OFFLINE");

    // Nếu máy tính chưa kết nối hoặc mất kết nối Server (> 6.5 giây không có gói tin)
    if (!live) {
      if (fullRedraw) {
        // Khung cảnh báo chính giữa màn hình
        tft->fillRoundRect(10, 58, w - 20, 224, 8, C_CARD_BG);
        tft->drawRoundRect(10, 58, w - 20, 224, 8, C_NEON_PINK);
        tft->drawRoundRect(12, 60, w - 24, 220, 6, C_CARD_BORDER);

        // Vẽ biểu tượng Máy Tính / Server Mất Kết Nối
        int cx = w / 2;
        tft->drawRoundRect(cx - 28, 74, 56, 38, 4, C_NEON_AMBER);
        tft->fillRect(cx - 24, 78, 48, 30, C_BLACK);
        tft->fillRect(cx - 10, 112, 20, 5, C_NEON_AMBER);
        tft->fillRect(cx - 18, 117, 36, 3, C_NEON_AMBER);
        // Dấu X đỏ ngắt kết nối trên màn hình PC
        tft->drawLine(cx - 10, 84, cx + 10, 102, C_NEON_PINK);
        tft->drawLine(cx - 9,  84, cx + 11, 102, C_NEON_PINK);
        tft->drawLine(cx + 10, 84, cx - 10, 102, C_NEON_PINK);
        tft->drawLine(cx + 11, 84, cx - 9,  102, C_NEON_PINK);

        // Dòng thông báo chính theo đúng yêu cầu
        tft->setTextSize(1);
        tft->setTextColor(C_NEON_PINK, C_CARD_BG);
        tft->setCursor(26, 132);
        tft->print("! CANH BAO KET NOI SERVER !");

        tft->fillRoundRect(18, 148, w - 36, 44, 5, C_BLACK);
        tft->drawRoundRect(18, 148, w - 36, 44, 5, C_NEON_AMBER);
        tft->setTextColor(C_YELLOW, C_BLACK);
        tft->setCursor(30, 157);
        tft->print("THIET BI DANG OFFLINE");
        tft->setTextColor(C_WHITE, C_BLACK);
        tft->setCursor(24, 173);
        tft->print("HOAC MAT KET NOI SERVER!");

        // Hướng dẫn kết nối giao thức Server
        tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
        tft->setCursor(20, 204);
        tft->print("Giao thuc: HTTP /api/pc_stats");
        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(20, 220);
        String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Chua ket noi WiFi";
        tft->printf("IP Tram: %s", ipStr.c_str());
        tft->setCursor(20, 236);
        tft->print("Chay script PC Telemetry de");
        tft->setCursor(20, 250);
        tft->print("dong bo CPU/GPU/RAM tu dong.");

        // Thanh Softkey Symbian S40 ở đáy
        tft->fillRect(0, h - 22, w, 22, 0x10A4);
        tft->drawFastHLine(0, h - 22, w, C_NEON_CYAN);
        tft->setTextColor(C_NEON_CYAN, 0x10A4);
        tft->setCursor(8, h - 15);
        tft->print("[MENU: S40]");
        tft->setTextColor(C_WHITE, 0x10A4);
        tft->setCursor(92, h - 15);
        tft->print("UP/DN:Kieu");
        tft->setTextColor(C_NEON_PINK, 0x10A4);
        tft->setCursor(w - 64, h - 15);
        tft->print("[EXIT:Ve]");
      }
      return;
    }

    int cpu = (int)round(m.cpu.usage);
    int gpu = (int)round(m.gpu.usage);
    int ram = (int)round(m.ram.usage);
    float cpuTemp = m.cpu.temp;
    float gpuTemp = m.gpu.temp;
    String cpuName = toCleanAscii(m.cpu.shortName);
    String gpuName = toCleanAscii(m.gpu.shortName);

    if (hudStyle == 0) {
      // STYLE 0: CYBERPUNK BARS
      auto drawBarCard = [&](int y, const char* label, const String& model, int pct, float temp, uint16_t col) {
        tft->fillRoundRect(8, y, w - 16, 56, 5, C_CARD_BG);
        tft->drawRoundRect(8, y, w - 16, 56, 5, C_CARD_BORDER);
        tft->setTextSize(1);
        tft->setTextColor(col, C_CARD_BG);
        tft->setCursor(14, y + 6);
        tft->printf("%s: %-10s", label, model.c_str());
        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(w - 52, y + 6);
        tft->printf("%3d%%", pct);

        int barW = w - 28;
        tft->drawRect(14, y + 20, barW, 14, col);
        int fillW = (barW - 4) * constrain(pct, 0, 100) / 100;
        tft->fillRect(16, y + 22, fillW, 10, col);
        tft->fillRect(16 + fillW, y + 22, (barW - 4) - fillW, 10, C_BLACK);

        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(14, y + 39);
        tft->printf("TEMP: %.1f oC", temp);
      };

      drawBarCard(54,  "CPU", cpuName, cpu, cpuTemp, C_NEON_CYAN);
      drawBarCard(116, "GPU", gpuName, gpu, gpuTemp, C_NEON_PINK);
      drawBarCard(178, "RAM", "DDR4 12GB", ram, 42.0f, C_NEON_AMBER);

      // Footer Net Speed
      tft->fillRoundRect(8, 240, w - 16, 70, 5, C_CARD_BG);
      tft->drawRoundRect(8, 240, w - 16, 70, 5, C_NEON_GREEN);
      tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
      tft->setCursor(14, 248);
      tft->printf("NET DL: %.1f KB/s | UL: %.1f KB/s", live ? m.net.dlSpeedKb : 142.5f, live ? m.net.ulSpeedKb : 24.1f);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 266);
      tft->printf("DISK USAGE: %.1f%% | FAN: %d RPM", live ? m.disk.usage : 82.0f, live ? m.fanRpm : 1450);
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(14, 286);
      tft->print("Bam phim 's' tren Serial de doi Style HUD");

    } else if (hudStyle == 1) {
      // STYLE 1: SPEEDOMETER GAUGES (Đồng hồ đo vòng tròn)
      auto drawGauge = [&](int cx, int cy, const char* title, const String& sub, int pct, float temp, uint16_t col) {
        tft->fillCircle(cx, cy, 46, C_CARD_BG);
        tft->drawCircle(cx, cy, 46, C_CARD_BORDER);
        tft->drawCircle(cx, cy, 45, col);
        tft->drawCircle(cx, cy, 38, col);

        tft->setTextSize(2);
        tft->setTextColor(col, C_CARD_BG);
        tft->setCursor(cx - 20, cy - 12);
        tft->printf("%2d%%", pct);

        tft->setTextSize(1);
        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(cx - 18, cy + 10);
        tft->printf("%.0foC", temp);

        tft->setTextColor(col, C_DARK_BG);
        tft->setCursor(cx - 32, cy + 52);
        tft->printf("%s %s", title, sub.c_str());
      };

      drawGauge(64,  112, "CPU", cpuName, cpu, cpuTemp, C_NEON_CYAN);
      drawGauge(176, 112, "GPU", gpuName, gpu, gpuTemp, C_NEON_PINK);
      drawGauge(64,  238, "RAM", "12GB",  ram, 41.0f,   C_NEON_AMBER);
      drawGauge(176, 238, "DSK", "SSD",   (int)(live ? m.disk.usage : 76), 39.0f, C_NEON_GREEN);

    } else if (hudStyle == 2) {
      // STYLE 2: REAL-TIME WAVEFORM GRAPH
      for (int i = 0; i < 19; i++) {
        graphHistoryCpu[i] = graphHistoryCpu[i + 1];
        graphHistoryGpu[i] = graphHistoryGpu[i + 1];
      }
      graphHistoryCpu[19] = constrain(cpu, 0, 100);
      graphHistoryGpu[19] = constrain(gpu, 0, 100);

      int gx = 12, gy = 58, gw = w - 24, gh = 160;
      tft->fillRect(gx, gy, gw, gh, C_BLACK);
      tft->drawRect(gx, gy, gw, gh, C_NEON_CYAN);

      // Grid lines
      for (int r = 1; r < 4; r++) {
        tft->drawFastHLine(gx + 1, gy + (gh * r) / 4, gw - 2, C_CARD_BORDER);
      }

      int stepX = (gw - 4) / 19;
      for (int i = 0; i < 19; i++) {
        int x1 = gx + 2 + i * stepX;
        int x2 = gx + 2 + (i + 1) * stepX;
        int yCpu1 = gy + gh - 4 - (graphHistoryCpu[i] * (gh - 8) / 100);
        int yCpu2 = gy + gh - 4 - (graphHistoryCpu[i + 1] * (gh - 8) / 100);
        int yGpu1 = gy + gh - 4 - (graphHistoryGpu[i] * (gh - 8) / 100);
        int yGpu2 = gy + gh - 4 - (graphHistoryGpu[i + 1] * (gh - 8) / 100);
        tft->drawLine(x1, yCpu1, x2, yCpu2, C_NEON_CYAN);
        tft->drawLine(x1, yGpu1, x2, yGpu2, C_NEON_PINK);
      }

      tft->fillRoundRect(8, 228, w - 16, 80, 6, C_CARD_BG);
      tft->setTextSize(1);
      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(16, 238);
      tft->printf("● CPU (%s): %d%%  [%.1f oC]   ", cpuName.c_str(), cpu, cpuTemp);
      tft->setTextColor(C_NEON_PINK, C_CARD_BG);
      tft->setCursor(16, 258);
      tft->printf("● GPU (%s): %d%%  [%.1f oC]   ", gpuName.c_str(), gpu, gpuTemp);
      tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
      tft->setCursor(16, 278);
      tft->printf("● RAM USED    : %d%%  [%.1f GB]   ", ram, live ? m.ram.usedGb : 8.2f);

    } else {
      // STYLE 3: TERMINAL MATRIX GRID (2x2)
      auto drawMatrixCell = [&](int x, int y, const char* tag, const String& model, int pct, float subVal, uint16_t col) {
        tft->fillRoundRect(x, y, 108, 122, 6, C_CARD_BG);
        tft->drawRoundRect(x, y, 108, 122, 6, col);
        tft->setTextSize(1);
        tft->setTextColor(col, C_CARD_BG);
        tft->setCursor(x + 8, y + 8);
        tft->printf("[%s] %s", tag, model.c_str());

        tft->setTextSize(3);
        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(x + 16, y + 34);
        tft->printf("%2d%%", pct);

        tft->setTextSize(1);
        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(x + 12, y + 74);
        tft->printf("TEMP: %.1f oC", subVal);

        // 5 segment blocks
        int activeSegs = (pct + 10) / 20;
        for (int s = 0; s < 5; s++) {
          tft->fillRect(x + 10 + s * 18, y + 96, 14, 12, (s < activeSegs) ? col : C_BLACK);
        }
      };

      drawMatrixCell(8,   56,  "CPU", cpuName, cpu, cpuTemp, C_NEON_CYAN);
      drawMatrixCell(124, 56,  "GPU", gpuName, gpu, gpuTemp, C_NEON_PINK);
      drawMatrixCell(8,   188, "RAM", "DDR4",  ram, 42.0f,   C_NEON_AMBER);
      drawMatrixCell(124, 188, "DSK", "NVMe",  (int)(live ? m.disk.usage : 84), 38.0f, C_NEON_GREEN);
    }
  }

  // Vẽ Chế độ 3 (Màn hình thứ 4): Bảng Kiểm Tra DIO (7 Phím + Chạm + SHT31) & Máy Hiện Sóng Âm Thanh INMP441
  static void drawHardwareDiagScreen(bool fullRedraw = true) {
    if (!tft) return;
    int w = tft->width();
    int h = tft->height();

    if (fullRedraw) {
      tft->fillScreen(C_DARK_BG);
      tft->drawRect(0, 0, w, h, C_CYAN);

      // Header
      tft->fillRect(4, 4, w - 8, 24, C_BLUE);
      tft->setTextColor(C_WHITE);
      tft->setTextSize(1);
      tft->setCursor(10, 8);
      tft->print("DIO & AUDIO OSCILLOSCOPE TEST");
      tft->setTextColor(C_YELLOW);
      tft->setCursor(10, 18);
      tft->print("SHT31(I2C) + INMP441(MIC) + 7-KEY DIO");

      // Khung Card 1: SHT31 I2C Status (y=31..85)
      tft->fillRoundRect(6, 31, w - 12, 54, 5, C_CARD_BG);

      // Khung Card 2: INMP441 Calibrated Oscilloscope (y=89..235)
      tft->fillRoundRect(6, 89, w - 12, 146, 5, C_CARD_BG);
      tft->drawRoundRect(6, 89, w - 12, 146, 5, C_NEON_CYAN);
      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(12, 94);
      tft->print("INMP441 (SCK:4 WS:5 SD:6) 24-BIT");

      // Khung Card 3: DIO (7 Phím Bấm + Chạm TTP223) (y=239..314)
      tft->fillRoundRect(6, 239, w - 12, 75, 5, C_CARD_BG);
      tft->drawRoundRect(6, 239, w - 12, 75, 5, C_NEON_GREEN);
    }

    // --- 1. CẬP NHẬT CARD 1: SHT31 I2C ---
    bool shtOk = TestSensors::isSht31Connected();
    tft->drawRoundRect(6, 31, w - 12, 54, 5, shtOk ? C_NEON_GREEN : C_NEON_PINK);
    tft->setTextSize(1);
    tft->setTextColor(shtOk ? C_NEON_GREEN : C_NEON_PINK, C_CARD_BG);
    tft->setCursor(12, 36);
    tft->printf("SHT31: %-28.28s", TestSensors::getDiagSummary().c_str());

    if (shtOk) {
      tft->setTextSize(2);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(12, 50);
      tft->printf("%4.1fC", TestSensors::getTemperatureC());
      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(96, 50);
      tft->printf("%4.1f%%RH", TestSensors::getHumidityPct());
      tft->setTextSize(1);
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(12, 71);
      tft->printf("I2C: SDA=IO%d, SCL=IO%d (50kHz)   ",
                  TestSensors::getActiveSdaPin(), TestSensors::getActiveSclPin());
    } else {
      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(12, 50);
      tft->print("Chua nhan ACK! Bam 't' tren Serial ");
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(12, 64);
      tft->print("de tu dong quet & dao thu SDA/SCL  ");
    }

    // --- 2. CẬP NHẬT CARD 2: INMP441 REAL-TIME WAVEFORM OSCILLOSCOPE ---
    tft->setTextSize(1);
    int micPct      = TestAudio::getLastMicLevelPct();
    int32_t micPeak = TestAudio::getLastMicPeak();
    int32_t raw24L  = TestAudio::getLastRaw24Left();
    tft->setTextColor(micPeak > 0 ? C_NEON_GREEN : C_NEON_AMBER, C_CARD_BG);
    tft->setCursor(12, 106);
    tft->printf("CH:%-18.18s L24:%+9ld", TestAudio::getActiveMicChannelName(), (long)raw24L);

    // Khung vẽ sóng âm (x=12, y=118, sw=216, sh=78)
    const int sx = 12, sy = 118, sw = 216, sh = 78;
    const int midY = sy + (sh / 2);
    tft->fillRect(sx, sy, sw, sh, C_BLACK);
    tft->drawRect(sx, sy, sw, sh, C_CARD_BORDER);

    for (int gx = sx + 4; gx < sx + sw - 4; gx += 6) {
      tft->drawPixel(gx, midY, C_GRAY);
    }

    const int8_t* wave = TestAudio::getWaveformBuffer();
    int numPts = 48;
    int prevX = sx + 3;
    int prevY = midY;

    for (int i = 0; i < numPts; i++) {
      int cx = sx + 3 + (i * (sw - 6)) / (numPts - 1);
      int offset = ((int)wave[i] * 35) / 28;
      int cy = midY - offset;
      if (cy < sy + 2) cy = sy + 2;
      if (cy > sy + sh - 3) cy = sy + sh - 3;

      uint16_t waveCol = (abs(wave[i]) > 18) ? C_NEON_PINK :
                         (abs(wave[i]) > 6)  ? C_NEON_GREEN : C_NEON_CYAN;

      if (abs(offset) > 1) {
        int topY = (cy < midY) ? cy : midY;
        int barH = abs(cy - midY);
        tft->drawFastVLine(cx, topY, barH, C_BLUE);
      }
      if (i > 0) {
        tft->drawLine(prevX, prevY, cx, cy, waveCol);
      }
      tft->fillRect(cx - 1, cy - 1, 2, 2, C_WHITE);
      prevX = cx;
      prevY = cy;
    }

    // Thanh VU Meter (y=200..228)
    tft->setTextColor(C_WHITE, C_CARD_BG);
    tft->setCursor(12, 200);
    tft->printf("VU: %3d%% | Peak24: %-9ld", micPct, (long)micPeak);

    int vuX = 12, vuY = 213, vuW = 216, vuH = 14;
    tft->drawRect(vuX, vuY, vuW, vuH, C_CARD_BORDER);
    int fillW = ((vuW - 4) * micPct) / 100;
    uint16_t vuCol = (micPct > 70) ? C_NEON_PINK : (micPct > 30) ? C_NEON_AMBER : C_NEON_GREEN;
    if (fillW > 0) {
      tft->fillRect(vuX + 2, vuY + 2, fillW, vuH - 4, vuCol);
    }
    if (fillW < vuW - 4) {
      tft->fillRect(vuX + 2 + fillW, vuY + 2, (vuW - 4) - fillW, vuH - 4, C_BLACK);
    }

    // --- 3. CẬP NHẬT CARD 3: THANG ĐIỆN TRỞ 7 NÚT ADC (GPIO 3) ---
    tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
    tft->setCursor(12, 245);
    tft->printf("ADC KEYPAD(IO%d): %4.2fV | ADC:%4d  ",
                TestButtons::getAdcPin(),
                TestButtons::getLastAdcVoltage(),
                TestButtons::getLastAdcRaw());

    tft->setTextColor(C_YELLOW, C_CARD_BG);
    tft->setCursor(12, 259);
    tft->printf("LAST KEY: %-26.26s", TestButtons::getLastButtonName().c_str());

    // Vẽ 7 ô trạng thái theo đúng thứ tự bảng thang trở: 0:OK, 1:UP, 2:DN, 3:LF, 4:RT, 5:MN, 6:EX
    const char* btnShort[7] = { "OK", "UP", "DN", "LF", "RT", "MN", "EX" };
    int activeIdx = TestButtons::getActiveButtonIndex();
    for (int b = 0; b < 7; b++) {
      bool pressed = (activeIdx == b);
      int bx = 12 + b * 31;
      int by = 273;
      tft->fillRoundRect(bx, by, 28, 18, 3, pressed ? C_NEON_PINK : C_BLACK);
      tft->drawRoundRect(bx, by, 28, 18, 3, pressed ? C_WHITE : C_CARD_BORDER);
      tft->setTextColor(pressed ? C_WHITE : C_SLATE);
      tft->setCursor(bx + 8, by + 5);
      tft->print(btnShort[b]);
    }

    tft->setTextColor(C_SLATE, C_CARD_BG);
    tft->setCursor(12, 297);
    tft->print("Bam MENU: Symbian S40 | EXIT: Thoat");
  }

  // ============================================================================
  // BỘ ĐIỀU KHIỂN GIAO DIỆN SYMBIAN S40 (MENU BIỂU TƯỢNG & CÁC ỨNG DỤNG CON)
  // ============================================================================

  // Vẽ Thanh Tiêu Đề Symbian S40 + Thanh Softkey Đáy + Thông Báo Toast
  static void drawSymbianChrome(const char* title, const char* leftSoft, const char* midSoft, const char* rightSoft) {
    int w = tft->width();
    int h = tft->height();

    // 1. Thanh trạng thái trên cùng (y=0..24)
    tft->fillRect(0, 0, w, 24, 0x10A4);
    tft->drawFastHLine(0, 24, w, C_NEON_CYAN);

    // Cột sóng Wi-Fi bên trái
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    for (int b = 0; b < 4; b++) {
      int bh = 4 + b * 3;
      tft->fillRect(6 + b * 4, 18 - bh, 3, bh, wifiOk ? C_NEON_GREEN : C_GRAY);
    }

    // Tiêu đề App ở giữa
    tft->setTextSize(1);
    tft->setTextColor(C_NEON_CYAN, 0x10A4);
    tft->setCursor(28, 8);
    tft->print(title);

    // Đồng hồ nhỏ + Biểu tượng đèn nền góc phải
    int hr, mn, sc, wd, dy, mo, yr;
    getCurrentDateTime(hr, mn, sc, wd, dy, mo, yr);
    tft->setTextColor(C_WHITE, 0x10A4);
    tft->setCursor(w - 64, 8);
    tft->printf("%02d:%02d", hr, mn);

    // Vạch pin / Độ sáng GPIO7
    tft->drawRect(w - 26, 7, 18, 10, C_NEON_GREEN);
    tft->fillRect(w - 8, 9, 2, 6, C_NEON_GREEN);
    int batFill = (screenBrightnessPct * 14) / 100;
    tft->fillRect(w - 24, 9, batFill, 6, C_NEON_GREEN);

    // 2. Thanh Softkey Symbian S40 ở đáy (y=298..320)
    tft->fillRect(0, h - 22, w, 22, 0x10A4);
    tft->drawFastHLine(0, h - 22, w, C_NEON_CYAN);
    tft->setTextSize(1);
    tft->setTextColor(C_NEON_GREEN, 0x10A4);
    tft->setCursor(6, h - 15);
    tft->print(leftSoft);

    if (midSoft && strlen(midSoft) > 0) {
      int mx = (w - (int)strlen(midSoft) * 6) / 2;
      tft->setTextColor(C_YELLOW, 0x10A4);
      tft->setCursor(mx, h - 15);
      tft->print(midSoft);
    }

    int rx = w - (int)strlen(rightSoft) * 6 - 6;
    tft->setTextColor(C_NEON_PINK, 0x10A4);
    tft->setCursor(max(140, rx), h - 15);
    tft->print(rightSoft);

    // 3. Nếu đang có Toast thông báo ngắn (ví dụ: "DA LUU HINH NEN!")
    if (s40ToastMsg.length() > 0 && millis() < s40ToastExpireMs) {
      tft->fillRoundRect(20, h - 50, w - 40, 22, 5, C_NEON_GREEN);
      tft->drawRoundRect(20, h - 50, w - 40, 22, 5, C_WHITE);
      tft->setTextColor(C_BLACK, C_NEON_GREEN);
      int tx = (w - (int)s40ToastMsg.length() * 6) / 2;
      tft->setCursor(max(24, tx), h - 43);
      tft->print(s40ToastMsg);
    }
  }

  static void showSymbianToast(const String& msg, unsigned long durationMs = 2000) {
    s40ToastMsg = msg;
    s40ToastExpireMs = millis() + durationMs;
  }

  // Vẽ biểu tượng đồ họa cho từng mục trong Menu Symbian S40
  static void drawSymbianIconArt(int idx, int cx, int cy) {
    if (idx == 0) {
      // 0: HÌNH NỀN (Khung tranh + Mặt trời + Núi)
      tft->drawRoundRect(cx - 18, cy - 14, 36, 26, 3, C_NEON_CYAN);
      tft->fillCircle(cx - 8, cy - 6, 4, C_YELLOW);
      tft->fillTriangle(cx - 14, cy + 10, cx - 2, cy - 2, cx + 8, cy + 10, C_NEON_GREEN);
      tft->fillTriangle(cx - 2, cy + 10, cx + 8, cy - 4, cx + 15, cy + 10, C_NEON_BLUE);
    } else if (idx == 1) {
      // 1: CÀI ĐẶT (3 thanh trượt Sliders + Bánh răng)
      for (int r = 0; r < 3; r++) {
        int ry = cy - 10 + r * 9;
        tft->drawFastHLine(cx - 16, ry, 32, C_SLATE);
        int knobX = (r == 0) ? (cx + 6) : ((r == 1) ? (cx - 6) : (cx + 2));
        uint16_t kCol = (r == 0) ? C_NEON_AMBER : ((r == 1) ? C_NEON_CYAN : C_NEON_GREEN);
        tft->fillCircle(knobX, ry, 3, kCol);
      }
    } else if (idx == 2) {
      // 2: THÔNG SỐ PC (Màn hình máy tính + Đồ thị xung nhịp)
      tft->drawRoundRect(cx - 18, cy - 14, 36, 23, 3, C_NEON_GREEN);
      tft->fillRect(cx - 6, cy + 9, 12, 3, C_NEON_GREEN);
      tft->drawFastHLine(cx - 12, cy + 12, 24, C_NEON_GREEN);
      tft->drawLine(cx - 13, cy + 2, cx - 6, cy - 6, C_NEON_CYAN);
      tft->drawLine(cx - 6, cy - 6, cx + 1, cy + 4, C_NEON_PINK);
      tft->drawLine(cx + 1, cy + 4, cx + 12, cy - 8, C_YELLOW);
    } else if (idx == 3) {
      // 3: THƯ VIỆN ẢNH (2 tấm ảnh xếp chồng Album)
      tft->drawRoundRect(cx - 14, cy - 14, 30, 22, 3, C_SLATE);
      tft->fillRoundRect(cx - 18, cy - 10, 30, 22, 3, C_CARD_BG);
      tft->drawRoundRect(cx - 18, cy - 10, 30, 22, 3, C_NEON_PINK);
      tft->fillCircle(cx - 10, cy - 3, 3, C_NEON_AMBER);
      tft->fillTriangle(cx - 14, cy + 9, cx - 3, cy, cx + 8, cy + 9, C_NEON_CYAN);
    } else if (idx == 4) {
      // 4: ABOUT / THÔNG TIN (Huy hiệu chữ 'i' tròn phát sáng)
      tft->drawCircle(cx, cy - 1, 14, C_NEON_PURPLE);
      tft->drawCircle(cx, cy - 1, 13, C_NEON_CYAN);
      tft->fillCircle(cx, cy - 8, 2, C_YELLOW);
      tft->fillRect(cx - 2, cy - 3, 4, 10, C_WHITE);
    } else {
      // 5: TEST DIO & SÓNG ÂM (Biểu tượng sóng âm + phím bấm)
      tft->drawRoundRect(cx - 18, cy - 13, 36, 24, 3, C_NEON_AMBER);
      for (int i = -12; i <= 12; i += 4) {
        int bh = (abs(i) == 4) ? 14 : ((i == 0) ? 18 : 6);
        tft->fillRect(cx + i - 1, cy - 1 - bh / 2, 2, bh, C_NEON_CYAN);
      }
    }
  }

  // CHẾ ĐỘ 4: MENU CHÍNH SYMBIAN S40 (LƯỚI 6 BIỂU TƯỢNG 3x2)
  static void drawSymbianMenuScreen() {
    if (!tft) return;
    int w = tft->width();
    tft->fillScreen(C_DARK_BG);
    drawSymbianChrome("MENU", "[OK]", "DIEU HUONG", "[EXIT]");

    const char* titles[6] = {
      "Hinh nen", "Cai dat", "Thong so",
      "Thu vien", "About",   "Test DIO"
    };
    const char* descs[6] = {
      "1. HINH NEN & DONG HO MAN HINH CHO",
      "2. CAI DAT & AM THANH",
      "3. THONG SO MAY TINH (PC TELEMETRY)",
      "4. THU VIEN ANH",
      "5. THONG TIN",
      "6. KIEM TRA DIO, KEYPAD & SONG AM"
    };

    // Banner mô tả mục đang chọn (y=28..50)
    tft->fillRoundRect(8, 28, w - 16, 22, 4, C_CARD_BG);
    tft->drawRoundRect(8, 28, w - 16, 22, 4, C_NEON_AMBER);
    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, C_CARD_BG);
    tft->setCursor(14, 35);
    tft->print(descs[s40MenuCursor % 6]);

    // Lưới 3 cột x 2 hàng (y=56..290)
    const int cellW = 70;
    const int cellH = 106;
    const int startX = 10;
    const int startY = 58;
    const int gapX = 5;
    const int gapY = 12;

    for (int i = 0; i < 6; i++) {
      int col = i % 3;
      int row = i / 3;
      int x = startX + col * (cellW + gapX);
      int y = startY + row * (cellH + gapY);
      bool sel = (i == s40MenuCursor);

      tft->fillRoundRect(x, y, cellW, cellH, 7, sel ? 0x1949 : C_CARD_BG);
      tft->drawRoundRect(x, y, cellW, cellH, 7, sel ? C_NEON_CYAN : C_CARD_BORDER);
      if (sel) {
        tft->drawRoundRect(x + 1, y + 1, cellW - 2, cellH - 2, 6, C_NEON_AMBER);
      }

      drawSymbianIconArt(i, x + cellW / 2, y + 38);

      tft->setTextSize(1);
      tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? 0x1949 : C_CARD_BG);
      int tw = strlen(titles[i]) * 6;
      tft->setCursor(x + (cellW - tw) / 2, y + 82);
      tft->print(titles[i]);
    }
  }

  // CHẾ ĐỘ 5: APP HÌNH NỀN (TÙY CHỈNH MÀN HÌNH CHỜ GIỐNG TRÊN WEB UI)
  static void drawWallpaperAppScreen() {
    if (!tft) return;
    int w = tft->width();
    tft->fillScreen(C_DARK_BG);
    drawSymbianChrome("CAI DAT HINH NEN", "[OK: Luu]", "<Doi Gia Tri>", "[EXIT: Menu]");

    // Đọc tên file ảnh hiện tại
    String imgLabel = stCfg.bgImage;
    if (imgLabel.startsWith("/")) imgLabel = imgLabel.substring(1);
    if (imgLabel.length() == 0) imgLabel = "(Chua chon)";
    if (imgLabel.length() > 14) imgLabel = imgLabel.substring(0, 14);

    // Tên màu đồng hồ
    const char* colorName = "Cyan";
    if (stCfg.clockColor == C_NEON_GREEN) colorName = "Xanh La";
    else if (stCfg.clockColor == C_NEON_PINK) colorName = "Hong Neon";
    else if (stCfg.clockColor == C_YELLOW || stCfg.clockColor == C_NEON_AMBER) colorName = "Vang";
    else if (stCfg.clockColor == C_NEON_PURPLE) colorName = "Tim";
    else if (stCfg.clockColor == C_WHITE) colorName = "Trang";

    String labels[10] = {
      "1. Che do nen",
      "2. Anh JPEG",
      "3. Do toi nen",
      "4. Kieu dong ho",
      "5. Vi tri gio",
      "6. He 12h/24h",
      "7. Hien so giay",
      "8. Hien thoi tiet",
      "9. Mau dong ho",
      "10.[XEM MAN HINH CHO]"
    };

    String values[10] = {
      stCfg.bgMode,
      imgLabel,
      String(stCfg.dimOverlay) + "%",
      stCfg.clockStyle,
      stCfg.clockPos,
      stCfg.clockFormat,
      stCfg.showSeconds ? "BAT (ON)" : "TAT (OFF)",
      stCfg.showWeather ? "BAT (ON)" : "TAT (OFF)",
      String(colorName),
      "Bam OK ->"
    };

    // Hiển thị cửa sổ cuộn 7 dòng mỗi trang
    int firstRow = 0;
    if (s40WallpaperCursor >= 7) firstRow = s40WallpaperCursor - 6;

    for (int vis = 0; vis < 7; vis++) {
      int idx = firstRow + vis;
      if (idx >= 10) break;
      int ry = 30 + vis * 37;
      bool sel = (idx == s40WallpaperCursor);

      tft->fillRoundRect(6, ry, w - 12, 33, 5, sel ? 0x1949 : C_CARD_BG);
      tft->drawRoundRect(6, ry, w - 12, 33, 5, sel ? C_NEON_CYAN : C_CARD_BORDER);

      tft->setTextSize(1);
      tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? 0x1949 : C_CARD_BG);
      tft->setCursor(12, ry + 6);
      tft->print(labels[idx]);

      tft->setTextColor(sel ? C_NEON_GREEN : C_NEON_CYAN, sel ? 0x1949 : C_CARD_BG);
      tft->setCursor(18, ry + 19);
      tft->printf("< %s >", values[idx].c_str());
    }
  }

  // CHẾ ĐỘ 6: APP CÀI ĐẶT HỆ THỐNG (ĐỘ SÁNG PWM GPIO 7, TIMEOUT, ÂM THANH MAX98357A)
  static void drawSettingsAppScreen() {
    if (!tft) return;
    int w = tft->width();
    tft->fillScreen(C_DARK_BG);
    drawSymbianChrome("CAI DAT HE THONG", "[OK: Chon]", "<Tang / Giam>", "[EXIT: Menu]");

    int spkVol = TestAudio::getSpeakerVolumePct();
    const char* micSensNames[3] = { "Thap (Low)", "Tieu chuan", "Cao (High)" };

    String labels[7] = {
      "1. Do sang",
      "2. Thoi gian sang man hinh",
      "3. Am luong",
      "4. Am phim bam (Key Beep)",
      "5. Do nhay Mic",
      "6. Bieu cam Tro ly XiaoZhi",
      "7. [MO MAY HIEN SONG & DIO]"
    };

    String values[7] = {
      String(screenBrightnessPct) + "% (PWM)",
      String(TIMEOUT_LABELS[screenTimeoutIdx % 6]),
      String(spkVol) + "% (USB-Safe)",
      keyBeepEnabled ? "BAT (ON)" : "TAT (OFF)",
      String(micSensNames[micSensitivityMode % 3]),
      getEmojiStateName(),
      "Bam OK de mo ->"
    };

    for (int idx = 0; idx < 7; idx++) {
      int ry = 30 + idx * 37;
      bool sel = (idx == s40SettingsCursor);

      tft->fillRoundRect(6, ry, w - 12, 34, 5, sel ? 0x1949 : C_CARD_BG);
      tft->drawRoundRect(6, ry, w - 12, 34, 5, sel ? C_NEON_CYAN : C_CARD_BORDER);

      tft->setTextSize(1);
      tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? 0x1949 : C_CARD_BG);
      tft->setCursor(12, ry + 5);
      tft->print(labels[idx]);

      // Nếu là Độ sáng (idx=0) hoặc Âm lượng Loa (idx=2) thì vẽ thêm thanh Slider trực quan
      if (idx == 0 || idx == 2) {
        int pct = (idx == 0) ? screenBrightnessPct : spkVol;
        uint16_t barCol = (idx == 0) ? C_NEON_AMBER : C_NEON_GREEN;
        int bx = 12, by = ry + 19, bw = 116, bh = 10;
        tft->drawRect(bx, by, bw, bh, barCol);
        int fillW = ((bw - 4) * constrain(pct, 0, 100)) / 100;
        if (fillW > 0) tft->fillRect(bx + 2, by + 2, fillW, bh - 4, barCol);
        if (fillW < bw - 4) tft->fillRect(bx + 2 + fillW, by + 2, (bw - 4) - fillW, bh - 4, C_BLACK);

        tft->setTextColor(barCol, sel ? 0x1949 : C_CARD_BG);
        tft->setCursor(134, ry + 19);
        tft->print(values[idx]);
      } else {
        tft->setTextColor(sel ? C_NEON_GREEN : C_NEON_CYAN, sel ? 0x1949 : C_CARD_BG);
        tft->setCursor(16, ry + 19);
        tft->printf("< %s >", values[idx].c_str());
      }
    }
  }

  // Callback giải mã JPEG thuần túy cho App Thư Viện (không áp lớp kính mờ của màn hình chờ)
  static bool jpgGalleryCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!tft || y >= tft->height() || x >= tft->width()) return false;
    tft->drawRGBBitmap(x, y, bitmap, w, h);
    return true;
  }

  // CHẾ ĐỘ 7: APP THƯ VIỆN ẢNH (DUYỆT ẢNH TRÊN LITTLEFS & ĐẶT LÀM HÌNH NỀN)
  static void drawGalleryAppScreen() {
    if (!tft) return;
    int w = tft->width();
    tft->fillScreen(C_DARK_BG);
    drawSymbianChrome("THU VIEN ANH", "[OK:DatNen]", "<Truoc/Sau>", "[EXIT: Menu]");

    String files[20];
    size_t sizes[20];
    int count = getStoredJpgList(files, sizes, 20);

    if (count <= 0) {
      tft->fillRoundRect(12, 56, w - 24, 180, 6, C_CARD_BG);
      tft->drawRoundRect(12, 56, w - 24, 180, 6, C_NEON_PINK);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(28, 96);
      tft->print("THU VIEN DANG TRONG!");
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(20, 120);
      tft->print("Hay truy cap Web UI cua Tram");
      tft->setCursor(20, 136);
      tft->print("de tai anh .JPG len bo nho");
      tft->setCursor(20, 152);
      tft->print("nhe!");
      return;
    }

    if (s40GalleryIndex < 0) s40GalleryIndex = count - 1;
    if (s40GalleryIndex >= count) s40GalleryIndex = 0;

    String curFile = files[s40GalleryIndex];
    size_t curSizeKb = (sizes[s40GalleryIndex] + 512) / 1024;
    bool isCurrentBg = (stCfg.bgImage == curFile || ("/" + stCfg.bgImage) == curFile);

    // Thanh thông tin file ảnh (y=28..50)
    tft->fillRoundRect(6, 28, w - 12, 22, 4, C_CARD_BG);
    tft->drawRoundRect(6, 28, w - 12, 22, 4, isCurrentBg ? C_NEON_GREEN : C_NEON_CYAN);
    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, C_CARD_BG);
    tft->setCursor(12, 35);
    String shortName = curFile.startsWith("/") ? curFile.substring(1) : curFile;
    if (shortName.length() > 16) shortName = shortName.substring(0, 16);
    tft->printf("[%d/%d] %s (%uKB)", s40GalleryIndex + 1, count, shortName.c_str(), (unsigned int)curSizeKb);

    // Khung hiển thị Preview ảnh (y=54..266)
    tft->drawRect(18, 54, 204, 212, C_CARD_BORDER);
    uint16_t jw = 0, jh = 0;
    TJpgDec.setCallback(jpgGalleryCallback);
    TJpgDec.setSwapBytes(false);
    if (TJpgDec.getFsJpgSize(&jw, &jh, curFile.c_str(), LittleFS) == JDR_OK && jw > 0 && jh > 0) {
      uint8_t scale = 2; // Thu nhỏ để nằm gọn trong khung Preview
      if (jw >= 480 || jh >= 640) scale = 4;
      else if (jw <= 180 && jh <= 200) scale = 1;
      TJpgDec.setJpgScale(scale);
      int dw = jw / scale;
      int dh = jh / scale;
      int px = max(20, (w - dw) / 2);
      int py = max(56, 56 + (208 - dh) / 2);
      TJpgDec.drawFsJpg(px, py, curFile.c_str(), LittleFS);
    }

    // Trạng thái dưới đáy khung ảnh (y=272..292)
    tft->fillRoundRect(6, 270, w - 12, 22, 4, C_CARD_BG);
    tft->setTextColor(isCurrentBg ? C_NEON_GREEN : C_WHITE, C_CARD_BG);
    tft->setCursor(14, 277);
    if (isCurrentBg) {
      tft->print("* DANG LA HINH NEN MAN HINH CHO *");
    } else {
      tft->print("Bam OK de dat lam Hinh Nen");
    }
  }

  // CHẾ ĐỘ 8: APP ABOUT (THÔNG TIN PHẦN MỀM & THÔNG SỐ PHẦN CỨNG ESP32-S3)
  static void drawAboutAppScreen() {
    if (!tft) return;
    int w = tft->width();
    tft->fillScreen(C_DARK_BG);
    drawSymbianChrome("THONG TIN (ABOUT)", "[OK:DoiTrang]", "<Trang 1/2>", "[EXIT: Menu]");

    tft->fillRoundRect(6, 28, w - 12, 264, 6, C_CARD_BG);
    tft->drawRoundRect(6, 28, w - 12, 264, 6, C_NEON_PURPLE);

    if (s40AboutPage == 0) {
      // TRANG 1: THÔNG TIN PHẦN MỀM & KẾT NỐI MẠNG
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(14, 38);
      tft->print("[TRANG 1/2] PHAN MEM & KET NOI");
      tft->drawFastHLine(12, 50, w - 24, C_CARD_BORDER);

      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(14, 58);
      tft->print("Ten Thiet Bi: Tram Decor Vu Tru");
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 74);
      tft->print("Giao Dien   : Symbian S40 OS v2.5");
      tft->setCursor(14, 90);
      tft->printf("Build Date  : %s", __DATE__);

      tft->drawFastHLine(12, 106, w - 24, C_CARD_BORDER);
      tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
      tft->setCursor(14, 114);
      tft->print("TRANG THAI MANG & SERVER:");

      bool wifiOk = (WiFi.status() == WL_CONNECTED);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 130);
      tft->printf("Wi-Fi : %s", wifiOk ? toCleanAscii(WiFi.SSID()).c_str() : "Chua ket noi (AP Mode)");
      tft->setCursor(14, 146);
      tft->printf("IP    : %s", wifiOk ? WiFi.localIP().toString().c_str() : "192.168.4.1");
      tft->setCursor(14, 162);
      tft->printf("MAC   : %s", WiFi.macAddress().c_str());
      tft->setCursor(14, 178);
      tft->printf("RSSI  : %d dBm", wifiOk ? WiFi.RSSI() : 0);

      bool pcLive = PcStatsManager::isLive();
      tft->setTextColor(pcLive ? C_NEON_GREEN : C_NEON_PINK, C_CARD_BG);
      tft->setCursor(14, 196);
      tft->printf("PC Server: %s", pcLive ? "DANG KET NOI (ONLINE)" : "OFFLINE / MAT KET NOI");

      tft->drawFastHLine(12, 214, w - 24, C_CARD_BORDER);
      tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
      tft->setCursor(14, 222);
      tft->printf("Uptime : %lu phut %lu giay", (millis() / 60000UL), (millis() / 1000UL) % 60UL);
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(14, 242);
      tft->print("Bam LEFT/RIGHT hoac OK de xem");
      tft->setCursor(14, 256);
      tft->print("Trang 2: Thong so Phan Cung.");
    } else {
      // TRANG 2: THÔNG SỐ PHẦN CỨNG & SƠ ĐỒ CHÂN GPIO
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(14, 38);
      tft->print("[TRANG 2/2] THONG SO PHAN CUNG");
      tft->drawFastHLine(12, 50, w - 24, C_CARD_BORDER);

      uint32_t freeRamKb  = ESP.getFreeHeap() / 1024;
      uint32_t totalRamKb = ESP.getHeapSize() / 1024;
      uint32_t freePsramKb  = ESP.getFreePsram() / 1024;
      uint32_t totalPsramKb = ESP.getPsramSize() / 1024;
      uint32_t fsUsedKb   = LittleFS.usedBytes() / 1024;
      uint32_t fsTotalKb  = LittleFS.totalBytes() / 1024;

      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(14, 58);
      tft->printf("Vi xu ly : %s @ %dMHz", ESP.getChipModel(), ESP.getCpuFreqMHz());
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 74);
      tft->printf("SRAM     : %u / %u KB Free", (unsigned)freeRamKb, (unsigned)totalRamKb);
      tft->setCursor(14, 90);
      tft->printf("PSRAM    : %u / %u KB Free", (unsigned)freePsramKb, (unsigned)totalPsramKb);
      tft->setCursor(14, 106);
      tft->printf("LittleFS : %u / %u KB Used", (unsigned)fsUsedKb, (unsigned)fsTotalKb);

      tft->drawFastHLine(12, 122, w - 24, C_CARD_BORDER);
      tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
      tft->setCursor(14, 130);
      tft->print("CAU HINH PHAN CUNG & GPIO:");

      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 146);
      tft->printf("* Man hinh: ST7789 (BL=GPIO7 %d%%)", screenBrightnessPct);
      tft->setCursor(14, 162);
      tft->printf("* Keypad  : 1-Wire ADC GPIO3 (%.2fV)", TestButtons::getLastAdcVoltage());
      tft->setCursor(14, 178);
      tft->print("* Mic I2S : INMP441 (SCK:4 WS:5 SD:6)");
      tft->setCursor(14, 194);
      tft->printf("* Loa I2S : MAX98357A (Vol: %d%%)", TestAudio::getSpeakerVolumePct());
      tft->setCursor(14, 210);
      if (TestSensors::isSht31Connected()) {
        tft->printf("* SHT31   : OK (%.1fC / %.1f%%RH)", TestSensors::getTemperatureC(), TestSensors::getHumidityPct());
      } else {
        tft->print("* SHT31   : Chua ket noi (I2C 8,9)");
      }
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(14, 238);
      tft->print("Bam OK/LEFT/RIGHT de ve Trang 1");
    }
  }

  static void refreshActiveScreen() {
    if (!tft) return;
    if (isScreenSleeping) {
      tft->fillScreen(C_BLACK);
      ledcWrite(0, 0);
      return;
    }
    if (currentMode == 0) drawStandbyScreenFull();
    else if (currentMode == 1) drawFullEmojiScreen();
    else if (currentMode == 2) drawPcHudScreen(true);
    else if (currentMode == 3) drawHardwareDiagScreen(true);
    else if (currentMode == 4) drawSymbianMenuScreen();
    else if (currentMode == 5) drawWallpaperAppScreen();
    else if (currentMode == 6) drawSettingsAppScreen();
    else if (currentMode == 7) drawGalleryAppScreen();
    else if (currentMode == 8) drawAboutAppScreen();
  }

  // Xử lý thay đổi giá trị trong App Hình Nền (Mode 5) khi bấm LEFT (-1) hoặc RIGHT/OK (+1)
  static void adjustWallpaperAppSetting(int row, int dir) {
    if (row == 0) {
      // Đổi chế độ nền: image -> gradient-cyber -> gradient-nebula -> gradient-sunset -> solid-black
      const char* modes[5] = { "image", "gradient-cyber", "gradient-nebula", "gradient-sunset", "solid-black" };
      int cur = 0;
      for (int i = 0; i < 5; i++) {
        if (stCfg.bgMode == modes[i]) { cur = i; break; }
      }
      cur = (cur + dir + 5) % 5;
      stCfg.bgMode = modes[cur];
    } else if (row == 1) {
      // Chọn ảnh .jpg trong LittleFS
      String files[20];
      int count = getStoredJpgList(files, nullptr, 20);
      if (count > 0) {
        int cur = 0;
        for (int i = 0; i < count; i++) {
          if (stCfg.bgImage == files[i] || ("/" + stCfg.bgImage) == files[i]) { cur = i; break; }
        }
        cur = (cur + dir + count) % count;
        stCfg.bgImage = files[cur];
        stCfg.bgMode = "image";
      }
    } else if (row == 2) {
      // Độ tối lớp phủ nền (0% -> 80%)
      stCfg.dimOverlay = constrain(stCfg.dimOverlay + dir * 10, 0, 80);
    } else if (row == 3) {
      // Kiểu đồng hồ
      const char* styles[4] = { "digital", "neon", "minimal", "retro" };
      int cur = 0;
      for (int i = 0; i < 4; i++) {
        if (stCfg.clockStyle == styles[i]) { cur = i; break; }
      }
      cur = (cur + dir + 4) % 4;
      stCfg.clockStyle = styles[cur];
    } else if (row == 4) {
      // Vị trí đồng hồ: center -> top -> bottom
      const char* pos[3] = { "center", "top", "bottom" };
      int cur = 0;
      for (int i = 0; i < 3; i++) {
        if (stCfg.clockPos == pos[i]) { cur = i; break; }
      }
      cur = (cur + dir + 3) % 3;
      stCfg.clockPos = pos[cur];
    } else if (row == 5) {
      // Định dạng 12h / 24h
      stCfg.clockFormat = (stCfg.clockFormat == "24h") ? "12h" : "24h";
    } else if (row == 6) {
      stCfg.showSeconds = !stCfg.showSeconds;
    } else if (row == 7) {
      stCfg.showWeather = !stCfg.showWeather;
    } else if (row == 8) {
      // Màu đồng hồ
      const uint16_t cols[6] = { C_NEON_CYAN, C_NEON_GREEN, C_NEON_PINK, C_YELLOW, C_NEON_PURPLE, C_WHITE };
      int cur = 0;
      for (int i = 0; i < 6; i++) {
        if (stCfg.clockColor == cols[i]) { cur = i; break; }
      }
      cur = (cur + dir + 6) % 6;
      stCfg.clockColor = cols[cur];
    } else if (row == 9) {
      saveConfigToFile();
      currentMode = 0;
      refreshActiveScreen();
      return;
    }

    saveConfigToFile();
    showSymbianToast("DA LUU CAU HINH HINH NEN!");
    drawWallpaperAppScreen();
  }

  // Xử lý thay đổi giá trị trong App Cài Đặt (Mode 6) khi bấm LEFT (-1) hoặc RIGHT/OK (+1)
  static void adjustSettingsAppItem(int row, int dir) {
    if (row == 0) {
      // Độ sáng đèn nền PWM trên chân GPIO 7 (10% .. 100%)
      setBacklightBrightness(screenBrightnessPct + dir * 10);
    } else if (row == 1) {
      // Thời gian sáng màn hình
      screenTimeoutIdx = (screenTimeoutIdx + dir + 6) % 6;
    } else if (row == 2) {
      // Âm lượng Loa MAX98357A (0% .. 100%, bước 5%)
      TestAudio::setSpeakerVolumePct(TestAudio::getSpeakerVolumePct() + dir * 5);
    } else if (row == 3) {
      // Âm phím bấm (Key Beep)
      keyBeepEnabled = !keyBeepEnabled;
    } else if (row == 4) {
      // Độ nhạy Mic INMP441
      micSensitivityMode = (micSensitivityMode + dir + 3) % 3;
    } else if (row == 5) {
      // Đổi biểu cảm XiaoZhi
      eyeState = (eyeState + dir + 12) % 12;
      lastExternalEmojiSync = millis();
    } else if (row == 6) {
      // Mở màn hình Kiểm tra DIO & Máy Hiện Sóng
      currentMode = 3;
      refreshActiveScreen();
      return;
    }
    drawSettingsAppScreen();
  }

  // ============================================================================
  // HÀM NHẬN SỰ KIỆN TỪ BÀN PHÍM 7 NÚT ADC (GPIO 3)
  // keyIndex: 0=OK, 1=UP, 2=DOWN, 3=LEFT, 4=RIGHT, 5=MENU, 6=EXIT
  // ============================================================================
  void onKeypadEvent(int keyIndex) {
    lastUserActivityMs = millis();

    // Nếu màn hình đang tắt đèn nền (Sleep Timeout) -> Bấm phím bất kỳ sẽ đánh thức màn hình ngay!
    if (isScreenSleeping) {
      isScreenSleeping = false;
      setBacklightBrightness(screenBrightnessPct);
      Serial.println("💡 Đã đánh thức màn hình từ chế độ tiết kiệm điện!");
      refreshActiveScreen();
      return;
    }

    // Phím MENU (keyIndex == 5): Từ bất kỳ màn hình nào đều mở ngay Giao diện Symbian S40 Menu
    if (keyIndex == 5) {
      currentMode = 4;
      refreshActiveScreen();
      return;
    }

    // Điều hướng tùy theo màn hình hiện tại
    if (currentMode == 0 || currentMode == 1 || currentMode == 3) {
      // Đang ở Màn Hình Chờ (0), Biểu Cảm (1), hoặc Bảng DIO (3)
      if (keyIndex == 0) {
        // Phím OK ở Màn Hình Chờ -> Mở Menu Symbian S40
        currentMode = 4;
        refreshActiveScreen();
      } else if (keyIndex == 3) {
        // LEFT: Lùi giữa 4 màn hình chính (0 <-> 1 <-> 2 <-> 3)
        currentMode = (currentMode + 3) % 4;
        refreshActiveScreen();
      } else if (keyIndex == 4) {
        // RIGHT: Tiến giữa 4 màn hình chính (0 -> 1 -> 2 -> 3)
        currentMode = (currentMode + 1) % 4;
        refreshActiveScreen();
      } else if (keyIndex == 1 && currentMode == 1) {
        // UP ở màn Emoji: Đổi biểu cảm
        eyeState = (eyeState + 1) % 12;
        drawFullEmojiScreen();
      } else if (keyIndex == 2 && currentMode == 1) {
        // DOWN ở màn Emoji: Đổi biểu cảm
        eyeState = (eyeState + 11) % 12;
        drawFullEmojiScreen();
      } else if (keyIndex == 6) {
        // EXIT: Nếu đang ở màn phụ (1, 3) thì quay về Màn Hình Chờ (0)
        if (currentMode != 0) {
          currentMode = 0;
          refreshActiveScreen();
        }
      }
    } else if (currentMode == 2) {
      // Đang ở Màn Hình Thông Số PC (Mode 2)
      if (keyIndex == 1 || keyIndex == 4) {
        cycleHudStyle(1); // UP / RIGHT: Đổi kiểu hiển thị HUD (Bars / Gauges / Graph / Matrix)
      } else if (keyIndex == 2 || keyIndex == 3) {
        cycleHudStyle(-1); // DOWN / LEFT
      } else if (keyIndex == 0) {
        drawPcHudScreen(true); // OK: Làm mới ngay
      } else if (keyIndex == 6) {
        // EXIT: Quay lại Menu Symbian S40
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 4) {
      // ĐANG Ở MENU CHÍNH SYMBIAN S40 (Lưới 3x2: 0..5)
      if (keyIndex == 3) { // LEFT
        s40MenuCursor = (s40MenuCursor + 5) % 6;
        drawSymbianMenuScreen();
      } else if (keyIndex == 4) { // RIGHT
        s40MenuCursor = (s40MenuCursor + 1) % 6;
        drawSymbianMenuScreen();
      } else if (keyIndex == 1) { // UP
        s40MenuCursor = (s40MenuCursor + 3) % 6;
        drawSymbianMenuScreen();
      } else if (keyIndex == 2) { // DOWN
        s40MenuCursor = (s40MenuCursor + 3) % 6;
        drawSymbianMenuScreen();
      } else if (keyIndex == 0) { // OK -> Mở ứng dụng tương ứng
        if (s40MenuCursor == 0)      currentMode = 5; // Hình nền
        else if (s40MenuCursor == 1) currentMode = 6; // Cài đặt
        else if (s40MenuCursor == 2) currentMode = 2; // Thông số PC
        else if (s40MenuCursor == 3) currentMode = 7; // Thư viện
        else if (s40MenuCursor == 4) currentMode = 8; // About (Thông tin)
        else if (s40MenuCursor == 5) currentMode = 3; // Test DIO & Sóng âm
        refreshActiveScreen();
      } else if (keyIndex == 6) { // EXIT -> Về Màn Hình Chờ (Mode 0)
        currentMode = 0;
        refreshActiveScreen();
      }
    } else if (currentMode == 5) {
      // ĐANG Ở APP HÌNH NỀN (Mode 5)
      if (keyIndex == 1) { // UP
        s40WallpaperCursor = (s40WallpaperCursor + 9) % 10;
        drawWallpaperAppScreen();
      } else if (keyIndex == 2) { // DOWN
        s40WallpaperCursor = (s40WallpaperCursor + 1) % 10;
        drawWallpaperAppScreen();
      } else if (keyIndex == 3) { // LEFT
        adjustWallpaperAppSetting(s40WallpaperCursor, -1);
      } else if (keyIndex == 4 || keyIndex == 0) { // RIGHT hoặc OK
        adjustWallpaperAppSetting(s40WallpaperCursor, 1);
      } else if (keyIndex == 6) { // EXIT -> Về Menu Symbian S40
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 6) {
      // ĐANG Ở APP CÀI ĐẶT HỆ THỐNG (Mode 6)
      if (keyIndex == 1) { // UP
        s40SettingsCursor = (s40SettingsCursor + 6) % 7;
        drawSettingsAppScreen();
      } else if (keyIndex == 2) { // DOWN
        s40SettingsCursor = (s40SettingsCursor + 1) % 7;
        drawSettingsAppScreen();
      } else if (keyIndex == 3) { // LEFT
        adjustSettingsAppItem(s40SettingsCursor, -1);
      } else if (keyIndex == 4 || keyIndex == 0) { // RIGHT hoặc OK
        adjustSettingsAppItem(s40SettingsCursor, 1);
      } else if (keyIndex == 6) { // EXIT -> Về Menu Symbian S40
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 7) {
      // ĐANG Ở APP THƯ VIỆN ẢNH (Mode 7)
      if (keyIndex == 1 || keyIndex == 3) { // UP / LEFT -> Ảnh trước
        s40GalleryIndex--;
        drawGalleryAppScreen();
      } else if (keyIndex == 2 || keyIndex == 4) { // DOWN / RIGHT -> Ảnh tiếp theo
        s40GalleryIndex++;
        drawGalleryAppScreen();
      } else if (keyIndex == 0) { // OK -> Đặt ảnh đang xem làm Hình nền Màn hình chờ
        String files[20];
        int count = getStoredJpgList(files, nullptr, 20);
        if (count > 0) {
          int idx = ((s40GalleryIndex % count) + count) % count;
          stCfg.bgImage = files[idx];
          stCfg.bgMode = "image";
          saveConfigToFile();
          showSymbianToast("DA DAT LAM HINH NEN!");
          drawGalleryAppScreen();
        }
      } else if (keyIndex == 6) { // EXIT -> Về Menu Symbian S40
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 8) {
      // ĐANG Ở APP ABOUT / THÔNG TIN (Mode 8)
      if (keyIndex == 0 || keyIndex == 1 || keyIndex == 2 || keyIndex == 3 || keyIndex == 4) {
        s40AboutPage = (s40AboutPage + 1) % 2;
        drawAboutAppScreen();
      } else if (keyIndex == 6) { // EXIT -> Về Menu Symbian S40
        currentMode = 4;
        refreshActiveScreen();
      }
    }
  }

  static void initDriver() {
    if (useST7789) {
      tft7789->init(240, 320);
      tft7789->setSPISpeed(40000000);
      tft7789->invertDisplay(inverted);
      tft7789->setRotation(rotation);
      tft = tft7789;
    } else {
      tft9341->begin(40000000);
      tft9341->invertDisplay(inverted);
      tft9341->setRotation(rotation);
      tft = tft9341;
    }
  }

  void init() {
    Serial.println("\n-------------------------------------------------------");
    Serial.println("🖥️ [DISPLAY] KHỞI TẠO MÀN HÌNH ST7789 240x320 & BỘ ĐIỀU KHIỂN SYMBIAN S40");
    Serial.printf("   + Pins: SCK=%d, MOSI=%d, RST=%d, DC=%d, CS=%d, BL(PWM)=%d\n",
                  PIN_TFT_SCK, PIN_TFT_MOSI, PIN_TFT_RST, PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_BL);
    Serial.println("-------------------------------------------------------");

    // Khởi tạo điều khiển độ sáng đèn nền PWM trên chân GPIO 7 (PIN_TFT_BL = 7)
    // Tần số 5kHz, độ phân giải 8-bit (0..255) giúp điều chỉnh độ sáng mượt mà từ 10% -> 100%
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, 0);
    lastUserActivityMs = millis();
    isScreenSleeping = false;
    setBacklightBrightness(screenBrightnessPct);

    if (!spiBus) {
      spiBus = new SPIClass(FSPI);
      spiBus->begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
    }
    if (!tft7789) tft7789 = new Adafruit_ST7789(spiBus, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
    if (!tft9341) tft9341 = new Adafruit_ILI9341(spiBus, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

    initDriver();

    // Đọc cấu hình Màn Hình Chờ đã lưu trên LittleFS
    loadConfigFromFile();

    tft->fillScreen(C_BLACK);
    currentMode = 0;
    refreshActiveScreen();

    Serial.println("✅ Đã hiển thị Màn Hình Chờ (Standby Screen) lên màn hình ST7789!");
    printHelp();
  }

  void updateLiveWeather(float tempC, float humPct) {
    float diffT = fabs(stCfg.temp - tempC);
    int newHum = (int)roundf(humPct);
    int diffH = abs(stCfg.humidity - newHum);
    stCfg.temp = tempC;
    stCfg.humidity = newHum;

    // Nếu đang ở Màn Hình Chờ (Mode 0) và nhiệt/ẩm thay đổi đáng kể (>0.2 độ hoặc >1%) thì vẽ lại phần thông tin
    if (tft && !isScreenSleeping && currentMode == 0 && (diffT >= 0.2f || diffH >= 1)) {
      drawStandbyScreenFull();
    }
  }

  void cycleScreenMode() {
    currentMode = (currentMode + 1) % 4; // Luân chuyển: 0 (Standby) -> 1 (Emoji) -> 2 (PC HUD) -> 3 (Diag/Oscilloscope)
    refreshActiveScreen();
  }

  void cycleHudStyle(int dir) {
    int next = ((int)hudStyle + dir + 4) % 4;
    hudStyle = (uint8_t)next;
    if (currentMode == 2) {
      drawPcHudScreen(true);
    }
  }

  void reloadStandbyConfig() {
    loadConfigFromFile();
    if (currentMode == 0) {
      drawStandbyScreenFull();
    }
  }

  void setScreenMode(const String& mode) {
    uint8_t newMode = currentMode;
    if (mode == "standby") newMode = 0;
    else if (mode == "emoji") newMode = 1;
    else if (mode == "pchud" || mode == "pc_hud") newMode = 2;
    else if (mode == "diag") newMode = 3;
    else if (mode == "symbian" || mode == "menu") newMode = 4;

    if (newMode != currentMode) {
      currentMode = newMode;
      refreshActiveScreen();
    } else if (currentMode == 0) {
      drawStandbyScreenFull();
    } else if (currentMode == 3) {
      drawHardwareDiagScreen(true);
    }
  }

  void setEmojiState(const String& state, const String& subtitle) {
    uint8_t newState = eyeState;
    if (state == "idle") newState = 0;
    else if (state == "blink") newState = 1;
    else if (state == "happy") newState = 2;
    else if (state == "left") newState = 3;
    else if (state == "right") newState = 4;
    else if (state == "up") newState = 5;
    else if (state == "down") newState = 6;
    else if (state == "angry") newState = 7;
    else if (state == "confused") newState = 8;
    else if (state == "wink-left") newState = 9;
    else if (state == "wink-right") newState = 10;
    else if (state == "sleepy") newState = 11;

    bool stateChanged = (newState != eyeState);
    eyeState = newState;
    lastExternalEmojiSync = millis();
    isAutoBlinkActive = false;

    bool subChanged = false;
    if (subtitle.length() > 0 && subtitle != customSubtitle) {
      customSubtitle = subtitle;
      subChanged = true;
    }

    if (currentMode == 1 && tft) {
      if (stateChanged) {
        drawXiaoZhiEyesBox(8, 28, tft->width() - 16, 186, eyeState, 2);
      }
      if (subChanged) {
        drawEmojiSubtitleBox();
      }
    }
  }

  void setHudStyle(const String& style) {
    uint8_t newStyle = hudStyle;
    if (style == "bars") newStyle = 0;
    else if (style == "gauges") newStyle = 1;
    else if (style == "graph") newStyle = 2;
    else if (style == "matrix") newStyle = 3;

    if (newStyle != hudStyle) {
      hudStyle = newStyle;
      if (currentMode == 2) {
        drawPcHudScreen(true);
      }
    } else if (currentMode == 2) {
      drawPcHudScreen(true);
    }
  }

  String getScreenModeName() {
    if (currentMode == 1) return "emoji";
    if (currentMode == 2) return "pchud";
    if (currentMode == 3) return "diag";
    if (currentMode >= 4) return "symbian";
    return "standby";
  }

  String getSubtitle() {
    return customSubtitle;
  }

  String getHudStyleName() {
    const char* styles[] = { "bars", "gauges", "graph", "matrix" };
    return styles[hudStyle % 4];
  }

  void loop() {
    if (!tft) return;
    unsigned long now = millis();

    // 1. Kiểm tra Thời gian sáng màn hình (Screen Timeout trên chân đèn nền GPIO 7)
    uint16_t timeoutSec = TIMEOUT_SECONDS_LIST[screenTimeoutIdx % 6];
    if (timeoutSec > 0 && !isScreenSleeping) {
      if (now - lastUserActivityMs >= (unsigned long)timeoutSec * 1000UL) {
        isScreenSleeping = true;
        ledcWrite(0, 0); // Tắt đèn nền GPIO 7
        Serial.printf("🌙 [BACKLIGHT GPIO7] Đã tắt đèn nền màn hình sau %u giây không thao tác.\n", (unsigned)timeoutSec);
      }
    }
    if (isScreenSleeping) {
      return; // Đang tắt màn hình tiết kiệm điện -> Không cần vẽ lại SPI
    }

    // 2. Tự động kích hoạt đồng bộ giờ chuẩn NTP (GMT+7) ngay khi có kết nối Wi-Fi
    bool wifiNow = (WiFi.status() == WL_CONNECTED);
    if (wifiNow && !ntpConfigured) {
      configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
      ntpConfigured = true;
      Serial.println("🕒 [NTP] Đã kích hoạt đồng bộ thời gian thực GMT+7 (pool.ntp.org)");
    }

    // Nếu trạng thái kết nối Wi-Fi vừa thay đổi -> Cập nhật lại thanh trạng thái và IP trên màn hình
    if (wifiNow != lastWifiState) {
      lastWifiState = wifiNow;
      refreshActiveScreen();
    }

    // 3. Nhịp cập nhật mỗi 1 giây (Đồng hồ Màn Hình Chờ & Telemetry PC HUD)
    if (now - lastSecondTick >= 1000) {
      lastSecondTick = now;
      if (currentMode == 0) {
        updateStandbyClockDigits(false);
      } else if (currentMode == 2) {
        drawPcHudScreen(false);
      }
    }

    // 4. Hiệu ứng nháy mắt Emoji (Mode 1) hoặc Máy Hiện Sóng (Mode 3)
    if (currentMode == 1 && (now - lastExternalEmojiSync > 5500)) {
      if (!isAutoBlinkActive && (now - lastAnimTick >= 4500)) {
        lastAnimTick = now;
        if (eyeState != 1 && eyeState != 8) {
          isAutoBlinkActive = true;
          drawXiaoZhiEyesBox(8, 28, tft->width() - 16, 186, 1, 2); // Chớp mắt '-'
        }
      } else if (isAutoBlinkActive && (now - lastAnimTick >= 140)) {
        isAutoBlinkActive = false;
        drawXiaoZhiEyesBox(8, 28, tft->width() - 16, 186, eyeState, 2); // Trở lại đúng biểu cảm hiện tại
      }
    } else if (currentMode == 3 && (now - lastAnimTick >= 65)) {
      // Ở Chế độ 4 (Hardware & Audio Oscilloscope): Cập nhật sóng âm INMP441 & SHT31 ở tốc độ ~15 FPS
      lastAnimTick = now;
      drawHardwareDiagScreen(false);
    }
  }

  void printHelp() {
    Serial.println("================= ĐIỀU KHIỂN MÀN HÌNH ST7789 =================");
    Serial.println("  [1] : Chế độ MÀN HÌNH CHỜ (Standby Clock + Thời tiết + NTP)");
    Serial.println("  [2] : Chế độ BIỂU CẢM XIAOZHI AI (70% Mắt + 30% Phụ đề)");
    Serial.println("  [3] : Chế độ PC STATUS HUD (Thông số PC + Cảnh báo Offline)");
    Serial.println("  [s] : Đổi phong cách PC HUD (Thanh Ngang -> Đồng Hồ -> Sóng -> Ma Trận)");
    Serial.println("  [4] : Chế độ BẢNG DIAGNOSTIC SƠ ĐỒ CHÂN & SÓNG ÂM INMP441");
    Serial.println("  [5] : Mở BỘ ĐIỀU KHIỂN SYMBIAN S40 MENU (Hình nền/Cài đặt/Thông số/Thư viện/About)");
    Serial.println("  [i] : Đảo ngược màu (Invert Display ON/OFF)");
    Serial.println("  [x] : Xoay màn hình 90 độ (Rotation 0..3)");
    Serial.println("==============================================================");
  }

  bool handleSerial(char cmd) {
    lastUserActivityMs = millis();
    if (isScreenSleeping) {
      isScreenSleeping = false;
      setBacklightBrightness(screenBrightnessPct);
    }
    switch (cmd) {
      case '1':
        currentMode = 0;
        Serial.println("⏰ Chuyển sang Chế độ 1: MÀN HÌNH CHỜ (Standby Screen)");
        refreshActiveScreen();
        return true;

      case '2':
        currentMode = 1;
        Serial.println("😊 Chuyển sang Chế độ 2: BIỂU CẢM XIAOZHI AI");
        refreshActiveScreen();
        return true;

      case '3':
        currentMode = 2;
        Serial.println("📊 Chuyển sang Chế độ 3: PC STATUS HUD");
        refreshActiveScreen();
        return true;

      case 's':
      case 'S':
        hudStyle = (hudStyle + 1) % 4;
        currentMode = 2;
        Serial.printf("📊 Đổi phong cách PC HUD sang Style #%d\n", hudStyle);
        refreshActiveScreen();
        return true;

      case '4':
        currentMode = 3;
        Serial.println("🔌 Chuyển sang Chế độ 4: BẢNG DIAGNOSTIC PHẦN CỨNG");
        refreshActiveScreen();
        return true;

      case '5':
      case 'm':
      case 'M':
        currentMode = 4;
        Serial.println("📱 Mở Giao diện Bộ Điều Khiển SYMBIAN S40 MENU");
        refreshActiveScreen();
        return true;

      case 'i':
      case 'I':
        inverted = !inverted;
        Serial.printf("🎨 Đảo ngược màu màn hình (invertDisplay): %s\n", inverted ? "BẬT" : "TẮT");
        if (tft) tft->invertDisplay(inverted);
        return true;

      case 'x':
      case 'X':
        rotation = (rotation + 1) % 4;
        Serial.printf("🔄 Xoay hướng màn hình (Rotation): %d\n", rotation);
        if (tft) {
          tft->setRotation(rotation);
          refreshActiveScreen();
        }
        return true;

      case 'd':
      case 'D':
        useST7789 = !useST7789;
        inverted = useST7789;
        Serial.printf("🔀 Chuyển đổi Driver màn hình sang: %s\n", useST7789 ? "ST7789" : "ILI9341");
        initDriver();
        refreshActiveScreen();
        return true;

      default:
        return false;
    }
  }

} // namespace TestDisplay
