#pragma once
#include <Arduino.h>
#include <time.h>

class TimeManager {
public:
  // Khởi tạo trạng thái ban đầu
  static void init();

  // Kích hoạt đồng bộ NTP khi Wi-Fi đã kết nối (chỉ gọi 1 lần khi có IP)
  static void onWifiConnected();

  // Vòng lặp kiểm tra giờ thực tế (chạy an toàn trên Core 1 trong loop())
  static void loop();

  // Kiểm tra thời gian hệ thống đã có giờ thực tế hợp lệ chưa (năm >= 2024)
  static bool isSynced();

  // Lấy chi tiết ngày giờ thực tế từ RTC (nhanh < 1µs, không blocking)
  static bool getDateTime(int& hour, int& minute, int& second, int& wday, int& day, int& month, int& year);

  // Chuỗi định dạng HH:MM:SS
  static String getFormattedTime();

  // Chuỗi định dạng DD/MM/YYYY
  static String getFormattedDate();
};
