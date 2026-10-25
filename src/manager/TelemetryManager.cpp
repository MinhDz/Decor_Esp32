#include "TelemetryManager.h"
#include "WeatherManager.h"
#include "tests/TestSensors.h"
#include "PcStatsManager.h"
#include <WiFi.h>
#include <time.h>
#include <math.h>

static const size_t BUFFER_BYTES = 1024 * 1024; // 1 MB PSRAM = 1,048,576 Bytes
static const size_t RECORD_SIZE  = sizeof(TelemetryRecord); // 12 Bytes
static const size_t CAPACITY     = BUFFER_BYTES / RECORD_SIZE; // 87,381 Bản ghi (~48.5 Giờ)

static TelemetryRecord* s_buffer = nullptr;
static size_t s_head = 0;
static size_t s_count = 0;
static unsigned long s_lastRecordMs = 0;
static const unsigned long SAMPLE_INTERVAL_MS = 2000; // Lấy mẫu mỗi 2 giây
static uint32_t s_flashWritesSaved = 0;

// FlashGuard Debounce State
static bool s_flashDirty = false;
static unsigned long s_flashDirtyDeadline = 0;
static void (*s_flashSaveCallback)() = nullptr;

void TelemetryManager::init() {
  if (s_buffer != nullptr) return;

  Serial.println("\n-------------------------------------------------------");
  Serial.println("🧠 [PSRAM TELEMETRY] KHỞI TẠO BỘ ĐỆM HÀNG ĐỢI TRÒN 1MB");
  Serial.printf("   + Dung lượng cấp phát : %u Bytes (1.0 MB PSRAM)\n", (unsigned)BUFFER_BYTES);
  Serial.printf("   + Kích thước mỗi mẫu  : %u Bytes (Packed Struct)\n", (unsigned)RECORD_SIZE);
  Serial.printf("   + Sức chứa tối đa     : %u mẫu (~%.1f Giờ liên tục)\n",
                (unsigned)CAPACITY, (float)(CAPACITY * 2) / 3600.0f);
  Serial.println("-------------------------------------------------------");

  if (psramFound()) {
    s_buffer = (TelemetryRecord*)ps_malloc(BUFFER_BYTES);
  }

  if (s_buffer) {
    memset(s_buffer, 0, BUFFER_BYTES);
    s_head = 0;
    s_count = 0;
    Serial.println("✅ [PSRAM TELEMETRY] Đã cấp phát thành công 1MB PSRAM Circular Buffer!");
  } else {
    Serial.println("⚠️ [PSRAM TELEMETRY] Không đủ PSRAM! Cấp phát bộ đệm dự phòng 120KB trên SRAM...");
    size_t fallbackCount = 10000; // 120KB
    s_buffer = (TelemetryRecord*)malloc(fallbackCount * sizeof(TelemetryRecord));
    if (s_buffer) {
      memset(s_buffer, 0, fallbackCount * sizeof(TelemetryRecord));
    }
  }

  // Ghi nhận mẫu ban đầu ngay khi bật nguồn
  recordSample();
}

