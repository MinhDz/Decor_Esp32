#include "WebAuth.h"
#include <Preferences.h>
#include <esp_system.h>
#include <esp_random.h>

static Preferences authPrefs;
static String activeSessionToken = "";
static String s_cachedUser = "admin";
static String s_cachedPass = "admin";
static bool initialized = false;

static String createRandomHex(int lenBytes = 16) {
  String out = "";
  out.reserve(lenBytes * 2);
  for (int i = 0; i < lenBytes; i++) {
    uint32_t r = esp_random();
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", (uint8_t)(r & 0xFF));
    out += buf;
  }
  return out;
}

void WebAuth::init() {
  if (initialized) return;
  authPrefs.begin("web_auth", false);
  if (!authPrefs.isKey("user")) {
    authPrefs.putString("user", "admin");
  }
  if (!authPrefs.isKey("pass")) {
    authPrefs.putString("pass", "admin");
  }
  s_cachedUser = authPrefs.getString("user", "admin");
  s_cachedPass = authPrefs.getString("pass", "admin");
  activeSessionToken = authPrefs.getString("token", "");
  if (activeSessionToken.length() < 16) {
    activeSessionToken = createRandomHex(16);
    authPrefs.putString("token", activeSessionToken);
  }
  authPrefs.end();
  initialized = true;
}

bool WebAuth::verify(const String& username, const String& password) {
  init();
  return (username == s_cachedUser && password == s_cachedPass);
}

bool WebAuth::changePassword(const String& oldPass, const String& newPass) {
  init();
  if (oldPass != s_cachedPass) return false;
  s_cachedPass = newPass;
  activeSessionToken = createRandomHex(16);
  authPrefs.begin("web_auth", false);
  authPrefs.putString("pass", newPass);
  authPrefs.putString("token", activeSessionToken);
  authPrefs.end();
  return true;
}

bool WebAuth::changeCredentials(const String& newUsername, const String& newPassword) {
  init();
  if (newUsername.length() == 0 || newPassword.length() == 0) return false;
  s_cachedUser = newUsername;
  s_cachedPass = newPassword;
  activeSessionToken = createRandomHex(16);
  authPrefs.begin("web_auth", false);
  authPrefs.putString("user", newUsername);
  authPrefs.putString("pass", newPassword);
  authPrefs.putString("token", activeSessionToken);
  authPrefs.end();
  return true;
}

String WebAuth::getUsername() {
  init();
  return s_cachedUser;
}

String WebAuth::getSessionToken() {
  init();
  return activeSessionToken;
}

String WebAuth::generateNewSessionToken() {
  init();
  activeSessionToken = createRandomHex(16);
  // Không ghi vào NVS Flash mỗi lần đăng nhập để tránh ngắt Flash Cache gây panic Core 0 khi đang phát nhạc
  return activeSessionToken;
}

bool WebAuth::isValidToken(const String& token) {
  init();
  if (token.length() < 16) return false;
  return (token == activeSessionToken);
}

void WebAuth::clearSession() {
  init();
  activeSessionToken = createRandomHex(16);
}

void WebAuth::resetToDefault() {
  authPrefs.begin("web_auth", false);
  authPrefs.clear();
  authPrefs.putString("user", "admin");
  authPrefs.putString("pass", "admin");
  activeSessionToken = createRandomHex(16);
  authPrefs.putString("token", activeSessionToken);
  authPrefs.end();
  s_cachedUser = "admin";
  s_cachedPass = "admin";
  initialized = true;
}

String WebAuth::extractToken(WebServer& server) {
  // 1. Kiểm tra header Cookie: spaceos_token=...
  if (server.hasHeader("Cookie")) {
    String cookie = server.header("Cookie");
    int idx = cookie.indexOf("spaceos_token=");
    if (idx >= 0) {
      int start = idx + 14;
      int end = cookie.indexOf(';', start);
      if (end < 0) end = cookie.length();
      String tok = cookie.substring(start, end);
      tok.trim();
      if (tok.length() > 0) return tok;
    }
  }

  // 2. Kiểm tra header X-Auth-Token
  if (server.hasHeader("X-Auth-Token")) {
    String tok = server.header("X-Auth-Token");
    tok.trim();
    if (tok.length() > 0) return tok;
  }

  // 3. Kiểm tra header Authorization: Bearer <token>
  if (server.hasHeader("Authorization")) {
    String auth = server.header("Authorization");
    if (auth.startsWith("Bearer ") || auth.startsWith("bearer ")) {
      String tok = auth.substring(7);
      tok.trim();
      if (tok.length() > 0) return tok;
    }
  }

  // 4. Kiểm tra URL query param: ?token=...
  if (server.hasArg("token")) {
    String tok = server.arg("token");
    tok.trim();
    if (tok.length() > 0) return tok;
  }

  return "";
}

bool WebAuth::isAuthenticated(WebServer& server) {
  init();
  String token = extractToken(server);
  return isValidToken(token);
}

