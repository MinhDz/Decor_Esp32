#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct CpuMetrics {
  String name = "Intel Core";
  String shortName = "CPU";
  float usage = 0.0f;
  float temp = 0.0f;
  float freq = 0.0f;
  int cores = 0;
};

struct GpuMetrics {
  String name = "Intel HD Graphics";
  String shortName = "GPU";
  float usage = 0.0f;
  float temp = 0.0f;
  float vramUsed = 0.0f;
  float vramTotal = 0.0f;
  int power = 0;
};

struct RamMetrics {
  float usage = 0.0f;
  float usedGb = 0.0f;
  float totalGb = 0.0f;
};

struct DiskMetrics {
  float usage = 0.0f;
  float readSpeed = 0.0f;
  float writeSpeed = 0.0f;
};

struct NetMetrics {
  float dlSpeedKb = 0.0f;
  float ulSpeedKb = 0.0f;
};

struct PcMetrics {
  CpuMetrics cpu;
  GpuMetrics gpu;
  RamMetrics ram;
  DiskMetrics disk;
  NetMetrics net;
  int fanRpm = 0;
  float voltage = 1.25f;
  uint32_t lastUpdateMillis = 0;
};

class PcStatsManager {
public:
  static void init();
  static bool updateMetrics(const String& jsonString);
  static void serializeMetrics(JsonDocument& doc);
  static bool isLive();
  static const PcMetrics& getMetrics();
};

