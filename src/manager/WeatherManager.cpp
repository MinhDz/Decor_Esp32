#include "WeatherManager.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "tests/TestDisplay.h"
#include "SystemLogger.h"

static WeatherData s_weatherData;
static bool s_isFetching = false;
static unsigned long s_lastFetchAttemptMs = 0;
static const unsigned long WEATHER_FETCH_INTERVAL = 20 * 60 * 1000; // 20 phút cập nhật 1 lần
static const unsigned long WEATHER_RETRY_INTERVAL = 60 * 1000;      // 1 phút thử lại nếu lỗi

static WeatherIconType mapWmoCodeToIcon(int code) {
  if (code == 0 || code == 1) return WEATHER_ICON_SUN;
  if (code == 2)              return WEATHER_ICON_PARTLY_CLOUDY;
  if (code == 3)              return WEATHER_ICON_CLOUDY;
  if (code == 45 || code == 48) return WEATHER_ICON_FOG;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WEATHER_ICON_RAIN;
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return WEATHER_ICON_SNOW;
  if (code >= 95)             return WEATHER_ICON_THUNDER;
  return WEATHER_ICON_CLOUDY;
}

static String mapWmoCodeToText(int code) {
  switch (code) {
    case 0:  return "Trời quang";
    case 1:  return "Nắng nhẹ";
    case 2:  return "Nắng có mây";
    case 3:  return "Nhiều mây";
    case 45: return "Sương mù";
    case 48: return "Sương muối";
    case 51: return "Mưa phùn nhẹ";
    case 53: return "Mưa phùn";
    case 55: return "Mưa phùn dày";
    case 61: return "Mưa nhỏ";
    case 63: return "Mưa vừa";
    case 65: return "Mưa to";
    case 80: return "Mưa rào nhẹ";
    case 81: return "Mưa rào vừa";
    case 82: return "Mưa rào to";
    case 71: case 73: case 75: return "Tuyết rơi";
    case 95: return "Dông sấm sét";
    case 96: case 99: return "Dông mưa đá";
    default: return (code >= 50 && code < 70) ? "Có mưa" : "Nhiều mây";
  }
}

static void doFetchWeatherTask(void* arg) {
  s_isFetching = true;
  WiFiClient client;
  HTTPClient http;

  // Bước 1: Lấy thông tin vị trí địa lý theo IP (IP Geolocation) nếu chưa có tọa độ
  static bool s_hasGeoLocation = false;
  if (!s_hasGeoLocation) {
    http.begin(client, "http://ip-api.com/json/?fields=city,lat,lon,status");
    http.setTimeout(3500);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      JsonDocument doc;
      if (!deserializeJson(doc, payload)) {
        if (doc["status"] == "success") {
          String c = doc["city"] | "";
          if (c.length() > 0) s_weatherData.cityName = c;
          s_weatherData.latitude  = doc["lat"] | 21.0285f;
          s_weatherData.longitude = doc["lon"] | 105.8542f;
          s_hasGeoLocation = true;
          Serial.printf("📍 [WEATHER GEO] Nhận diện vị trí: %s (Lat: %.4f, Lon: %.4f)\n",
                        s_weatherData.cityName.c_str(), s_weatherData.latitude, s_weatherData.longitude);
        }
      }
    }
    http.end();
  }

  // Bước 2: Truy vấn dữ liệu thời tiết thực tế từ Open-Meteo API
  char urlBuf[220];
  snprintf(urlBuf, sizeof(urlBuf),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&timezone=auto",
           s_weatherData.latitude, s_weatherData.longitude);

  http.begin(client, urlBuf);
  http.setTimeout(4000);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err && doc.containsKey("current")) {
      JsonObject cur = doc["current"];
      s_weatherData.outdoorTemp     = cur["temperature_2m"] | 28.0f;
      s_weatherData.outdoorHumidity = cur["relative_humidity_2m"] | 65;
      s_weatherData.weatherCode     = cur["weather_code"] | 0;
      s_weatherData.windSpeed       = cur["wind_speed_10m"] | 0.0f;
      s_weatherData.conditionText   = mapWmoCodeToText(s_weatherData.weatherCode);
      s_weatherData.iconType        = mapWmoCodeToIcon(s_weatherData.weatherCode);
      s_weatherData.isValid         = true;
      s_weatherData.lastFetchMs     = millis();
      s_weatherData.lastUpdateEpoch = (uint32_t)time(nullptr);

      Serial.printf("🌤️ [WEATHER API] Cập nhật thành công: %s | %.1f°C | %d%% | %s (Mã: %d, Gió: %.1f km/h)\n",
                    s_weatherData.cityName.c_str(), s_weatherData.outdoorTemp,
                    s_weatherData.outdoorHumidity, s_weatherData.conditionText.c_str(),
                    s_weatherData.weatherCode, s_weatherData.windSpeed);
      LOG_I("WEATHER", "Cap nhat Open-Meteo thanh cong: %s | %.1fC | %d%% | %s",
            s_weatherData.cityName.c_str(), s_weatherData.outdoorTemp,
            s_weatherData.outdoorHumidity, s_weatherData.conditionText.c_str());

      // Kích hoạt vẽ lại màn hình chờ Standby nếu đang hiển thị
      TestDisplay::triggerStandbyRedraw();
    } else {
      Serial.println("⚠️ [WEATHER API] Lỗi phân tích JSON từ Open-Meteo!");
      LOG_W("WEATHER", "Loi phan tich JSON tu Open-Meteo");
    }
  } else {
    Serial.printf("❌ [WEATHER API] Lỗi kết nối Open-Meteo HTTP: %d\n", httpCode);
    LOG_W("WEATHER", "Loi ket noi Open-Meteo HTTP: %d", httpCode);
  }
  http.end();

  s_isFetching = false;
  vTaskDelete(NULL);
}

