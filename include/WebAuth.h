#pragma once
#include <Arduino.h>
#include <WebServer.h>

class WebAuth {
public:
  // Khởi tạo thông tin xác thực từ NVS Preferences ("web_auth")
  static void init();

  // Xác thực username và password
  static bool verify(const String& username, const String& password);

  // Đổi mật khẩu tài khoản
  static bool changePassword(const String& oldPass, const String& newPass);

  // Đổi cả username và mật khẩu
  static bool changeCredentials(const String& newUsername, const String& newPassword);

  // Lấy username hiện tại
  static String getUsername();

  // Lấy hoặc tạo mới token phiên làm việc hiện tại
  static String getSessionToken();
  static String generateNewSessionToken();

  // Kiểm tra token có hợp lệ hay không
  static bool isValidToken(const String& token);

  // Hủy phiên làm việc hiện tại (đăng xuất)
  static void clearSession();

  // Khôi phục tài khoản về mặc định (admin / admin)
  static void resetToDefault();

  // Trích xuất token từ HTTP Request (Cookie, Header X-Auth-Token, Bearer hoặc Query param)
  static String extractToken(WebServer& server);

  // Kiểm tra request đã xác thực hay chưa
  static bool isAuthenticated(WebServer& server);
};

