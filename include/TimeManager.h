#pragma once
#include <Arduino.h>
#include <time.h>
#include <esp_sntp.h>

class TimeManager {
public:
  // Khởi tạo SNTP client với chế độ đồng bộ tức thì SNTP_SYNC_MODE_IMMED
  static void init();

  // Kích hoạt đồng bộ NTP với các server siêu tốc (Google, Cloudflare, Asia Pool)
  // Nếu waitFast = true, đợi tối đa ~400ms để bắt gói tin NTP ngay trong lúc khởi động
  static bool syncNow(bool waitFast = true);

  // Kiểm tra hệ thống đã có thời gian thực chuẩn từ Internet chưa
  static bool isSynced();

  // Lấy chi tiết ngày giờ thực tế
  static bool getDateTime(int& hour, int& minute, int& second, int& wday, int& day, int& month, int& year);

  // Chuỗi định dạng HH:MM:SS
  static String getFormattedTime();

  // Chuỗi định dạng DD/MM/YYYY
  static String getFormattedDate();
};
