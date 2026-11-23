#include "services/PowerManagementService.h"
#include "Config.h"
#include "SystemLogger.h"
#include "drivers/DisplayDriver.h"
#include "drivers/SensorDriver.h"
#include "drivers/CobLedDriver.h"
#include "drivers/MotorDriver.h"
#include "drivers/AudioDriver.h"
#include "tests/TestDisplay.h"
#include "WebStreamManager.h"
#include "XiaoZhiClient.h"
#include "WebRoutes.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>

namespace PowerManagementService {

  static PowerProfile s_currentProfile = POWER_BALANCED;
  static bool s_autoSwitchEnabled = true;
  static unsigned long s_lastUserActivityMs = 0;
  static unsigned long s_screenSleepTimeoutMs = 300000; // 5 phút mặc định
  static unsigned long s_saturationStartMs = 0;
  static bool s_isBatteryFullyCharged = false;
  static unsigned long s_lastLoopCheckMs = 0;

  // Debounce chuyển cấu hình tự động
  static PowerProfile s_pendingProfile = POWER_BALANCED;
  static unsigned long s_pendingProfileSinceMs = 0;

  static const char* PROFILE_NAMES[] = { "PERFORMANCE", "BALANCED", "SAVER" };

  static void savePrefs() {
    Preferences p;
    if (p.begin("station_pwr", false)) {
      p.putUChar("pwr_prof", (uint8_t)s_currentProfile);
      p.putBool("auto_sw", s_autoSwitchEnabled);
      p.putULong("sleep_tout", s_screenSleepTimeoutMs);
      p.end();
    }
  }

  static void applyProfileSettings(PowerProfile prof) {
    uint32_t targetMhz = 240;
    wifi_ps_type_t targetWifiPs = WIFI_PS_NONE; // Luôn giữ WIFI_PS_NONE để WebControl & Wi-Fi phản hồi tức thì <2ms!
    uint32_t sensorInterval = 2500;
    unsigned long sleepTimeout = 0;

    switch (prof) {
      case POWER_PERFORMANCE:
        targetMhz = 240;
        targetWifiPs = WIFI_PS_NONE;
        sensorInterval = 2500;
        sleepTimeout = 0; // Không tự tắt màn hình khi cắm sạc/hiệu năng cao
        break;

      case POWER_BALANCED:
        targetMhz = 240; // Giữ 240MHz để ST7789, game và thao tác cảm ứng luôn đạt 60Hz/mượt mà
        targetWifiPs = WIFI_PS_NONE; // Tuyệt đối không bật Modem-Sleep gây lag 3-5s WebControl & rớt mạng
        sensorInterval = 10000;
        sleepTimeout = 300000;  // 5 phút
        break;

      case POWER_SAVER:
        targetMhz = 160; // Giữ tối thiểu 160MHz (không hạ xuống 80MHz làm giật quét ngang màn hình)
        targetWifiPs = WIFI_PS_NONE; // Vẫn giữ WIFI_PS_NONE để bàn phím ảo & Web phản hồi tức thì
        sensorInterval = 30000;
        sleepTimeout = 120000;  // 2 phút
        
        // Tắt bớt ngoại vi hao pin khi vào chế độ tiết kiệm pin
        if (CobLeds::isOn()) {
          CobLeds::turnOff();
          LOG_I("PWR", "Che do SAVER: Da tu dong tat 2 thanh LED COB (Tiet kiem ~80mA)");
        }
        if (MotorDriver::getEdisonBrightness() > 30) {
          MotorDriver::setEdisonBrightness(30);
          LOG_I("PWR", "Che do SAVER: Da giam do sang LED Edison xuong 30%");
        }
        break;
    }

    // 1. Điều chỉnh xung nhịp CPU ESP32-S3 (APB Clock luôn giữ 80MHz ở cả 240, 160, 80MHz)
    if (getCpuFrequencyMhz() != targetMhz) {
      setCpuFrequencyMhz(targetMhz);
    }

    // 2. Điều chỉnh Wi-Fi Modem Sleep
    if (WiFi.status() == WL_CONNECTED) {
      esp_wifi_set_ps(targetWifiPs);
    }

    // 3. Điều chỉnh chu kỳ đọc cảm biến SHT31
    SensorDriver::setSampleInterval(sensorInterval);

    // 4. Áp dụng thời gian chờ tắt màn hình
    s_screenSleepTimeoutMs = sleepTimeout;

    LOG_I("PWR", "Ap dung Profile: %s | CPU: %u MHz | Wi-Fi PS: %s | SHT31: %u ms | Sleep: %lu s",
          PROFILE_NAMES[prof], targetMhz, (targetWifiPs == WIFI_PS_NONE ? "NONE" : "MIN_MODEM"),
          sensorInterval, sleepTimeout / 1000);
  }

