#include "drivers/TouchDriver.h"
#include "drivers/DisplayDriver.h"
#include "Config.h"
#include <algorithm>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "esp32-hal-spi.h"
#include "esp32-hal-matrix.h"
#include "soc/gpio_sig_map.h"

namespace TouchDriver {

  static SPIClass* s_spi = nullptr;
  static int8_t    s_csPin = PIN_TOUCH_SCREEN_CS;

  // Dải giá trị hiệu chuẩn mặc định cho màn hình Feiyang 2.4" ST7789 240x320
  // Màn hình dọc hiển thị từ phía dưới lên hàng chân: InvertX = true, InvertY = true
  static int16_t   s_minX = 250;
  static int16_t   s_maxX = 3850;
  static int16_t   s_minY = 250;
  static int16_t   s_maxY = 3850;
  static bool      s_swapXY = false;
  static bool      s_invertX = true;
  static bool      s_invertY = true;
  static bool      s_debugLog = true; // Bật để chẩn đoán cảm ứng qua Serial monitor

  static TouchPoint     s_currentPoint = { 0, 0, 0, 0, 0, false, 0, 0, 0 };
  static TouchEventType s_queuedEvent  = TOUCH_NONE;
  static TouchPoint     s_queuedPoint  = { 0, 0, 0, 0, 0, false, 0, 0, 0 };
  static bool           s_hasEvent     = false;

  static bool           s_wasTouched   = false;
  static unsigned long  s_touchDownMs  = 0;
  static int16_t        s_touchDownX   = 0;
  static int16_t        s_touchDownY   = 0;
  static unsigned long  s_lastScanMs   = 0;
  static unsigned long  s_lastLogMs    = 0;
  static uint8_t        s_releaseDebounceCount = 0;

  static int16_t besttwoavg(int16_t x, int16_t y, int16_t z) {
    int16_t da = abs(x - y);
    int16_t db = abs(x - z);
    int16_t dc = abs(y - z);
    if (da <= db && da <= dc) return (x + y) >> 1;
    if (db <= da && db <= dc) return (x + z) >> 1;
    return (y + z) >> 1;
  }

  // Đọc 12-bit ADC từ XPT2046 bằng cách truyền lệnh 8-bit và đọc 16-bit clock (tránh sai lệch endianness)
  static inline uint16_t xptRead12(uint8_t cmd) {
    s_spi->transfer(cmd);
    uint8_t msb = s_spi->transfer(0x00);
    uint8_t lsb = s_spi->transfer(0x00);
    return (((uint16_t)msb << 8) | lsb) >> 3;
  }

