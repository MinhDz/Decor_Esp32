#include "TimeManager.h"
#include "SystemLogger.h"
#include "tests/TestDisplay.h"
#include <WiFi.h>

static bool s_configured = false;
static bool s_timeSynced = false;
static bool s_syncLogged = false;

void TimeManager::init() {
  s_configured = false;
  s_timeSynced = false;
  s_syncLogged = false;
}

void TimeManager::onWifiConnected() {
  if (s_configured) return;
  s_configured = true;

  // Cấu hình múi giờ GMT+7 và 3 máy chủ Anycast độ trễ thấp nhất
  configTime(7 * 3600, 0, "time.google.com", "time.cloudflare.com", "asia.pool.ntp.org");
  Serial.println("🕒 [NTP] Đã cấu hình đồng bộ thời gian thực: time.google.com, time.cloudflare.com, asia.pool.ntp.org");

  // Chờ nhanh tối đa ~300ms trong lúc kết nối để bắt ngay gói tin NTP đầu tiên
  unsigned long start = millis();
  while (millis() - start < 350) {
    time_t now = time(nullptr);
    if (now >= 1700000000) { // Đã nhận giờ thực tế hợp lệ (năm >= 2023)
      s_timeSynced = true;
      Serial.println("🕒 [NTP] Đã nhận gói tin thời gian thực trong pha khởi động!");
      break;
    }
    delay(20);
    yield();
  }
}

void TimeManager::loop() {
  if (!s_configured) return;

  if (!s_syncLogged) {
    time_t now = time(nullptr);
    if (now >= 1700000000) { // Đã đồng bộ thành công
      s_timeSynced = true;
      s_syncLogged = true;

      struct tm ti;
      localtime_r(&now, &ti);

      Serial.printf("🕒 [TIME] Đồng bộ Real-Time thành công: %02d:%02d:%02d %02d/%02d/%04d\n",
                    ti.tm_hour, ti.tm_min, ti.tm_sec, ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
      LOG_I("TIME", "Dong bo Real-Time thanh cong: %02d:%02d:%02d %02d/%02d/%04d",
            ti.tm_hour, ti.tm_min, ti.tm_sec, ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);

      // Yêu cầu vẽ lại mặt đồng hồ chờ Standby ngay khi có giờ chuẩn
      TestDisplay::triggerStandbyRedraw();
    }
  }
}

bool TimeManager::isSynced() {
  if (s_timeSynced) return true;
  time_t now = time(nullptr);
  if (now >= 1700000000) {
    s_timeSynced = true;
    return true;
  }
  return false;
}

bool TimeManager::getDateTime(int& hour, int& minute, int& second, int& wday, int& day, int& month, int& year) {
  time_t now = time(nullptr);
  if (now >= 1700000000) {
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    hour   = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    second = timeinfo.tm_sec;
    wday   = timeinfo.tm_wday; // 0 = CN, 1 = T2 ... 6 = T7
    day    = timeinfo.tm_mday;
    month  = timeinfo.tm_mon + 1;
    year   = timeinfo.tm_year + 1900;
    s_timeSynced = true;
    return true;
  }
  return false;
}

String TimeManager::getFormattedTime() {
  time_t now = time(nullptr);
  if (now >= 1700000000) {
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    return String(buf);
  }
  return "--:--:--";
}

String TimeManager::getFormattedDate() {
  time_t now = time(nullptr);
  if (now >= 1700000000) {
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    return String(buf);
  }
  return "--/--/----";
}