void WeatherManager::init() {
  s_weatherData.cityName = "Hà Nội";
  s_weatherData.latitude = 21.0285f;
  s_weatherData.longitude = 105.8542f;
  s_weatherData.outdoorTemp = 28.0f;
  s_weatherData.outdoorHumidity = 65;
  s_weatherData.weatherCode = 0;
  s_weatherData.conditionText = "Trời quang";
  s_weatherData.iconType = WEATHER_ICON_SUN;
  s_weatherData.isValid = false;
  s_weatherData.lastFetchMs = 0;
  s_lastFetchAttemptMs = 0;
}

void WeatherManager::loop() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (s_isFetching) return;

  unsigned long now = millis();
  unsigned long interval = s_weatherData.isValid ? WEATHER_FETCH_INTERVAL : WEATHER_RETRY_INTERVAL;

  if (s_weatherData.lastFetchMs == 0 || (now - s_lastFetchAttemptMs >= interval)) {
    refreshNow();
  }
}

bool WeatherManager::refreshNow() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ [WEATHER] Wi-Fi chưa kết nối, không thể cập nhật thời tiết!");
    return false;
  }
  if (s_isFetching) return false;

  s_lastFetchAttemptMs = millis();
  // Khởi chạy tác vụ nhẹ trên Core 0 để hoàn toàn không chặn giao diện hiển thị & âm thanh trên Core 1
  BaseType_t res = xTaskCreatePinnedToCore(
    doFetchWeatherTask,
    "WeatherTask",
    6144,
    NULL,
    1,
    NULL,
    0
  );
  return (res == pdPASS);
}

bool WeatherManager::isAvailable() {
  return s_weatherData.isValid;
}

const WeatherData& WeatherManager::getData() {
  return s_weatherData;
}

String WeatherManager::getCityName() {
  return s_weatherData.cityName;
}

float WeatherManager::getOutdoorTemp() {
  return s_weatherData.outdoorTemp;
}

int WeatherManager::getOutdoorHumidity() {
  return s_weatherData.outdoorHumidity;
}

int WeatherManager::getWeatherCode() {
  return s_weatherData.weatherCode;
}

String WeatherManager::getConditionText() {
  return s_weatherData.conditionText;
}

WeatherIconType WeatherManager::getIconType() {
  return s_weatherData.iconType;
}

const char* WeatherManager::getIconTypeString() {
  switch (s_weatherData.iconType) {
    case WEATHER_ICON_SUN:           return "sun";
    case WEATHER_ICON_PARTLY_CLOUDY: return "partly_cloudy";
    case WEATHER_ICON_CLOUDY:        return "cloudy";
    case WEATHER_ICON_RAIN:          return "rain";
    case WEATHER_ICON_THUNDER:       return "thunder";
    case WEATHER_ICON_FOG:           return "fog";
    case WEATHER_ICON_SNOW:          return "snow";
    default:                         return "sun";
  }
}

