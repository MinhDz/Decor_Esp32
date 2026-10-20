#include "TaskProfiler.h"
#include <esp_timer.h>

static TaskStats s_taskStats[PROF_TASK_COUNT] = {
  { "Modules",    0, 0, 0, 0 },
  { "CobLeds",    0, 0, 0, 0 },
  { "Telemetry",  0, 0, 0, 0 },
  { "Weather",    0, 0, 0, 0 },
  { "WiFi",       0, 0, 0, 0 },
  { "WebRoutes",  0, 0, 0, 0 },
  { "WebStream",  0, 0, 0, 0 },
  { "XiaoZhi",    0, 0, 0, 0 },
  { "Hardware",   0, 0, 0, 0 }
};

static int64_t s_taskStartUs[PROF_TASK_COUNT] = {0};
static int64_t s_loopStartUs = 0;
static uint32_t s_loopLastUs = 0;
static uint32_t s_loopAvgUs = 0;
static uint32_t s_loopMaxUs = 0;

void TaskProfiler::init() {
  resetMax();
}

void TaskProfiler::beginLoop() {
  s_loopStartUs = esp_timer_get_time();
}

void TaskProfiler::endLoop() {
  int64_t now = esp_timer_get_time();
  uint32_t dur = (uint32_t)(now - s_loopStartUs);
  s_loopLastUs = dur;
  if (s_loopAvgUs == 0) s_loopAvgUs = dur;
  else s_loopAvgUs = (s_loopAvgUs * 31 + dur) / 32;
  if (dur > s_loopMaxUs) s_loopMaxUs = dur;
}

void TaskProfiler::start(ProfiledTask task) {
  if (task < PROF_TASK_COUNT) {
    s_taskStartUs[task] = esp_timer_get_time();
  }
}

void TaskProfiler::stop(ProfiledTask task) {
  if (task < PROF_TASK_COUNT) {
    int64_t now = esp_timer_get_time();
    uint32_t dur = (uint32_t)(now - s_taskStartUs[task]);
    TaskStats& ts = s_taskStats[task];
    ts.lastUs = dur;
    if (ts.avgUs == 0) ts.avgUs = dur;
    else ts.avgUs = (ts.avgUs * 15 + dur) / 16;
    if (dur > ts.maxUs) ts.maxUs = dur;
    if (dur > 10000) { // Vượt ngưỡng 10ms
      ts.slowCount++;
    }
  }
}

uint32_t TaskProfiler::getLoopLastUs() {
  return s_loopLastUs;
}

uint32_t TaskProfiler::getLoopAvgUs() {
  return s_loopAvgUs;
}

uint32_t TaskProfiler::getLoopMaxUs() {
  return s_loopMaxUs;
}

float TaskProfiler::getFps() {
  if (s_loopAvgUs == 0) return 60.0f;
  return 1000000.0f / (float)s_loopAvgUs;
}

const TaskStats& TaskProfiler::getStats(ProfiledTask task) {
  if (task < PROF_TASK_COUNT) {
    return s_taskStats[task];
  }
  return s_taskStats[0];
}

void TaskProfiler::resetMax() {
  s_loopMaxUs = 0;
  for (int i = 0; i < PROF_TASK_COUNT; i++) {
    s_taskStats[i].maxUs = 0;
    s_taskStats[i].slowCount = 0;
  }
}