  static bool readRawTouch(int16_t& outX, int16_t& outY, int16_t& outZ) {
    if (!s_spi) return false;

    // Tốc độ 2.0 MHz chuẩn an toàn cao nhất cho bộ điều khiển XPT2046
    s_spi->beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(s_csPin, LOW);

    // 1. Đo áp lực tiếp xúc Z1 & Z2
    uint16_t z1 = xptRead12(0xB1 /* CMD_Z1 */);
    uint16_t z2 = xptRead12(0xC1 /* CMD_Z2 */);
    int z = (int)z1 + 4095 - (int)z2;
    outZ = z;

    // Ngưỡng chạm ngón tay: Hysteresis thông minh
    // - Khi chưa chạm: cần Z >= 200 (chặn đứng 100% chạm ảo do hở mạch)
    // - Khi ĐANG CHẠM / ĐANG VUỐT: hạ ngưỡng xuống Z >= 40 để giữ nét vuốt và nhấn giữ Push-to-Talk ổn định liên tục
    int minZ = s_wasTouched ? 40 : 200;
    if (z < minZ || z > 4050) {
      s_spi->transfer(0xD0 /* Tắt nguồn ADC XPT2046 */);
      s_spi->transfer(0x00);
      digitalWrite(s_csPin, HIGH);
      s_spi->endTransaction();

      s_spi->beginTransaction(SPISettings(DisplayDriver::SPI_MAX_SPEED_HZ, MSBFIRST, SPI_MODE0));
      s_spi->endTransaction();
      return false;
    }

    // 2. KHI CÓ NGÓN TAY CHẠM: Đo tọa độ X & Y (lấy 3 mẫu để lọc nhiễu)
    // - Lệnh 0x91: Channel 001 (Đo trên trục ngắn 240px của LCD ST7789 Portrait)
    // - Lệnh 0xD1: Channel 101 (Đo trên trục dài 320px của LCD ST7789 Portrait)
    int16_t sample91[3];
    int16_t sampleD1[3];

    for (int i = 0; i < 3; i++) {
      sample91[i] = xptRead12(0x91);
      sampleD1[i] = xptRead12(0xD1);
    }

    s_spi->transfer(0xD0 /* Power down */);
    s_spi->transfer(0x00);
    digitalWrite(s_csPin, HIGH);
    s_spi->endTransaction();

    // Khôi phục ngay xung nhịp 40MHz cho ST7789!
    s_spi->beginTransaction(SPISettings(DisplayDriver::SPI_MAX_SPEED_HZ, MSBFIRST, SPI_MODE0));
    s_spi->endTransaction();

    // Lọc loại bỏ trường hợp đường dây tín hiệu trôi nổi bất thường (jitter quá lớn giữa 2 mẫu liên tiếp)
    int maxJitter = s_wasTouched ? 220 : 130;
    if (abs(sample91[0] - sample91[1]) > maxJitter || abs(sampleD1[0] - sampleD1[1]) > maxJitter) {
      return false;
    }

    // Lọc trung vị 2 mẫu gần nhau nhất
    int16_t raw91 = besttwoavg(sample91[0], sample91[1], sample91[2]);
    int16_t rawD1 = besttwoavg(sampleD1[0], sampleD1[1], sampleD1[2]);

    // Kiểm tra mẫu nằm trong dải làm việc hợp lý của ADC 12-bit
    if (rawD1 < 60 || rawD1 > 4060 || raw91 < 60 || raw91 > 4060) {
      return false;
    }

    // Gán chuẩn xác trục màn hình Portrait 240x320:
    // - outX: trục ngang (0..239) đo từ raw91
    // - outY: trục dọc (0..319) đo từ rawD1
    outX = raw91;
    outY = rawD1;

    if (s_debugLog) {
      unsigned long now = millis();
      static unsigned long s_lastDiagMs = 0;
      if (now - s_lastDiagMs >= 80) {
        s_lastDiagMs = now;
        Serial.printf("👆 [TOUCH LIVE] CHẠM! Z(Lực):%4d | RawX(91):%4d | RawY(D1):%4d\n",
                      z, raw91, rawD1);
      }
    }

    return true;
  }

  void loadConfig();

  void init(SPIClass* spiBus, int8_t csPin) {
    s_spi = spiBus;
    s_csPin = csPin;

    pinMode(s_csPin, OUTPUT);
    digitalWrite(s_csPin, HIGH);

    // Kích hoạt đường truyền MISO riêng biệt (GPIO 19) cho FSPI bus trong ma trận tín hiệu ESP32-S3
    pinMode(PIN_TOUCH_SCREEN_MISO, INPUT_PULLUP);
    pinMatrixInAttach(PIN_TOUCH_SCREEN_MISO, FSPIQ_IN_IDX, false);

    int misoBefore = digitalRead(PIN_TOUCH_SCREEN_MISO);

    // Kiểm tra phản hồi chân CS cảm ứng (XPT2046)
    digitalWrite(s_csPin, LOW);
    delayMicroseconds(10);
    int misoCsLow = digitalRead(PIN_TOUCH_SCREEN_MISO);
    digitalWrite(s_csPin, HIGH);

    loadConfig();

    Serial.println("\n-------------------------------------------------------");
    Serial.println("👆 [TOUCH] KHỞI TẠO BỘ ĐIỀU KHIỂN CẢM ỨNG XPT2046 (SPI)");
    Serial.printf ("   + T_CS Pin : GPIO %d | T_DO (MISO): GPIO %d (FSPIQ_IN_IDX=%d)\n", s_csPin, PIN_TOUCH_SCREEN_MISO, FSPIQ_IN_IDX);
    Serial.printf ("   + T_CLK    : GPIO %d | T_DIN (MOSI): GPIO %d @ 1.0MHz (Ổn định)\n", PIN_TFT_SCK, PIN_TFT_MOSI);
    Serial.printf ("   + MISO Diagnostic Probe: CS_HIGH=%d | CS_LOW=%d\n", misoBefore, misoCsLow);
    Serial.printf ("   + Cấu hình : SwapXY=%d | InvX=%d | InvY=%d | X[%d..%d] | Y[%d..%d]\n",
                   s_swapXY, s_invertX, s_invertY, s_minX, s_maxX, s_minY, s_maxY);
    Serial.println("-------------------------------------------------------");
  }

