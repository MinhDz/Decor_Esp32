#include "MemoryScavenger.h"
#include "tests/TestAudio.h"
#include "tests/TestDisplay.h"
#include "WebStreamManager.h"
#include "WebRoutes.h"
#include "XiaoZhiClient.h"
#include "SystemLogger.h"
#include <Preferences.h>

namespace {
  static StrictModeLevel s_strictMode = STRICT_MODE_BALANCED;
  static bool s_initialized = false;
  static Preferences s_prefs;

  static unsigned long s_lastAudioActivity   = 0;
  static unsigned long s_lastStreamActivity  = 0;
  static unsigned long s_lastXiaoZhiActivity = 0;
  static unsigned long s_lastUploadActivity  = 0;

  static unsigned long s_lastPeriodicCheckMs = 0;
  static ScavengeReport s_lastReport = {0, 0, 0, 0, "NONE"};
  static uint32_t s_totalScavengeRuns = 0;
  static uint32_t s_cumulativeFreedKb  = 0;
}

void MemoryScavenger::init() {
  if (s_initialized) return;
  s_initialized = true;

  if (s_prefs.begin("scavenger", false)) {
    uint8_t mode = s_prefs.getUChar("strict", (uint8_t)STRICT_MODE_BALANCED);
    if (mode > STRICT_MODE_AGGRESSIVE) mode = STRICT_MODE_BALANCED;
    s_strictMode = (StrictModeLevel)mode;
    s_prefs.end();
  }

  unsigned long now = millis();
  s_lastAudioActivity   = now;
  s_lastStreamActivity  = now;
  s_lastXiaoZhiActivity = now;
  s_lastUploadActivity  = now;
  s_lastPeriodicCheckMs = now;

  LOG_I("SCAVENGER", "Da khoi tao MemoryScavenger | Che do: %s", getStrictModeName());
  Serial.printf("🧹 [MEMORY SCAVENGER] Khoi tao he thong quan ly bo nho thong minh | Che do: %s\n", getStrictModeName());
}

void MemoryScavenger::setStrictMode(StrictModeLevel level) {
  if (level > STRICT_MODE_AGGRESSIVE) level = STRICT_MODE_AGGRESSIVE;
  s_strictMode = level;

  if (s_prefs.begin("scavenger", false)) {
    s_prefs.putUChar("strict", (uint8_t)s_strictMode);
    s_prefs.end();
  }

  LOG_I("SCAVENGER", "Da luu Che do Nghiem ngat moi: %s", getStrictModeName());
  Serial.printf("🧹 [MEMORY SCAVENGER] Che do Nghiem ngat cap nhat: %s\n", getStrictModeName());
}

StrictModeLevel MemoryScavenger::getStrictMode() {
  return s_strictMode;
}

const char* MemoryScavenger::getStrictModeName() {
  switch (s_strictMode) {
    case STRICT_MODE_OFF:
      return "OFF (Chi don khi khan cap/thu cong)";
    case STRICT_MODE_BALANCED:
      return "BALANCED (Can bang: 3 phut idle)";
    case STRICT_MODE_AGGRESSIVE:
      return "AGGRESSIVE (Nghiem ngat: 60s / AOD Sleep)";
    default:
      return "UNKNOWN";
  }
}

void MemoryScavenger::notifyAudioActivity() {
  s_lastAudioActivity = millis();
}

void MemoryScavenger::notifyStreamActivity() {
  s_lastStreamActivity = millis();
}

void MemoryScavenger::notifyXiaoZhiActivity() {
  s_lastXiaoZhiActivity = millis();
}

void MemoryScavenger::notifyUploadActivity() {
  s_lastUploadActivity = millis();
}

uint32_t MemoryScavenger::getAudioIdleSec() {
  if (TestAudio::isVoiceRecording() || TestAudio::isMusicPlaying() ||
      TestAudio::isVoicePlaybackAsyncRunning() || TestAudio::isVoiceStreamActive()) {
    s_lastAudioActivity = millis();
    return 0;
  }
  return (millis() - s_lastAudioActivity) / 1000;
}

uint32_t MemoryScavenger::getStreamIdleSec() {
  if (WebStreamManager::isVideoStreaming() || WebStreamManager::isAudioStreaming()) {
    s_lastStreamActivity = millis();
    return 0;
  }
  return (millis() - s_lastStreamActivity) / 1000;
}