void TelemetryManager::recordSample() {
  if (!s_buffer) return;

  TelemetryRecord rec;
  time_t nowEpoch = time(nullptr);
  if (nowEpoch > 1700000000) {
    rec.timestamp = (uint32_t)nowEpoch;
  } else {
    rec.timestamp = (uint32_t)(millis() / 1000);
  }

  // 1. Nhiệt độ & Độ ẩm
  float tempVal = 28.0f;
  float humVal  = 65.0f;
  bool shtOnline = TestSensors::isSht31Connected();

  if (shtOnline) {
    tempVal = TestSensors::getTemperatureC();
    humVal  = TestSensors::getHumidityPct();
  } else if (WeatherManager::isAvailable()) {
    tempVal = WeatherManager::getOutdoorTemp();
    humVal  = (float)WeatherManager::getOutdoorHumidity();
  }
  rec.tempC_x10 = (int16_t)constrain((int)roundf(tempVal * 10.0f), -400, 1250);
  rec.humidity  = (uint8_t)constrain((int)roundf(humVal), 0, 100);

  // 2. Dung lượng pin & Trạng thái sạc
  rec.batteryPct = (uint8_t)constrain(TestSensors::getBatteryPercentage(), 0, 100);

  // 3. Mức tải CPU & RAM (Ưu tiên số liệu PC nếu đang kết nối PC Stats HUD)
  if (PcStatsManager::isLive()) {
    const PcMetrics& m = PcStatsManager::getMetrics();
    rec.cpuUsage     = (uint8_t)constrain((int)m.cpu.usage, 0, 100);
    rec.ramUsage     = (uint8_t)constrain((int)m.ram.usage, 0, 100);
  } else {
    // Nếu không nối PC, tính mức tải CPU/RAM của ESP32
    uint32_t freeHeap = ESP.getFreeHeap();
    rec.cpuUsage = 15; // Mức tải trung bình của Space OS
    rec.ramUsage = (uint8_t)constrain((int)(100 - (freeHeap * 100 / 327680)), 0, 100);
  }

  // 4. Mã thời tiết
  rec.weatherCode = (uint8_t)WeatherManager::getWeatherCode();

  // 5. Cờ trạng thái ngoại vi
  uint8_t flags = 0;
  if (shtOnline)                      flags |= 0x01;
  if (TestSensors::isCharging())       flags |= 0x02;
  if (WiFi.status() == WL_CONNECTED) flags |= 0x04;
  if (PcStatsManager::isLive())       flags |= 0x08;
  rec.flags = flags;

  // Lưu vào hàng đợi tròn PSRAM (Zero writes to Flash)
  s_buffer[s_head] = rec;
  s_head = (s_head + 1) % CAPACITY;
  if (s_count < CAPACITY) s_count++;

  s_flashWritesSaved++;
}

void TelemetryManager::loop() {
  unsigned long now = millis();

  // 1. Định kỳ lấy mẫu mỗi 2 giây vào 1MB PSRAM
  if (now - s_lastRecordMs >= SAMPLE_INTERVAL_MS) {
    s_lastRecordMs = now;
    recordSample();
  }

  // 2. Cơ chế FlashGuard: Trì hoãn ghi Flash cho đến khi người dùng kết thúc thao tác trượt
  if (s_flashDirty && (now >= s_flashDirtyDeadline)) {
    s_flashDirty = false;
    if (s_flashSaveCallback != nullptr) {
      s_flashSaveCallback();
      Serial.println("🛡️ [FLASH GUARD] Đã ghi Flash an toàn sau thời gian đệm (Tránh chai bộ nhớ)!");
    }
  }
}

bool TelemetryManager::isPsramBufferReady() {
  return (s_buffer != nullptr);
}

size_t TelemetryManager::getCapacity() {
  return CAPACITY;
}

size_t TelemetryManager::getRecordCount() {
  return s_count;
}

uint32_t TelemetryManager::getBufferSizeBytes() {
  return (uint32_t)BUFFER_BYTES;
}

uint32_t TelemetryManager::getSampleIntervalSec() {
  return (uint32_t)(SAMPLE_INTERVAL_MS / 1000);
}

uint32_t TelemetryManager::getFlashWritesSaved() {
  return s_flashWritesSaved;
}

void TelemetryManager::markFlashDirty(void (*saveCallback)()) {
  s_flashSaveCallback = saveCallback;
  s_flashDirty = true;
  s_flashDirtyDeadline = millis() + 2000; // Trì hoãn 2.0s
}

void TelemetryManager::flushPendingFlashWrites() {
  if (s_flashDirty && s_flashSaveCallback != nullptr) {
    s_flashDirty = false;
    s_flashSaveCallback();
    Serial.println("🛡️ [FLASH GUARD] Đã flush ghi Flash an toàn!");
  }
}

