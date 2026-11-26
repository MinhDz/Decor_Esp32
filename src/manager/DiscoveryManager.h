#pragma once
#include <Arduino.h>

class DiscoveryManager {
public:
  static void init();
  static void loop();
  static void sendBeaconNow();
};