uint32_t MemoryScavenger::getXiaoZhiIdleSec() {
  if (TestAudio::isVoiceStreamActive() || !XiaoZhiClient::isTtsFinished()) {
    s_lastXiaoZhiActivity = millis();
    return 0;
  }
  return (millis() - s_lastXiaoZhiActivity) / 1000;
}

uint32_t MemoryScavenger::getUploadIdleSec() {
  if (WebStreamManager::isVideoStreaming()) {
    s_lastUploadActivity = millis();
    return 0;
  }
  return (millis() - s_lastUploadActivity) / 1000;
}

const ScavengeReport& MemoryScavenger::getLastReport() {
  return s_lastReport;
}

uint32_t MemoryScavenger::getTotalScavengeRuns() {
  return s_totalScavengeRuns;
}

uint32_t MemoryScavenger::getCumulativeFreedKb() {
  return s_cumulativeFreedKb;
}

ScavengeReport MemoryScavenger::scavenge(bool forceAll, const char* reason) {
  size_t heapBefore  = ESP.getFreeHeap();
  size_t psramBefore = ESP.getFreePsram();

  uint32_t idleLimit = (s_strictMode == STRICT_MODE_AGGRESSIVE) ? 60 : 180;
  size_t candidateFreedBytes = 0;

  // 1. Phân hệ Audio (pcmRecBuffer 160KB - 256KB)
  if (forceAll || getAudioIdleSec() >= idleLimit) {
    candidateFreedBytes += TestAudio::trimMemory();
  }

  // 2. Phân hệ Web Stream (s_audioRingBuf 384KB + s_asyncVideoBuf 64KB)
  if (forceAll || getStreamIdleSec() >= idleLimit) {
    candidateFreedBytes += WebStreamManager::trimMemory();
  }

  // 3. Phân hệ Web Upload Buffer (s_streamJpegUploadBuf 64KB)
  if (forceAll || getUploadIdleSec() >= idleLimit) {
    candidateFreedBytes += WebRoutes::trimMemory();
  }

  // 4. Phân hệ Trợ lý XiaoZhi AI Opus Decoder (~25KB)
  if (forceAll || getXiaoZhiIdleSec() >= idleLimit) {
    candidateFreedBytes += XiaoZhiClient::trimMemory();
  }

  // 5. Xả sạch bộ đệm SystemLogger xuống thẻ nhớ SD
  SystemLogger::flush();

  size_t heapAfter  = ESP.getFreeHeap();
  size_t psramAfter = ESP.getFreePsram();

  size_t heapFreed  = (heapAfter > heapBefore) ? (heapAfter - heapBefore) : 0;
  size_t psramFreed = (psramAfter > psramBefore) ? (psramAfter - psramBefore) : 0;
  size_t totalFreed = heapFreed + psramFreed;

  if (totalFreed == 0 && candidateFreedBytes > 0) {
    totalFreed = candidateFreedBytes;
  }

  s_lastReport.heapFreedBytes  = heapFreed;
  s_lastReport.psramFreedBytes = psramFreed;
  s_lastReport.totalFreedBytes = totalFreed;
  s_lastReport.timestamp       = millis() / 1000;
  s_lastReport.triggerReason   = reason;

  if (totalFreed > 0 || forceAll) {
    s_totalScavengeRuns++;
    s_cumulativeFreedKb += (uint32_t)(totalFreed / 1024);

    LOG_I("SCAVENGER", "Don RAM hoan tat (%s): Thu hoi %u KB (Heap: +%u B, PSRAM: +%u B). Free Heap: %u KB, PSRAM: %u KB",
          reason, (unsigned)(totalFreed / 1024), (unsigned)heapFreed, (unsigned)psramFreed,
          (unsigned)(heapAfter / 1024), (unsigned)(psramAfter / 1024));

    Serial.printf("🧹 [MEMORY SCAVENGER] Hoàn tất dọn RAM (%s): +%u KB (Heap: +%u KB, PSRAM: +%u KB) | Free Heap: %u KB, Free PSRAM: %u KB\n",
                  reason, (unsigned)(totalFreed / 1024), (unsigned)(heapFreed / 1024), (unsigned)(psramFreed / 1024),
                  (unsigned)(heapAfter / 1024), (unsigned)(psramAfter / 1024));
  }

  return s_lastReport;
}

