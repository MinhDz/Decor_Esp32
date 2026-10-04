#include "tests/TestButtons.h"
#include "tests/TestDisplay.h"
#include "Config.h"

namespace TestButtons {

  // Danh sách 7 nút theo đúng thứ tự bảng thiết kế Thang Điện Trở ADC của người dùng:
  // Index: 0=OK (0Ω), 1=UP (1kΩ), 2=DOWN (2.2kΩ), 3=LEFT (4.7kΩ), 4=RIGHT (10kΩ), 5=MENU (20kΩ), 6=EXIT (47kΩ)
  struct AdcButtonSpec {
    const char* name;
    const char* shortCode;
    const char* resistorLabel;
    float idealVoltage;
    int minMv;
    int maxMv;
  };

  static const AdcButtonSpec BUTTON_SPECS[7] = {
    { "OK (Giữa)",    "OK", "0 Ohm (GND)",  0.00f,    0,  175 }, // Chuẩn: < 0.15V (0 - 150mV)
    { "UP (Lên)",     "UP", "1.0 kOhm",     0.30f,  180,  465 }, // Chuẩn: 0.20V - 0.45V (~0.30V)
    { "DOWN (Xuống)", "DN", "2.2 kOhm",     0.60f,  470,  820 }, // Chuẩn: 0.50V - 0.75V (~0.60V)
    { "LEFT (Trái)",  "LF", "4.7 kOhm",     1.05f,  830, 1340 }, // Chuẩn: 0.90V - 1.20V (~1.05V)
    { "RIGHT (Phải)", "RT", "10 kOhm",      1.65f, 1350, 1940 }, // Chuẩn: 1.45V - 1.85V (~1.65V)
    { "MENU",         "MN", "20 kOhm",      2.18f, 1920, 2325 }, // Thực tế 3.27V: ~2.18V (1.92V - 2.32V)
    { "EXIT",         "EX", "47 kOhm",      2.65f, 2330, 3040 }  // Thực tế 3.27V + ADC S3: ~2.36V - 2.90V (2.33V - 3.04V)
  };

  static uint8_t activeAdcPin = PIN_ADC_KEYPAD; // Mặc định GPIO 3 (ADC1_CH2)
  static int lastAdcRaw = 4095;
  static int lastAdcMv  = 3300;
  static int activeBtnIndex = -1;     // -1 = Không bấm (Hở mạch > 3.04V)
  static int candidateBtnIndex = -1;
  static int prevSettledMv = 3300;
  static unsigned long candidateStartMs = 0;
  static bool buttonActionTriggered = false;

  static String lastButtonStr = "NONE (3.30V)";
  static bool liveAdcSerialStream = true; // Mặc định in log khi ở màn hình 4 (DIO/ADC) hoặc khi bấm phím
  static unsigned long lastSerialPrintMs = 0;

  // Đọc trung bình 16 mẫu ADC (loại bỏ cực trị nhiễu xung)
  static void readFilteredAdc(int& outRaw, int& outMv) {
    long sumRaw = 0;
    long sumMv  = 0;
    int minMv = 9999, maxMv = -1;

    for (int i = 0; i < 16; i++) {
      int r = analogRead(activeAdcPin);
      int mv = analogReadMilliVolts(activeAdcPin);
      sumRaw += r;
      sumMv  += mv;
      if (mv < minMv) minMv = mv;
      if (mv > maxMv) maxMv = mv;
    }

    // Trừ đi 1 mẫu lớn nhất và 1 mẫu nhỏ nhất, chia cho 14 để lọc sạch nhiễu gai
    outRaw = (int)(sumRaw / 16);
    outMv  = (int)((sumMv - minMv - maxMv) / 14);

    // Quy đổi nội suy chuẩn từ mV sang thang ADC 12-bit (0..4095 tại 3300mV)
    // giúp giá trị ADC hiển thị khớp tuyệt đối với cột D trong bảng Excel (VD: 1.65V -> 2048)
    int calibratedRaw = (outMv * 4095) / 3300;
    if (calibratedRaw > 4095) calibratedRaw = 4095;
    outRaw = calibratedRaw;
  }

