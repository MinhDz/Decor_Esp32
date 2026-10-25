#include "SpaceOS_ClockSuite.h"
#include "drivers/AudioDriver.h"
#include "TimeManager.h"
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

namespace SpaceOS_ClockSuite {

  using SpaceOS::vnStrLen;
  using SpaceOS::vnSubstr;

  static ShellDelegates s_shell;

  void setShellDelegates(const ShellDelegates& delegates) {
    s_shell = delegates;
  }

  uint8_t s40ClockTab = 0; // 0 = Báo thức, 1 = Bấm giờ, 2 = Hẹn giờ, 3 = Cài đặt giờ

  // Trạng thái Báo thức (3 mốc) + Lưu NVS Vĩnh viễn + Hoãn 5 phút (Snooze)
  static uint8_t s40AlarmHour[3]   = { 6, 7, 12 };
  static uint8_t s40AlarmMin[3]    = { 30, 15, 0 };
  static bool    s40AlarmEnable[3] = { true, false, false };
  static int     s40AlarmCursor    = 0;
  static uint8_t s40AlarmEditField = 0; // 0 = Chọn dòng, 1 = Sửa Giờ, 2 = Sửa Phút
  static int     s40LastAlarmTrigMin = -1;
  static bool    s40ClockPrefsLoaded = false;
  static int     s40AlarmTuneIdx   = 1; // 0: Bíp Dồn Dập, 1: Nokia Tune Cổ Điển, 2: SMS Morse, 3: Chuông 4 Nốt

  // Trạng thái Hoãn báo thức 5 phút (Snooze)
  static bool    s40SnoozeActive   = false;
  static uint8_t s40SnoozeHour     = 0;
  static uint8_t s40SnoozeMin      = 0;
  static uint8_t s40SnoozeAlarmIdx = 0;

  // Trạng thái THÔNG BÁO ĐẨY TOÀN MÀN HÌNH (FULL-SCREEN PUSH ALERT MODAL)
  static bool          s40PushAlertActive      = false;
  static uint8_t       s40PushAlertType        = 0; // 1 = Báo thức (Alarm), 2 = Hẹn giờ (Countdown Timer)
  static uint8_t       s40PushAlertAlarmIdx    = 0; // 0..2
  static unsigned long s40PushAlertStartMs     = 0;
  static unsigned long s40PushAlertLastAnimMs  = 0;
  static unsigned long s40PushAlertLastBeepMs  = 0;
  static uint8_t       s40PushAlertAnimPhase   = 0;
  static String        s40PushAlertTitle       = "";
  static String        s40PushAlertSubTitle    = "";
  static String        s40PushAlertBigDigits   = "00:00";

  // Trạng thái Bấm giờ (Stopwatch)
  static bool          s40SwRunning = false;
  static unsigned long s40SwStartMs = 0;
  static unsigned long s40SwElapsedMs = 0;
  static unsigned long s40SwLaps[3] = { 0, 0, 0 };
  static int           s40SwLapCount = 0;

  // Trạng thái Hẹn giờ đếm ngược (Countdown Timer)
  static bool          s40TimerRunning = false;
  static uint32_t      s40TimerPresetSec = 180; // Mặc định 03:00
  static uint32_t      s40TimerRemainSec = 180;
  static unsigned long s40TimerLastTickMs = 0;

  // Trạng thái Cài đặt thời gian thủ công
  static int s40TimeEditField = 0; // 0=Giờ, 1=Phút, 2=Ngày, 3=Tháng, 4=Năm
  static int s40EditHour = 12, s40EditMin = 0, s40EditDay = 28, s40EditMonth = 9, s40EditYear = 2026;
  static bool s40EditInitialized = false;

  uint8_t  getActiveTab() { return s40ClockTab; }
  void     setActiveTab(uint8_t tab) { s40ClockTab = tab % 4; }
  uint8_t& getActiveTabRef() { return s40ClockTab; }

  int& getAlarmTuneIdxRef() { return s40AlarmTuneIdx; }
  int  getAlarmTuneIdx() { return s40AlarmTuneIdx; }
  void setAlarmTuneIdx(int idx) { s40AlarmTuneIdx = (idx % 4 + 4) % 4; }

  bool isPushAlertActive() { return s40PushAlertActive; }
  bool isTimerRunning() { return s40TimerRunning; }
  uint32_t getTimerPresetSec() { return s40TimerPresetSec; }
  uint32_t getTimerRemainSec() { return s40TimerRemainSec; }
  bool isStopwatchRunning() { return s40SwRunning; }

  void loadPrefsIfNeeded() {
    if (s40ClockPrefsLoaded) return;
    s40ClockPrefsLoaded = true;
    Preferences p;
    if (p.begin("s40_clock", true)) {
      for (int i = 0; i < 3; i++) {
        char kh[8], km[8], ke[8];
        snprintf(kh, sizeof(kh), "ah%d", i);
        snprintf(km, sizeof(km), "am%d", i);
        snprintf(ke, sizeof(ke), "ae%d", i);
        s40AlarmHour[i]   = p.getUChar(kh, s40AlarmHour[i]) % 24;
        s40AlarmMin[i]    = p.getUChar(km, s40AlarmMin[i]) % 60;
        s40AlarmEnable[i] = p.getBool(ke, s40AlarmEnable[i]);
      }
      s40AlarmTuneIdx = p.getInt("al_tune", 1) % 4;
      uint32_t savedPreset = p.getUInt("tm_pre", s40TimerPresetSec);
      if (savedPreset >= 10 && savedPreset <= 3600) {
        s40TimerPresetSec = savedPreset;
        if (!s40TimerRunning) s40TimerRemainSec = savedPreset;
      }
      p.end();
    }
  }

