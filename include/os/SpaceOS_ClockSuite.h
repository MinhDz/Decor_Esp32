#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>
#include "SpaceOS_Themes.h"
#include "SpaceOS_VnGfx.h"

namespace SpaceOS_ClockSuite {

  // Delegate types for Shell integration
  using DrawChromeFn     = void (*)(const char* title, const char* leftSoft, const char* midSoft, const char* rightSoft);
  using DrawSoftkeysFn   = void (*)(const char* leftSoft, const char* midSoft, const char* rightSoft);
  using ShowToastFn      = void (*)(const String& msg, unsigned long durationMs);
  using ExitCallbackFn   = void (*)();
  using WakeScreenFn     = void (*)();
  using RefreshScreenFn  = void (*)();
  using GetDateTimeFn    = void (*)(int& hr, int& mn, int& sc, int& wd, int& dy, int& mo, int& yr);

  struct ShellDelegates {
    DrawChromeFn    drawChrome    = nullptr;
    DrawSoftkeysFn  drawSoftkeys  = nullptr;
    ShowToastFn     showToast     = nullptr;
    ExitCallbackFn  onExit        = nullptr;
    WakeScreenFn    wakeScreen    = nullptr;
    RefreshScreenFn refreshScreen = nullptr;
    GetDateTimeFn   getDateTime   = nullptr;
  };

  void setShellDelegates(const ShellDelegates& delegates);

  // Lifecycle & Persistence
  void init();
  void resetDefaults(); // Used for Factory Reset
  void loadPrefsIfNeeded();
  void savePrefs();

  // State accessors
  uint8_t  getActiveTab();
  void     setActiveTab(uint8_t tab);
  uint8_t& getActiveTabRef();

  int& getAlarmTuneIdxRef();
  int  getAlarmTuneIdx();
  void setAlarmTuneIdx(int idx);

  bool isPushAlertActive();
  void dismissPushAlert(bool okPressed);
  void triggerPushAlert(uint8_t alertType, uint8_t alarmIdx, const String& title, const String& subTitle, const String& bigDigits);

  bool isTimerRunning();
  uint32_t getTimerPresetSec();
  uint32_t getTimerRemainSec();

  bool isStopwatchRunning();

  // Drawing & Loop
  void draw(Adafruit_SPITFT* tft, bool fullRedraw = true);
  void drawPushAlertModal(Adafruit_SPITFT* tft, bool fullRedraw = true);
  void loop(Adafruit_SPITFT* tft, unsigned long now, bool isScreenSleeping, int currentMode);
  void handleKey(int keyIndex, Adafruit_SPITFT* tft);
  bool handlePushAlertKey(int keyIndex, Adafruit_SPITFT* tft);

} // namespace SpaceOS_ClockSuite

