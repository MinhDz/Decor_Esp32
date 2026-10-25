#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>
#include "SpaceOS_Themes.h"
#include "SpaceOS_VnGfx.h"

namespace SpaceOS_Settings {

  static const int SETTINGS_COUNT = 15;

  static const uint16_t TIMEOUT_SECONDS_LIST[6] = { 0, 15, 30, 60, 300, 600 };
  static const char* TIMEOUT_LABELS[6] = { "Luôn sáng", "15 giây", "30 giây", "1 phút", "5 phút", "10 phút" };

  // Delegate types for Shell integration
  using DrawChromeFn     = void (*)(const char* title, const char* leftSoft, const char* midSoft, const char* rightSoft);
  using DrawThemedBoxFn  = void (*)(int x, int y, int w, int h, bool sel);
  using ShowToastFn      = void (*)(const String& msg, unsigned long durationMs);
  using ExitCallbackFn   = void (*)();
  using RefreshScreenFn  = void (*)();
  using SetBrightnessFn  = void (*)(int brightnessPct);
  using ApplyThemeFn     = void (*)(int themeIdx);
  using SwitchModeFn     = void (*)(int newMode);
  using GetEmojiNameFn   = String (*)();
  using ResetConfigFn    = void (*)();
  using CycleEmojiStyleFn = void (*)(int dir);
  using GetEmojiStyleNameFn = String (*)();

  struct ShellDelegates {
    DrawChromeFn    drawChrome    = nullptr;
    DrawThemedBoxFn drawThemedBox = nullptr;
    ShowToastFn     showToast     = nullptr;
    ExitCallbackFn  onExit        = nullptr;
    RefreshScreenFn refreshScreen = nullptr;
    SetBrightnessFn setBrightness = nullptr;
    ApplyThemeFn    applyTheme    = nullptr;
    SwitchModeFn    switchMode    = nullptr;
    GetEmojiNameFn  getEmojiName  = nullptr;
    ResetConfigFn   resetConfig   = nullptr;
    CycleEmojiStyleFn cycleEmojiStyle = nullptr;
    GetEmojiStyleNameFn getEmojiStyleName = nullptr;
  };

  void setShellDelegates(const ShellDelegates& delegates);

  // Persistence
  void loadPrefsIfNeeded();
  void savePrefs();
  void init();

  // Settings State accessors
  int& getCursorRef();
  int  getCursor();
  void setCursor(int c);

  int&  getBrightnessPctRef();
  int&  getTimeoutIdxRef();
  int&  getAodClockStyleRef();
  bool& getAlwaysOnDisplayRef();
  bool& getKeyBeepEnabledRef();
  int&  getMicSensitivityRef();

  // Factory reset
  void performFactoryReset();

  // Drawing & Controls
  void draw(Adafruit_SPITFT* tft, bool fullRedraw = true);
  void handleKey(int keyIndex, Adafruit_SPITFT* tft);
  void adjustItem(int row, int dir, Adafruit_SPITFT* tft);

} // namespace SpaceOS_Settings
