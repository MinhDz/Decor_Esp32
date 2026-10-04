#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

class WifiManager {
public:
  static void begin();
  static bool smartConnect();
  static bool connectDirect(const String& ssid, const String& password, bool save);
  static void scanNetworks(JsonDocument& resDoc);
  
  static void loadSavedWifiList(JsonDocument& doc);
  static void saveSavedWifiList(const JsonDocument& doc);
  static bool saveOrUpdateWifiNetwork(const String& ssid, const String& password);
  static bool deleteSavedWifiNetwork(const String& ssid);

  static bool isConnected();
  static bool isApActive();
  static void loop();
  static String getCurrentSsid();
  static int getRssi();
  static String getIp();
  static String getLastNotification();
  static void setLastNotification(const String& msg);
};

