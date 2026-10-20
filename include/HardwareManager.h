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
  static uint32_t getMinFreeHeapKb();
  static float getFreePsramMb();
  static float getMinFreePsramMb();
  static uint32_t getMaxAllocHeapKb();
  static uint32_t getMaxAllocPsramKb();
  static float getFsUsedMb();
  static float getFsTotalMb();
  static void recordAllocFailure();
  static uint32_t getAllocFailures();
};