  void loop() {
    unsigned long now = millis();
    // Cơ chế Adaptive Polling thông minh:
    // - Khi đang chạm (s_wasTouched == true): quét mỗi 16ms (~60Hz) để vuốt / vẽ / chạm cực kỳ nhạy và mượt mà
    // - Khi không chạm (idle): giãn cách 25ms (40Hz) để trả 99.9% bus SPI và thời gian CPU cho ST7789 & WebControl
    unsigned long interval = s_wasTouched ? 16 : 25;
    if (now - s_lastScanMs < interval) return;
    s_lastScanMs = now;

    int16_t rx = 0, ry = 0, rz = 0;
    bool touched = readRawTouch(rx, ry, rz);

    if (touched) {
      s_releaseDebounceCount = 0;
      // Chuẩn XPT2046 cho màn hình Portrait 240x320:
      // rx là raw91 (trục ngang), ry là rawD1 (trục dọc)
      int16_t rawX = rx;
      int16_t rawY = ry;
      if (s_swapXY) {
        std::swap(rawX, rawY);
      }

      int16_t px = map(rawX, s_minX, s_maxX, 0, 239);
      int16_t py = map(rawY, s_minY, s_maxY, 0, 319);

      if (s_invertX) px = 239 - px;
      if (s_invertY) py = 319 - py;

      px = constrain(px, 0, 239);
      py = constrain(py, 0, 319);

      s_currentPoint.x = px;
      s_currentPoint.y = py;
      s_currentPoint.rawX = rx;
      s_currentPoint.rawY = ry;
      s_currentPoint.pressure = rz;
      s_currentPoint.isTouched = true;

      if (!s_wasTouched) {
        // Sự kiện BẮT ĐẦU CHẠM (TOUCH_DOWN)
        s_wasTouched = true;
        s_touchDownMs = now;
        s_touchDownX = px;
        s_touchDownY = py;

        s_currentPoint.startX = px;
        s_currentPoint.startY = py;
        s_currentPoint.durationMs = 0;

        s_queuedEvent = TOUCH_DOWN;
        s_queuedPoint = s_currentPoint;
        s_hasEvent = true;

        if (s_debugLog) {
          Serial.printf("👆 [TOUCH DOWN] Raw(X:%4d, Y:%4d, Z:%4d) -> Screen(x:%3d, y:%3d)\n",
                        rx, ry, rz, px, py);
        }
      } else {
        s_currentPoint.startX = s_touchDownX;
        s_currentPoint.startY = s_touchDownY;
        s_currentPoint.durationMs = now - s_touchDownMs;

        // Đang di chuyển ngón tay (TOUCH_MOVE)
        if (abs(px - s_touchDownX) > 4 || abs(py - s_touchDownY) > 4) {
          s_queuedEvent = TOUCH_MOVE;
          s_queuedPoint = s_currentPoint;
          s_hasEvent = true;
        }

        if (s_debugLog && (now - s_lastLogMs >= 200)) {
          s_lastLogMs = now;
          Serial.printf("👉 [TOUCH MOVE] Raw(X:%4d, Y:%4d, Z:%4d) -> Screen(x:%3d, y:%3d)\n",
                        rx, ry, rz, px, py);
        }
      }
    } else {
      if (s_wasTouched) {
        s_releaseDebounceCount++;
        // Yêu cầu ít nhất 4 chu kỳ quét liên tiếp (~64ms) không có tín hiệu chạm mới xác nhận nhấc tay thật sự.
        // Cơ chế này loại bỏ 100% hiện tượng sụt áp lực tức thời trên màng cảm ứng điện trở làm ngắt Push-to-Talk!
        if (s_releaseDebounceCount < 4) {
          return;
        }
        s_releaseDebounceCount = 0;

        // Sự kiện NHẤC TAY (TOUCH_UP)
        s_wasTouched = false;
        s_currentPoint.isTouched = false;

        unsigned long holdDuration = now - s_touchDownMs;
        int dx = s_currentPoint.x - s_touchDownX;
        int dy = s_currentPoint.y - s_touchDownY;
        int distMove = abs(dx) + abs(dy);

        s_currentPoint.startX = s_touchDownX;
        s_currentPoint.startY = s_touchDownY;
        s_currentPoint.durationMs = holdDuration;

        // 1. Nhận diện thao tác VUỐT (SWIPE GESTURE: di chuyển >= 35px trong thời gian <= 750ms)
        if (holdDuration <= 750 && distMove >= 35) {
          if (abs(dy) >= abs(dx)) {
            // Vuốt theo trục dọc
            s_queuedEvent = (dy < 0) ? TOUCH_SWIPE_UP : TOUCH_SWIPE_DOWN;
            if (s_debugLog) {
              Serial.printf("⚡ [TOUCH SWIPE] Vuốt %s (dy=%d, dx=%d, time=%lums)\n",
                            (dy < 0) ? "LÊN (UP)" : "XUỐNG (DOWN)", dy, dx, holdDuration);
            }
          } else {
            // Vuốt theo trục ngang
            s_queuedEvent = (dx < 0) ? TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT;
            if (s_debugLog) {
              Serial.printf("⚡ [TOUCH SWIPE] Vuốt %s (dx=%d, dy=%d, time=%lums)\n",
                            (dx < 0) ? "TRÁI (LEFT)" : "PHẢI (RIGHT)", dx, dy, holdDuration);
            }
          }
          s_queuedPoint = s_currentPoint;
          s_hasEvent = true;
        } else if (distMove <= 45) {
          // Lọc loại bỏ hoàn toàn nhiễu xung ngắn (glitch/noise cực ngắn < 50ms)
          if (holdDuration < 50) {
            if (s_debugLog) {
              Serial.printf("🛡️ [TOUCH FILTER] Bỏ qua nhiễu chạm tức thời: hold=%lums, dist=%dpx\n", holdDuration, distMove);
            }
            return;
          }

          // 2. Chạm dứt khoát của người thật -> CLICK (hoặc LONG PRESS nếu giữ lâu > 600ms)
          if (holdDuration >= 600) {
            s_queuedEvent = TOUCH_LONG_PRESS;
            s_currentPoint.x = s_touchDownX;
            s_currentPoint.y = s_touchDownY;
            if (s_debugLog) {
              Serial.printf("⏳ [TOUCH LONG PRESS] Tại (x:%3d, y:%3d) - Giữ: %lums\n",
                            s_touchDownX, s_touchDownY, holdDuration);
            }
          } else {
            s_queuedEvent = TOUCH_CLICK;
            // Dùng tọa độ lúc bắt đầu ấn xuống (s_touchDownX/Y) để click chính xác mục mong muốn dù ngón tay hơi run lúc nhấc lên
            s_currentPoint.x = s_touchDownX;
            s_currentPoint.y = s_touchDownY;
            if (s_debugLog) {
              Serial.printf("🎯 [TOUCH CLICK] Tại điểm (x:%3d, y:%3d) - Thời gian giữ: %lums\n",
                            s_touchDownX, s_touchDownY, holdDuration);
            }
          }
          s_queuedPoint = s_currentPoint;
          s_hasEvent = true;
        } else {
          s_queuedEvent = TOUCH_UP;
          s_queuedPoint = s_currentPoint;
          s_hasEvent = true;
        }
      }
    }
  }

