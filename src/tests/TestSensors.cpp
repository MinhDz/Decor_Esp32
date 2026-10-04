#include "tests/TestSensors.h"
#include "tests/TestDisplay.h"
#include "tests/TestAudio.h"
#include "HardwareManager.h"
#include "Config.h"
#include <Wire.h>

namespace TestSensors {

  static bool sht31Online = false;
  static uint8_t sht31Addr = SHT31_I2C_ADDR_1;
  static uint8_t activeSda = PIN_I2C_SDA; // Mặc định GPIO 8
  static uint8_t activeScl = PIN_I2C_SCL; // Mặc định GPIO 9
  static String diagSummary = "Scanning I2C(8,9)...";

  static float currentTempC = 28.5f;
  static float currentHumPct = 65.0f;

  // Máy trạng thái đọc SHT31 không gây trễ
  static bool waitingMeasurement = false;
  static unsigned long cmdSentTime = 0;
  static unsigned long lastMeasureCycle = 0;

  // Trạng thái cảm biến chạm TTP223 (GPIO 20)
  static bool touchModeEnabled = true;
  static bool lastTouchState = false;
  static unsigned long touchDownTime = 0;
  static bool touchLongHandled = false;
  static uint32_t touchCount = 0;

  static uint8_t crc8Sht31(const uint8_t* data, int len) {
    uint8_t crc = 0xFF;
    for (int j = 0; j < len; ++j) {
      crc ^= data[j];
      for (int i = 0; i < 8; ++i) {
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
      }
    }
    return crc;
  }