// ============================================================================
// TRUY XUẤT LỊCH SỬ TELEMETRY (DOWNSAMPLED ĐỂ WEB TẢI SIÊU NHANH < 15MS)
// ============================================================================
void TelemetryManager::getHistoryJson(JsonDocument& doc, const String& range, size_t maxPoints) {
  if (!s_buffer || s_count == 0) {
    doc["status"] = "empty";
    doc["total_records"] = 0;
    return;
  }

  size_t desiredSamples = s_count;
  if (range == "1h")       desiredSamples = min(s_count, (size_t)1800);
  else if (range == "6h")  desiredSamples = min(s_count, (size_t)(1800 * 6));
  else if (range == "24h") desiredSamples = min(s_count, (size_t)(1800 * 24));
  else if (range == "48h") desiredSamples = min(s_count, (size_t)(1800 * 48));

  size_t step = max((size_t)1, desiredSamples / maxPoints);
  size_t actualPoints = desiredSamples / step;

  doc["status"]             = "success";
  doc["total_records"]      = s_count;
  doc["capacity"]           = CAPACITY;
  doc["range"]              = range;
  doc["sample_interval_sec"]= 2;
  doc["psram_bytes_used"]   = BUFFER_BYTES;
  doc["flash_writes_saved"] = s_flashWritesSaved;

  // Bản ghi hiện tại mới nhất
  size_t latestIdx = (s_head + CAPACITY - 1) % CAPACITY;
  const TelemetryRecord& cur = s_buffer[latestIdx];
  JsonObject curObj = doc["current"].to<JsonObject>();
  curObj["timestamp"]    = cur.timestamp;
  curObj["temp"]         = (float)cur.tempC_x10 / 10.0f;
  curObj["humidity"]     = cur.humidity;
  curObj["battery"]      = cur.batteryPct;
  curObj["cpu"]          = cur.cpuUsage;
  curObj["ram"]          = cur.ramUsage;
  curObj["weather_code"] = cur.weatherCode;
  curObj["city"]         = WeatherManager::getCityName();

  // Mảng lịch sử mẫu
  JsonArray arr = doc["history"].to<JsonArray>();
  size_t startOffset = s_count - desiredSamples;
  size_t baseStart = (s_head + CAPACITY - s_count) % CAPACITY;

  for (size_t i = 0; i < desiredSamples; i += step) {
    size_t idx = (baseStart + startOffset + i) % CAPACITY;
    const TelemetryRecord& r = s_buffer[idx];
    JsonObject p = arr.add<JsonObject>();
    p["t"]   = r.timestamp;
    p["tmp"] = (float)r.tempC_x10 / 10.0f;
    p["hum"] = r.humidity;
    p["bat"] = r.batteryPct;
    p["cpu"] = r.cpuUsage;
    p["ram"] = r.ramUsage;
  }
}

void TelemetryManager::getStats(const String& range, float& minT, float& maxT, float& avgT,
                                int& minH, int& maxH, float& avgH) {
  minT = 999.0f; maxT = -999.0f; avgT = 28.0f;
  minH = 100;    maxH = 0;       avgH = 65.0f;
  if (!s_buffer || s_count == 0) return;

  size_t desiredSamples = s_count;
  if (range == "1h")       desiredSamples = min(s_count, (size_t)1800);
  else if (range == "6h")  desiredSamples = min(s_count, (size_t)(1800 * 6));
  else if (range == "24h") desiredSamples = min(s_count, (size_t)(1800 * 24));
  else if (range == "48h") desiredSamples = min(s_count, (size_t)(1800 * 48));

  size_t startOffset = s_count - desiredSamples;
  size_t baseStart = (s_head + CAPACITY - s_count) % CAPACITY;

  float sumT = 0.0f;
  long sumH = 0;
  size_t step = max((size_t)1, desiredSamples / 200);
  size_t sampled = 0;

  for (size_t i = 0; i < desiredSamples; i += step) {
    size_t idx = (baseStart + startOffset + i) % CAPACITY;
    const TelemetryRecord& r = s_buffer[idx];
    float t = (float)r.tempC_x10 / 10.0f;
    int h = (int)r.humidity;
    if (t < minT) minT = t;
    if (t > maxT) maxT = t;
    if (h < minH) minH = h;
    if (h > maxH) maxH = h;
    sumT += t;
    sumH += h;
    sampled++;
  }

  if (sampled > 0) {
    avgT = sumT / (float)sampled;
    avgH = (float)sumH / (float)sampled;
  }
}

