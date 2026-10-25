#include "SpaceOS_Settings.h"
#include "SpaceOS_Themes.h"
#include "SpaceOS_ClockSuite.h"
#include "SpaceOS_GameCenter.h"
#include "drivers/AudioDriver.h"
#include "drivers/MotorDriver.h"
#include "CobLedController.h"
#include "WebAuth.h"
#include "ImageManager.h"
#include <Preferences.h>

namespace SpaceOS_Settings {

  using SpaceOS::vnStrLen;
  using SpaceOS::vnSubstr;

  static ShellDelegates s_shell;

  void setShellDelegates(const ShellDelegates& delegates) {
    s_shell = delegates;
  }

  static int s40SettingsCursor = 0;
  static int screenBrightnessPct = 100;
  static int screenTimeoutIdx = 2; // 30s
  static int aodClockStyle = 1; // Digital
  static bool alwaysOnDisplayEnabled = true;
  static bool keyBeepEnabled = true;
  static int micSensitivityMode = 1; // Standard

  static bool s40SystemPrefsLoaded = false;
  static int lastDrawnSettingsCursor = -1;
  static int lastDrawnSettingsFirstRow = -1;

  int& getCursorRef() { return s40SettingsCursor; }
  int  getCursor() { return s40SettingsCursor; }
  void setCursor(int c) { s40SettingsCursor = (c % SETTINGS_COUNT + SETTINGS_COUNT) % SETTINGS_COUNT; }

  int&  getBrightnessPctRef() { return screenBrightnessPct; }
  int&  getTimeoutIdxRef() { return screenTimeoutIdx; }
  int&  getAodClockStyleRef() { return aodClockStyle; }
  bool& getAlwaysOnDisplayRef() { return alwaysOnDisplayEnabled; }
  bool& getKeyBeepEnabledRef() { return keyBeepEnabled; }
  int&  getMicSensitivityRef() { return micSensitivityMode; }

  void loadPrefsIfNeeded() {
    if (s40SystemPrefsLoaded) return;
    s40SystemPrefsLoaded = true;
    Preferences p;
    if (p.begin("s40_sys", true)) {
      screenBrightnessPct    = p.getInt("bright", screenBrightnessPct);
      screenTimeoutIdx       = p.getInt("tout", 2) % 6;
      aodClockStyle          = p.getInt("aod_sty", 1) % 4;
      alwaysOnDisplayEnabled = (aodClockStyle > 0);
      keyBeepEnabled         = p.getBool("kbeep", keyBeepEnabled);
      micSensitivityMode     = p.getInt("msens", 1) % 3;
      AudioDriver::setMicSensitivityMode(micSensitivityMode);
      int wmMod              = p.getInt("wm_mod", 0) % 5;
      MotorDriver::setMode(wmMod);
      int edL                = p.getInt("ed_l", MotorDriver::getEdisonLeftMode()) % MotorDriver::getEdisonModeCount();
      int edR                = p.getInt("ed_r", MotorDriver::getEdisonRightMode()) % MotorDriver::getEdisonModeCount();
      MotorDriver::setEdisonLeftMode(edL);
      MotorDriver::setEdisonRightMode(edR);
      bool fl                = p.getBool("fl_en", true);
      int cobMod             = p.getInt("cob_mod", 1);
      CobLeds::init(fl);
      CobLeds::setCurrentMode(cobMod);
      MotorDriver::setFlowingLeds(fl);
      p.end();
    }
  }

  void savePrefs() {
    Preferences p;
    if (p.begin("s40_sys", false)) {
      p.putInt("bright", screenBrightnessPct);
      p.putInt("tout", screenTimeoutIdx);
      p.putInt("aod_sty", aodClockStyle);
      p.putBool("kbeep", keyBeepEnabled);
      p.putInt("msens", micSensitivityMode);
      p.putInt("wm_mod", MotorDriver::getMode());
      p.putInt("ed_l", MotorDriver::getEdisonLeftMode());
      p.putInt("ed_r", MotorDriver::getEdisonRightMode());
      p.putBool("fl_en", CobLeds::isOn());
      p.putInt("cob_mod", CobLeds::getCurrentMode());
      p.end();
    }
  }

  void init() {
    loadPrefsIfNeeded();
  }