  void savePrefs() {
    Preferences p;
    if (p.begin("s40_clock", false)) {
      for (int i = 0; i < 3; i++) {
        char kh[8], km[8], ke[8];
        snprintf(kh, sizeof(kh), "ah%d", i);
        snprintf(km, sizeof(km), "am%d", i);
        snprintf(ke, sizeof(ke), "ae%d", i);
        p.putUChar(kh, s40AlarmHour[i]);
        p.putUChar(km, s40AlarmMin[i]);
        p.putBool(ke, s40AlarmEnable[i]);
      }
      p.putInt("al_tune", s40AlarmTuneIdx);
      p.putUInt("tm_pre", s40TimerPresetSec);
      p.end();
    }
  }

  void resetDefaults() {
    for (int i = 0; i < 3; i++) {
      s40AlarmEnable[i] = false;
      s40AlarmHour[i]   = 7;
      s40AlarmMin[i]    = 0;
    }
    s40AlarmTuneIdx   = 1; // Nokia Tune mac dinh
    s40TimerPresetSec = 300;
    s40TimerRemainSec = 300;
    s40TimerRunning   = false;
    savePrefs();
  }

  void init() {
    loadPrefsIfNeeded();
  }

  static void initManualTimeFieldsFromSystem() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5)) {
      s40EditHour  = timeinfo.tm_hour;
      s40EditMin   = timeinfo.tm_min;
      s40EditDay   = timeinfo.tm_mday;
      s40EditMonth = timeinfo.tm_mon + 1;
      s40EditYear  = timeinfo.tm_year + 1900;
    }
    s40EditInitialized = true;
  }

  static void applyManualTimeToSystem() {
    struct tm t = {0};
    t.tm_year = s40EditYear - 1900;
    t.tm_mon  = s40EditMonth - 1;
    t.tm_mday = s40EditDay;
    t.tm_hour = s40EditHour;
    t.tm_min  = s40EditMin;
    t.tm_sec  = 0;
    time_t epoch = mktime(&t);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
  }

  void triggerPushAlert(uint8_t alertType, uint8_t alarmIdx, const String& title, const String& subTitle, const String& bigDigits) {
    if (s_shell.wakeScreen) s_shell.wakeScreen();

    s40PushAlertActive     = true;
    s40PushAlertType       = alertType;
    s40PushAlertAlarmIdx   = alarmIdx;
    s40PushAlertStartMs    = millis();
    s40PushAlertLastAnimMs = millis();
    s40PushAlertLastBeepMs = 0;
    s40PushAlertAnimPhase  = 0;
    s40PushAlertTitle      = title;
    s40PushAlertSubTitle   = subTitle;
    s40PushAlertBigDigits  = bigDigits;

    if (s_shell.refreshScreen) s_shell.refreshScreen();

    if (alertType == 1) {
      AudioDriver::playAlarmTuneStep(s40AlarmTuneIdx, 0);
    } else {
      AudioDriver::playSmsSpecialTone();
    }
  }

  void drawPushAlertModal(Adafruit_SPITFT* tft, bool fullRedraw) {
    if (!tft || !s40PushAlertActive) return;
    int W = tft->width();
    int H = tft->height();

    uint16_t accentCol = (s40PushAlertAnimPhase % 2 == 0)
                           ? ((s40PushAlertType == 1) ? C_NEON_PINK : C_NEON_CYAN)
                           : C_YELLOW;
    uint16_t badgeBg   = (s40PushAlertType == 1) ? 0x6008 : 0x0210;

    if (fullRedraw) {
      // Làm tối nền và vẽ khung Modal nổi bật chính giữa màn hình (240x320)
      tft->fillScreen(0x0841);
      tft->fillRoundRect(8, 12, W - 16, H - 24, 10, C_CARD_BG);
    }

    // Viền Neon đôi nhấp nháy cảnh báo theo nhịp 350ms
    tft->drawRoundRect(8, 12, W - 16, H - 24, 10, accentCol);
    tft->drawRoundRect(10, 14, W - 20, H - 28, 9, (s40PushAlertAnimPhase % 2 == 0) ? C_WHITE : accentCol);

    // Thanh Header của Push Alert
    tft->fillRoundRect(14, 18, W - 28, 30, 6, badgeBg);
    tft->drawRoundRect(14, 18, W - 28, 30, 6, accentCol);
    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, badgeBg);
    int tw = vnStrLen(s40PushAlertTitle) * 6;
    tft->setCursor(max(18, (W - tw) / 2), 29);
    tft->print(s40PushAlertTitle);

    // Vẽ biểu tượng Chuông rung / Đồng hồ cát động ở giữa trên (cx = W/2, cy = 86)
    int cx = W / 2;
    int cy = 84;
    tft->fillRect(cx - 56, cy - 28, 112, 56, C_CARD_BG);
    int shake = (s40PushAlertAnimPhase % 2 == 0) ? -3 : 3;

    // Sóng âm tỏa ra 2 bên
    uint16_t waveCol1 = (s40PushAlertAnimPhase % 2 == 0) ? C_NEON_CYAN : C_SLATE;
    uint16_t waveCol2 = (s40PushAlertAnimPhase % 2 == 1) ? C_NEON_PINK : C_SLATE;
    tft->drawCircleHelper(cx - 18, cy, 14, 0x09, waveCol1);
    tft->drawCircleHelper(cx + 18, cy, 14, 0x06, waveCol1);
    tft->drawCircleHelper(cx - 22, cy, 22, 0x09, waveCol2);
    tft->drawCircleHelper(cx + 22, cy, 22, 0x06, waveCol2);

    if (s40PushAlertType == 1) {
      // Vẽ quả chuông báo thức lắc lư
      tft->fillCircle(cx + shake, cy - 6, 12, C_YELLOW);
      tft->fillRoundRect(cx - 15 + shake, cy - 4, 30, 14, 4, C_YELLOW);
      tft->fillCircle(cx + shake, cy + 14, 4, C_NEON_PINK);
      tft->drawCircle(cx + shake, cy - 18, 3, C_WHITE);
    } else {
      // Vẽ đồng hồ hẹn giờ phát sáng
      tft->fillCircle(cx, cy, 18, 0x1949);
      tft->drawCircle(cx, cy, 18, accentCol);
      tft->drawCircle(cx, cy, 16, C_WHITE);
      tft->drawLine(cx, cy, cx, cy - 10, C_YELLOW);
      tft->drawLine(cx, cy, cx + shake * 3, cy + 4, C_NEON_PINK);
    }

    // Khung số lớn hiển thị Giờ Báo Thức hoặc 00:00
    tft->fillRoundRect(22, 118, W - 44, 58, 8, 0x0841);
    tft->drawRoundRect(22, 118, W - 44, 58, 8, accentCol);
    tft->setTextSize(4);
    tft->setTextColor((s40PushAlertAnimPhase % 2 == 0) ? C_WHITE : C_YELLOW, 0x0841);
    int dW = s40PushAlertBigDigits.length() * 24;
    tft->setCursor(max(26, (W - dW) / 2), 132);
    tft->print(s40PushAlertBigDigits);

    // Dòng mô tả chi tiết & Thời gian thực hiện tại
    int hr = 0, mn = 0, sc = 0, wd = 0, dy = 0, mo = 0, yr = 0;
    if (s_shell.getDateTime) s_shell.getDateTime(hr, mn, sc, wd, dy, mo, yr);
    tft->setTextSize(1);
    tft->fillRect(16, 184, W - 32, 36, C_CARD_BG);
    tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
    int stW = vnStrLen(s40PushAlertSubTitle) * 6;
    tft->setCursor(max(18, (W - stW) / 2), 188);
    tft->print(s40PushAlertSubTitle);

    char nowBuf[48];
    snprintf(nowBuf, sizeof(nowBuf), "Giờ hệ thống: %02d:%02d:%02d - %02d/%02d/%04d", hr, mn, sc, dy, mo, yr);
    tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
    int nbW = vnStrLen(String(nowBuf)) * 6;
    tft->setCursor(max(18, (W - nbW) / 2), 204);
    tft->print(nowBuf);

    // 2 Nút thao tác lớn ở đáy Modal (TẮT CHUÔNG / HOÃN 5 PHÚT)
    tft->fillRoundRect(16, 228, W - 32, 34, 6, C_NEON_GREEN);
    tft->drawRoundRect(16, 228, W - 32, 34, 6, C_WHITE);
    tft->setTextColor(C_BLACK, C_NEON_GREEN);
    const char* btnOkTxt = "[OK] : TẮT CHUÔNG BÁO NGAY";
    tft->setCursor((W - vnStrLen(btnOkTxt) * 6) / 2, 241);
    tft->print(btnOkTxt);

    tft->fillRoundRect(16, 268, W - 32, 28, 6, 0x1949);
    tft->drawRoundRect(16, 268, W - 32, 28, 6, C_NEON_CYAN);
    tft->setTextColor(C_YELLOW, 0x1949);
    const char* btnSnoozeTxt = (s40PushAlertType == 1)
                                 ? "[TRÁI/PHẢI/EXIT] : HOÃN BÁO THỨC (+5 PHÚT)"
                                 : "[TRÁI/PHẢI/EXIT] : ĐẾM THÊM +1 PHÚT";
    tft->setCursor(max(18, (W - vnStrLen(btnSnoozeTxt) * 6) / 2), 278);
    tft->print(btnSnoozeTxt);
  }

  void draw(Adafruit_SPITFT* tft, bool fullRedraw) {
    if (!tft) return;
    loadPrefsIfNeeded();
    int W = tft->width();
    if (!s40EditInitialized) initManualTimeFieldsFromSystem();

    if (fullRedraw) {
      tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
      if (s_shell.drawChrome) {
        s_shell.drawChrome("ĐỒNG HỒ & BÁO THỨC", "[MENU:ĐổiMục]", "[OK:Chọn]", "[EXIT:Menu]");
      }
    }

    // 1. Thanh 4 Tab trên cùng (y = 26..50)
    const char* tabNames[4] = { "BÁO THỨC", "BẤM GIỜ", "HẸN GIỜ", "CÀI GIỜ" };
    for (int i = 0; i < 4; i++) {
      int tx = 4 + i * 58;
      bool active = (s40ClockTab == i);
      tft->fillRoundRect(tx, 26, 56, 22, 4, active ? 0x1949 : C_CARD_BG);
      tft->drawRoundRect(tx, 26, 56, 22, 4, active ? C_NEON_CYAN : C_CARD_BORDER);
      tft->setTextSize(1);
      tft->setTextColor(active ? C_YELLOW : C_SLATE, active ? 0x1949 : C_CARD_BG);
      int tw = vnStrLen(String(tabNames[i])) * 6;
      tft->setCursor(tx + (56 - tw) / 2, 33);
      tft->print(tabNames[i]);
    }

    if (fullRedraw) {
      tft->fillRect(4, 52, W - 8, 240, C_DARK_BG);
    }

    if (s40ClockTab == 0) {
      // --- TAB 0: BÁO THỨC (ALARM CLOCK) ---
      tft->setTextSize(1);
      tft->fillRect(6, 54, W - 12, 14, C_DARK_BG);
      tft->setTextColor(s40AlarmEditField > 0 ? C_YELLOW : C_NEON_CYAN, C_DARK_BG);
      tft->setCursor(8, 56);
      if (s40AlarmCursor == 3) {
        tft->print("CHỌN CHUÔNG: [TRÁI/PHẢI] Đổi Chuông | [OK] Nghe thử");
      } else if (s40AlarmEditField == 1) {
        tft->print("ĐANG CHỈNH GIỜ: [LÊN/XUỐNG] +-1h | [OK] Sang Phút");
      } else if (s40AlarmEditField == 2) {
        tft->print("CHỈNH PHÚT: [LÊN/XUỐNG] +-1p, [TRÁI/PHẢI] +-5p");
      } else if (s40SnoozeActive) {
        tft->printf("Đang hoãn chuông -> Sẽ kêu lại lúc %02u:%02u", s40SnoozeHour, s40SnoozeMin);
      } else {
        tft->print("[OK]: Chỉnh Giờ/Phút | [TRÁI/PHẢI]: Bật/Tắt nhanh");
      }

      for (int i = 0; i < 3; i++) {
        int ry = 70 + i * 58;
        bool sel = (i == s40AlarmCursor);
        uint16_t cardBg = sel ? 0x1949 : C_CARD_BG;
        tft->fillRoundRect(8, ry, W - 16, 52, 6, cardBg);
        tft->drawRoundRect(8, ry, W - 16, 52, 6, sel ? (s40AlarmEditField > 0 ? C_YELLOW : C_NEON_CYAN) : C_CARD_BORDER);

        if (sel && s40AlarmEditField == 1) {
          tft->fillRoundRect(15, ry + 8, 28, 22, 4, C_NEON_PINK);
        } else if (sel && s40AlarmEditField == 2) {
          tft->fillRoundRect(51, ry + 8, 28, 22, 4, C_NEON_PINK);
        }

        char hStr[6], mStr[6];
        snprintf(hStr, sizeof(hStr), "%02u", s40AlarmHour[i]);
        snprintf(mStr, sizeof(mStr), "%02u", s40AlarmMin[i]);

        tft->setTextSize(2);
        uint16_t baseTimeCol = s40AlarmEnable[i] ? C_YELLOW : C_SLATE;
        tft->setTextColor((sel && s40AlarmEditField == 1) ? C_WHITE : baseTimeCol,
                          (sel && s40AlarmEditField == 1) ? C_NEON_PINK : cardBg);
        tft->setCursor(18, ry + 11);
        tft->print(hStr);

        tft->setTextColor(baseTimeCol, cardBg);
        tft->setCursor(42, ry + 11);
        tft->print(":");

        tft->setTextColor((sel && s40AlarmEditField == 2) ? C_WHITE : baseTimeCol,
                          (sel && s40AlarmEditField == 2) ? C_NEON_PINK : cardBg);
        tft->setCursor(54, ry + 11);
        tft->print(mStr);

        tft->setTextSize(1);
        tft->setTextColor(C_WHITE, cardBg);
        tft->setCursor(18, ry + 35);
        if (sel && s40AlarmEditField == 1) {
          tft->print(">> Đang sửa GIỜ (Bấm OK sang Phút)");
        } else if (sel && s40AlarmEditField == 2) {
          tft->print(">> Đang sửa PHÚT (Bấm OK để Lưu)");
        } else {
          tft->printf("Báo thức #%d (Chuông đẩy toàn màn hình)", i + 1);
        }

        // Nút gạt BẬT/TẮT
        uint16_t badgeCol = s40AlarmEnable[i] ? C_NEON_GREEN : 0x2124;
        tft->fillRoundRect(W - 72, ry + 14, 52, 24, 5, badgeCol);
        tft->drawRoundRect(W - 72, ry + 14, 52, 24, 5, C_WHITE);
        tft->setTextColor(s40AlarmEnable[i] ? C_BLACK : C_SLATE, badgeCol);
        tft->setCursor(W - 58, ry + 22);
        tft->print(s40AlarmEnable[i] ? "BẬT" : "TẮT");
      }

      // 4. Mục chọn Nhạc chuông Báo thức (Alarm Ringtone)
      int ryTune = 250;
      bool selTune = (s40AlarmCursor == 3);
      uint16_t tuneBg = selTune ? 0x1949 : C_CARD_BG;
      tft->fillRoundRect(8, ryTune, W - 16, 40, 5, tuneBg);
      tft->drawRoundRect(8, ryTune, W - 16, 40, 5, selTune ? C_NEON_CYAN : C_CARD_BORDER);

      tft->setTextSize(1);
      tft->setTextColor(selTune ? C_YELLOW : C_SLATE, tuneBg);
      tft->setCursor(14, ryTune + 6);
      tft->print("Nhạc chuông báo thức:");

      tft->setTextColor(selTune ? C_NEON_GREEN : C_NEON_CYAN, tuneBg);
      tft->setCursor(14, ryTune + 22);
      tft->printf("< %s >", AudioDriver::getAlarmTuneName(s40AlarmTuneIdx));

      tft->fillRoundRect(W - 68, ryTune + 8, 54, 24, 4, selTune ? C_NEON_PINK : 0x2124);
      tft->setTextColor(C_WHITE, selTune ? C_NEON_PINK : 0x2124);
      tft->setCursor(W - 58, ryTune + 16);
      tft->print("Nghe");

      if (s_shell.drawSoftkeys) {
        s_shell.drawSoftkeys("[MENU:Tab]",
                            s40AlarmCursor == 3 ? "[OK:NgheThử]" :
                            (s40AlarmEditField == 0 ? "[OK:ChỉnhGiờ]" : (s40AlarmEditField == 1 ? "[OK:SangPhút]" : "[OK:Lưu&Bật]")),
                            s40AlarmEditField > 0 ? "[EXIT:Xong]" : "[EXIT:Menu]");
      }
    } else if (s40ClockTab == 1) {
      // --- TAB 1: BẤM GIỜ THỂ THAO (STOPWATCH) ---
      unsigned long totalMs = s40SwElapsedMs + (s40SwRunning ? (millis() - s40SwStartMs) : 0);
      unsigned int mins = (totalMs / 60000UL) % 100;
      unsigned int secs = (totalMs / 1000UL) % 60;
      unsigned int csec = (totalMs / 10UL) % 100;

      tft->fillRoundRect(8, 56, W - 16, 86, 8, C_CARD_BG);
      tft->drawRoundRect(8, 56, W - 16, 86, 8, s40SwRunning ? C_NEON_GREEN : C_NEON_CYAN);

      tft->setTextSize(1);
      tft->setTextColor(s40SwRunning ? C_NEON_GREEN : C_YELLOW, C_CARD_BG);
      tft->setCursor(18, 64);
      tft->print(s40SwRunning ? "ĐANG BẤM GIỜ THỂ THAO..." : "ĐỒNG HỒ BẤM GIỜ (SẴN SÀNG)");

      char swBuf[16];
      snprintf(swBuf, sizeof(swBuf), "%02u:%02u.%02u", mins, secs, csec);
      tft->setTextSize(3);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(32, 86);
      tft->print(swBuf);

      tft->setTextSize(1);
      tft->setTextColor(C_NEON_PINK, C_CARD_BG);
      tft->setCursor(18, 124);
      tft->print("[OK]: Chạy/Dừng    [LÊN]: Lưu vòng / Đặt lại");

      // Bảng lưu 3 vòng chạy (Laps)
      tft->fillRoundRect(8, 150, W - 16, 134, 6, C_CARD_BG);
      tft->drawRoundRect(8, 150, W - 16, 134, 6, C_CARD_BORDER);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(16, 160);
      tft->print("LỊCH SỬ CÁC VÒNG ĐO:");

      for (int i = 0; i < 3; i++) {
        int ly = 180 + i * 32;
        tft->fillRoundRect(16, ly, W - 32, 26, 4, 0x0841);
        tft->setTextColor(C_NEON_CYAN, 0x0841);
        tft->setCursor(24, ly + 9);
        if (s40SwLaps[i] > 0) {
          unsigned long lm = s40SwLaps[i];
          tft->printf("VÒNG ĐO #%d :      %02lu:%02lu.%02lu", i + 1,
                      (lm / 60000UL) % 100, (lm / 1000UL) % 60, (lm / 10UL) % 100);
        } else {
          tft->printf("VÒNG ĐO #%d :      --:--.--", i + 1);
        }
      }
      if (s_shell.drawSoftkeys) {
        s_shell.drawSoftkeys("[MENU:Mục]", s40SwRunning ? "[OK:Dừng]" : "[OK:Chạy]", "[EXIT:Menu]");
      }
    } else if (s40ClockTab == 2) {
      // --- TAB 2: HẸN GIỜ ĐẾM NGƯỢC (COUNTDOWN TIMER) ---
      tft->fillRoundRect(8, 56, W - 16, 136, 8, C_CARD_BG);
      tft->drawRoundRect(8, 56, W - 16, 136, 8, s40TimerRunning ? C_NEON_PINK : C_NEON_CYAN);

      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(16, 66);
      tft->print(s40TimerRunning ? "ĐANG ĐẾM NGƯỢC THỜI GIAN..." : "HẸN GIỜ ĐẾM NGƯỢC (LÊN/XUỐNG: Phút)");

      char tmBuf[16];
      snprintf(tmBuf, sizeof(tmBuf), "%02u:%02u", (unsigned)(s40TimerRemainSec / 60), (unsigned)(s40TimerRemainSec % 60));
      tft->setTextSize(4);
      tft->setTextColor((s40TimerRemainSec == 0) ? C_NEON_PINK : C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(52, 92);
      tft->print(tmBuf);

      // Thanh tiến trình rút dần
      int barX = 22, barY = 148, barW = W - 44;
      tft->fillRoundRect(barX, barY, barW, 12, 4, 0x2124);
      uint32_t denom = (s40TimerPresetSec > 0) ? s40TimerPresetSec : 1;
      int fillW = (int)((uint64_t)s40TimerRemainSec * barW / denom);
      fillW = constrain(fillW, 0, barW);
      if (fillW > 0) {
        tft->fillRoundRect(barX, barY, fillW, 12, 4, C_NEON_GREEN);
      }
      tft->drawRoundRect(barX, barY, barW, 12, 4, C_WHITE);

      tft->setTextSize(1);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(18, 170);
      tft->printf("Mức hẹn đặt: %02u phút %02u giây", (unsigned)(s40TimerPresetSec / 60), (unsigned)(s40TimerPresetSec % 60));

      // Hộp hướng dẫn phím bên dưới
      tft->fillRoundRect(8, 200, W - 16, 84, 6, C_CARD_BG);
      tft->drawRoundRect(8, 200, W - 16, 84, 6, C_CARD_BORDER);
      tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
      tft->setCursor(14, 212);
      tft->print("ĐIỀU KHIỂN HẸN GIỜ ĐẾM NGƯỢC:");
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 230);
      tft->print("+ [OK]: Bắt đầu / Tạm dừng đếm ngược");
      tft->setCursor(14, 246);
      tft->print("+ [LÊN/XUỐNG]: +-1 Phút | [TRÁI/PHẢI]: +-10 Giây");
      tft->setCursor(14, 262);
      tft->print("+ Về 00:00: Bật Chuông Đẩy Toàn Màn Hình!");
      if (s_shell.drawSoftkeys) {
        s_shell.drawSoftkeys("[MENU:Tab]", s40TimerRunning ? "[OK:TạmDừng]" : "[OK:BắtĐầu]", "[EXIT:Menu]");
      }
    } else {
      // --- TAB 3: CÀI ĐẶT THỜI GIAN & NGÀY THÁNG THỦ CÔNG ---
      tft->fillRoundRect(8, 56, W - 16, 228, 8, C_CARD_BG);
      tft->drawRoundRect(8, 56, W - 16, 228, 8, C_NEON_CYAN);

      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(16, 66);
      tft->print("CÀI ĐẶT NGÀY GIỜ HỆ THỐNG:");

      const char* fLabels[5] = {
        "1. Giờ hiện tại (0..23)",
        "2. Phút hiện tại (0..59)",
        "3. Ngày trong tháng (1..31)",
        "4. Tháng trong năm (1..12)",
        "5. Năm dương lịch"
      };
      int fVals[5] = { s40EditHour, s40EditMin, s40EditDay, s40EditMonth, s40EditYear };

      for (int i = 0; i < 5; i++) {
        int ry = 86 + i * 36;
        bool sel = (i == s40TimeEditField);
        tft->fillRoundRect(16, ry, W - 32, 30, 5, sel ? 0x1949 : 0x0841);
        tft->drawRoundRect(16, ry, W - 32, 30, 5, sel ? C_NEON_CYAN : 0x2124);
        tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? 0x1949 : 0x0841);
        tft->setCursor(24, ry + 11);
        tft->print(fLabels[i]);

        tft->setTextColor(C_NEON_GREEN, sel ? 0x1949 : 0x0841);
        tft->setCursor(W - 68, ry + 11);
        tft->printf("< %02d >", fVals[i]);
      }
      if (s_shell.drawSoftkeys) {
        s_shell.drawSoftkeys("[MENU:Mục]", "[OK:ĐổiÔ]", "[EXIT:Lưu&Ra]");
      }
    }
  }

  bool handlePushAlertKey(int keyIndex, Adafruit_SPITFT* tft) {
    if (!s40PushAlertActive) return false;
    s40PushAlertActive = false;
    if (s_shell.wakeScreen) s_shell.wakeScreen();

    if (keyIndex == 0) {
      // Phím OK: Tắt chuông hoàn toàn
      AudioDriver::playOkChime();
      if (s40PushAlertType == 1) {
        s40SnoozeActive = false;
        if (s_shell.showToast) s_shell.showToast("ĐÃ TẮT CHUÔNG BÁO THỨC!", 2500);
      } else {
        s40TimerRemainSec = s40TimerPresetSec;
        if (s_shell.showToast) s_shell.showToast("ĐÃ TẮT CHUÔNG HẸN GIỜ!", 2500);
      }
    } else {
      // Các phím khác (TRÁI / PHẢI / EXIT / LÊN / XUỐNG / MENU): Hoãn +5 Phút (Snooze) hoặc Đếm thêm +1 Phút
      AudioDriver::playDeleteChime();
      if (s40PushAlertType == 1) {
        int hr = 0, mn = 0, sc = 0, wd = 0, dy = 0, mo = 0, yr = 0;
        if (s_shell.getDateTime) s_shell.getDateTime(hr, mn, sc, wd, dy, mo, yr);
        int snzTot = hr * 60 + mn + 5;
        s40SnoozeHour     = (uint8_t)((snzTot / 60) % 24);
        s40SnoozeMin      = (uint8_t)(snzTot % 60);
        s40SnoozeAlarmIdx = s40PushAlertAlarmIdx;
        s40SnoozeActive   = true;
        char msg[44];
        snprintf(msg, sizeof(msg), "HOÃN +5P -> BÁO LẠI LÚC %02u:%02u", s40SnoozeHour, s40SnoozeMin);
        if (s_shell.showToast) s_shell.showToast(String(msg), 3000);
      } else {
        s40TimerRemainSec  = 60;
        s40TimerRunning    = true;
        s40TimerLastTickMs = millis();
        if (s_shell.showToast) s_shell.showToast("ĐÃ ĐẾM THÊM +1 PHÚT (01:00)!", 2500);
      }
    }
    if (s_shell.refreshScreen) s_shell.refreshScreen();
    return true;
  }

  void handleKey(int keyIndex, Adafruit_SPITFT* tft) {
    loadPrefsIfNeeded();
    if (keyIndex == 3) { // LEFT
      if (s40ClockTab == 0) {
        if (s40AlarmCursor == 3) {
          s40AlarmTuneIdx = (s40AlarmTuneIdx + 3) % 4;
          savePrefs();
          AudioDriver::playAlarmTuneStep(s40AlarmTuneIdx, 0);
        } else if (s40AlarmEditField == 1) {
          // Đang sửa GIỜ: Giảm 1 giờ
          s40AlarmHour[s40AlarmCursor] = (s40AlarmHour[s40AlarmCursor] + 23) % 24;
          savePrefs();
        } else if (s40AlarmEditField == 2) {
          // Đang sửa PHÚT: Giảm nhanh 5 phút
          s40AlarmMin[s40AlarmCursor] = (s40AlarmMin[s40AlarmCursor] + 55) % 60;
          savePrefs();
        } else {
          // Đang chọn dòng: Bật/Tắt nhanh báo thức đang chọn
          s40AlarmEnable[s40AlarmCursor] = !s40AlarmEnable[s40AlarmCursor];
          s40LastAlarmTrigMin = -1;
          savePrefs();
        }
        draw(tft, false);
      } else if (s40ClockTab == 2 && !s40TimerRunning) {
        // Trong Tab Hẹn giờ (khi tạm dừng): LEFT giảm 10 giây
        if (s40TimerPresetSec > 10) s40TimerPresetSec -= 10;
        else s40TimerPresetSec = 10;
        s40TimerRemainSec = s40TimerPresetSec;
        savePrefs();
        draw(tft, false);
      } else {
        s40AlarmEditField = 0;
        s40ClockTab = (s40ClockTab + 3) % 4;
        draw(tft, true);
      }
    } else if (keyIndex == 4) { // RIGHT
      if (s40ClockTab == 0) {
        if (s40AlarmCursor == 3) {
          s40AlarmTuneIdx = (s40AlarmTuneIdx + 1) % 4;
          savePrefs();
          AudioDriver::playAlarmTuneStep(s40AlarmTuneIdx, 0);
        } else if (s40AlarmEditField == 1) {
          // Đang sửa GIỜ: Tăng 1 giờ
          s40AlarmHour[s40AlarmCursor] = (s40AlarmHour[s40AlarmCursor] + 1) % 24;
          savePrefs();
        } else if (s40AlarmEditField == 2) {
          // Đang sửa PHÚT: Tăng nhanh 5 phút
          s40AlarmMin[s40AlarmCursor] = (s40AlarmMin[s40AlarmCursor] + 5) % 60;
          savePrefs();
        } else {
          // Đang chọn dòng: Bật/Tắt nhanh báo thức đang chọn
          s40AlarmEnable[s40AlarmCursor] = !s40AlarmEnable[s40AlarmCursor];
          s40LastAlarmTrigMin = -1;
          savePrefs();
        }
        draw(tft, false);
      } else if (s40ClockTab == 2 && !s40TimerRunning) {
        // Trong Tab Hẹn giờ (khi tạm dừng): RIGHT tăng 10 giây
        s40TimerPresetSec = min((uint32_t)3600, s40TimerPresetSec + 10);
        s40TimerRemainSec = s40TimerPresetSec;
        savePrefs();
        draw(tft, false);
      } else {
        s40AlarmEditField = 0;
        s40ClockTab = (s40ClockTab + 1) % 4;
        draw(tft, true);
      }
    } else if (keyIndex == 1) { // UP
      if (s40ClockTab == 0) {
        if (s40AlarmEditField == 1) {
          s40AlarmHour[s40AlarmCursor] = (s40AlarmHour[s40AlarmCursor] + 1) % 24;
          savePrefs();
        } else if (s40AlarmEditField == 2) {
          s40AlarmMin[s40AlarmCursor] = (s40AlarmMin[s40AlarmCursor] + 1) % 60;
          savePrefs();
        } else {
          s40AlarmCursor = (s40AlarmCursor + 3) % 4;
        }
        draw(tft, false);
      } else if (s40ClockTab == 1) {
        // Trong Tab Bấm giờ: UP lưu vòng (Lap) khi đang chạy, hoặc Reset về 0 khi đang dừng
        if (s40SwRunning) {
          unsigned long curTotal = s40SwElapsedMs + (millis() - s40SwStartMs);
          s40SwLaps[s40SwLapCount % 3] = curTotal;
          s40SwLapCount++;
        } else {
          s40SwElapsedMs = 0;
          s40SwLapCount = 0;
          s40SwLaps[0] = s40SwLaps[1] = s40SwLaps[2] = 0;
        }
        draw(tft, false);
      } else if (s40ClockTab == 2) {
        // Tăng +1 phút hẹn giờ
        s40TimerPresetSec = min((uint32_t)3600, s40TimerPresetSec + 60);
        s40TimerRemainSec = s40TimerPresetSec;
        savePrefs();
        draw(tft, false);
      } else if (s40ClockTab == 3) {
        if (s40TimeEditField == 0) s40EditHour = (s40EditHour + 1) % 24;
        else if (s40TimeEditField == 1) s40EditMin = (s40EditMin + 1) % 60;
        else if (s40TimeEditField == 2) s40EditDay = (s40EditDay % 31) + 1;
        else if (s40TimeEditField == 3) s40EditMonth = (s40EditMonth % 12) + 1;
        else if (s40TimeEditField == 4) s40EditYear++;
        applyManualTimeToSystem();
        draw(tft, false);
      }
    } else if (keyIndex == 2) { // DOWN
      if (s40ClockTab == 0) {
        if (s40AlarmEditField == 1) {
          s40AlarmHour[s40AlarmCursor] = (s40AlarmHour[s40AlarmCursor] + 23) % 24;
          savePrefs();
        } else if (s40AlarmEditField == 2) {
          s40AlarmMin[s40AlarmCursor] = (s40AlarmMin[s40AlarmCursor] + 59) % 60;
          savePrefs();
        } else {
          s40AlarmCursor = (s40AlarmCursor + 1) % 4;
        }
        draw(tft, false);
      } else if (s40ClockTab == 1) {
        s40SwRunning = false;
        s40SwElapsedMs = 0;
        s40SwLapCount = 0;
        s40SwLaps[0] = s40SwLaps[1] = s40SwLaps[2] = 0;
        draw(tft, false);
      } else if (s40ClockTab == 2) {
        // Giảm -1 phút hẹn giờ (tối thiểu 10 giây)
        if (s40TimerPresetSec > 60) s40TimerPresetSec -= 60;
        else s40TimerPresetSec = 10;
        s40TimerRemainSec = s40TimerPresetSec;
        savePrefs();
        draw(tft, false);
      } else if (s40ClockTab == 3) {
        if (s40TimeEditField == 0) s40EditHour = (s40EditHour + 23) % 24;
        else if (s40TimeEditField == 1) s40EditMin = (s40EditMin + 59) % 60;
        else if (s40TimeEditField == 2) s40EditDay = (s40EditDay + 29) % 31 + 1;
        else if (s40TimeEditField == 3) s40EditMonth = (s40EditMonth + 10) % 12 + 1;
        else if (s40TimeEditField == 4 && s40EditYear > 2024) s40EditYear--;
        applyManualTimeToSystem();
        draw(tft, false);
      }
    } else if (keyIndex == 0) { // OK
      if (s40ClockTab == 0) {
        if (s40AlarmCursor == 3) {
          AudioDriver::playAlarmTuneStep(s40AlarmTuneIdx, 0);
          if (s_shell.showToast) s_shell.showToast(String("ĐÃ CHỌN: ") + AudioDriver::getAlarmTuneName(s40AlarmTuneIdx), 2500);
        } else {
          if (s40AlarmEditField == 0) {
            s40AlarmEditField = 1;
          } else if (s40AlarmEditField == 1) {
            s40AlarmEditField = 2;
          } else {
            s40AlarmEditField = 0;
            s40AlarmEnable[s40AlarmCursor] = true;
            s40LastAlarmTrigMin = -1;
            savePrefs();
            AudioDriver::playOkChime();
            if (s_shell.showToast) s_shell.showToast("ĐÃ LƯU & BẬT BÁO THỨC!", 2500);
          }
        }
        draw(tft, false);
      } else if (s40ClockTab == 1) {
        if (s40SwRunning) {
          s40SwElapsedMs += (millis() - s40SwStartMs);
          s40SwRunning = false;
        } else {
          s40SwStartMs = millis();
          s40SwRunning = true;
        }
        draw(tft, false);
      } else if (s40ClockTab == 2) {
        if (s40TimerRemainSec == 0) s40TimerRemainSec = s40TimerPresetSec;
        s40TimerRunning = !s40TimerRunning;
        s40TimerLastTickMs = millis();
        draw(tft, false);
      } else if (s40ClockTab == 3) {
        s40TimeEditField = (s40TimeEditField + 1) % 5;
        draw(tft, false);
      }
    } else if (keyIndex == 6) { // EXIT
      if (s40ClockTab == 0 && s40AlarmEditField > 0) {
        s40AlarmEditField = 0;
        s40AlarmEnable[s40AlarmCursor] = true;
        s40LastAlarmTrigMin = -1;
        savePrefs();
        if (s_shell.showToast) s_shell.showToast("ĐÃ LƯU BÁO THỨC!", 2500);
        draw(tft, false);
        return;
      }
      if (s40ClockTab == 3) {
        applyManualTimeToSystem();
        if (s_shell.showToast) s_shell.showToast("ĐÃ CẬP NHẬT THỜI GIAN!", 2500);
      }
      if (s_shell.onExit) s_shell.onExit();
    }
  }

  void loop(Adafruit_SPITFT* tft, unsigned long now, bool isScreenSleeping, int currentMode) {
    loadPrefsIfNeeded();

    // 1. Kiểm tra Hẹn giờ đếm ngược (Countdown Timer) chạy ngầm
    if (s40TimerRunning && (now - s40TimerLastTickMs >= 1000)) {
      s40TimerLastTickMs = now;
      if (s40TimerRemainSec > 0) {
        s40TimerRemainSec--;
        if (!isScreenSleeping && !s40PushAlertActive && currentMode == 13 && s40ClockTab == 2) {
          draw(tft, false);
        }
      }
      if (s40TimerRemainSec == 0) {
        s40TimerRunning = false;
        char sub[48];
        snprintf(sub, sizeof(sub), "Đã đếm ngược xong %02u phút %02u giây!",
                 (unsigned)(s40TimerPresetSec / 60), (unsigned)(s40TimerPresetSec % 60));
        triggerPushAlert(2, 0, "ĐÃ HẾT GIỜ HẸN ĐẾM NGƯỢC!", String(sub), "00:00");
        return;
      }
    }

    // 2. Kiểm tra Báo thức hệ thống (Alarm Clock + Snooze)
    {
      static unsigned long s40LastBgAlarmCheckMs = 0;
      if (now - s40LastBgAlarmCheckMs >= 1000) {
        s40LastBgAlarmCheckMs = now;
        int hr = 0, mn = 0, sc = 0, wd = 0, dy = 0, mo = 0, yr = 0;
        if (s_shell.getDateTime) s_shell.getDateTime(hr, mn, sc, wd, dy, mo, yr);
        int curMinOfDay = hr * 60 + mn;
        if (curMinOfDay != s40LastAlarmTrigMin) {
          // Kiểm tra 3 mốc Báo thức chính
          bool triggered = false;
          for (int a = 0; a < 3; a++) {
            if (s40AlarmEnable[a] && hr == (int)s40AlarmHour[a] && mn == (int)s40AlarmMin[a]) {
              s40LastAlarmTrigMin = curMinOfDay;
              char bigT[12], subT[48];
              snprintf(bigT, sizeof(bigT), "%02u:%02u", s40AlarmHour[a], s40AlarmMin[a]);
              snprintf(subT, sizeof(subT), "Đến giờ Báo thức #%d (Hằng ngày)", a + 1);
              triggerPushAlert(1, (uint8_t)a, "CHUÔNG BÁO THỨC ĐẾN GIỜ!", String(subT), String(bigT));
              triggered = true;
              break;
            }
          }
          // Kiểm tra mốc Hoãn báo thức 5 phút (Snooze)
          if (!triggered && s40SnoozeActive && hr == (int)s40SnoozeHour && mn == (int)s40SnoozeMin) {
            s40LastAlarmTrigMin = curMinOfDay;
            s40SnoozeActive = false;
            char bigT[12], subT[48];
            snprintf(bigT, sizeof(bigT), "%02u:%02u", s40SnoozeHour, s40SnoozeMin);
            snprintf(subT, sizeof(subT), "Báo lại sau 5 phút (Báo thức #%u)", (unsigned)(s40SnoozeAlarmIdx + 1));
            triggerPushAlert(1, s40SnoozeAlarmIdx, "BÁO THỨC LẠI (SNOOZE)!", String(subT), String(bigT));
          }
        }
      }
    }

    // 3. Nếu đang hiển thị THÔNG BÁO ĐẨY TOÀN MÀN HÌNH (Push Alert Modal):
    if (s40PushAlertActive) {
      if (now - s40PushAlertLastAnimMs >= 360) {
        s40PushAlertLastAnimMs = now;
        s40PushAlertAnimPhase++;
        drawPushAlertModal(tft, false);
      }
      unsigned long stepInterval = (s40PushAlertType == 1 && s40AlarmTuneIdx == 1) ? 1400UL : 750UL;
      if ((now - s40PushAlertStartMs < 60000UL) && (now - s40PushAlertLastBeepMs >= stepInterval)) {
        s40PushAlertLastBeepMs = now;
        AudioDriver::playAlarmTuneStep(s40PushAlertType == 1 ? s40AlarmTuneIdx : 0, s40PushAlertAnimPhase);
      }
      return;
    }

    // 4. Nếu đang ở màn hình Bấm giờ Tab 1 và đang chạy:
    if (currentMode == 13 && s40ClockTab == 1 && s40SwRunning) {
      static unsigned long lastSwTick = 0;
      if (now - lastSwTick >= 85) {
        lastSwTick = now;
        draw(tft, false);
      }
    }
  }

} // namespace SpaceOS_ClockSuite
