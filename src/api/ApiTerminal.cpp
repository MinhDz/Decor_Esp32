#include "api/ApiTerminal.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
#include "WebAuth.h"
#include "SystemLogger.h"
#include "MemoryScavenger.h"
#include "CobLedController.h"
#include "TelemetryManager.h"
#include "OtaManager.h"
#include "drivers/MotorDriver.h"
#include "drivers/AudioDriver.h"
#include "drivers/SensorDriver.h"
#include "drivers/StorageDriver.h"
#include "drivers/DisplayDriver.h"
#include "tests/TestDisplay.h"
#include "tests/TestAudio.h"
#include "tests/TestWindmill.h"
#include "tests/TestSensors.h"
#include "tests/TestSDCard.h"
#include "TaskProfiler.h"
#include "WifiManager.h"
#include "XiaoZhiClient.h"

namespace ApiTerminal {

static const char* getResetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "Power-on (Vcc khởi động)";
    case ESP_RST_SW:        return "Software reset (ESP.restart)";
    case ESP_RST_PANIC:     return "Software panic / Exception crash";
    case ESP_RST_INT_WDT:   return "Interrupt Watchdog timeout";
    case ESP_RST_TASK_WDT:  return "Task Watchdog timeout";
    case ESP_RST_WDT:       return "Other Watchdog reset";
    case ESP_RST_DEEPSLEEP: return "Deep-sleep wake-up";
    case ESP_RST_BROWNOUT:  return "Brownout (Sụt áp nguồn)";
    case ESP_RST_SDIO:      return "SDIO reset";
    default:                return "Khởi động bình thường";
  }
}