void MemoryScavenger::loop() {
  unsigned long now = millis();
  if (now - s_lastPeriodicCheckMs < 10000) return; // Kiểm tra định kỳ mỗi 10 giây
  s_lastPeriodicCheckMs = now;

  // 1. CẢNH BÁO BỘ NHỚ NỘI KHẨN CẤP (Heap < 65KB): Bắt buộc dọn RAM ngay lập tức ở mọi chế độ
  if (ESP.getFreeHeap() < 65536) {
    LOG_W("SCAVENGER", "Heap thap duoi nguong an toan (%u KB) -> Kich hoat don RAM khan cap!", (unsigned)(ESP.getFreeHeap() / 1024));
    scavenge(true, "LOW_RAM_ALERT");
    return;
  }

  // 2. Nếu chế độ STRICT_MODE_OFF -> Không tự động dọn theo idle
  if (s_strictMode == STRICT_MODE_OFF) return;

  // 3. Chế độ STRICT_MODE_AGGRESSIVE:
  // - Nếu màn hình đang ngủ (AOD/Sleep) -> Thu hồi ngay toàn bộ buffer không dùng
  // - Hoặc nếu bất kỳ subsystem nào nhàn rỗi >= 60 giây
  if (s_strictMode == STRICT_MODE_AGGRESSIVE) {
    if (TestDisplay::isSleeping()) {
      bool hasAllocated = TestAudio::hasAllocatedRecBuffer() ||
                           WebStreamManager::hasAllocatedBuffers() ||
                           WebRoutes::hasAllocatedUploadBuffer() ||
                           XiaoZhiClient::hasAllocatedDecoder();
      if (hasAllocated) {
        scavenge(false, "AOD_SLEEP_TRIM");
        return;
      }
    }

    if (getAudioIdleSec() >= 60 || getStreamIdleSec() >= 60 ||
        getUploadIdleSec() >= 60 || getXiaoZhiIdleSec() >= 60) {
      scavenge(false, "IDLE_TIMEOUT_60S");
      return;
    }
  }

  // 4. Chế độ STRICT_MODE_BALANCED (Mặc định):
  // - Thu hồi nếu bất kỳ subsystem nào nhàn rỗi >= 180 giây (3 phút)
  if (s_strictMode == STRICT_MODE_BALANCED) {
    if (getAudioIdleSec() >= 180 || getStreamIdleSec() >= 180 ||
        getUploadIdleSec() >= 180 || getXiaoZhiIdleSec() >= 180) {
      scavenge(false, "IDLE_TIMEOUT_180S");
      return;
    }
  }
}

void MemoryScavenger::getStatusJson(JsonDocument& doc) {
  doc["strict_mode"]          = (uint8_t)s_strictMode;
  doc["strict_mode_name"]     = getStrictModeName();
  doc["audio_idle_s"]         = getAudioIdleSec();
  doc["stream_idle_s"]        = getStreamIdleSec();
  doc["xiaozhi_idle_s"]       = getXiaoZhiIdleSec();
  doc["upload_idle_s"]        = getUploadIdleSec();

  doc["audio_has_buf"]        = TestAudio::hasAllocatedRecBuffer();
  doc["stream_has_buf"]       = WebStreamManager::hasAllocatedBuffers();
  doc["upload_has_buf"]       = WebRoutes::hasAllocatedUploadBuffer();
  doc["xiaozhi_has_buf"]      = XiaoZhiClient::hasAllocatedDecoder();

  doc["total_scavenge_runs"]  = s_totalScavengeRuns;
  doc["cumulative_freed_kb"]  = s_cumulativeFreedKb;

  JsonObject last = doc["last_scavenge"].to<JsonObject>();
  last["freed_bytes"]         = s_lastReport.totalFreedBytes;
  last["freed_kb"]            = s_lastReport.totalFreedBytes / 1024;
  last["heap_freed"]          = s_lastReport.heapFreedBytes;
  last["psram_freed"]         = s_lastReport.psramFreedBytes;
  last["reason"]              = s_lastReport.triggerReason;
  last["timestamp"]           = s_lastReport.timestamp;

  doc["free_heap"]            = ESP.getFreeHeap();
  doc["min_free_heap"]        = ESP.getMinFreeHeap();
  doc["free_psram"]           = ESP.getFreePsram();
}

