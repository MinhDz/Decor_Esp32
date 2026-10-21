#pragma once
#include <Arduino.h>

enum ProfiledTask {
  PROF_MODULES = 0,
  PROF_COB_LEDS,
  PROF_TELEMETRY,
  PROF_WEATHER,
  PROF_WIFI,
  PROF_WEB_ROUTES,
  PROF_WEB_STREAM,
  PROF_XIAOZHI,
  PROF_HARDWARE,
  PROF_LOGGER,
  PROF_TASK_COUNT
};

struct TaskStats {
  const char* name;
  uint32_t lastUs;
  uint32_t avgUs;
  uint32_t maxUs;
  uint32_t slowCount; // Số lần thực thi > 10ms (10,000 µs)
};

class TaskProfiler {
public:
  static void init();
  static void beginLoop();
  static void endLoop();
  static void start(ProfiledTask task);
  static void stop(ProfiledTask task);

  static uint32_t getLoopLastUs();
  static uint32_t getLoopAvgUs();
  static uint32_t getLoopMaxUs();
  static float getFps();

  static const TaskStats& getStats(ProfiledTask task);
  static void resetMax();
};

