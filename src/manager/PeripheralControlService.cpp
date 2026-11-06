#include "services/PeripheralControlService.h"
#include "services/PowerManagementService.h"

#include "drivers/CobLedDriver.h"
#include "drivers/DisplayDriver.h"
#include "drivers/MotorDriver.h"
#include "drivers/AudioDriver.h"
#include "drivers/SensorDriver.h"
#include "tests/TestDisplay.h"
#include "SystemLogger.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

namespace PeripheralControlService {

PeripheralControlResult setWindmill(int mode, int speedPct) {
  if (mode < 0 || mode > 4 || speedPct < 0 || speedPct > 100) {
    return {false, "Chế độ hoặc tốc độ cối xay không hợp lệ."};
  }
  MotorDriver::setMode((uint8_t)mode);
  MotorDriver::setMotorSpeed((uint8_t)speedPct);
  if (mode == 0 || speedPct == 0) {
    return {true, "Đã tắt motor cối xay gió."};
  }
  return {true, "Đã đặt cối xay gió ở tốc độ " + String(speedPct) + "% (" + MotorDriver::getModeName() + ")."};
}

PeripheralControlResult setEdisonLights(int side, bool enabled, int brightnessPct, bool breathing) {
  if (side < 0 || side > 2 || brightnessPct < 0 || brightnessPct > 100) {
    return {false, "Vị trí hoặc độ sáng đèn Edison không hợp lệ."};
  }
  uint8_t brightness = enabled ? (uint8_t)brightnessPct : 0;
  if (side == 1) MotorDriver::setEdisonLeftBrightness(brightness, enabled && breathing);
  else if (side == 2) MotorDriver::setEdisonRightBrightness(brightness, enabled && breathing);
  else MotorDriver::setEdisonBrightness(brightness);

  return {true, String(enabled ? "Đã đặt đèn Edison " : "Đã tắt đèn Edison ") +
                (side == 1 ? "bên trái" : side == 2 ? "bên phải" : "hai bên") +
                (enabled ? " ở mức " + String(brightnessPct) + "%" : ".")};
}

PeripheralControlResult setCobLeds(bool enabled, bool nextMode) {
  if (nextMode) {
    CobLeds::nextMode();
    return {true, "Đã chuyển hiệu ứng đèn COB."};
  }
  if (enabled) {
    CobLeds::turnOn();
    MotorDriver::setFlowingLeds(true);
    return {true, "Đã bật đèn COB."};
  }
  CobLeds::turnOff();
  MotorDriver::setFlowingLeds(false);
  return {true, "Đã tắt đèn COB."};
}

PeripheralControlResult getTelemetry() {
  float temp = SensorDriver::getTemperatureC();
  float hum = SensorDriver::getHumidityPct();
  int bat = SensorDriver::getBatteryPercentage();
  float vbat = SensorDriver::getBatteryVoltage();
  bool charging = SensorDriver::isCharging();
  uint8_t speed = MotorDriver::getMotorSpeed();
  String message = "Nhiệt độ phòng: " + String(temp, 1) + "°C, độ ẩm: " + String(hum, 0) +
                   "%, pin: " + String(bat) + "% (" + String(vbat, 2) + "V" +
                   (charging ? ", đang sạc" : "") + "), tốc độ cối xay: " + String(speed) + "%.";
  return {true, message};
}

PeripheralControlResult switchScreen(const String& requestedMode) {
  String mode = requestedMode;
  mode.toLowerCase();
  if (mode == "standby" || mode == "home" || mode == "cho") {
    TestDisplay::setScreenMode("standby");
    return {true, "Đã chuyển về màn hình chờ."};
  }
  if (mode == "pc_status" || mode == "pc" || mode == "hud") {
    TestDisplay::setScreenMode("pchud");
    return {true, "Đã chuyển sang màn hình PC HUD."};
  }
  if (mode == "emoji" || mode == "ai") {
    TestDisplay::setScreenMode("emoji");
    return {true, "Đã chuyển sang màn hình biểu cảm."};
  }
  if (mode == "diag" || mode == "sensors" || mode == "song_am") {
    TestDisplay::setScreenMode("diag");
    return {true, "Đã chuyển sang màn hình chẩn đoán."};
  }
  return {false, "Chế độ màn hình không được hỗ trợ: " + requestedMode};
}

PeripheralControlResult setAudioVolume(int volumePct) {
  if (volumePct < 0 || volumePct > 100) return {false, "Âm lượng phải nằm trong khoảng 0 đến 100%."};
  AudioDriver::setSpeakerVolumePct(volumePct);
  AudioDriver::setXiaoZhiVolumePct(volumePct);
  return {true, "Đã đặt âm lượng loa: " + String(volumePct) + "%."};
}

PeripheralControlResult setEmojiStyle(const String& requestedStyle) {
  String style = requestedStyle;
  style.toLowerCase();
  if (style == "neon" || style == "default") {
    TestDisplay::setEmojiStyle(0);
    return {true, "Đã chọn phong cách Mắt Neon."};
  }
  if (style == "cute_girl" || style == "chibi" || style == "girl" || style == "co_be") {
    TestDisplay::setEmojiStyle(1);
    return {true, "Đã chọn phong cách Cô Bé Chibi."};
  }
  if (style == "doi_mat" || style == "robot" || style == "eyes" || style == "dong") {
    TestDisplay::setEmojiStyle(2);
    return {true, "Đã chọn phong cách Đôi Mắt Robot."};
  }
  if (style == "gome" || style == "meo" || style == "cat") {
    TestDisplay::setEmojiStyle(3);
    return {true, "Đã chọn phong cách Mèo Gome."};
  }
  return {false, "Phong cách biểu cảm không được hỗ trợ: " + requestedStyle};
}

PeripheralControlResult setPowerProfile(const String& profileStr, bool autoSwitch) {
  String p = profileStr;
  p.toLowerCase();
  PowerProfile prof = POWER_BALANCED;
  if (p == "performance" || p == "hieu_nang" || p == "max" || p == "0") prof = POWER_PERFORMANCE;
  else if (p == "saver" || p == "battery_saver" || p == "tiet_kiem" || p == "save" || p == "2") prof = POWER_SAVER;
  else if (p == "balanced" || p == "can_bang" || p == "1") prof = POWER_BALANCED;
  else return {false, "Profile nguồn phải là: performance, balanced hoặc saver."};

  PowerManagementService::setProfile(prof, true);
  if (autoSwitch) {
    PowerManagementService::setAutoSwitch(true);
  }
  return {true, "Đã chuyển sang cấu hình nguồn " + String(PowerManagementService::getProfileName()) +
                (autoSwitch ? " (Tự động chuyển: BẬT)" : " (Cố định profile)")};
}

PeripheralControlResult getPowerStatus() {
  const char* prof = PowerManagementService::getProfileName();
  uint32_t cpu = getCpuFrequencyMhz();
  int bat = SensorDriver::getBatteryPercentage();
  float vbat = SensorDriver::getBatteryVoltage();
  bool chg = SensorDriver::isCharging();
  bool full = PowerManagementService::isBatteryFullyCharged();
  bool autoSw = PowerManagementService::isAutoSwitchEnabled();

  String msg = "Cấu hình nguồn: " + String(prof) + " (CPU " + String(cpu) + "MHz), Pin: " +
               String(bat) + "% (" + String(vbat, 2) + "V), " +
               (chg ? (full ? "Đã sạc đầy 100% bão hòa" : "Đang cắm sạc") : "Đang chạy pin") +
               ", Tự động chuyển: " + (autoSw ? "BẬT" : "TẮT") + ".";
  return {true, msg};
}

static String s_cachedNewsResult = "";
static unsigned long s_cachedNewsMs = 0;
static const unsigned long NEWS_CACHE_TTL_MS = 15 * 60 * 1000UL; // 15 phut

PeripheralControlResult getDailyNews(int limit) {
  unsigned long now = millis();
  // Neu da co ban tin cache trong vong 15 phut va noi dung hop le, tra ve ngay tuc thi (0ms latency)!
  if (s_cachedNewsResult.length() > 0 && (now - s_cachedNewsMs < NEWS_CACHE_TTL_MS)) {
    SystemLogger::log(LOG_LVL_INFO, "NEWS", "Tra ve ban tin tu RAM cache (TTL con lai: %lu giay)",
                      (unsigned long)((NEWS_CACHE_TTL_MS - (now - s_cachedNewsMs)) / 1000UL));
    return {true, s_cachedNewsResult};
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (s_cachedNewsResult.length() > 0) {
      return {true, s_cachedNewsResult + "\n(Lưu ý: Thiết bị đang mất mạng, đây là bản tin đã lưu trước đó)."};
    }
    return {false, "Thiết bị chưa kết nối Wi-Fi, không thể lấy tin tức."};
  }

  SystemLogger::log(LOG_LVL_INFO, "NEWS", "Dang tai tin tuc tu Cloudflare Worker...");

  String payload = "";
  int httpCode = -1;

  // 1. Uu tien dung HTTP cong 80 (WiFiClient sieu nhe, chi ton ~1KB RAM, khong chiem heap SSL cua XiaoZhi)
  {
    WiFiClient client;
    client.setTimeout(5000);
    HTTPClient http;
    http.setTimeout(5000);
    const char* newsUrlHttp = "http://daily-news-feed.chicanemthoi96.workers.dev/";
    if (http.begin(client, newsUrlHttp)) {
      http.addHeader("User-Agent", "ESP32-S3-DecorStation/1.0");
      httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        payload = http.getString();
      }
      http.end();
    }
  }