// ============================================================================
// VẼ ICON THỜI TIẾT ĐỒ HỌA SẮC NÉT TRỰC TIẾP LÊN MÀN HÌNH ST7789 (VECTOR GFX)
// ============================================================================
void WeatherManager::drawWeatherIcon(Adafruit_GFX* gfx, int x, int y, WeatherIconType icon, int size) {
  if (!gfx) return;
  if (size < 12) size = 12;

  const uint16_t C_GOLD        = 0xFDE0; // Vàng ánh kim ☀️
  const uint16_t C_WHITE       = 0xFFFF; // Trắng tinh
  const uint16_t C_CYAN        = 0x07FF; // Xanh Cyan 🌧️
  const uint16_t C_CLOUD       = 0xD77E; // Xanh bạc ánh mây ☁️
  const uint16_t C_CLOUD_DARK  = 0x7BEF; // Mây xám dông
  const uint16_t C_LIGHTNING   = 0xFFE0; // Tia sét vàng neon ⚡
  const uint16_t C_FOG         = 0xAD75; // Sương bạc 🌫️

  int half = size / 2;
  int cx = x + half;
  int cy = y + half;

  switch (icon) {
    case WEATHER_ICON_SUN: {
      int r = size / 4;
      gfx->fillCircle(cx, cy, r, C_GOLD);
      // 4 tia nắng chính
      gfx->drawFastVLine(cx, y + 1, 2, C_GOLD);
      gfx->drawFastVLine(cx, y + size - 3, 2, C_GOLD);
      gfx->drawFastHLine(x + 1, cy, 2, C_GOLD);
      gfx->drawFastHLine(x + size - 3, cy, 2, C_GOLD);
      // 4 chấm tia chéo
      gfx->drawPixel(cx - r - 1, cy - r - 1, C_GOLD);
      gfx->drawPixel(cx + r + 1, cy - r - 1, C_GOLD);
      gfx->drawPixel(cx - r - 1, cy + r + 1, C_GOLD);
      gfx->drawPixel(cx + r + 1, cy + r + 1, C_GOLD);
      break;
    }

    case WEATHER_ICON_PARTLY_CLOUDY: {
      // Mặt trời ló dạng góc trên bên phải
      gfx->fillCircle(x + size - 4, y + 4, 3, C_GOLD);
      gfx->drawPixel(x + size - 2, y + 1, C_GOLD);
      // Đám mây che phía trước
      gfx->fillCircle(x + 5, y + size - 5, 3, C_CLOUD);
      gfx->fillCircle(x + 9, y + size - 7, 4, C_WHITE);
      gfx->fillRoundRect(x + 2, y + size - 5, size - 4, 4, 2, C_CLOUD);
      break;
    }

    case WEATHER_ICON_CLOUDY: {
      // Cụm mây đôi bồng bềnh
      gfx->fillCircle(x + 4, cy, 3, C_CLOUD);
      gfx->fillCircle(x + 8, cy - 2, 4, C_WHITE);
      gfx->fillCircle(x + 12, cy, 3, C_CLOUD);
      gfx->fillRoundRect(x + 2, cy, size - 4, 4, 2, C_CLOUD);
      break;
    }

    case WEATHER_ICON_RAIN: {
      // Đám mây ở nửa trên
      gfx->fillCircle(x + 4, y + 5, 3, C_CLOUD);
      gfx->fillCircle(x + 8, y + 3, 4, C_WHITE);
      gfx->fillCircle(x + 12, y + 5, 3, C_CLOUD);
      gfx->fillRoundRect(x + 2, y + 5, size - 4, 4, 2, C_CLOUD);
      // 3 vệt mưa nghiêng màu xanh Cyan
      gfx->drawLine(x + 4, y + size - 4, x + 3, y + size - 1, C_CYAN);
      gfx->drawLine(x + 8, y + size - 5, x + 7, y + size - 2, C_CYAN);
      gfx->drawLine(x + 12, y + size - 4, x + 11, y + size - 1, C_CYAN);
      break;
    }

    case WEATHER_ICON_THUNDER: {
      // Đám mây dông đen bạc
      gfx->fillCircle(x + 4, y + 4, 3, C_CLOUD_DARK);
      gfx->fillCircle(x + 8, y + 2, 4, C_CLOUD);
      gfx->fillCircle(x + 12, y + 4, 3, C_CLOUD_DARK);
      gfx->fillRoundRect(x + 2, y + 4, size - 4, 4, 2, C_CLOUD_DARK);
      // Tia sét vàng neon zíc-zắc
      gfx->drawLine(x + 8, y + 7, x + 6, y + 10, C_LIGHTNING);
      gfx->drawLine(x + 6, y + 10, x + 9, y + 10, C_LIGHTNING);
      gfx->drawLine(x + 9, y + 10, x + 7, y + size - 1, C_LIGHTNING);
      break;
    }

    case WEATHER_ICON_FOG: {
      // 3 dải sương mù ngang lượn nhẹ
      gfx->fillRoundRect(x + 1, y + 3, size - 2, 2, 1, C_FOG);
      gfx->fillRoundRect(x + 3, y + 7, size - 6, 2, 1, C_WHITE);
      gfx->fillRoundRect(x + 2, y + 11, size - 4, 2, 1, C_FOG);
      break;
    }

    case WEATHER_ICON_SNOW: {
      // Bông tuyết 6 cánh trắng
      gfx->drawFastVLine(cx, y + 2, size - 4, C_WHITE);
      gfx->drawFastHLine(x + 2, cy, size - 4, C_WHITE);
      gfx->drawPixel(cx - 3, cy - 3, C_WHITE);
      gfx->drawPixel(cx + 3, cy - 3, C_WHITE);
      gfx->drawPixel(cx - 3, cy + 3, C_WHITE);
      gfx->drawPixel(cx + 3, cy + 3, C_WHITE);
      break;
    }
  }
}