  void init() {
    s_lastUserActivityMs = millis();

    Preferences p;
    if (p.begin("station_pwr", true)) {
      uint8_t prof = p.getUChar("pwr_prof", (uint8_t)POWER_BALANCED);
      if (prof > 2) prof = (uint8_t)POWER_BALANCED;
      s_currentProfile = (PowerProfile)prof;
      s_autoSwitchEnabled = p.getBool("auto_sw", true);
      s_screenSleepTimeoutMs = p.getULong("sleep_tout", 300000);
      p.end();
    }

    // Nếu kích hoạt Auto Switch, tự động nhận diện theo nguồn khởi động
    if (s_autoSwitchEnabled) {
      float vbat = SensorDriver::getBatteryVoltage();
      // Nếu cắm sạc HOẶC điện áp < 2.5V (đang dùng nguồn ngoài 3.3V / USB trực tiếp, không có pin 18650)
      if (SensorDriver::isCharging() || vbat < 2.5f) {
        s_currentProfile = POWER_PERFORMANCE;
      } else {
        int pct = SensorDriver::getBatteryPercentage();
        s_currentProfile = (pct <= 15) ? POWER_SAVER : POWER_BALANCED;
      }
      applyProfileSettings(s_currentProfile);
    }

    s_pendingProfile = s_currentProfile;
    s_pendingProfileSinceMs = millis();
    applyProfileSettings(s_currentProfile);

    Serial.printf("⚡ [POWER SERVICE] Khoi tao thanh cong! Profile: %s | AutoSwitch: %s | SleepTimeout: %lu s\n",
                  PROFILE_NAMES[s_currentProfile], s_autoSwitchEnabled ? "BAT" : "TAT", s_screenSleepTimeoutMs / 1000);
  }

  void setProfile(PowerProfile profile, bool userOverride) {
    if (profile > POWER_SAVER) profile = POWER_BALANCED;
    if (userOverride) {
      s_autoSwitchEnabled = false; // Người dùng cố định profile -> Tắt tự động nhảy
    }
    s_currentProfile = profile;
    s_pendingProfile = profile;
    applyProfileSettings(profile);
    savePrefs();
  }

  PowerProfile getProfile() {
    return s_currentProfile;
  }

  const char* getProfileName() {
    return PROFILE_NAMES[s_currentProfile];
  }

  void setAutoSwitch(bool enabled) {
    s_autoSwitchEnabled = enabled;
    savePrefs();
    LOG_I("PWR", "Che do Auto Switch da %s", enabled ? "BAT" : "TAT");
  }

  bool isAutoSwitchEnabled() {
    return s_autoSwitchEnabled;
  }

  bool isBatteryFullyCharged() {
    return s_isBatteryFullyCharged;
  }

  uint32_t getFullChargeDurationSec() {
    if (s_saturationStartMs == 0) return 0;
    return (uint32_t)((millis() - s_saturationStartMs) / 1000);
  }

  unsigned long getSensorIntervalMs() {
    return SensorDriver::getSampleInterval();
  }

  unsigned long getScreenSleepTimeoutMs() {
    return s_screenSleepTimeoutMs;
  }

  void setScreenSleepTimeoutMs(unsigned long timeoutMs) {
    s_screenSleepTimeoutMs = timeoutMs;
    savePrefs();
  }

