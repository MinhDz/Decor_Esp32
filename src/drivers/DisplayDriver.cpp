#include "drivers/DisplayDriver.h"

namespace DisplayDriver {

  static SPIClass* s_spiBus = nullptr;
  static SpaceOS::VnST7789* s_tft7789 = nullptr;
  static SpaceOS::VnILI9341* s_tft9341 = nullptr;
  static Adafruit_SPITFT* s_tft = nullptr;

  static bool s_useST7789 = true;
  static bool s_inverted = false;
  static uint8_t s_rotation = 0;

  static int s_screenBrightnessPct = 100;
  static volatile bool s_isScreenSleeping = false;
  static volatile bool s_wakeUpRequested = false;

  static bool s_isLedcAttached = false;
  static uint8_t s_ledcChannel = 0;
  static double s_ledcFreq = 2000.0;
  static uint8_t s_ledcRes = 8;

  static void applyBacklightHardware() {
    if (s_isScreenSleeping) {
      if (s_isLedcAttached) {
        ledcDetachPin(PIN_BL);
        s_isLedcAttached = false;
      }
      pinMode(PIN_BL, OUTPUT);
      digitalWrite(PIN_BL, LOW);
      return;
    }

    if (s_screenBrightnessPct >= 100) {
      // 100% độ sáng: Chuyển sang DC HIGH phẳng tuyệt đối (3.3V tĩnh).
      // Loại bỏ hoàn toàn tổn hao đóng cắt PWM (switching loss) trên transistor đèn nền,
      // giúp màn hình sáng tối đa phần cứng và triệt tiêu mọi hiện tượng nhấp nháy quang học!
      if (s_isLedcAttached) {
        ledcDetachPin(PIN_BL);
        s_isLedcAttached = false;
      }
      pinMode(PIN_BL, OUTPUT);
      digitalWrite(PIN_BL, HIGH);
    } else {
      // Khi dimming (< 100%): Sử dụng xung PWM tần số 2000 Hz dứt khoát
      if (!s_isLedcAttached) {
        ledcSetup(s_ledcChannel, s_ledcFreq, s_ledcRes);
        ledcAttachPin(PIN_BL, s_ledcChannel);
        s_isLedcAttached = true;
      }
      uint32_t duty = (uint32_t)((s_screenBrightnessPct * 255) / 100);
      ledcWrite(s_ledcChannel, duty);
    }
  }

  void initBacklight(uint8_t channel, double freq, uint8_t resolution) {
    s_ledcChannel = channel;
    s_ledcFreq = freq;
    s_ledcRes = resolution;
    pinMode(PIN_TOUCH, INPUT_PULLDOWN);
    applyBacklightHardware();
  }

  void setBrightness(int pct) {
    s_screenBrightnessPct = constrain(pct, 10, 100);
    applyBacklightHardware();
  }

  int getBrightness() {
    return s_screenBrightnessPct;
  }

  int& getBrightnessRef() {
    return s_screenBrightnessPct;
  }

  bool isSleeping() {
    return s_isScreenSleeping;
  }

  void setSleeping(bool sleeping) {
    s_isScreenSleeping = sleeping;
    applyBacklightHardware();
  }

  volatile bool& getSleepingRef() {
    return s_isScreenSleeping;
  }

  bool isWakeUpRequested() {
    return s_wakeUpRequested;
  }

  void clearWakeUpRequest() {
    s_wakeUpRequested = false;
  }

  void requestWakeUp() {
    s_wakeUpRequested = true;
  }

  volatile bool& getWakeUpRequestedRef() {
    return s_wakeUpRequested;
  }

  void wakeUpScreenFromTask() {
    if (!s_isScreenSleeping) return;
    s_isScreenSleeping = false;
    applyBacklightHardware();
    s_wakeUpRequested = true;
    Serial.println("🔆 [TASK WAKEUP] Đã đánh thức màn hình tức thì từ FreeRTOS KeypadTask!");
  }

  void reinitController() {
    if (s_useST7789 && s_tft7789) {
      s_tft7789->init(240, 320);
      s_tft7789->setSPISpeed(SPI_MAX_SPEED_HZ);
      s_tft7789->invertDisplay(s_inverted);
      s_tft7789->setRotation(s_rotation);
      s_tft = s_tft7789;
    } else if (s_tft9341) {
      s_tft9341->begin(SPI_MAX_SPEED_HZ);
      s_tft9341->invertDisplay(s_inverted);
      s_tft9341->setRotation(s_rotation);
      s_tft = s_tft9341;
    }
  }

  void initHardware(bool useST7789, bool inverted, uint8_t rotation) {
    s_useST7789 = useST7789;
    s_inverted = inverted;
    s_rotation = rotation;

    initBacklight();

    if (!s_spiBus) {
      s_spiBus = new SPIClass(FSPI);
      s_spiBus->begin(PIN_SCK, -1, PIN_MOSI, PIN_CS);
    }
    if (!s_tft7789) s_tft7789 = new SpaceOS::VnST7789(s_spiBus, PIN_CS, PIN_DC, PIN_RST);
    if (!s_tft9341) s_tft9341 = new SpaceOS::VnILI9341(s_spiBus, PIN_CS, PIN_DC, PIN_RST);

    reinitController();
  }

  void setRotation(uint8_t rotation) {
    s_rotation = rotation;
    if (s_tft) s_tft->setRotation(rotation);
  }

  uint8_t getRotation() {
    return s_rotation;
  }

  uint8_t& getRotationRef() {
    return s_rotation;
  }

  void setInverted(bool inverted) {
    s_inverted = inverted;
    if (s_tft) s_tft->invertDisplay(inverted);
  }

  bool isInverted() {
    return s_inverted;
  }

  bool& getInvertedRef() {
    return s_inverted;
  }

  bool isUseST7789() {
    return s_useST7789;
  }

  void setUseST7789(bool use) {
    s_useST7789 = use;
  }

  bool& getUseST7789Ref() {
    return s_useST7789;
  }

  Adafruit_SPITFT* getTft() { return s_tft; }
  Adafruit_SPITFT*& getTftRef() { return s_tft; }

  SpaceOS::VnST7789* getST7789() { return s_tft7789; }
  SpaceOS::VnST7789*& getST7789Ref() { return s_tft7789; }

  SpaceOS::VnILI9341* getILI9341() { return s_tft9341; }
  SpaceOS::VnILI9341*& getILI9341Ref() { return s_tft9341; }

  SPIClass* getSpiBus() { return s_spiBus; }
  SPIClass*& getSpiBusRef() { return s_spiBus; }

  void PsramCanvas16::invertDisplay(bool i) {
    if (s_tft7789) s_tft7789->invertDisplay(i);
    else if (s_tft9341) s_tft9341->invertDisplay(i);
  }

} // namespace DisplayDriver

