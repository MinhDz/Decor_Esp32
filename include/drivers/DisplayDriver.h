#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ILI9341.h>
#include "Config.h"
#include "SpaceOS_VnGfx.h"

namespace DisplayDriver {

  // Pin definitions (defaults from Config.h)
  static const int PIN_SCK   = PIN_TFT_SCK;  // GPIO 12
  static const int PIN_MOSI  = PIN_TFT_MOSI; // GPIO 11
  static const int PIN_RST   = PIN_TFT_RST;  // GPIO 10
  static const int PIN_DC    = PIN_TFT_DC;   // GPIO 13
  static const int PIN_CS    = PIN_TFT_CS;   // GPIO 14
  static const int PIN_BL    = PIN_TFT_BL;   // GPIO 7 (PWM)
  static const int PIN_TOUCH = PIN_TOUCH_TTP223; // GPIO 20/21

  static const uint32_t SPI_MAX_SPEED_HZ = 80000000; // 80 MHz max hardware SPI on ESP32-S3

  // Backlight & Sleep state
  void initBacklight(uint8_t channel = 0, double freq = 5000, uint8_t resolution = 8);
  void setBrightness(int pct);
  int  getBrightness();
  int& getBrightnessRef();

  bool isSleeping();
  void setSleeping(bool sleeping);
  volatile bool& getSleepingRef();

  bool isWakeUpRequested();
  void clearWakeUpRequest();
  void requestWakeUp();
  volatile bool& getWakeUpRequestedRef();
  void wakeUpScreenFromTask();

  // Hardware SPI & Controller Initialization
  void initHardware(bool useST7789 = true, bool inverted = false, uint8_t rotation = 0);
  void reinitController();

  // Rotation & Inversion & Controller Type
  void setRotation(uint8_t rotation);
  uint8_t getRotation();
  uint8_t& getRotationRef();

  void setInverted(bool inverted);
  bool isInverted();
  bool& getInvertedRef();

  bool isUseST7789();
  void setUseST7789(bool use);
  bool& getUseST7789Ref();

  // Controller Instances
  Adafruit_SPITFT* getTft();
  Adafruit_SPITFT*& getTftRef();
  SpaceOS::VnST7789* getST7789();
  SpaceOS::VnST7789*& getST7789Ref();
  SpaceOS::VnILI9341* getILI9341();
  SpaceOS::VnILI9341*& getILI9341Ref();
  SPIClass* getSpiBus();
  SPIClass*& getSpiBusRef();

  // ============================================================================
  // LỚP CANVAS 16-BIT RGB565 TOÀN KHUNG HÌNH (240x320) NẰM TRÊN BỘ NHỚ PSRAM
  // Hỗ trợ Double-Buffering 2 tầng, memcpy dòng cực nhanh & SPI 80MHz Non-blocking
  // ============================================================================
  class PsramCanvas16 : public Adafruit_GFX {
  public:
    uint16_t* buffer;
    PsramCanvas16(uint16_t w, uint16_t h) : Adafruit_GFX(w, h), buffer(nullptr) {
      size_t bytes = (size_t)w * h * sizeof(uint16_t);
      if (psramFound()) {
        buffer = (uint16_t*)ps_malloc(bytes);
      }
      if (!buffer) {
        buffer = (uint16_t*)malloc(bytes);
      }
      if (buffer) {
        memset(buffer, 0, bytes);
      }
    }

    virtual ~PsramCanvas16() {
      if (buffer) free(buffer);
      buffer = nullptr;
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
      if (buffer && (x >= 0) && (x < _width) && (y >= 0) && (y < _height)) {
        buffer[y * _width + x] = color;
      }
    }

    void fillScreen(uint16_t color) override {
      if (!buffer) return;
      int count = _width * _height;
      for (int i = 0; i < count; i++) {
        buffer[i] = color;
      }
    }

    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
      if (!buffer || y < 0 || y >= _height) return;
      if (x < 0) { w += x; x = 0; }
      if (x + w > _width) w = _width - x;
      if (w <= 0) return;
      uint16_t* p = &buffer[y * _width + x];
      for (int i = 0; i < w; i++) p[i] = color;
    }

    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
      if (!buffer || x < 0 || x >= _width) return;
      if (y < 0) { h += y; y = 0; }
      if (y + h > _height) h = _height - y;
      if (h <= 0) return;
      uint16_t* p = &buffer[y * _width + x];
      for (int i = 0; i < h; i++) {
        *p = color;
        p += _width;
      }
    }

    void drawRGBBitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h) {
      if (!buffer || !bitmap) return;
      int16_t x2 = x + w - 1;
      int16_t y2 = y + h - 1;
      if (x >= _width || y >= _height || x2 < 0 || y2 < 0) return;

      int16_t bx1 = 0, by1 = 0, saveW = w;
      if (x < 0) { w += x; bx1 = -x; x = 0; }
      if (y < 0) { h += y; by1 = -y; y = 0; }
      if (x2 >= _width)  w = _width - x;
      if (y2 >= _height) h = _height - y;
      if (w <= 0 || h <= 0) return;

      const uint16_t* src = bitmap + by1 * saveW + bx1;
      for (int row = 0; row < h; row++) {
        uint16_t* dst = &buffer[(y + row) * _width + x];
        memcpy(dst, src, w * sizeof(uint16_t));
        src += saveW;
      }
    }

    void drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
      drawRGBBitmap(x, y, (const uint16_t*)bitmap, w, h);
    }

    void invertDisplay(bool i);

    uint16_t* getBuffer() const { return buffer; }
  };

  using VnPsramCanvas16 = SpaceOS::VnGfxAdapter<PsramCanvas16>;

} // namespace DisplayDriver