  // Quét thử 1 cặp chân (sdaPin, sclPin) ở tốc độ 50kHz (an toàn cho dây dài / trở kéo nội)
  static uint8_t scanPairForDevice(uint8_t sdaPin, uint8_t sclPin) {
    Wire.end();
    pinMode(sdaPin, INPUT_PULLUP);
    pinMode(sclPin, INPUT_PULLUP);
    delay(2);
    Wire.begin(sdaPin, sclPin, 50000); // 50kHz giúp xung I2C vuông vắn ngay cả khi dây breadboard dài
    Wire.setTimeOut(25);

    // Ưu tiên kiểm tra 0x44 và 0x45 (SHT30/SHT31/SHT40) trước
    const uint8_t priorityAddrs[] = { 0x44, 0x45, 0x38, 0x40, 0x76, 0x77 };
    for (uint8_t addr : priorityAddrs) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        return addr;
      }
    }

    // Quét toàn dải 0x08..0x77
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        return addr;
      }
    }
    return 0;
  }

  static void scanAndInitSht31(bool verboseLog = false) {
    // 1. Kiểm tra mức logic vật lý trên GPIO 8 và GPIO 9 trước khi khởi tạo I2C
    Wire.end();
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    delay(2);
    int lvl8 = digitalRead(PIN_I2C_SDA);
    int lvl9 = digitalRead(PIN_I2C_SCL);

    if (verboseLog) {
      Serial.printf("🔍 [I2C DIAG] Mức áp tĩnh (Pull-up): GPIO %d = %s | GPIO %d = %s\n",
                    PIN_I2C_SDA, lvl8 ? "HIGH (3.3V OK)" : "LOW (Đang bị kéo GND!)",
                    PIN_I2C_SCL, lvl9 ? "HIGH (3.3V OK)" : "LOW (Đang bị kéo GND!)");
    }

    // 2. Thử chiều chuẩn: SDA = GPIO 8, SCL = GPIO 9
    uint8_t found = scanPairForDevice(PIN_I2C_SDA, PIN_I2C_SCL);
    if (found != 0) {
      activeSda = PIN_I2C_SDA;
      activeScl = PIN_I2C_SCL;
      sht31Addr = found;
      sht31Online = true;
      diagSummary = "OK 0x" + String(found, HEX) + " (SDA=8,SCL=9)";
    } else {
      // 3. Tự động thử đảo chiều: SDA = GPIO 9, SCL = GPIO 8 (Phòng trường hợp cắm ngược dây SDA/SCL!)
      found = scanPairForDevice(PIN_I2C_SCL, PIN_I2C_SDA);
      if (found != 0) {
        activeSda = PIN_I2C_SCL;
        activeScl = PIN_I2C_SDA;
        sht31Addr = found;
        sht31Online = true;
        diagSummary = "OK 0x" + String(found, HEX) + " (SDA=9,SCL=8 DAO DAY)";
        Serial.printf("💡 [SHT31 AUTO-SWAP] Phát hiện dây SDA/SCL đang cắm đảo (SDA=GPIO %d, SCL=GPIO %d) -> Đã tự động đảo chân phần mềm thành công!\n",
                      activeSda, activeScl);
      } else {
        sht31Online = false;
        // Khôi phục về cặp mặc định 8, 9
        activeSda = PIN_I2C_SDA;
        activeScl = PIN_I2C_SCL;
        Wire.begin(activeSda, activeScl, 50000);
        Wire.setTimeOut(25);
        if (!lvl8 || !lvl9) {
          diagSummary = String("CHAM GND: IO8=") + (lvl8 ? "H" : "L") + " IO9=" + (lvl9 ? "H" : "L");
        } else {
          diagSummary = "NO ACK (IO8=H, IO9=H)";
        }
      }
    }

    if (sht31Online) {
      // Gửi lệnh Soft Reset SHT31 (0x30A2)
      Wire.beginTransmission(sht31Addr);
      Wire.write(0x30);
      Wire.write(0xA2);
      Wire.endTransmission();
      delay(5);
    }
  }

  void runFullI2cDiagnostics() {
    Serial.println("\n================ 🔍 QUÉT CHẨN ĐOÁN BUS I2C (SHT31) ================");
    scanAndInitSht31(true);
    if (sht31Online) {
      Serial.printf("✅ ĐÃ TÌM THẤY THIẾT BỊ I2C TẠI 0x%02X (SDA=GPIO %d, SCL=GPIO %d)!\n",
                    sht31Addr, activeSda, activeScl);
      // Đọc thử ngay lập tức 1 mẫu
      Wire.beginTransmission(sht31Addr);
      Wire.write(0x24);
      Wire.write(0x00);
      if (Wire.endTransmission() == 0) {
        delay(25);
        uint8_t buf[6] = {0};
        if (Wire.requestFrom((int)sht31Addr, 6) == 6) {
          for (int i = 0; i < 6; i++) buf[i] = Wire.read();
          uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
          uint16_t rawH = ((uint16_t)buf[3] << 8) | buf[4];
          currentTempC  = -45.0f + 175.0f * ((float)rawT / 65535.0f);
          currentHumPct = 100.0f * ((float)rawH / 65535.0f);
          Serial.printf("🌡️ Kết quả đọc tức thì: Nhiệt độ = %.2f °C | Độ ẩm = %.1f %%\n",
                        currentTempC, currentHumPct);
        }
      }
    } else {
      Serial.println("⚠️ Không nhận được xung ACK từ SHT31 trên cả (SDA=8, SCL=9) lẫn (SDA=9, SCL=8)!");
      Serial.println("   👉 Gợi ý kiểm tra nhanh:");
      Serial.println("      1. Chân GND của SHT31 đã nối chung với GND của ESP32-S3 chưa?");
      Serial.println("      2. Trên board ESP32-S3 DevKitC-1, chân số 8 và 9 nằm ở hàng chân bên trái (dưới GPIO 3, 46).");
      Serial.println("      3. Một số module SHT31 có chân ADDR/AD: nối GND (0x44) hoặc thả nổi/VCC (0x45).");
    }
    Serial.println("===================================================================");
  }

  void init() {
    Serial.println("\n-------------------------------------------------------");
    Serial.println("🌡️ [SENSORS] KHỞI TẠO CẢM BIẾN SHT31 (I2C) & CHẠM TTP223");
    Serial.printf("   + SHT31 I2C : Tự động dò (SDA=8, SCL=9) & (SDA=9, SCL=8)\n");
    Serial.printf("   + TTP223 SIG: GPIO %d (Active HIGH, Phím chạm điện dung)\n", PIN_TOUCH_TTP223);
    Serial.println("-------------------------------------------------------");

    pinMode(PIN_TOUCH_TTP223, INPUT_PULLDOWN);

    scanAndInitSht31(true);
    if (sht31Online) {
      Serial.printf("✅ [SHT31] Đã kết nối thành công tại 0x%02X (SDA=%d, SCL=%d)!\n",
                    sht31Addr, activeSda, activeScl);
    } else {
      Serial.printf("⚠️ [SHT31] Chưa phản hồi (%s). Sẽ tự động quét ngầm định kỳ!\n", diagSummary.c_str());
    }
  }

  void loop() {
    unsigned long now = millis();

    // ================= 1. XỬ LÝ CẢM BIẾN CHẠM TTP223 (GPIO 20) =================
    if (touchModeEnabled) {
      bool touched = (digitalRead(PIN_TOUCH_TTP223) == HIGH);

      if (touched && !lastTouchState) {
        touchDownTime = now;
        touchLongHandled = false;
        touchCount++;
        HardwareManager::setLedColor(0, 220, 255);
        TestAudio::playClickBeep(1568);
        Serial.printf("👆 [TTP223 - GPIO %d] Phát hiện CHẠM (#%lu)!\n", PIN_TOUCH_TTP223, touchCount);
      } else if (touched && lastTouchState) {
        if (!touchLongHandled && (now - touchDownTime >= 900)) {
          touchLongHandled = true;
          HardwareManager::setLedColor(180, 0, 255);
          TestAudio::playTone(1318, 60, 50);
          delay(10);
          TestAudio::playTone(1760, 90, 55);
          Serial.println("🎙️ [TTP223 - GPIO 20] CHẠM GIỮ (>0.9s) -> Đánh thức Trợ lý ảo XiaoZhi AI!");
          TestDisplay::setScreenMode("emoji");
          TestDisplay::setEmojiState("listen", "TTP223 (GPIO 20): Da danh thuc XiaoZhi AI!");
        }
      } else if (!touched && lastTouchState) {
        unsigned long pressDur = now - touchDownTime;
        if (!touchLongHandled && pressDur >= 25) {
          TestDisplay::cycleScreenMode();
          String newMode = TestDisplay::getScreenModeName();
          Serial.printf("✨ [TTP223 - GPIO 20] Chạm ngắn (%lums) -> Đổi màn hình sang: [%s]\n",
                        pressDur, newMode.c_str());
          if (newMode == "emoji") {
            TestDisplay::setEmojiState("happy", "TTP223 Touch (GPIO 20): Xin chao! Minh dang vui ve!");
          }
        }
      }
      lastTouchState = touched;
    }

    // ================= 2. XỬ LÝ CẢM BIẾN NHIỆT/ẨM SHT31 (GPIO 8, 9) =================
    if (!waitingMeasurement) {
      unsigned long interval = sht31Online ? 2500 : 4500;
      if (now - lastMeasureCycle >= interval) {
        lastMeasureCycle = now;
        if (!sht31Online) {
          scanAndInitSht31(false);
          if (sht31Online) {
            Serial.printf("✅ [SHT31] Đã tự động nhận diện cảm biến tại 0x%02X (SDA=%d, SCL=%d)!\n",
                          sht31Addr, activeSda, activeScl);
          }
        }
        if (sht31Online) {
          Wire.beginTransmission(sht31Addr);
          Wire.write(0x24);
          Wire.write(0x00);
          if (Wire.endTransmission() == 0) {
            waitingMeasurement = true;
            cmdSentTime = now;
          } else {
            sht31Online = false;
            diagSummary = "LOST ACK 0x" + String(sht31Addr, HEX);
          }
        }
      }
    } else {
      if (now - cmdSentTime >= 22) {
        waitingMeasurement = false;
        uint8_t buf[6] = {0};
        if (Wire.requestFrom((int)sht31Addr, 6) == 6) {
          for (int i = 0; i < 6; i++) buf[i] = Wire.read();
          if (crc8Sht31(&buf[0], 2) == buf[2] && crc8Sht31(&buf[3], 2) == buf[5]) {
            uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
            uint16_t rawH = ((uint16_t)buf[3] << 8) | buf[4];
            currentTempC  = -45.0f + 175.0f * ((float)rawT / 65535.0f);
            currentHumPct = 100.0f * ((float)rawH / 65535.0f);
            TestDisplay::updateLiveWeather(currentTempC, currentHumPct);
          }
        } else {
          sht31Online = false;
        }
      }
    }
  }

  bool isSht31Connected() { return sht31Online; }
  uint8_t getDetectedAddress() { return sht31Addr; }
  uint8_t getActiveSdaPin() { return activeSda; }
  uint8_t getActiveSclPin() { return activeScl; }
  String getDiagSummary() { return diagSummary; }
  float getTemperatureC() { return currentTempC; }
  float getHumidityPct() { return currentHumPct; }
  bool isTouchPressed() { return lastTouchState; }

  bool handleSerial(char cmd) {
    if (cmd == 'g' || cmd == 'G') {
      touchModeEnabled = !touchModeEnabled;
      Serial.printf("👆 [TTP223] Trạng thái cảm biến chạm GPIO 20: %s\n", touchModeEnabled ? "BẬT" : "TẮT");
      return true;
    }
    if (cmd == 't' || cmd == 'T') {
      runFullI2cDiagnostics();
      // Đồng thời chuyển màn hình ST7789 sang Chế độ 4 (Màn hình Test Module & Sóng Âm) để xem trực tiếp!
      TestDisplay::setScreenMode("diag");
      return true;
    }
    return false;
  }

}