  // 2. Neu HTTP that bai, fallback sang HTTPS (WiFiClientSecure) voi bo dem TLS thu nho
  if (httpCode != HTTP_CODE_OK || payload.length() == 0) {
    SystemLogger::log(LOG_LVL_WARN, "NEWS", "HTTP GET port 80 failed (%d). Thu lai qua HTTPS...", httpCode);
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    secureClient.setTimeout(5000);

    HTTPClient http;
    http.setTimeout(5000);
    const char* newsUrlHttps = "https://daily-news-feed.chicanemthoi96.workers.dev/";
    if (http.begin(secureClient, newsUrlHttps)) {
      http.addHeader("User-Agent", "ESP32-S3-DecorStation/1.0");
      httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        payload = http.getString();
      } else {
        SystemLogger::log(LOG_LVL_WARN, "NEWS", "HTTPS GET that bai, ma: %d", httpCode);
      }
      http.end();
    }
  }

  // 3. Neu ca 2 deu khong lay duoc payload nhung co cache cu, su dung cache cu thay vi bao loi
  if (payload.length() == 0) {
    if (s_cachedNewsResult.length() > 0) {
      SystemLogger::log(LOG_LVL_WARN, "NEWS", "Khong the lay tin moi, su dung ban tin cache cu");
      return {true, s_cachedNewsResult};
    }
    return {false, "Lấy tin tức thất bại (mã phản hồi: " + String(httpCode) + ")."};
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    SystemLogger::log(LOG_LVL_ERROR, "NEWS", "deserializeJson that bai: %s", error.c_str());
    if (s_cachedNewsResult.length() > 0) return {true, s_cachedNewsResult};
    return {false, "Không thể giải mã dữ liệu tin tức JSON."};
  }

  JsonArrayConst newsArr = doc["news"].as<JsonArrayConst>();
  if (newsArr.isNull() || newsArr.size() == 0) {
    if (s_cachedNewsResult.length() > 0) return {true, s_cachedNewsResult};
    return {false, "Hiện tại không có tin tức mới."};
  }

  int maxCount = (limit > 0 && limit <= 5) ? limit : 5;
  int count = 0;
  String result = "Bản tin tức hôm nay gồm các tin nổi bật sau:\n";

  for (JsonObjectConst item : newsArr) {
    const char* title = item["title"] | "";
    const char* summary = item["summary"] | "";
    if (strlen(title) == 0) continue;

    count++;
    result += String(count) + ". " + title;
    if (strlen(summary) > 0) {
      result += ": " + String(summary);
    }
    result += "\n";

    if (count >= maxCount) break;
  }

  if (count == 0) {
    if (s_cachedNewsResult.length() > 0) return {true, s_cachedNewsResult};
    return {false, "Không tìm thấy nội dung tin tức hợp lệ."};
  }

  // Luu vao cache
  s_cachedNewsResult = result;
  s_cachedNewsMs = millis();

  SystemLogger::log(LOG_LVL_INFO, "NEWS", "Da lay thanh cong %d ban tin moi va cap nhat cache", count);
  return {true, result};
}

} // namespace PeripheralControlService