  // Phân loại nút bấm từ điện áp mV theo đúng bảng thiết kế
  static int classifyButtonFromMv(int mv) {
    if (mv >= 3045) {
      return -1; // Không bấm (> 3.045V ~ 3.30V)
    }
    for (int i = 0; i < 7; i++) {
      if (mv >= BUTTON_SPECS[i].minMv && mv <= BUTTON_SPECS[i].maxMv) {
        return i;
      }
    }
    return -1;
  }

  // Xử lý sự kiện khi 1 nút bấm đã ổn định (Settled)
  static void onButtonConfirmed(int idx, int mv, int raw) {
    const AdcButtonSpec& spec = BUTTON_SPECS[idx];
    char buf[48];
    snprintf(buf, sizeof(buf), "%s (%.2fV|%d)", spec.shortCode, mv / 1000.0f, raw);
    lastButtonStr = String(buf);

    Serial.printf("🔘 [ADC KEYPAD GPIO %d] NHẤN: %-13s | Trở: %-11s | Đo: %.2fV (%4d mV) | ADC: %4d / 4095\n",
                  activeAdcPin, spec.name, spec.resistorLabel, mv / 1000.0f, mv, raw);

    // Gửi trực tiếp sự kiện phím bấm (0:OK, 1:UP, 2:DOWN, 3:LEFT, 4:RIGHT, 5:MENU, 6:EXIT)
    // sang bộ điều hướng Hệ Điều Hành Symbian S40 trên màn hình ST7789
    TestDisplay::onKeypadEvent(idx);
  }

  void printAdcTableGuide() {
    Serial.println("\n==================== 🎛️ BẢNG CHUẨN THANG ĐIỆN TRỞ 7 NÚT ADC ====================");
    Serial.printf ("  📌 Chân tín hiệu ADC : GPIO %d (ADC1_CH2) | Trở kéo lên (Pull-up 3.3V): 10 kOhm\n", activeAdcPin);
    Serial.println("  ------------------------------------------------------------------------------");
    Serial.println("  Nút bấm        | Điện trở (R)  | Điện áp lý thuyết | Giá trị ADC | Ngưỡng nhận diện");
    Serial.println("  ---------------+---------------+-------------------+-------------+-----------------");
    Serial.println("  Không bấm      | Hở mạch (∞)   | 3.30 V            | 4000 - 4095 | > 3.00 V");
    Serial.println("  Nút 1: OK      | 0 Ohm (GND)   | 0.00 V            |    0 - 100  | < 0.17 V");
    Serial.println("  Nút 2: UP      | 1.0 kOhm      | 0.30 V            |  350 - 420  | 0.18 V - 0.46 V");
    Serial.println("  Nút 3: DOWN    | 2.2 kOhm      | 0.60 V            |  700 - 800  | 0.47 V - 0.82 V");
    Serial.println("  Nút 4: LEFT    | 4.7 kOhm      | 1.05 V            | 1250 - 1380 | 0.83 V - 1.34 V");
    Serial.println("  Nút 5: RIGHT   | 10 kOhm       | 1.65 V            | 2000 - 2100 | 1.35 V - 1.94 V");
    Serial.println("  Nút 6: MENU    | 20 kOhm       | 2.27 V            | 2750 - 2880 | 1.95 V - 2.48 V");
    Serial.println("  Nút 7: EXIT    | 47 kOhm       | 2.72 V            | 3300 - 3450 | 2.49 V - 2.96 V");
    Serial.println("  ------------------------------------------------------------------------------");
    Serial.println("  💡 Phím tắt Serial: Gõ [b] để xem trực tiếp Điện áp & ADC, gõ [4] để mở màn hình DIO/ADC");
    Serial.println("================================================================================");
  }