void handleExec(WebServer& server) {
  if (!WebAuth::isAuthenticated(server)) {
    server.send(401, "application/json", "{\"success\":false,\"output\":\"Lỗi: Chưa xác thực quyền quản trị Web!\"}");
    return;
  }
  String rawCmd = "";
  if (server.hasArg("plain")) {
    JsonDocument inDoc;
    DeserializationError err = deserializeJson(inDoc, server.arg("plain"));
    if (!err && inDoc["cmd"].is<const char*>()) {
      rawCmd = inDoc["cmd"].as<String>();
    }
  }
  if (rawCmd.length() == 0 && server.hasArg("cmd")) {
    rawCmd = server.arg("cmd");
  }

  rawCmd.trim();
  if (rawCmd.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"output\":\"Lỗi: Lệnh trống.\"}");
    return;
  }

  // Tiền xử lý: Bóc tách tiền tố (sudo, space, spaceos, decor)
  String cmd = rawCmd;
  while (true) {
    cmd.trim();
    String low = cmd;
    low.toLowerCase();
    if (low.startsWith("sudo ")) cmd = cmd.substring(5);
    else if (low.startsWith("space ")) cmd = cmd.substring(6);
    else if (low.startsWith("spaceos ")) cmd = cmd.substring(8);
    else if (low.startsWith("decor ")) cmd = cmd.substring(6);
    else break;
  }
  cmd.trim();

  // Ghi nhật ký lệnh đã gọi vào SystemLogger
  SystemLogger::log(LOG_LVL_INFO, "CLI", "Exec: '%s'", rawCmd.c_str());

  int spIdx = cmd.indexOf(' ');
  String verb = (spIdx >= 0) ? cmd.substring(0, spIdx) : cmd;
  String args = (spIdx >= 0) ? cmd.substring(spIdx + 1) : "";
  verb.toLowerCase();
  args.trim();

  JsonDocument resp;
  resp["success"] = true;
  resp["clear"] = false;

  String out = "";

  // 1. HELP / INFO / ?
  if (verb == "help" || verb == "?" || verb == "info") {
    out = "======================= SPACE OS CLI TERMINAL =======================\n";
    out += "Hệ điều hành: Space OS v3.2.7 | Trạm vi điều khiển: ESP32-S3 DevKit\n";
    out += "Tiền tố hỗ trợ : sudo, space, spaceos, decor (hoặc gõ trực tiếp lệnh)\n\n";
    out += "[DANH MỤC & TRẠNG THÁI NGOẠI VI]\n";
    out += "  modules | list | periph  : Hiển thị bảng kiểm tra 12+ phân hệ ngoại vi\n";
    out += "  status <module>          : Thông số chẩn đoán chi tiết một module:\n";
    out += "                             (sht31, sd, audio, mic, wifi, mem, motor,\n";
    out += "                              led, xiaozhi, display, sys, profiler)\n\n";
    out += "[ĐIỀU KHIỂN & TÁC VỤ HỆ THỐNG]\n";
    out += "  clean | scavenge         : Dọn dẹp & thu hồi bộ nhớ RAM ngay lập tức\n";
    out += "  strict <0|1|2>           : Đặt chế độ dọn RAM (0=Tắt, 1=Tiêu chuẩn, 2=Gắt gao)\n";
    out += "  vol <0-100>              : Cài đặt âm lượng loa chính MAX98357A\n";
    out += "  beep [freq] [ms]         : Phát âm beep kiểm tra loa (mặc định 1000Hz 150ms)\n";
    out += "  tune <nokia|ok|del|sms>  : Phát giai điệu âm thanh Nokia S40\n";
    out += "  toast <nội_dung>         : Gửi thông báo pop-up Toast lên màn hình LCD\n";
    out += "  mode <0-19|tên>          : Đổi chế độ giao diện màn hình tròn ST7789\n";
    out += "  motor <0-100|off|on>     : Đặt tốc độ quay cánh quạt cối xay gió\n";
    out += "  led <left|right|cob> <v> : Chỉnh độ sáng đèn Edison hoặc đổi mode LED lưng\n";
    out += "  snapshot                 : Chụp toàn bộ trạng thái phần cứng ghi thẻ SD\n";
    out += "  clear                    : Xóa sạch màn hình Terminal Web\n";
    out += "  reboot                   : Khởi động lại vi điều khiển ESP32-S3\n";
    out += "=====================================================================\n";
  }
  // 2. MODULES / LIST
  else if (verb == "modules" || verb == "list" || verb == "periph" || verb == "peripherals") {
    char b[256];
    out = "======================= SYSTEM MODULES & PERIPHERALS =======================\n";
    out += "MODULE          STATUS     CHI TIẾT & THÔNG SỐ HOẠT ĐỘNG\n";
    out += "----------------------------------------------------------------------------\n";

    bool shtOk = TestSensors::isSht31Connected();
    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "SHT31 (I2C)", shtOk ? "ONLINE" : "OFFLINE",
      shtOk ? (String(TestSensors::getTemperatureC(), 1) + "°C | " + String((int)roundf(TestSensors::getHumidityPct())) + "% RH (Addr 0x" + String(TestSensors::getDetectedAddress(), HEX) + ")").c_str() : "Mất kết nối I2C");
    out += b;

    bool sdOk = TestSDCard::isMounted() && TestSDCard::isHealthy();
    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "SD CARD (SPI)",
      sdOk ? "HEALTHY" : (TestSDCard::isMounted() ? "DEGRADED" : "OFFLINE"),
      sdOk ? (TestSDCard::getCardTypeName() + " | Trống " + String(TestSDCard::getFreeMB()) + "/" + String(TestSDCard::getTotalMB()) + " MB").c_str() :
      (TestSDCard::isMounted() ? TestSDCard::getHealthStateStr() : "Chưa cắm / Đệm 128KB PSRAM"));
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "AUDIO DAC", "READY",
      ("MAX98357A I2S | Âm lượng: " + String(TestAudio::getSpeakerVolumePct()) + "%").c_str());
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "MIC (INMP441)", "READY",
      ("16kHz 32-bit DMA | VU: " + String(TestAudio::getLastMicLevelPct()) + "%").c_str());
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "DISPLAY (LCD)", "ONLINE",
      ("ST7789 240x240 | " + TestDisplay::getScreenModeName() + " | Sáng " + String(TestDisplay::getBacklightBrightness()) + "%").c_str());
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "WINDMILL", TestWindmill::getMotorSpeed() > 0 ? "SPINNING" : "STOPPED",
      (String(TestWindmill::getModeName()) + " | Tốc độ: " + String(TestWindmill::getMotorSpeed()) + "%").c_str());
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "COB & EDISON", CobLeds::isOn() ? "LIGHT ON" : "STANDBY",
      (String(CobLeds::getModeName()) + " | Edison L:" + String(TestWindmill::getEdisonLeftBrightness()) + "% R:" + String(TestWindmill::getEdisonRightBrightness()) + "%").c_str());
    out += b;

    bool wifiOk = WifiManager::isConnected();
    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "WIFI 2.4G", wifiOk ? "CONNECTED" : "DISCONN",
      wifiOk ? ("'" + WifiManager::getCurrentSsid() + "' (" + String(WifiManager::getRssi()) + " dBm) | " + WifiManager::getIp()).c_str() : "Chưa kết nối");
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "XIAOZHI AI", XiaoZhiClient::isWebSocketConnected() ? "CONNECTED" : (XiaoZhiClient::isDeviceBound() ? "BOUND" : "STANDBY"),
      XiaoZhiClient::isWebSocketConnected() ? "WebSocket Live | Sẵn sàng đàm thoại" : "Chờ tương tác");
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "MEMORY (RAM)", MemoryScavenger::getStrictMode() > 0 ? "OPTIMIZED" : "MONITOR",
      ("Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB | PSRAM: " + String(ESP.getFreePsram() / (1024 * 1024)) + " MB | Mode: " + MemoryScavenger::getStrictModeName()).c_str());
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "PROFILER", "RUNNING",
      (String(TaskProfiler::getFps(), 1) + " FPS | Loop Avg: " + String(TaskProfiler::getLoopAvgUs()) + " µs").c_str());
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "TELEMETRY", TelemetryManager::isPsramBufferReady() ? "ACTIVE" : "INIT",
      (String(TelemetryManager::getRecordCount()) + "/" + String(TelemetryManager::getCapacity()) + " bản ghi (1MB PSRAM)").c_str());
    out += b;

    snprintf(b, sizeof(b), "%-15s %-10s %s\n", "BATTERY/PWR", TestSensors::isCharging() ? "CHARGING" : "OK",
      (String(TestSensors::getBatteryVoltage(), 2) + "V (" + String(TestSensors::getBatteryPercentage()) + "%) | " + (TestSensors::isCharging() ? "Vin Bo sạc" : "Pin 18650")).c_str());
    out += b;

    out += "----------------------------------------------------------------------------\n";
    out += "Gõ 'status <module>' (VD: status sht31, status mem) để xem chẩn đoán chi tiết.\n";
    out += "============================================================================\n";
  }
  // 3. STATUS <MODULE>
  else if (verb == "status") {
    String sub = args;
    sub.toLowerCase();
    if (sub == "sht31" || sub == "temp" || sub == "humidity" || sub == "sensor") {
      out = "=== [CHẨN ĐOÁN CẢM BIẾN MÔI TRƯỜNG SHT31] ===\n";
      bool ok = TestSensors::isSht31Connected();
      out += "Trạng thái kết nối : " + String(ok ? "ONLINE (Đang hoạt động tốt)" : "OFFLINE (Không tìm thấy trên bus I2C)") + "\n";
      out += "Địa chỉ I2C        : 0x" + String(TestSensors::getDetectedAddress(), HEX) + " (Mặc định 0x44)\n";
      out += "Chân I2C phần cứng : SDA = GPIO " + String(TestSensors::getActiveSdaPin()) + " | SCL = GPIO " + String(TestSensors::getActiveSclPin()) + "\n";
      if (ok) {
        float t = TestSensors::getTemperatureC();
        float h = TestSensors::getHumidityPct();
        out += "Nhiệt độ phòng     : " + String(t, 2) + " °C (" + String(t * 1.8f + 32.0f, 2) + " °F)\n";
        out += "Độ ẩm không khí    : " + String(h, 1) + " % RH\n";
      }
      out += "Bản tóm tắt I2C    : " + TestSensors::getDiagSummary() + "\n";
    }
    else if (sub == "sd" || sub == "sdcard" || sub == "card") {
      out = "=== [CHẨN ĐOÁN & SỨC KHỎE THẺ NHỚ MICROSD (SPI)] ===\n";
      bool mounted = TestSDCard::isMounted();
      bool healthy = TestSDCard::isHealthy();
      StorageDriver::SdHealthStats hs = StorageDriver::getHealthStats();

      out += "Trạng thái Mount   : " + String(mounted ? "ĐÃ MOUNT THÀNH CÔNG" : "CHƯA MOUNT / KHÔNG CẮM THẺ") + "\n";
      out += "Sức khỏe phần cứng : " + String(TestSDCard::getHealthStateStr()) + "\n";
      out += "Circuit Breaker    : " + String(hs.circuitBreakerTripped ? "⚠️ ĐÃ NGẮT BẢO VỆ (CÔ LẬP BUS)" : "BÌNH THƯỜNG") + "\n";
      out += "Lỗi ghi flash (W)  : " + String(hs.writeErrors) + " lần\n";
      out += "Lỗi đọc/mất SPI (R): " + String(hs.readErrors) + " lần\n";
      out += "Lỗi dồn dập        : " + String(hs.consecutiveErrors) + "\n";
      out += "Thao tác thành công: " + String(hs.successfulOps) + "\n";

      if (mounted) {
        out += "Loại thẻ nhớ       : " + TestSDCard::getCardTypeName() + "\n";
        out += "Dung lượng tổng    : " + String(TestSDCard::getTotalMB()) + " MB\n";
        out += "Dung lượng đã dùng : " + String(TestSDCard::getUsedMB()) + " MB (" + String((TestSDCard::getUsedMB() * 100) / (TestSDCard::getTotalMB() ? TestSDCard::getTotalMB() : 1)) + "%)\n";
        out += "Dung lượng còn trống: " + String(TestSDCard::getFreeMB()) + " MB\n";
        out += "Số tệp phát hiện   : " + String(TestSDCard::getFileCount()) + "\n";
        out += "Cấu hình chân SPI  : " + TestSDCard::getMatchedPinMapDesc() + "\n";
        out += "Bản tóm tắt        : " + TestSDCard::getStatusSummary() + "\n";
      } else {
        out += "Cơ chế bảo vệ      : Toàn bộ nhật ký và lịch sử đang được lưu an toàn trên đệm 128KB Octal PSRAM.\n";
        out += "Tự động phục hồi   : Hệ thống tự thăm dò định kỳ bằng xung CMD0 và mount lại ngay khi cắm thẻ mới.\n";
      }
    }
    else if (sub == "audio" || sub == "sound" || sub == "speaker") {
      out = "=== [CHẨN ĐOÁN HỆ THỐNG ÂM THANH I2S DAC] ===\n";
      out += "Module phần cứng   : MAX98357A I2S Class-D Amplifier (3W)\n";
      out += "Chân I2S phát      : DIN: GPIO 15 | BCLK: GPIO 16 | LRC/WS: GPIO 17\n";
      out += "Âm lượng loa chung : " + String(TestAudio::getSpeakerVolumePct()) + " %\n";
      out += "Âm lượng thông báo : " + String(TestAudio::getNotifyVolumePct()) + " %\n";
      out += "Âm lượng nghe nhạc : " + String(TestAudio::getMusicVolumePct()) + " %\n";
      out += "Âm lượng XiaoZhi AI: " + String(TestAudio::getXiaoZhiVolumePct()) + " %\n";
      out += "Trình phát nhạc SD : " + String(TestAudio::isMusicPlaying() ? "ĐANG PHÁT NHẠC" : "ĐANG NGHỈ") + "\n";
      out += "Stream âm thanh PCM: " + String(TestAudio::isVoiceStreamActive() ? "ĐANG NHẬN GÓI STREAM" : "NGHỈ") + "\n";
      out += "Mic Monitor (Loop) : " + String(TestAudio::isMicMonitorActive() ? "ĐANG BẬT" : "TẮT") + "\n";
    }
    else if (sub == "mic" || sub == "inmp441" || sub == "microphone") {
      out = "=== [CHẨN ĐOÁN MICROPHONE SỐ INMP441] ===\n";
      out += "Phần cứng mic      : INMP441 Omnidirectional MEMS Microphone\n";
      out += "Chân I2S thu       : SCK: GPIO 4 | WS: GPIO 5 | SD: GPIO 6\n";
      out += "Tần số lấy mẫu     : " + String(TestAudio::getRecordSampleRate()) + " Hz (16-bit Mono)\n";
      out += "Mức âm thanh VU    : " + String(TestAudio::getLastMicLevelPct()) + " %\n";
      out += "Mức âm lượng đỉnh  : " + String(TestAudio::getRecordedMaxVuPct()) + " %\n";
      out += "Thời lượng thu gần : " + String(TestAudio::getRecordedDurationSec(), 2) + " giây\n";
      out += "Đang ghi âm mic    : " + String(TestAudio::isVoiceRecording() ? "CÓ" : "KHÔNG") + "\n";
    }
    else if (sub == "wifi" || sub == "network" || sub == "ip" || sub == "net") {
      out = "=== [CHẨN ĐOÁN KẾT NỐI MẠNG KHÔNG DÂY WI-FI] ===\n";
      bool conn = WifiManager::isConnected();
      out += "Trạng thái kết nối : " + String(conn ? "ĐÃ KẾT NỐI (CONNECTED)" : "MẤT KẾT NỐI (DISCONNECTED)") + "\n";
      if (conn) {
        out += "Tên mạng (SSID)    : " + WifiManager::getCurrentSsid() + "\n";
        out += "Cường độ sóng RSSI : " + String(WifiManager::getRssi()) + " dBm\n";
        out += "Địa chỉ IPv4 trạm  : " + WifiManager::getIp() + "\n";
        out += "Cổng Gateway       : " + WiFi.gatewayIP().toString() + "\n";
        out += "Mặt nạ mạng Subnet : " + WiFi.subnetMask().toString() + "\n";
        out += "Máy chủ DNS        : " + WiFi.dnsIP().toString() + "\n";
        out += "Địa chỉ vật lý MAC : " + WiFi.macAddress() + "\n";
        out += "Kênh phát sóng     : Kênh " + String(WiFi.channel()) + " (2.4 GHz)\n";
        out += "BSSID Router       : " + WiFi.BSSIDstr() + "\n";
      } else {
        out += "Thông báo cuối     : " + WifiManager::getLastNotification() + "\n";
      }
    }
    else if (sub == "mem" || sub == "ram" || sub == "heap") {
      out = "=== [CHẨN ĐOÁN BỘ NHỚ RAM & DỌN RÁC THÔNG MINH] ===\n";
      uint32_t freeH = ESP.getFreeHeap();
      uint32_t minH  = ESP.getMinFreeHeap();
      uint32_t maxA  = ESP.getMaxAllocHeap();
      uint32_t freeP = ESP.getFreePsram();
      uint32_t totalP= psramFound() ? ESP.getPsramSize() : 0;
      out += "Heap tự do (Free)  : " + String(freeH / 1024) + " KB (" + String(freeH) + " Bytes)\n";
      out += "Heap tối thiểu     : " + String(minH / 1024) + " KB (Đáy thấp nhất từng ghi nhận)\n";
      out += "Khối Heap lớn nhất : " + String(maxA / 1024) + " KB (Khối liền kề có thể malloc)\n";
      out += "Octal PSRAM tự do  : " + String(freeP / (1024 * 1024)) + " MB (" + String(freeP / 1024) + " KB)\n";
      out += "Tổng Octal PSRAM   : " + String(totalP / (1024 * 1024)) + " MB\n";
      out += "Chế độ nghiêm ngặt : [" + String(MemoryScavenger::getStrictMode()) + "] " + MemoryScavenger::getStrictModeName() + "\n";
      out += "Số lần dọn rác     : " + String(MemoryScavenger::getTotalScavengeRuns()) + " chu kỳ\n";
      out += "Tổng RAM thu hồi   : " + String(MemoryScavenger::getCumulativeFreedKb()) + " KB\n";
      out += "Thời gian nhàn rỗi : Audio=" + String(MemoryScavenger::getAudioIdleSec()) + "s | Stream=" + String(MemoryScavenger::getStreamIdleSec()) + "s | XiaoZhi=" + String(MemoryScavenger::getXiaoZhiIdleSec()) + "s | Upload=" + String(MemoryScavenger::getUploadIdleSec()) + "s\n";
    }
    else if (sub == "motor" || sub == "windmill" || sub == "fan") {
      out = "=== [CHẨN ĐOÁN ĐỘNG CƠ CỐI XAY GIÓ] ===\n";
      out += "Driver điều khiển  : Mạch cầu H L298N Mini / MX1508 (PWM LEDC)\n";
      out += "Chân GPIO kích tín : IN1 = GPIO " + String(PIN_L298N_IN1_MOTOR) + "\n";
      out += "Chế độ hiện tại    : [" + String(TestWindmill::getMode()) + "] " + TestWindmill::getModeName() + "\n";
      out += "Tốc độ động cơ     : " + String(TestWindmill::getMotorSpeed()) + " %\n";
      out += "Hiệu ứng thở PWM   : " + String(TestWindmill::isBreathing() ? "ĐANG BẬT" : "TẮT") + "\n";
    }
    else if (sub == "led" || sub == "lights" || sub == "edison") {
      out = "=== [CHẨN ĐOÁN HỆ THỐNG ĐÈN TRANG TRÍ] ===\n";
      out += "Bóng Edison Cổ Điển:\n";
      out += "  - Bóng Trái (GPIO " + String(PIN_EDISON_TUBE_L) + "): " + TestWindmill::getEdisonLeftModeName() + " | Sáng " + String(TestWindmill::getEdisonLeftBrightness()) + "%\n";
      out += "  - Bóng Phải (GPIO " + String(PIN_EDISON_TUBE_R) + "): " + TestWindmill::getEdisonRightModeName() + " | Sáng " + String(TestWindmill::getEdisonRightBrightness()) + "%\n";
      out += "Dải LED Nước Chảy COB (GPIO " + String(PIN_FLOWING_LED_L) + ", " + String(PIN_FLOWING_LED_R) + "):\n";
      out += "  - Nguồn cấp       : " + String(CobLeds::isOn() ? "ĐANG SÁNG" : "TẮT NGUỒN") + "\n";
      out += "  - Chế độ hiệu ứng : [" + String(CobLeds::getCurrentMode()) + "] " + CobLeds::getModeName() + "\n";
    }
    else if (sub == "xiaozhi" || sub == "ai" || sub == "voice") {
      out = "=== [CHẨN ĐOÁN TRỢ LÝ GIỌNG NÓI XIAOZHI AI] ===\n";
      out += "Ràng buộc thiết bị : " + String(XiaoZhiClient::isDeviceBound() ? "ĐÃ LIÊN KẾT TÀI KHOẢN" : "CHƯA LIÊN KẾT") + "\n";
      out += "Kết nối WebSocket  : " + String(XiaoZhiClient::isWebSocketConnected() ? "ONLINE (Đã kết nối máy chủ)" : "OFFLINE (Nghỉ chờ gọi)") + "\n";
      out += "Giao thức truyền âm: Opus Codec Audio Frames qua WebSocket WSS\n";
    }
    else if (sub == "display" || sub == "screen" || sub == "lcd") {
      out = "=== [CHẨN ĐOÁN MÀN HÌNH MÀU ST7789] ===\n";
      out += "Phần cứng hiển thị : ST7789V 2.4 inch IPS tròn 240x240 SPI 80MHz\n";
      out += "Chế độ giao diện   : [" + String(TestDisplay::getCurrentMode()) + "] " + TestDisplay::getScreenModeName() + "\n";
      out += "Tiêu đề màn hình   : " + TestDisplay::getCurrentScreenTitle() + "\n";
      out += "Phụ đề / Trạng thái: " + TestDisplay::getSubtitle() + "\n";
      out += "Phong cách HUD     : " + TestDisplay::getHudStyleName() + "\n";
      out += "Độ sáng đèn nền    : " + String(TestDisplay::getBacklightBrightness()) + " %\n";
      out += "Ảnh nền chờ        : " + TestDisplay::getStandbyBgImage() + " (" + TestDisplay::getStandbyBgMode() + ")\n";
    }
    else if (sub == "sys" || sub == "system" || sub == "info" || sub == "board") {
      out = "=== [THÔNG SỐ PHẦN CỨNG VI ĐIỀU KHIỂN ESP32-S3] ===\n";
      out += "Dòng chip vi xử lý : " + String(ESP.getChipModel()) + " Rev " + String(ESP.getChipRevision()) + " (" + String(ESP.getChipCores()) + " Cores)\n";
      out += "Tần số xung nhịp   : " + String(ESP.getCpuFreqMHz()) + " MHz\n";
      out += "Dung lượng Flash   : " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB\n";
      out += "Dung lượng PSRAM   : " + String(ESP.getPsramSize() / (1024 * 1024)) + " MB Octal PSRAM\n";
      out += "Phiên bản ESP-IDF  : " + String(ESP.getSdkVersion()) + "\n";
      out += "Phiên bản Firmware : " + String(OtaManager::getFirmwareVersion()) + " (" + OtaManager::getOsName() + ")\n";
      out += "Thời điểm biên dịch: " + String(OtaManager::getBuildDateTime()) + "\n";
      out += "Phân vùng chạy     : " + String(OtaManager::getRunningPartitionName()) + " | Kế tiếp: " + String(OtaManager::getNextPartitionName()) + "\n";
      uint32_t up = millis() / 1000;
      out += "Thời gian hoạt động: " + String(up / 86400) + " ngày " + String((up % 86400) / 3600) + " giờ " + String((up % 3600) / 60) + " phút " + String(up % 60) + " giây (" + String(up) + "s)\n";
      out += "Lý do khởi động lại: " + String(getResetReasonStr(esp_reset_reason())) + "\n";
    }
    else if (sub == "profiler" || sub == "tasks" || sub == "cpu" || sub == "fps") {
      out = "=== [GIÁM SÁT HIỆU NĂNG TÁC VỤ PROFILER] ===\n";
      out += "Tần số khung hình  : " + String(TaskProfiler::getFps(), 1) + " FPS\n";
      out += "Thời gian chu kỳ loop():\n";
      out += "  - Vòng gần nhất  : " + String(TaskProfiler::getLoopLastUs()) + " µs\n";
      out += "  - Trung bình     : " + String(TaskProfiler::getLoopAvgUs()) + " µs\n";
      out += "  - Lớn nhất       : " + String(TaskProfiler::getLoopMaxUs()) + " µs\n";
    }
    else {
      out = "Lỗi: Module '" + sub + "' không tồn tại.\n";
      out += "Vui lòng chọn một trong các module sau:\n";
      out += "  status sht31 | sd | audio | mic | wifi | mem | motor | led | xiaozhi | display | sys | profiler\n";
      resp["success"] = false;
    }
  }
  // 3b. FACTORY-RESET / RESET-DEFAULTS
  else if (verb == "factory-reset" || verb == "reset-defaults" || (verb == "sys" && args == "reset")) {
    TestDisplay::performFactoryReset();
    out = "================ KHÔI PHỤC CÀI ĐẶT BAN ĐẦU ================\n";
    out += "Trạng thái : ĐÃ HOÀN TẤT THÀNH CÔNG\n";
    out += "Giao diện  : Đã chuyển sang MÈO GOME CHIBI (Theme 5)\n";
    out += "Tài khoản  : admin / admin (Web Control Center)\n";
    out += "Hệ thống   : Toàn bộ tùy chỉnh cá nhân đã được đặt lại.\n";
    out += "===========================================================\n";
  }
  // 4. CLEAN / SCAVENGE
  else if (verb == "clean" || verb == "scavenge" || verb == "gc") {
    ScavengeReport rep = MemoryScavenger::scavenge(true, "CLI_COMMAND");
    out = "=================== KẾT QUẢ THU HỒI BỘ NHỚ RAM ===================\n";
    out += "Lý do kích hoạt    : " + String(rep.triggerReason) + "\n";
    out += "Heap đã thu hồi    : " + String(rep.heapFreedBytes / 1024) + " KB (" + String(rep.heapFreedBytes) + " Bytes)\n";
    out += "PSRAM đã thu hồi   : " + String(rep.psramFreedBytes / 1024) + " KB (" + String(rep.psramFreedBytes) + " Bytes)\n";
    out += "Tổng RAM giải phóng: " + String(rep.totalFreedBytes / 1024) + " KB\n";
    out += "------------------------------------------------------------------\n";
    out += "Heap tự do hiện tại: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
    out += "PSRAM tự do hiện tại: " + String(ESP.getFreePsram() / (1024 * 1024)) + " MB (" + String(ESP.getFreePsram() / 1024) + " KB)\n";
    out += "Chế độ nghiêm ngặt : " + String(MemoryScavenger::getStrictModeName()) + "\n";
    out += "==================================================================\n";
  }
  // 5. STRICT <0|1|2>
  else if (verb == "strict") {
    if (args == "0" || args == "off") {
      MemoryScavenger::setStrictMode(STRICT_MODE_OFF);
    } else if (args == "2" || args == "aggressive" || args == "ultra") {
      MemoryScavenger::setStrictMode(STRICT_MODE_AGGRESSIVE);
    } else {
      MemoryScavenger::setStrictMode(STRICT_MODE_BALANCED);
    }
    out = "Đã cập nhật chế độ dọn RAM: [" + String(MemoryScavenger::getStrictMode()) + "] " + MemoryScavenger::getStrictModeName();
  }
  // 6. VOL <0-100>
  else if (verb == "vol" || verb == "volume") {
    int v = args.toInt();
    if (v < 0) v = 0; if (v > 100) v = 100;
    TestAudio::setSpeakerVolumePct(v);
    TestAudio::saveVolumePrefs();
    out = "Đã cài đặt âm lượng loa chính: " + String(v) + " %";
  }
  // 7. BEEP [freq] [ms]
  else if (verb == "beep") {
    int freq = 1000, dur = 150;
    if (args.length() > 0) {
      int sp = args.indexOf(' ');
      if (sp >= 0) {
        freq = args.substring(0, sp).toInt();
        dur = args.substring(sp + 1).toInt();
      } else {
        freq = args.toInt();
      }
    }
    if (freq <= 0) freq = 1000;
    if (dur <= 0) dur = 150;
    TestAudio::playTone(freq, dur);
    out = "Đang phát âm beep thử nghiệm: " + String(freq) + " Hz (" + String(dur) + " ms)";
  }
  // 8. TUNE <name>
  else if (verb == "tune") {
    String t = args; t.toLowerCase();
    if (t == "nokia" || t == "1") { TestAudio::playNokiaTune(); out = "Đang phát giai điệu: Nokia Tune"; }
    else if (t == "ok") { TestAudio::playOkChime(); out = "Đang phát âm: OK Chime"; }
    else if (t == "del" || t == "delete") { TestAudio::playDeleteChime(); out = "Đang phát âm: Delete Chime"; }
    else if (t == "sms" || t == "2") { TestAudio::playSmsSpecialTone(); out = "Đang phát âm: Nokia SMS Special"; }
    else if (t == "beep" || t == "0") { TestAudio::playKeyBeep(); out = "Đang phát âm: Key Beep"; }
    else if (t == "chime" || t == "3") { TestAudio::playStartupChime(); out = "Đang phát âm: Startup Chime"; }
    else {
      out = "Lỗi tune: Chọn một trong các giai điệu: nokia | ok | del | sms | beep | chime";
      resp["success"] = false;
    }
  }
  // 9. TOAST <message>
  else if (verb == "toast") {
    if (args.length() == 0) args = "Thong bao tu Web CLI!";
    TestDisplay::showSymbianToast(args);
    out = "Đã hiển thị thông báo Toast lên màn hình LCD: \"" + args + "\"";
  }
  // 10. MODE <id|name>
  else if (verb == "mode") {
    TestDisplay::setScreenMode(args);
    out = "Đã chuyển chế độ màn hình ST7789 sang: " + args;
  }
  // 11. MOTOR <0-100|off|on>
  else if (verb == "motor") {
    String low = args; low.toLowerCase();
    if (low == "off") {
      TestWindmill::setMotorSpeed(0);
      out = "Đã dừng động cơ cối xay gió.";
    } else if (low == "on") {
      TestWindmill::setMotorSpeed(60);
      out = "Đã bật động cơ cối xay gió ở mức 60%.";
    } else {
      int sp = args.toInt();
      if (sp < 0) sp = 0; if (sp > 100) sp = 100;
      TestWindmill::setMotorSpeed(sp);
      out = "Đã đặt tốc độ cối xay gió: " + String(sp) + " %";
    }
  }
  // 12. LED <left|right|cob> <val>
  else if (verb == "led") {
    String low = args; low.toLowerCase();
    if (low.startsWith("left ")) {
      int brt = low.substring(5).toInt();
      if (brt < 0) brt = 0; if (brt > 100) brt = 100;
      TestWindmill::setEdisonLeftBrightness(brt);
      out = "Đã đặt độ sáng bóng Edison Trái: " + String(brt) + "%";
    } else if (low.startsWith("right ")) {
      int brt = low.substring(6).toInt();
      if (brt < 0) brt = 0; if (brt > 100) brt = 100;
      TestWindmill::setEdisonRightBrightness(brt);
      out = "Đã đặt độ sáng bóng Edison Phải: " + String(brt) + "%";
    } else if (low.startsWith("cob next")) {
      CobLeds::nextMode();
      out = "Đã chuyển hiệu ứng LED COB sang: " + String(CobLeds::getModeName());
    } else if (low.startsWith("cob on")) {
      CobLeds::turnOn();
      out = "Đã bật dải LED COB lưng.";
    } else if (low.startsWith("cob off")) {
      CobLeds::turnOff();
      out = "Đã tắt dải LED COB lưng.";
    } else {
      out = "Cú pháp: led left <0-100> | led right <0-100> | led cob <next|on|off>";
      resp["success"] = false;
    }
  }
  // 13. SNAPSHOT
  else if (verb == "snapshot") {
    SystemLogger::logSystemSnapshot();
    out = "Đã ghi bản chụp hệ thống (Snapshot) vào bộ đệm và lưu xuống thẻ nhớ SD.";
  }
  // 14. CLEAR
  else if (verb == "clear") {
    resp["clear"] = true;
    out = "Màn hình Terminal đã được xóa sạch.";
  }
  // 15. REBOOT
  else if (verb == "reboot" || verb == "restart") {
    SystemLogger::log(LOG_LVL_WARN, "CLI", "Trạm nhận lệnh Reboot từ Web CLI Terminal.");
    resp["output"] = "Trạm Decor ESP32 đang khởi động lại trong giây lát...";
    String s; serializeJson(resp, s);
    server.send(200, "application/json", s);
    delay(400);
    ESP.restart();
    return;
  }
  // UNKNOWN
  else {
    out = "Lỗi: Không tìm thấy lệnh '" + verb + "'.\n";
    out += "Gõ 'help' hoặc 'modules' để xem danh sách lệnh được hỗ trợ.";
    resp["success"] = false;
  }

  resp["output"] = out;
  String resStr;
  serializeJson(resp, resStr);
  server.send(200, "application/json", resStr);
}


} // namespace ApiTerminal
