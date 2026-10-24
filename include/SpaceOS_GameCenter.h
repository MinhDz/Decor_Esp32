#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SPITFT.h>
#include "SpaceOS_Themes.h"
#include "SpaceOS_VnGfx.h"

namespace SpaceOS_GameCenter {

  // Shell integration callbacks
  using DrawChromeFn    = void (*)(const char* title, const char* leftSoft, const char* midSoft, const char* rightSoft);
  using DrawThemedBoxFn = void (*)(int x, int y, int w, int h, bool sel);
  using ExitCallbackFn  = void (*)();

  void setShellCallbacks(DrawChromeFn chromeFn, DrawThemedBoxFn boxFn, ExitCallbackFn exitFn);

  void init();
  void resetHighScores();

  uint8_t  getActiveGameId();
  void     setActiveGameId(uint8_t id);
  uint8_t& getActiveGameIdRef();

  int  getSnakeBestScore();
  int  getFlappyBestScore();
  int  getTetrisBestScore();

  void draw(Adafruit_SPITFT* tft, SpaceOS::VnCanvas16*& canvas, bool fullRedraw = true);
  void drawPlayfieldOnly(Adafruit_SPITFT* tft, SpaceOS::VnCanvas16*& canvas);
  void loop(Adafruit_SPITFT* tft, SpaceOS::VnCanvas16*& canvas, unsigned long now);
  void handleKey(int keyIndex, Adafruit_SPITFT* tft, SpaceOS::VnCanvas16*& canvas);

} // namespace SpaceOS_GameCenter