  void init() {
    analogReadResolution(12);
    analogSetPinAttenuation(activeAdcPin, ADC_11db); // Đọc toàn dải 0.00V .. 3.30V
    pinMode(activeAdcPin, INPUT);

    readFilteredAdc(lastAdcRaw, lastAdcMv);
    printAdcTableGuide();
  }

  void loop() {
    int raw = 0, mv = 0;
    readFilteredAdc(raw, mv);
    lastAdcRaw = raw;
    lastAdcMv  = mv;

    int detected = classifyButtonFromMv(mv);
    unsigned long now = millis();

    // Kiểm tra độ ổn định điện áp (Settling Filter: chênh lệch giữa 2 lần đọc < 55mV trong 35ms)
    int deltaMv = abs(mv - prevSettledMv);
    prevSettledMv = mv;

    if (detected != candidateBtnIndex || deltaMv > 65) {
      candidateBtnIndex = detected;
      candidateStartMs  = now;
    } else if (now - candidateStartMs >= 35) {
      // Trạng thái nút bấm đã ổn định sau 35ms!
      if (activeBtnIndex != candidateBtnIndex) {
        int prevBtn = activeBtnIndex;
        activeBtnIndex = candidateBtnIndex;

        if (activeBtnIndex >= 0 && !buttonActionTriggered) {
          buttonActionTriggered = true;
          onButtonConfirmed(activeBtnIndex, mv, raw);
        } else if (activeBtnIndex < 0 && prevBtn >= 0) {
          buttonActionTriggered = false;
          Serial.printf("⚪ [ADC KEYPAD GPIO %d] NHẢ NÚT -> Về mức nghỉ: %.2fV (%4d mV | ADC: %4d)\n",
                        activeAdcPin, mv / 1000.0f, mv, raw);
        }
      }
    }

    // Khi đang ở màn hình 4 (Kiểm tra DIO & ADC) hoặc khi đang nhấn giữ nút, in thông số ADC mỗi 300ms
    if (liveAdcSerialStream && (now - lastSerialPrintMs >= 300) &&
        (TestDisplay::getScreenModeName() == "diag" || activeBtnIndex >= 0)) {
      lastSerialPrintMs = now;
      const char* btnName = (activeBtnIndex >= 0) ? BUTTON_SPECS[activeBtnIndex].name : "KHÔNG BẤM (Hở mạch)";
      const char* resName = (activeBtnIndex >= 0) ? BUTTON_SPECS[activeBtnIndex].resistorLabel : "∞ (Pull-up 10k)";
      Serial.printf("[ADC IO%d] Áp: %4.2fV (%4d mV) | ADC(12-bit): %4d | Trở: %-15s | Nút: %s\n",
                    activeAdcPin, mv / 1000.0f, mv, raw, resName, btnName);
    }
  }

  String getLastButtonName() {
    return lastButtonStr;
  }

  int getActiveButtonIndex() {
    return activeBtnIndex;
  }

  float getLastAdcVoltage() {
    return lastAdcMv / 1000.0f;
  }

  int getLastAdcMilliVolts() {
    return lastAdcMv;
  }

  int getLastAdcRaw() {
    return lastAdcRaw;
  }

  uint8_t getAdcPin() {
    return activeAdcPin;
  }

  bool handleSerial(char cmd) {
    if (cmd == 'b' || cmd == 'B') {
      int raw = 0, mv = 0;
      readFilteredAdc(raw, mv);
      int idx = classifyButtonFromMv(mv);
      printAdcTableGuide();
      Serial.printf("🔎 [ĐO TỨC THỜI GPIO %d] Điện áp = %.2f V (%d mV) | Giá trị ADC = %d / 4095 | Nhận diện = %s\n",
                    activeAdcPin, mv / 1000.0f, mv, raw,
                    (idx >= 0) ? BUTTON_SPECS[idx].name : "KHÔNG BẤM (Hở mạch > 3.00V)");
      TestDisplay::setScreenMode("diag");
      return true;
    }
    return false;
  }

}