  bool isTouched() {
    return s_wasTouched;
  }

  TouchPoint getPoint() {
    return s_currentPoint;
  }

  bool getTouchEvent(TouchEventType& outEvt, TouchPoint& outPt) {
    if (!s_hasEvent) return false;
    outEvt = s_queuedEvent;
    outPt = s_queuedPoint;
    s_hasEvent = false;
    s_queuedEvent = TOUCH_NONE;
    return true;
  }

  void setCalibration(int16_t minX, int16_t maxX, int16_t minY, int16_t maxY,
                      bool swapXY, bool invertX, bool invertY) {
    s_minX = minX;
    s_maxX = maxX;
    s_minY = minY;
    s_maxY = maxY;
    s_swapXY = swapXY;
    s_invertX = invertX;
    s_invertY = invertY;
  }

  void getCalibration(int16_t& minX, int16_t& maxX, int16_t& minY, int16_t& maxY,
                      bool& swapXY, bool& invertX, bool& invertY) {
    minX = s_minX;
    maxX = s_maxX;
    minY = s_minY;
    maxY = s_maxY;
    swapXY = s_swapXY;
    invertX = s_invertX;
    invertY = s_invertY;
  }

  void loadConfig() {
    if (!LittleFS.exists("/touch_config.json")) {
      return;
    }
    File f = LittleFS.open("/touch_config.json", "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (!err) {
      int version = doc["version"] | 1;
      if (version >= 2) {
        if (doc["min_x"].is<int>())   s_minX = doc["min_x"];
        if (doc["max_x"].is<int>())   s_maxX = doc["max_x"];
        if (doc["min_y"].is<int>())   s_minY = doc["min_y"];
        if (doc["max_y"].is<int>())   s_maxY = doc["max_y"];
        if (doc["swap_xy"].is<bool>())  s_swapXY = doc["swap_xy"];
        if (doc["invert_x"].is<bool>()) s_invertX = doc["invert_x"];
        if (doc["invert_y"].is<bool>()) s_invertY = doc["invert_y"];
      } else {
        // Tự động nâng cấp cấu hình cũ lên v2 với chuẩn màn hình dọc Feiyang ST7789V (Invert X & Invert Y)
        s_invertX = true;
        s_invertY = true;
        s_swapXY = false;
        saveConfig();
      }
      Serial.printf("👆 [TOUCH] Đã nạp /touch_config.json (v%d): swapXY=%d, invX=%d, invY=%d, X[%d..%d], Y[%d..%d]\n",
                    version, s_swapXY, s_invertX, s_invertY, s_minX, s_maxX, s_minY, s_maxY);
    }
  }

  void saveConfig() {
    File f = LittleFS.open("/touch_config.json", "w");
    if (!f) return;
    JsonDocument doc;
    doc["version"] = 2;
    doc["calibrated"] = true;
    doc["min_x"] = s_minX;
    doc["max_x"] = s_maxX;
    doc["min_y"] = s_minY;
    doc["max_y"] = s_maxY;
    doc["swap_xy"] = s_swapXY;
    doc["invert_x"] = s_invertX;
    doc["invert_y"] = s_invertY;
    serializeJson(doc, f);
    f.close();
    Serial.println("💾 [TOUCH] Đã lưu cấu hình cảm ứng vào /touch_config.json (v2)");
  }

  void setDebugLog(bool enable) {
    s_debugLog = enable;
  }

  bool isDebugLog() {
    return s_debugLog;
  }

}