  static void drawSingleSettingsRow(Adafruit_SPITFT* tft, int idx, int vis, bool sel) {
    if (!tft || idx < 0 || idx >= SETTINGS_COUNT || vis < 0 || vis >= 7) return;
    int w = tft->width();
    int ry = 30 + vis * 37;

    int spkVol = AudioDriver::getNotifyVolumePct();
    const char* micSensNames[3] = { "Thấp", "Tiêu chuẩn", "Cao" };
    const char* aodStyleLabels[4] = {
      "Tắt (Tắt màn hình)",
      "1. Thường (Kỹ thuật số)",
      "2. Lật số (Retro Flip)",
      "3. Đồng hồ kim (Analog)"
    };

    const char* labels[15] = {
      "1. Phong cách Giao diện",
      "2. Độ sáng màn hình",
      "3. Thời gian tắt màn hình",
      "4. Màn hình khóa (AOD)",
      "5. Âm lượng thông báo",
      "6. Âm báo phím bấm",
      "7. Nhạc chuông báo thức",
      "8. Độ nhạy Micro thu âm",
      "9. Biểu cảm Trợ lý AI",
      "10. Biểu đồ Sóng âm & Cảm biến",
      "11. Cối xay gió L298N (G1)",
      "12. LED Edison Trái (G47)",
      "13. LED Edison Phải (G21)",
      "14. LED Nước Chảy COB (G18/38)",
      "15. Khôi phục Cài đặt Gốc"
    };

    String values[15] = {
      String(curTheme().name),
      String(screenBrightnessPct) + "%",
      String(TIMEOUT_LABELS[screenTimeoutIdx % 6]),
      String(aodStyleLabels[aodClockStyle % 4]),
      String(spkVol) + "%",
      keyBeepEnabled ? "Đang bật" : "Đang tắt",
      String(AudioDriver::getAlarmTuneName(SpaceOS_ClockSuite::getAlarmTuneIdx())),
      String(micSensNames[micSensitivityMode % 3]),
      s_shell.getEmojiName ? s_shell.getEmojiName() : "Sẵn sàng",
      "Bấm OK để mở ->",
      String(MotorDriver::getModeName()),
      String(MotorDriver::getEdisonLeftModeName()),
      String(MotorDriver::getEdisonRightModeName()),
      String(CobLeds::getModeName()),
      "< Bấm OK để đặt lại >"
    };

    tft->fillRect(6, ry, w - 12, 34, C_DARK_BG);
    if (s_shell.drawThemedBox) {
      s_shell.drawThemedBox(6, ry, w - 12, 34, sel);
    } else {
      tft->fillRoundRect(6, ry, w - 12, 34, 6, sel ? C_CARD_SEL : C_CARD_BG);
      tft->drawRoundRect(6, ry, w - 12, 34, 6, sel ? C_NEON_CYAN : C_CARD_BORDER);
    }

    tft->setTextSize(1);
    tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? C_CARD_SEL : C_CARD_BG);
    tft->setCursor(12, ry + 5);
    tft->print(labels[idx]);