  void notifyUserActivity() {
    s_lastUserActivityMs = millis();
    bool wasSleeping = DisplayDriver::isSleeping();
    if (wasSleeping) {
      DisplayDriver::wakeUpScreenFromTask();
      LOG_I("PWR", "Danh thuc man hinh tu dong do co tuong tac nguoi dung!");
    }

    // Luôn đưa Wi-Fi về chế độ phản hồi tức thì (<2ms) khi có tương tác người dùng
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);
      esp_wifi_set_ps(WIFI_PS_NONE);
    }

    // Đánh thức Web Server & làm mới socket lwIP khi trạm vừa thức dậy từ chế độ ngủ
    if (wasSleeping) {
      WebRoutes::onStationWakeup();
    }
  }

  unsigned long getIdleDurationSec() {
    return (millis() - s_lastUserActivityMs) / 1000;
  }

  void onWifiConnected() {
    wifi_ps_type_t ps = (s_currentProfile == POWER_SAVER) ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE;
    esp_wifi_set_ps(ps);
    LOG_I("PWR", "Wi-Fi da ket noi, thiet lap Wi-Fi Power Save: %s", (ps == WIFI_PS_NONE ? "NONE" : "MIN_MODEM"));
  }

  void loop() {
    unsigned long now = millis();
    if (now - s_lastLoopCheckMs < 1000) return;
    s_lastLoopCheckMs = now;

    bool isCharging = SensorDriver::isCharging();
    float vbat = SensorDriver::getBatteryVoltage();
    int batteryPct = SensorDriver::getBatteryPercentage();

    // =========================================================================
    // 1. THUẬT TOÁN NHẬN DIỆN BÃO HÒA SẠC PIN 100% (FULL CHARGE SATURATION)
    // Pin Li-ion 18650 qua module MH-CD42 giai đoạn CV nạp duy trì >= 4.18V.
    // Khi giữ ổn định >= 4.18V trong 30 phút -> Xác nhận 100% bão hòa dòng nạp!
    // =========================================================================
    if (isCharging && vbat >= 4.18f) {
      if (s_saturationStartMs == 0) {
        s_saturationStartMs = now;
        LOG_I("PWR", "Bat dau giai doan nap bao hoa CV (Vbat=%.2fV >= 4.18V)", vbat);
      } else if (!s_isBatteryFullyCharged && (now - s_saturationStartMs >= 30 * 60 * 1000UL)) {
        s_isBatteryFullyCharged = true;
        LOG_I("PWR", "🎉 PIN DA SAC DAY 100% BAO HOA (Duy tri >= 4.18V trong 30 phut)!");
      }
    } else {
      // Khi rút sạc hoặc điện áp tụt dưới 4.15V -> Hủy trạng thái bão hòa sạc
      if (!isCharging || vbat < 4.15f) {
        if (s_saturationStartMs != 0 || s_isBatteryFullyCharged) {
          s_saturationStartMs = 0;
          s_isBatteryFullyCharged = false;
          LOG_I("PWR", "Ket thuc trang thai sac bao hoa (isCharging=%d, Vbat=%.2fV)", isCharging, vbat);
        }
      }
    }

    // =========================================================================
    // 2. TỰ ĐỘNG CHUYỂN PROFILE NGUỒN THEO TRẠNG THÁI (AUTO PROFILE SWITCH)
    // =========================================================================
    if (s_autoSwitchEnabled) {
      PowerProfile targetProfile = POWER_BALANCED;

      // Nhận diện nguồn ngoài / 3.3V rail:
      // Pin Lithium 18650 hoạt động từ 3.0V đến 4.2V. Nếu vbat < 2.5V tức là trạm đang được cấp nguồn
      // trực tiếp qua chân 3.3V hoặc USB không qua mạch đo phân áp pin -> Luôn chạy POWER_PERFORMANCE 240MHz!
      bool isExternal3v3OrUsb = isCharging || (vbat < 2.5f);

      if (isExternal3v3OrUsb) {
        targetProfile = POWER_PERFORMANCE; // Cắm sạc USB / Nguồn ngoài 3.3V -> Chạy hết công suất 240MHz
      } else {
        // Chạy pin thực tế: Nếu <= 15% -> Tiết kiệm pin (160MHz); Nếu > 15% -> Cân bằng (240MHz)
        if (batteryPct <= 15) {
          targetProfile = POWER_SAVER;
        } else if (s_currentProfile == POWER_SAVER && batteryPct < 20) {
          targetProfile = POWER_SAVER; // Hysteresis 5% chống nhảy qua lại liên tục
        } else {
          targetProfile = POWER_BALANCED;
        }
      }

      if (targetProfile != s_currentProfile) {
        if (targetProfile != s_pendingProfile) {
          s_pendingProfile = targetProfile;
          s_pendingProfileSinceMs = now;
        } else if (now - s_pendingProfileSinceMs >= 3000) { // Chống nhiễu gợn 3 giây
          s_currentProfile = targetProfile;
          applyProfileSettings(targetProfile);
          savePrefs();
          LOG_I("PWR", "Auto-Switch chuyen sang Profile: %s (isCharging=%d, Bat=%d%%, Vbat=%.2fV)",
                PROFILE_NAMES[targetProfile], isCharging, batteryPct, vbat);
        }
      } else {
        s_pendingProfile = s_currentProfile;
      }
    }

    // =========================================================================
    // 3. TỰ ĐỘNG TẮT MÀN HÌNH KHI KHÔNG SỬ DỤNG (AUTO SCREEN SLEEP)
    // Chỉ tự động tắt khi ở Màn hình chờ (Mode 0). Khi đang ở màn hình chức năng (XiaoZhi, Game, Stream,...) KHÔNG tự ý tắt.
    // =========================================================================
    if (s_screenSleepTimeoutMs > 0 && !DisplayDriver::isSleeping()) {
      if (now - s_lastUserActivityMs >= s_screenSleepTimeoutMs) {
        bool isInApp = (TestDisplay::getCurrentMode() != 0);
        bool isBusy = isInApp ||
                      WebStreamManager::isVideoStreaming() ||
                      WebStreamManager::isAudioStreaming() ||
                      AudioDriver::isVoiceRecording() ||
                      AudioDriver::isMusicPlaying() ||
                      AudioDriver::isVoiceStreamActive() ||
                      XiaoZhiClient::isConversationActive();

        if (!isBusy) {
          DisplayDriver::setSleeping(true);
          LOG_I("PWR", "Man hinh tu dong tat den nen (Idle %lu giay)", (now - s_lastUserActivityMs) / 1000);
        }
      }
    }
  }

  void getStatusJson(JsonDocument& doc) {
    doc["profile"]                  = (uint8_t)s_currentProfile;
    doc["profile_name"]             = PROFILE_NAMES[s_currentProfile];
    doc["cpu_mhz"]                  = getCpuFrequencyMhz();
    doc["wifi_ps"]                  = (s_currentProfile == POWER_PERFORMANCE ? "NONE" : "MIN_MODEM");
    doc["battery_v"]                = SensorDriver::getBatteryVoltage();
    doc["battery_mv"]               = SensorDriver::getBatteryMilliVolts();
    doc["battery_pct"]              = SensorDriver::getBatteryPercentage();
    doc["battery_bars"]             = SensorDriver::getBatteryBars();
    doc["charging"]                 = SensorDriver::isCharging();
    doc["fully_charged"]            = s_isBatteryFullyCharged;
    doc["full_charge_duration_sec"] = getFullChargeDurationSec();
    doc["auto_switch"]              = s_autoSwitchEnabled;
    doc["sensor_interval_ms"]       = SensorDriver::getSampleInterval();
    doc["screen_sleep_timeout_ms"]  = s_screenSleepTimeoutMs;
    doc["screen_sleeping"]          = DisplayDriver::isSleeping();
    doc["idle_sec"]                 = getIdleDurationSec();
    doc["speaker_suspended"]        = AudioDriver::isSpeakerSuspended();
    doc["mic_suspended"]            = AudioDriver::isMicSuspended();
  }

} // namespace PowerManagementService

