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

  void initBacklight(uint8_t channel, double freq, uint8_t resolution) {
    ledcSetup(channel, freq, resolution);
    ledcAttachPin(PIN_BL, channel);
    pinMode(PIN_TOUCH, INPUT_PULLDOWN);
    setBrightness(s_screenBrightnessPct);
  }

  void setBrightness(int pct) {
    s_screenBrightnessPct = constrain(pct, 10, 100);
    if (!s_isScreenSleeping) {
      uint32_t duty = (uint32_t)((s_screenBrightnessPct * 255) / 100);
      ledcWrite(0, duty);
    }
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
    uint32_t duty = (uint32_t)((s_screenBrightnessPct * 255) / 100);
    ledcWrite(0, duty);
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