    if (idx == 0) {
      // Hiển thị tên Chủ đề + 4 ô màu mẫu (Swatch) bên góc phải
      tft->setTextColor(C_NEON_CYAN, sel ? C_CARD_SEL : C_CARD_BG);
      tft->setCursor(12, ry + 19);
      tft->printf("< %s >", values[0].c_str());
      int sx = w - 48;
      tft->fillRect(sx,      ry + 6, 7, 7, C_DARK_BG);
      tft->drawRect(sx,      ry + 6, 7, 7, C_WHITE);
      tft->fillRect(sx + 9,  ry + 6, 7, 7, C_NEON_CYAN);
      tft->fillRect(sx + 18, ry + 6, 7, 7, C_NEON_GREEN);
      tft->fillRect(sx + 27, ry + 6, 7, 7, C_NEON_PINK);
    } else if (idx == 1 || idx == 4) {
      int pct = (idx == 1) ? screenBrightnessPct : spkVol;
      uint16_t barCol = (idx == 1) ? C_NEON_AMBER : C_NEON_GREEN;
      int bx = 12, by = ry + 19, bw = 136, bh = 10;
      tft->drawRect(bx, by, bw, bh, barCol);
      int fillW = ((bw - 4) * constrain(pct, 0, 100)) / 100;
      if (fillW > 0) tft->fillRect(bx + 2, by + 2, fillW, bh - 4, barCol);
      if (fillW < bw - 4) tft->fillRect(bx + 2 + fillW, by + 2, (bw - 4) - fillW, bh - 4, C_BLACK);

      tft->setTextColor(barCol, sel ? C_CARD_SEL : C_CARD_BG);
      tft->setCursor(156, ry + 19);
      tft->print(values[idx]);
    } else if (idx == 14) {
      tft->setTextColor(sel ? C_YELLOW : C_NEON_PINK, sel ? C_CARD_SEL : C_CARD_BG);
      tft->setCursor(16, ry + 19);
      tft->print(values[14]);
    } else {
      uint16_t valCol = (idx == 3) ? (aodClockStyle > 0 ? C_NEON_GREEN : C_NEON_PINK) : (sel ? C_NEON_GREEN : C_NEON_CYAN);
      tft->setTextColor(valCol, sel ? C_CARD_SEL : C_CARD_BG);
      tft->setCursor(16, ry + 19);
      tft->printf("< %s >", values[idx].c_str());
    }
  }

  void draw(Adafruit_SPITFT* tft, bool fullRedraw) {
    if (!tft) return;
    int w = tft->width();
    if (fullRedraw) {
      tft->fillRect(0, 22, w, tft->height() - 44, C_DARK_BG);
      if (s_shell.drawChrome) {
        s_shell.drawChrome("CÀI ĐẶT HỆ THỐNG", "Đổi (OK)", "< Trái / Phải >", "Quay lại");
      }
      lastDrawnSettingsCursor = -1;
      lastDrawnSettingsFirstRow = -1;
    }

    int firstRow = 0;
    if (s40SettingsCursor >= 7) firstRow = s40SettingsCursor - 6;

    // Tối ưu chống quét màn hình
    if (!fullRedraw && firstRow == lastDrawnSettingsFirstRow && lastDrawnSettingsCursor >= 0 && lastDrawnSettingsCursor != s40SettingsCursor) {
      int oldVis = lastDrawnSettingsCursor - firstRow;
      int newVis = s40SettingsCursor - firstRow;
      if (oldVis >= 0 && oldVis < 7) drawSingleSettingsRow(tft, lastDrawnSettingsCursor, oldVis, false);
      if (newVis >= 0 && newVis < 7) drawSingleSettingsRow(tft, s40SettingsCursor, newVis, true);
      lastDrawnSettingsCursor = s40SettingsCursor;
      return;
    }

    if (!fullRedraw && firstRow == lastDrawnSettingsFirstRow && lastDrawnSettingsCursor == s40SettingsCursor) {
      int curVis = s40SettingsCursor - firstRow;
      if (curVis >= 0 && curVis < 7) drawSingleSettingsRow(tft, s40SettingsCursor, curVis, true);
      return;
    }

    for (int vis = 0; vis < 7; vis++) {
      int idx = firstRow + vis;
      if (idx >= SETTINGS_COUNT) {
        int ry = 30 + vis * 37;
        tft->fillRect(6, ry, w - 12, 34, C_DARK_BG);
        continue;
      }
      drawSingleSettingsRow(tft, idx, vis, idx == s40SettingsCursor);
    }

    // Thanh cuộn chỉ báo vị trí danh sách cài đặt
    int sbX = w - 4;
    int sbY = 30;
    int sbH = 7 * 37 - 3;
    tft->drawFastVLine(sbX, sbY, sbH, 0x2124);
    int thumbH = max(16, (sbH * 7) / SETTINGS_COUNT);
    int thumbY = sbY + ((sbH - thumbH) * s40SettingsCursor) / (SETTINGS_COUNT - 1);
    tft->fillRoundRect(sbX - 1, thumbY, 3, thumbH, 1, C_NEON_CYAN);

    lastDrawnSettingsCursor = s40SettingsCursor;
    lastDrawnSettingsFirstRow = firstRow;
  }

  void adjustItem(int row, int dir, Adafruit_SPITFT* tft) {
    if (row == 0) {
      if (s_shell.applyTheme) s_shell.applyTheme(s40UiThemeIdx + dir);
      if (s_shell.showToast) s_shell.showToast(String("GIAO DIỆN: ") + curTheme().headerTag, 2000);
      draw(tft, true);
      return;
    } else if (row == 1) {
      screenBrightnessPct = constrain(screenBrightnessPct + dir * 10, 0, 100);
      if (s_shell.setBrightness) s_shell.setBrightness(screenBrightnessPct);
      savePrefs();
    } else if (row == 2) {
      screenTimeoutIdx = (screenTimeoutIdx + dir + 6) % 6;
      savePrefs();
    } else if (row == 3) {
      aodClockStyle = (aodClockStyle + dir + 4) % 4;
      alwaysOnDisplayEnabled = (aodClockStyle > 0);
      savePrefs();
      const char* aodStNames[4] = { "Đã TẮT màn hình khóa", "AOD: Kỹ thuật số", "AOD: Lật số cổ điển", "AOD: Đồng hồ kim" };
      if (s_shell.showToast) s_shell.showToast(aodStNames[aodClockStyle], 2000);
    } else if (row == 4) {
      AudioDriver::setNotifyVolumePct(AudioDriver::getNotifyVolumePct() + dir * 5);
      AudioDriver::playKeyBeep();
      if (s_shell.showToast) s_shell.showToast(String("THÔNG BÁO: ") + AudioDriver::getNotifyVolumePct() + "%", 2000);
    } else if (row == 5) {
      keyBeepEnabled = !keyBeepEnabled;
      savePrefs();
      if (keyBeepEnabled) AudioDriver::playOkChime();
      else AudioDriver::playDeleteChime();
      if (s_shell.showToast) s_shell.showToast(keyBeepEnabled ? "ĐÃ BẬT ÂM PHÍM BẤM" : "ĐÃ TẮT ÂM PHÍM BẤM", 2000);
    } else if (row == 6) {
      SpaceOS_ClockSuite::loadPrefsIfNeeded();
      int curTune = SpaceOS_ClockSuite::getAlarmTuneIdx();
      SpaceOS_ClockSuite::setAlarmTuneIdx(curTune + dir);
      SpaceOS_ClockSuite::savePrefs();
      AudioDriver::playAlarmTuneStep(SpaceOS_ClockSuite::getAlarmTuneIdx(), 0);
      if (s_shell.showToast) s_shell.showToast(String("CHUÔNG: ") + AudioDriver::getAlarmTuneName(SpaceOS_ClockSuite::getAlarmTuneIdx()), 2000);
    } else if (row == 7) {
      micSensitivityMode = (micSensitivityMode + dir + 3) % 3;
      AudioDriver::setMicSensitivityMode(micSensitivityMode);
      savePrefs();
      const char* mNames[3] = { "Mic: THẤP (1.8x)", "Mic: TIÊU CHUẨN (3.5x)", "Mic: CAO (5.5x)" };
      if (s_shell.showToast) s_shell.showToast(mNames[micSensitivityMode], 2000);
    } else if (row == 8) {
      // Toggle emoji/assist eye state handled externally via switchMode or helper
    } else if (row == 9) {
      if (s_shell.switchMode) s_shell.switchMode(3);
      return;
    } else if (row == 10) {
      int m = (MotorDriver::getMode() + dir + 5) % 5;
      MotorDriver::setMode(m);
      savePrefs();
      if (s_shell.showToast) s_shell.showToast(String("CỐI XAY: ") + MotorDriver::getModeName(), 2000);
    } else if (row == 11) {
      int cnt = MotorDriver::getEdisonModeCount();
      int m = (MotorDriver::getEdisonLeftMode() + dir + cnt) % cnt;
      MotorDriver::setEdisonLeftMode(m);
      savePrefs();
      if (s_shell.showToast) s_shell.showToast(String("EDISON TRÁI: ") + MotorDriver::getEdisonLeftModeName(), 2000);
    } else if (row == 12) {
      int cnt = MotorDriver::getEdisonModeCount();
      int m = (MotorDriver::getEdisonRightMode() + dir + cnt) % cnt;
      MotorDriver::setEdisonRightMode(m);
      savePrefs();
      if (s_shell.showToast) s_shell.showToast(String("EDISON PHẢI: ") + MotorDriver::getEdisonRightModeName(), 2000);
    } else if (row == 13) {
      if (dir > 0) {
        CobLeds::nextMode();
      } else {
        CobLeds::togglePower();
      }
      MotorDriver::setFlowingLeds(CobLeds::isOn());
      savePrefs();
      if (s_shell.showToast) s_shell.showToast(String("LED COB: ") + CobLeds::getModeName(), 2000);
    } else if (row == 14) {
      performFactoryReset();
      return;
    }
    draw(tft, false);
  }

  void performFactoryReset() {
    Serial.println("[FactoryReset] === BAT DAU KHOI PHUC CAI DAT BAN DAU (MEO GOME CHIBI) ===");

    // 1. Chuyen giao dien mac dinh sang MEO GOME CHIBI (Theme 5)
    s40UiThemeIdx = 5;
    if (s_shell.applyTheme) s_shell.applyTheme(5);

    // 2. Thiet lap cau hinh Man Hinh Cho mac dinh voi Theme Meo Chibi
    if (s_shell.resetConfig) s_shell.resetConfig();

    // 3. Khoi phuc Cai Dat He Thong (s40_sys)
    screenBrightnessPct    = 100;
    if (s_shell.setBrightness) s_shell.setBrightness(100);
    screenTimeoutIdx       = 2; // 30 giay tu tat man hinh
    aodClockStyle          = 1; // Dong ho AOD ky thuat so
    alwaysOnDisplayEnabled = true;
    keyBeepEnabled         = true;
    micSensitivityMode     = 1; // Tieu chuan (3.5x)
    AudioDriver::setMicSensitivityMode(1);
    MotorDriver::setMode(0);
    MotorDriver::setEdisonLeftMode(0);
    MotorDriver::setEdisonRightMode(0);
    CobLeds::init(true);
    CobLeds::setCurrentMode(1);
    MotorDriver::setFlowingLeds(true);
    savePrefs();

    // 4. Khoi phuc Am Luong Thong Bao & Loa
    AudioDriver::setNotifyVolumePct(70);
    AudioDriver::setSpeakerVolumePct(70);

    // 5. Khoi phuc Bao Thuc / Hen Gio (s40_clock)
    Preferences pClock;
    if (pClock.begin("s40_clock", false)) {
      pClock.clear();
      pClock.end();
    }
    SpaceOS_ClockSuite::resetDefaults();

    // 6. Dat lai Diem Ky Luc Tro Choi (s40_games)
    Preferences pGames;
    if (pGames.begin("s40_games", false)) {
      pGames.clear();
      pGames.end();
    }
    SpaceOS_GameCenter::resetHighScores();

    // 7. Khoi phuc Mat Khau Quan Tri Web (admin / admin)
    WebAuth::resetToDefault();

    // 8. Dat lai anh nen active
    ImageManager::selectActiveImage("");

    // 9. Am thanh chao mung & Thong bao truc quan
    AudioDriver::playStartupChime();
    if (s_shell.showToast) s_shell.showToast("DA KHOI PHUC CAI DAT GOC (MEO CHIBI)", 3500);

    // 10. Dat lai con tro cai dat ve dong 1 va ve lai man hinh
    s40SettingsCursor = 0;
    lastDrawnSettingsCursor = -1;
    lastDrawnSettingsFirstRow = -1;
    if (s_shell.refreshScreen) s_shell.refreshScreen();
    Serial.println("[FactoryReset] === HOAN TAT KHOI PHUC CAI DAT BAN DAU ===");
  }

  void handleKey(int keyIndex, Adafruit_SPITFT* tft) {
    if (keyIndex == 1) {
      s40SettingsCursor = (s40SettingsCursor + SETTINGS_COUNT - 1) % SETTINGS_COUNT;
      draw(tft, false);
    } else if (keyIndex == 2) {
      s40SettingsCursor = (s40SettingsCursor + 1) % SETTINGS_COUNT;
      draw(tft, false);
    } else if (keyIndex == 3) {
      adjustItem(s40SettingsCursor, -1, tft);
    } else if (keyIndex == 4 || keyIndex == 0) {
      adjustItem(s40SettingsCursor, 1, tft);
    } else if (keyIndex == 6) {
      if (s_shell.onExit) s_shell.onExit();
    }
  }

} // namespace SpaceOS_Settings
