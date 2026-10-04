#pragma once
#include <Arduino.h>

class HardwareManager {
public:
  static void init();
  static void setLedColor(uint8_t r, uint8_t g, uint8_t b);
  static void updateLedStatus();
  static void heartbeat();
  static void handleSerialCommands();

  static uint32_t getFreeHeapKb();
  static float getFreePsramMb();
  static float getFsUsedMb();
  static float getFsTotalMb();
};

