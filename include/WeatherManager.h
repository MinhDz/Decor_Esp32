#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

enum WeatherIconType {
  WEATHER_ICON_SUN = 0,          // Trời quang / Nắng
  WEATHER_ICON_PARTLY_CLOUDY,    // Nắng có mây
  WEATHER_ICON_CLOUDY,           // Nhiều mây / U ám
  WEATHER_ICON_RAIN,             // Mưa rào / Mưa phùn
  WEATHER_ICON_THUNDER,          // Dông sấm sét
  WEATHER_ICON_FOG,              // Sương mù
  WEATHER_ICON_SNOW              // Tuyết rơi
};

struct WeatherData {
  bool isValid = false;
  String cityName = "Hà Nội";
  float latitude = 21.0285f;
  float longitude = 105.8542f;
  float outdoorTemp = 28.0f;
  int outdoorHumidity = 65;
  int weatherCode = 0;          // WMO code (0..99)
  float windSpeed = 0.0f;
  String conditionText = "Trời quang";
  WeatherIconType iconType = WEATHER_ICON_SUN;
  uint32_t lastUpdateEpoch = 0;
  unsigned long lastFetchMs = 0;
};

class WeatherManager {
public:
  static void init();
  static void loop();
  static bool refreshNow();

  // Trạng thái và dữ liệu thời tiết thực
  static bool isAvailable();
  static const WeatherData& getData();
  static String getCityName();
  static float getOutdoorTemp();
  static int getOutdoorHumidity();
  static int getWeatherCode();
  static String getConditionText();
  static WeatherIconType getIconType();
  static const char* getIconTypeString();

  // Vẽ Icon thời tiết đồ họa sắc nét (vector graphic) trực tiếp lên màn hình ST7789
  static void drawWeatherIcon(Adafruit_GFX* gfx, int x, int y, WeatherIconType icon, int size = 16);
};
