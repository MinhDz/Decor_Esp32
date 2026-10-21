#include "TimeManager.h"
#include "SystemLogger.h"
#include "tests/TestDisplay.h"
#include <WiFi.h>

static volatile bool s_timeSynced = false;
static unsigned long s_lastSyncMillis = 0;

static void timeSyncNotificationCallback(struct timeval *tv) {
  s_timeSynced = true;
  s_lastSyncMillis = millis();

  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);

  Serial.printf("🕒 [TIME] Đồng bộ NTP thành công tức thì: %02d:%02d:%02d %02d/%02d/%04d\n",
                ti.tm_hour, ti.tm_min, ti.tm_sec, ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
  LOG_I("TIME", "Dong bo Real-Time thanh cong: %02d:%02d:%02d %02d/%02d/%04d",
        ti.tm_hour, ti.tm_min, ti.tm_sec, ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);

  // Yêu cầu vẽ lại mặt đồng hồ chờ Standby ngay khi có giờ chuẩn
  TestDisplay::triggerStandbyRedraw();
}

void TimeManager::init() {
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  sntp_set_time_sync_notification_cb(timeSyncNotificationCallback);
}

bool TimeManager::syncNow(bool waitFast) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  // Khởi động SNTP với các máy chủ độ trễ thấp nhất tại Việt Nam / Châu Á
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  sntp_set_time_sync_notification_cb(timeSyncNotificationCallback);
  configTime(7 * 3600, 0, "time.google.com", "time.cloudflare.com", "asia.pool.ntp.org");

  Serial.println("🕒 [NTP] Kích hoạt truy vấn NTP: time.google.com, time.cloudflare.com, asia.pool.ntp.org");

  if (waitFast) {
    // Đợi tối đa ~400ms (8 lần x 50ms) để bắt gói tin NTP siêu nhanh
    for (int i = 0; i < 8; i++) {
      if (s_timeSynced) break;
      struct tm ti;
      if (getLocalTime(&ti, 40)) {
        if (ti.tm_year >= 124) { // Năm >= 2024
          s_timeSynced = true;
          s_lastSyncMillis = millis();
          TestDisplay::triggerStandbyRedraw();
          break;
        }
      }
      delay(50);
      yield();
    }
  }

  return s_timeSynced;
}

bool TimeManager::isSynced() {
  if (s_timeSynced) return true;
  struct tm ti;
  if (getLocalTime(&ti, 5)) {
    if (ti.tm_year >= 124) {
      s_timeSynced = true;
      return true;
    }
  }
  return false;
}

bool TimeManager::getDateTime(int& hour, int& minute, int& second, int& wday, int& day, int& month, int& year) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5)) {
    if (timeinfo.tm_year >= 124) {
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
  }
  return false;
}

String TimeManager::getFormattedTime() {
  struct tm ti;
  if (getLocalTime(&ti, 5) && ti.tm_year >= 124) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    return String(buf);
  }
  return "--:--:--";
}

String TimeManager::getFormattedDate() {
  struct tm ti;
  if (getLocalTime(&ti, 5) && ti.tm_year >= 124) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    return String(buf);
  }
  return "--/--/----";
}
