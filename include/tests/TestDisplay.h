#pragma once
#include <Arduino.h>
#include <Preferences.h>

namespace TestDisplay {
  // Khởi tạo màn hình ST7789 (GPIO 12, 11, 10, 13, 14) và nạp giao diện Màn Hình Chờ
  void init();

  // Vòng lặp cập nhật đồng hồ thời gian thực, hiệu ứng mắt và thông số PC HUD
  void loop();

  // Đọc lại cấu hình Màn Hình Chờ từ tệp /standby_config.json trên LittleFS và vẽ lại ngay
  void reloadStandbyConfig();

  // Đồng bộ chế độ hiển thị từ giao diện Web ("standby", "emoji", "pchud", "diag")
  void setScreenMode(const String& mode);

  // Đồng bộ trạng thái biểu cảm XiaoZhi từ Web ("idle", "happy", "angry", "confused", "left", "right", "up", "down", "blink")
  void setEmojiState(const String& state, const String& subtitle = "");

  // Đồng bộ phong cách PC HUD từ Web ("bars", "gauges", "graph", "matrix")
  void setHudStyle(const String& style);

  // Lấy trạng thái màn hình hiện tại để đồng bộ ngược về Web UI
  String getScreenModeName();
  String getEmojiStateName();
  String getSubtitle();
  String getHudStyleName();

  // Cập nhật nhiệt độ & độ ẩm thực từ cảm biến SHT31 (GPIO 8, 9) lên Màn Hình Chờ
  void updateLiveWeather(float tempC, float humPct);

  // Điều khiển chuyển màn hình / kiểu HUD từ phím cứng & cảm biến chạm TTP223
  void cycleScreenMode();
  void cycleHudStyle(int dir = 1);

  // Điều hướng Hệ Điều Hành Symbian S40 từ cụm 7 phím bấm ADC (0=OK, 1=UP, 2=DOWN, 3=LEFT, 4=RIGHT, 5=MENU, 6=EXIT)
  void onKeypadEvent(int keyIndex);

  // Điều chỉnh độ sáng đèn nền màn hình ST7789 (PWM GPIO 7: 0..100%)
  void setBacklightBrightness(int pct);
  int  getBacklightBrightness();

  // --- ĐỒNG BỘ & ĐIỀU KHIỂN TRÌNH PHÁT NHẠC ĐA PHƯƠNG TIỆN (MODE 12 <-> WEB UI) ---
  void getMediaPlayerState(String& trackName, int& trackIdx, int& trackCount, bool& playing,
                           uint8_t& visMode, uint8_t& repeatMode, uint16_t& curSec,
                           uint16_t& totalSec, int& volumePct, bool& fromSd);
  void controlMediaPlayer(const String& action, int value = 0, const String& trackName = "");
  int  getMediaPlaylistItems(String names[], uint16_t durations[], bool fromSd[], size_t sizes[], int maxCount);
  int  getUiThemeIdx();
  void setUiThemeIdx(int idx);

  // Xử lý lệnh điều khiển nhanh từ Serial Monitor ('1', '2', '3', '4', '5', 's', 'i', 'x', 'd')
  bool handleSerial(char cmd);

  // In bảng hướng dẫn các phím điều khiển màn hình
  void printHelp();

  // Chạy hoạt ảnh Boot Cyberpunk & Bảng kiểm tra ngoại vi POST (Power-On Self-Test)
  void runBootSequence(bool playChime = true);

  // Hiển thị tiến trình cập nhật phần mềm từ xa tối giản (OTA Progress Screen)
  void drawOtaProgressScreen(const char* status, int pct);
}