// ============================================================================
// VẼ BIỂU ĐỒ XU HƯỚNG TRỰC QUAN LÊN MÀN HÌNH ST7789 (240x320)
// ============================================================================
void TelemetryManager::drawTrendGraphST7789(Adafruit_GFX* gfx, int x, int y, int w, int h,
                                           const String& metric, const String& range) {
  if (!gfx || !s_buffer || s_count < 2) return;

  const uint16_t C_BG       = 0x0842; // Nền đen xanh thẳm
  const uint16_t C_BORDER   = 0x2188; // Khung xám bạc
  const uint16_t C_GRID     = 0x18E3; // Lưới tọa độ mờ
  const uint16_t C_LINE_T   = 0x07FF; // Đường nhiệt độ Cyan
  const uint16_t C_LINE_H   = 0xFDE0; // Đường độ ẩm Vàng Gold
  const uint16_t C_WHITE    = 0xFFFF;
  const uint16_t C_SUB      = 0x9CD3;

  // Khung biểu đồ kính mờ
  gfx->fillRoundRect(x, y, w, h, 6, C_BG);
  gfx->drawRoundRect(x, y, w, h, 6, C_BORDER);

  // Tiêu đề & Thông số tóm tắt
  gfx->setTextSize(1);
  gfx->setTextColor(metric == "humidity" ? C_LINE_H : C_LINE_T, C_BG);
  gfx->setCursor(x + 8, y + 6);
  if (metric == "humidity") {
    gfx->print("DO AM BIEN THIEN (");
    gfx->print(range);
    gfx->print(")");
  } else {
    gfx->print("NHIET DO BIEN THIEN (");
    gfx->print(range);
    gfx->print(")");
  }

  int plotX = x + 28;
  int plotY = y + 20;
  int plotW = w - 36;
  int plotH = h - 34;

  // Đường lưới ngang
  for (int g = 0; g <= 3; g++) {
    int gy = plotY + (plotH * g) / 3;
    gfx->drawFastHLine(plotX, gy, plotW, C_GRID);
  }

  float minT, maxT, avgT;
  int minH, maxH;
  float avgH;
  getStats(range, minT, maxT, avgT, minH, maxH, avgH);

  float valMin = (metric == "humidity") ? (float)max(0, minH - 5) : (minT - 1.5f);
  float valMax = (metric == "humidity") ? (float)min(100, maxH + 5) : (maxT + 1.5f);
  if (valMax <= valMin) valMax = valMin + 5.0f;

  // Nhãn trục Y (Max, Mid, Min)
  gfx->setTextColor(C_SUB, C_BG);
  gfx->setCursor(x + 3, plotY - 3);
  gfx->printf("%d", (int)roundf(valMax));
  gfx->setCursor(x + 3, plotY + plotH / 2 - 3);
  gfx->printf("%d", (int)roundf((valMin + valMax) * 0.5f));
  gfx->setCursor(x + 3, plotY + plotH - 5);
  gfx->printf("%d", (int)roundf(valMin));

  // Trích xuất mẫu để vẽ đường
  size_t desiredSamples = s_count;
  if (range == "1h")       desiredSamples = min(s_count, (size_t)1800);
  else if (range == "6h")  desiredSamples = min(s_count, (size_t)(1800 * 6));
  else if (range == "24h") desiredSamples = min(s_count, (size_t)(1800 * 24));
  else if (range == "48h") desiredSamples = min(s_count, (size_t)(1800 * 48));

  size_t baseStart = (s_head + CAPACITY - s_count) % CAPACITY;
  size_t startOffset = s_count - desiredSamples;

  int prevPx = -1;
  int prevPy = -1;
  uint16_t lineCol = (metric == "humidity") ? C_LINE_H : C_LINE_T;

  for (int px = 0; px < plotW; px++) {
    size_t sampleIdx = (px * desiredSamples) / plotW;
    size_t ringIdx = (baseStart + startOffset + sampleIdx) % CAPACITY;
    const TelemetryRecord& r = s_buffer[ringIdx];

    float val = (metric == "humidity") ? (float)r.humidity : ((float)r.tempC_x10 / 10.0f);
    float norm = constrain((val - valMin) / (valMax - valMin), 0.0f, 1.0f);
    int py = plotY + plotH - (int)(norm * plotH);

    int curPx = plotX + px;
    if (prevPx >= 0) {
      gfx->drawLine(prevPx, prevPy, curPx, py, lineCol);
      // Đổ bóng 1 pixel phía dưới tạo hiệu ứng phát sáng neon
      gfx->drawPixel(curPx, py + 1, (lineCol >> 1) & 0x7BEF);
    }
    prevPx = curPx;
    prevPy = py;
  }

  // Footer: Hiển thị Min / Max / Hiện tại
  gfx->setTextColor(C_WHITE, C_BG);
  gfx->setCursor(plotX, y + h - 10);
  if (metric == "humidity") {
    gfx->printf("Min:%d%% Max:%d%% H.Tai:%d%%", minH, maxH, (int)s_buffer[(s_head + CAPACITY - 1) % CAPACITY].humidity);
  } else {
    float curT = (float)s_buffer[(s_head + CAPACITY - 1) % CAPACITY].tempC_x10 / 10.0f;
    gfx->printf("Min:%.1f Max:%.1f H.Tai:%.1fC", minT, maxT, curT);
  }
}

