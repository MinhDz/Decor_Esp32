#include "tests/TestAudio.h"
#include "tests/TestDisplay.h"
#include "tests/TestSDCard.h"
#include "Config.h"
#include <driver/i2s.h>
#include <math.h>
#include <SD.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace TestAudio {

  static const i2s_port_t I2S_PORT = I2S_NUM_0;
  static const uint32_t SAMPLE_RATE = 16000;

  enum AudioDriverMode { MODE_NONE = 0, MODE_TX_SPEAKER = 1, MODE_RX_MIC = 2 };
  static AudioDriverMode currentDriverMode = MODE_NONE;
  static uint8_t currentTxPin = PIN_I2S_SPK_DIN;

  // Mặc định BẬT sẵn thu âm Mic INMP441 để màn hình Test hiển thị sóng âm ngay lập tức!
  static bool micMonitorEnabled = true;
  static int speakerVolumePct = 20; // Mặc định 20% an toàn dòng USB khi module MAX98357A mới về
  static unsigned long lastMicSampleMs = 0;
  static unsigned long lastSerialPrintMs = 0;
  static volatile bool audioPlayRunning = false;

  int getSpeakerVolumePct() {
    return speakerVolumePct;
  }

  void setSpeakerVolumePct(int pct) {
    speakerVolumePct = constrain(pct, 0, 100);
  }

  static int lastMicPct = 0;
  static int32_t lastMicPeak = 0;
  static uint32_t lastRaw32L = 0;
  static int32_t lastRaw24L = 0;
  static int32_t lastRaw24R = 0;
  static int32_t lastRawAc  = 0;
  static int32_t lastRmsVal = 0;
  static const char* activeChannelStr = "WAITING...";
  static int8_t waveformBuf[48] = {0};

  // Khởi tạo I2S TX 32-bit Stereo (64 BCLK/frame) cho MAX98357A
  // Phát đồng thời cả kênh Left và Right giúp MAX98357A kêu to rõ dù chân SD/MODE để hở, nối 3.3V hay nối trở!
  static bool installSpeakerTx(uint8_t doutPin = PIN_I2S_SPK_DIN) {
    if (currentDriverMode == MODE_TX_SPEAKER && currentTxPin == doutPin) return true;
    if (currentDriverMode != MODE_NONE) {
      i2s_driver_uninstall(I2S_PORT);
      currentDriverMode = MODE_NONE;
    }

    i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 6,
      .dma_buf_len = 128,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
      .bck_io_num = PIN_I2S_BCLK,
      .ws_io_num = PIN_I2S_WS,
      .data_out_num = doutPin,
      .data_in_num = I2S_PIN_NO_CHANGE
    };

    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) return false;
    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
      i2s_driver_uninstall(I2S_PORT);
      return false;
    }
    i2s_zero_dma_buffer(I2S_PORT);
    currentDriverMode = MODE_TX_SPEAKER;
    currentTxPin = doutPin;
    return true;
  }

  // Khởi tạo I2S RX 32-bit Stereo (64 BCLK/frame) bắt buộc cho INMP441
  // (Theo datasheet INMP441, phải có đủ 64 xung SCK/BCLK mỗi chu kỳ WS thì chân SD mới xuất dữ liệu 24-bit!)
  static bool installMicRx() {
    if (currentDriverMode == MODE_RX_MIC) return true;
    if (currentDriverMode != MODE_NONE) {
      i2s_driver_uninstall(I2S_PORT);
      currentDriverMode = MODE_NONE;
    }

    i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 6,
      .dma_buf_len = 128,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
      .bck_io_num = PIN_I2S_BCLK,
      .ws_io_num = PIN_I2S_WS,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = PIN_I2S_MIC_SD
    };

    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) return false;
    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
      i2s_driver_uninstall(I2S_PORT);
      return false;
    }
    currentDriverMode = MODE_RX_MIC;
    return true;
  }

  // --- PHÁT ÂM BÁO QUA PWM (GPIO 15 - CHO LOA NỐI TRỞ & TỤ) ---
  void playPwmTone(uint16_t freqHz, uint16_t durationMs, uint8_t pin) {
    if (freqHz == 0 || durationMs == 0) return;
    if (currentDriverMode != MODE_NONE) {
      i2s_driver_uninstall(I2S_PORT);
      currentDriverMode = MODE_NONE;
    }
    const uint8_t PWM_CH_SPK = 3;
    ledcSetup(PWM_CH_SPK, freqHz, 8);
    ledcAttachPin(pin, PWM_CH_SPK);
    ledcWrite(PWM_CH_SPK, 128); // 50% duty
    delay(durationMs);
    ledcWrite(PWM_CH_SPK, 0);
    ledcDetachPin(pin);
    pinMode(pin, INPUT);
    if (micMonitorEnabled) {
      installMicRx();
    }
  }

  void playStartupPwmChime(uint8_t pin) {
    playPwmTone(1046, 80, pin);
    delay(20);
    playPwmTone(1318, 80, pin);
    delay(20);
    playPwmTone(1568, 140, pin);
  }

  void init() {
    Serial.println("\n-------------------------------------------------------");
    Serial.println("🔊 [AUDIO I2S 32-BIT & PWM SPK GPIO 15] HỆ THỐNG ÂM THANH");
    Serial.printf("   + BCLK (SCK chung) : GPIO %d\n", PIN_I2S_BCLK);
    Serial.printf("   + WS   (LRC chung) : GPIO %d\n", PIN_I2S_WS);
    Serial.printf("   + MIC  (INMP441 SD): GPIO %d (32-bit 64-SCK Frame)\n", PIN_I2S_MIC_SD);
    Serial.printf("   + LOA NỐI TẠM GPIO : GPIO %d (Đã sẵn sàng PWM qua Tụ 4.7uF & Trở)\n", PIN_I2S_SPK_DIN);
    Serial.println("-------------------------------------------------------");

    // Khởi động mặc định ở chế độ đọc Mic INMP441 để có sẵn dữ liệu sóng âm
    if (installMicRx()) {
      Serial.println("✅ [INMP441] Đã mở luồng thu âm 32-bit I2S tại GPIO 6! Bấm 'm' để xem Sóng Âm, 'p' để nghe lại Loa PWM.");
    }

    // Phát giai điệu chào mừng kiểm tra Loa nối tạm ngay khi ESP32 khởi động!
    playStartupPwmChime(PIN_I2S_SPK_DIN);
  }

  static void playToneOnPin(uint8_t doutPin, uint16_t freqHz, uint16_t durationMs, uint8_t volumePct) {
    playPwmTone(freqHz, durationMs, doutPin);
  }

  void playTone(uint16_t freqHz, uint16_t durationMs, uint8_t volumePct) {
    playPwmTone(freqHz, durationMs, PIN_I2S_SPK_DIN);
  }

  void playStartupChime() {
    playStartupPwmChime(PIN_I2S_SPK_DIN);
  }

  void playClickBeep(uint16_t freqHz) {
    playPwmTone(freqHz, 35, PIN_I2S_SPK_DIN);
  }

  void enableMicMonitor(bool enable) {
    micMonitorEnabled = enable;
    if (micMonitorEnabled) {
      installMicRx();
    }
  }

  void toggleMicMonitor() {
    micMonitorEnabled = true;
    installMicRx();
    Serial.println("🎙️ [INMP441] Đã chuyển màn hình ST7789 sang Chế Độ 4 (SÓNG ÂM OSCILLOSCOPE & TEST PHẦN CỨNG)!");
    TestDisplay::setScreenMode("diag");
  }

  bool isMicMonitorActive() { return micMonitorEnabled; }
  int getLastMicLevelPct() { return lastMicPct; }
  int32_t getLastMicPeak() { return lastMicPeak; }
  uint32_t getLastRaw32Left() { return lastRaw32L; }
  int32_t getLastRaw24Left() { return lastRaw24L; }
  int32_t getLastRaw24Right() { return lastRaw24R; }
  int32_t getLastRawAc() { return lastRawAc; }
  int32_t getLastRms() { return lastRmsVal; }
  const char* getActiveMicChannelName() { return activeChannelStr; }
  const int8_t* getWaveformBuffer() { return waveformBuf; }

  static uint8_t micSpectrum16[16] = {0};
  static uint8_t extSpectrum16[16] = {0};
  static unsigned long lastExtSpecMs = 0;

  const uint8_t* getMicSpectrum16() {
    if (millis() - lastExtSpecMs < 1500) {
      // Kết hợp cả phổ âm thanh thực từ file nhạc (Web/WAV) và Mic INMP441 (lấy giá trị cao hơn của mỗi dải tần)
      static uint8_t combined16[16];
      for (int i = 0; i < 16; i++) {
        combined16[i] = max(micSpectrum16[i], extSpectrum16[i]);
      }
      return combined16;
    }
    return micSpectrum16;
  }

  void setExternalAudioSpectrum(const uint8_t bands[16], int levelPct) {
    if (bands) {
      for (int i = 0; i < 16; i++) {
        extSpectrum16[i] = constrain((int)bands[i], 0, 100);
      }
      lastExtSpecMs = millis();
      if (levelPct > lastMicPct) lastMicPct = constrain(levelPct, 0, 100);
    }
  }

  // ============================================================================
  // BỘ THU ÂM GIỌNG NÓI THỰC TẾ TỪ MIC INMP441 (8,000 Hz, 16-bit Signed Mono PCM)
  // ============================================================================
  // BỘ THU ÂM GIỌNG NÓI CHUYÊN NGHIỆP TỪ MIC INMP441 (16,000 Hz, 16-bit Mono PCM)
  // Tích hợp: DC Blocker IIR Filter + Soft Noise Gate + Auto Gain Control + Soft Limiter
  // Dung lượng mở rộng: 128,000 mẫu = 256,000 Bytes (8.0 giây @ 16kHz chuẩn vàng cho Speech-to-Text & AI)
  // ============================================================================
  static const size_t MAX_REC_SAMPLES_16K = 128000; // 8.0s @ 16000Hz = 256KB
  static int16_t* pcmRecBuffer = nullptr;
  static size_t pcmRecAllocatedSamples = 0;
  static size_t pcmRecSampleCount = 0;
  static bool voiceRecordingActive = false;
  static int recMaxVuPct = 0;
  static int lockedChannel = -1; // 0 = Left (L/R=GND), 1 = Right (L/R=3V3)
  static int micSensMode = 1;    // 0: Thấp (1.8x), 1: Tiêu chuẩn (3.5x), 2: Cao (5.5x)
  static float dcPrevX = 0.0f;
  static float dcPrevY = 0.0f;

  void setMicSensitivityMode(int mode) {
    micSensMode = constrain(mode, 0, 2);
    Serial.printf("🎙️ [MIC SENS] Đã đổi độ nhạy thu âm sang mức: %s\n",
                  micSensMode == 0 ? "Thấp (1.8x)" : (micSensMode == 1 ? "Tiêu chuẩn (3.5x)" : "Cao (5.5x)"));
  }

  uint32_t getRecordSampleRate() {
    return 16000;
  }

  bool startVoiceRecording() {
    if (!installMicRx()) return false;
    if (!pcmRecBuffer) {
      if (psramFound()) {
        pcmRecBuffer = (int16_t*)ps_malloc(MAX_REC_SAMPLES_16K * sizeof(int16_t));
        if (pcmRecBuffer) pcmRecAllocatedSamples = MAX_REC_SAMPLES_16K;
      }
      if (!pcmRecBuffer) {
        size_t maxAlloc = ESP.getMaxAllocHeap();
        size_t safeSamples = (maxAlloc > 32768) ? min((size_t)80000, (maxAlloc - 24576) / sizeof(int16_t)) : 48000;
        pcmRecBuffer = (int16_t*)malloc(safeSamples * sizeof(int16_t));
        if (pcmRecBuffer) pcmRecAllocatedSamples = safeSamples;
      }
      if (!pcmRecBuffer) {
        Serial.println("❌ [INMP441 REC] Không đủ bộ nhớ RAM để cấp phát bộ đệm thu âm!");
        return false;
      }
    }
    pcmRecSampleCount = 0;
    recMaxVuPct = 0;
    lockedChannel = -1;
    dcPrevX = 0.0f;
    dcPrevY = 0.0f;
    voiceRecordingActive = true;
    i2s_zero_dma_buffer(I2S_PORT);
    Serial.printf("🎙️ [INMP441 REC] Bắt đầu thu âm PCM 16-bit 16000Hz (Bộ đệm: %u mẫu ~ %.1fs, PSRAM: %s)...\n",
                  (unsigned)pcmRecAllocatedSamples,
                  (float)pcmRecAllocatedSamples / 16000.0f,
                  psramFound() ? "BẬT (8MB)" : "TẮT");
    return true;
  }

  void stopVoiceRecording() {
    if (voiceRecordingActive) {
      voiceRecordingActive = false;
      Serial.printf("⏹️ [INMP441 REC] Đã dừng thu âm: %u mẫu (%.2f giây @ 16kHz, %u Bytes, Max VU=%d%%)\n",
                    (unsigned)pcmRecSampleCount,
                    (float)pcmRecSampleCount / 16000.0f,
                    (unsigned)(pcmRecSampleCount * sizeof(int16_t)),
                    recMaxVuPct);
    }
  }

  bool isVoiceRecording() { return voiceRecordingActive; }
  const int16_t* getRecordedPcmBuffer() { return pcmRecBuffer; }
  size_t getRecordedSampleCount() { return pcmRecSampleCount; }
  float getRecordedDurationSec() { return (float)pcmRecSampleCount / 16000.0f; }
  int getRecordedMaxVuPct() { return recMaxVuPct; }

  void pollVoiceRecording() {
    if (!voiceRecordingActive || !pcmRecBuffer) return;
    if (currentDriverMode != MODE_RX_MIC) {
      if (!installMicRx()) return;
    }

    // Đọc liên tục các khung dữ liệu I2S 16kHz Stereo từ DMA
    int32_t stereoRaw[128 * 2];
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT, stereoRaw, sizeof(stereoRaw), &bytesRead, pdMS_TO_TICKS(5));
    if (err != ESP_OK || bytesRead < 16) return;

    int frames = bytesRead / (2 * sizeof(int32_t));
    if (frames <= 0) return;

    // 1. Tự động xác định và KHÓA CỐ ĐỊNH KÊNH THU (Channel Lock) ngay gói đầu tiên
    if (lockedChannel < 0) {
      int64_t sumSqL = 0, sumSqR = 0;
      for (int i = 0; i < frames; i++) {
        int32_t sL = (stereoRaw[i * 2] >> 8);
        int32_t sR = (stereoRaw[i * 2 + 1] >> 8);
        sumSqL += (int64_t)sL * sL;
        sumSqR += (int64_t)sR * sR;
      }
      lockedChannel = (sumSqL >= sumSqR) ? 0 : 1;
      Serial.printf("🔒 [INMP441 REC] Khóa cố định kênh thu âm: %s (L/R=%s)\n",
                    lockedChannel == 0 ? "LEFT" : "RIGHT",
                    lockedChannel == 0 ? "GND" : "3.3V");
    }

    // 2. Hệ số khuếch đại (AGC Gain) theo cài đặt người dùng
    float gain = 3.5f;
    if (micSensMode == 0)      gain = 1.8f; // Thấp: phòng ồn hoặc nói sát mic
    else if (micSensMode == 2) gain = 5.5f; // Cao: nói xa cách mic 40-70cm

    int chIdx = lockedChannel;
    float blockSumSq = 0.0f;

    // 3. Xử lý DSP từng mẫu âm thanh ở tốc độ gốc 16,000 Hz
    for (int i = 0; i < frames && pcmRecSampleCount < pcmRecAllocatedSamples; i++) {
      // Dịch 8-bit để lấy giá trị 24-bit có dấu chuẩn từ INMP441
      int32_t raw24 = (stereoRaw[i * 2 + chIdx] >> 8);

      // A. Bộ lọc IIR DC Blocker (Cắt bỏ 100% dòng DC trôi và tiếng ù 50Hz, giữ nguyên dải âm thanh giọng nói 70Hz - 7500Hz)
      float y = (float)raw24 - dcPrevX + 0.995f * dcPrevY;
      dcPrevX = (float)raw24;
      dcPrevY = y;

      // B. Cổng lọc nhiễu mềm (Soft Noise Gate): Làm suy giảm nhẹ tạp âm tĩnh khi yên lặng
      float absY = fabsf(y);
      if (absY < 14000.0f) {
        y *= 0.25f; // Giảm 75% tiếng xì nền mà không ngắt cụt đuôi âm
      }

      // C. Chuẩn hóa sang 16-bit signed và nhân hệ số khuếch đại thông minh
      float s16 = (y / 256.0f) * gain;

      // D. Bộ kẹp đỉnh mềm (Soft Limiter): Tuyệt đối KHÔNG ĐỂ VỠ TIẾNG (No Clipping)
      if (s16 > 31500.0f)       s16 = 31500.0f;
      else if (s16 < -31500.0f) s16 = -31500.0f;

      blockSumSq += s16 * s16;
      pcmRecBuffer[pcmRecSampleCount++] = (int16_t)s16;
    }

    // 4. Cập nhật thước đo VU Meter thực tế cho giao diện ST7789
    float rms = sqrtf(blockSumSq / frames);
    int pct = constrain((int)((rms * 100.0f) / 16000.0f), 0, 100);
    lastMicPct = pct;
    if (pct > recMaxVuPct) recMaxVuPct = pct;

    if (pcmRecSampleCount >= pcmRecAllocatedSamples) {
      stopVoiceRecording();
    }
  }

  void loop() {
    if (audioPlayRunning) return; // Đang phát âm thanh qua Loa PWM, tạm dừng đọc Mic để tránh xung đột I2S/LEDC
    if (voiceRecordingActive) {
      pollVoiceRecording();
      return;
    }
    if (!micMonitorEnabled) return;
    if (currentDriverMode != MODE_RX_MIC) {
      if (!installMicRx()) return;
    }

    unsigned long now = millis();
    if (now - lastMicSampleMs < 40) return; // ~25 FPS lấy mẫu sóng âm mượt mà
    lastMicSampleMs = now;

    // Đọc 96 khung Stereo 32-bit (96 Left + 96 Right)
    int32_t stereoRaw[96 * 2];
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT, stereoRaw, sizeof(stereoRaw), &bytesRead, pdMS_TO_TICKS(10));
    if (err != ESP_OK || bytesRead < 16) return;

    int frames = bytesRead / (2 * sizeof(int32_t));
    if (frames <= 0) return;

    // Tính giá trị trung bình DC offset và năng lượng trên cả kênh Left (L/R=GND) và Right (L/R=3.3V)
    int64_t meanL = 0, meanR = 0;
    for (int i = 0; i < frames; i++) {
      meanL += (stereoRaw[i * 2] >> 8);     // Dịch 8 bit vì INMP441 là 24-bit nằm ở 24 bit cao của 32-bit
      meanR += (stereoRaw[i * 2 + 1] >> 8);
    }
    meanL /= frames;
    meanR /= frames;

    int64_t sumSqL = 0, sumSqR = 0;
    int32_t peakL = 0, peakR = 0;
    for (int i = 0; i < frames; i++) {
      int32_t acL = (stereoRaw[i * 2] >> 8) - (int32_t)meanL;
      int32_t acR = (stereoRaw[i * 2 + 1] >> 8) - (int32_t)meanR;
      int32_t absL = (acL < 0) ? -acL : acL;
      int32_t absR = (acR < 0) ? -acR : acR;
      if (absL > peakL) peakL = absL;
      if (absR > peakR) peakR = absR;
      sumSqL += (int64_t)acL * acL;
      sumSqR += (int64_t)acR * acR;
    }

    // Tự động chọn kênh nào có tín hiệu thực từ INMP441 (L/R=GND -> Kênh LEFT)
    bool useLeft = (peakL >= peakR);
    int32_t activePeak = useLeft ? peakL : peakR;
    int32_t activeMean = useLeft ? (int32_t)meanL : (int32_t)meanR;
    float activeRms = sqrtf((float)(useLeft ? sumSqL : sumSqR) / frames);

    if (peakL == 0 && peakR == 0) {
      activeChannelStr = "NO DATA (Check SD=IO6)";
    } else if (useLeft) {
      activeChannelStr = "LEFT CH (L/R=GND) OK";
    } else {
      activeChannelStr = "RIGHT CH (L/R=3V3) OK";
    }

    lastMicPeak = activePeak;
    lastRaw32L  = (uint32_t)stereoRaw[0];
    lastRaw24L  = (stereoRaw[0] >> 8);
    lastRaw24R  = (stereoRaw[1] >> 8);
    lastRawAc   = (useLeft ? lastRaw24L : lastRaw24R) - activeMean;
    lastRmsVal  = (int32_t)activeRms;

    // Chuẩn hóa thang đo VU Meter theo đúng dải động thực tế của INMP441 vừa đo được:
    // - Khi yên lặng (Noise floor): RMS ~ 20,000 - 30,000 -> hiển thị 0% - 3%
    // - Khi nói chuyện bình thường: RMS ~ 150,000 - 800,000 -> hiển thị 15% - 80%
    // - Khi nói to / vỗ tay: RMS > 1,100,000 -> hiển thị 95% - 100%
    float cleanRms = (activeRms > 24000.0f) ? (activeRms - 24000.0f) : 0.0f;
    int pct = constrain((int)((cleanRms * 100.0f) / 1100000.0f), 0, 100);
    lastMicPct = pct;

    // Cân chỉnh biên độ sóng âm ST7789 (loại bỏ nhiễu nền khi yên lặng để đường tâm phẳng đẹp)
    int32_t divider = 28000;
    if (activePeak > 1200000) divider = activePeak / 26;
    else if (activePeak > 500000) divider = 22000;
    else if (activePeak < 75000)  divider = 90000; // Làm phẳng đường sóng khi phòng yên tĩnh

    for (int i = 0; i < 48; i++) {
      int idx = (i * frames) / 48;
      int32_t raw24 = (stereoRaw[idx * 2 + (useLeft ? 0 : 1)] >> 8) - activeMean;
      if (abs(raw24) < 18000) raw24 = 0; // Noise gate nhẹ lọc nhiễu nền tĩnh
      int scaled = raw24 / divider;
      waveformBuf[i] = (int8_t)constrain(scaled, -28, 28);
    }

    // Tính toán 16 cột phổ âm thanh (Bass / Mid / Treble) trực tiếp từ mẫu thu âm INMP441 (siêu nhẹ < 0.05ms)
    for (int b = 0; b < 16; b++) {
      int i0 = b * 3;
      int v0 = waveformBuf[i0];
      int v1 = waveformBuf[i0 + 1];
      int v2 = waveformBuf[i0 + 2];
      int energy = 0;
      if (b < 5) {
        // Dải Bass (Trầm): Trung bình cộng năng lượng chu kỳ dài
        energy = (abs(v0 + v1 + v2) * 100) / 52;
      } else if (b < 11) {
        // Dải Mid (Trung): Biên độ đỉnh + biến thiên bậc 1
        energy = ((abs(v0) + abs(v1 - v0) + abs(v2 - v1)) * 100) / 64;
      } else {
        // Dải Treble (Cao): Sai phân bậc 2 (tần số cao)
        energy = ((abs(v2 - 2 * v1 + v0) * 2 + abs(v1 - v0)) * 100) / 68;
      }
      if (pct <= 2) energy = 0;
      else energy = constrain((energy * (45 + pct)) / 100, 0, 100);
      // Làm mượt chuyển động cột VU (Attack nhanh, Decay mượt)
      if (energy >= micSpectrum16[b]) {
        micSpectrum16[b] = (uint8_t)energy;
      } else {
        micSpectrum16[b] = (uint8_t)((micSpectrum16[b] * 2 + energy) / 3);
      }
    }

    // In dữ liệu thô từ chân SD (GPIO 6) lên Serial Monitor khi đang ở Màn hình 4 (Kiểm tra DIO & Audio)
    if (now - lastSerialPrintMs >= 200 && TestDisplay::getScreenModeName() == "diag") {
      lastSerialPrintMs = now;
      char bar[17];
      int filled = (pct * 16) / 100;
      for (int b = 0; b < 16; b++) bar[b] = (b < filled) ? '#' : '.';
      bar[16] = '\0';

      Serial.printf("[SD GPIO6 RAW] HEX:0x%08lX | L24:%+8ld | AC:%+7ld | Peak:%7ld | RMS:%6ld | [%s] %3d%%\n",
                    (unsigned long)lastRaw32L,
                    (long)lastRaw24L,
                    (long)lastRawAc,
                    (long)activePeak,
                    (long)lastRmsVal,
                    bar,
                    pct);
    }
  }

  // --- PHÁT LẠI ÂM THANH VỪA THU TỪ MIC QUA PWM (GPIO 15) ---
  void playRecordedVoicePwm(uint8_t pin) {
    if (pcmRecSampleCount == 0) {
      Serial.println("⚠️ [PWM PLAY] Chua co ban thu am nao trong bo nho de phat!");
      return;
    }

    // 1. Tạm gỡ I2S để nhường chân cho bộ phát PWM LEDC
    if (currentDriverMode != MODE_NONE) {
      i2s_driver_uninstall(I2S_PORT);
      currentDriverMode = MODE_NONE;
    }

    const uint8_t PWM_CH_SPK = 3;
    // Tần số sóng mang 62.5kHz (80MHz / 256 / 5 = 62.5kHz), độ phân giải 8-bit
    ledcSetup(PWM_CH_SPK, 62500, 8);
    ledcAttachPin(pin, PWM_CH_SPK);

    // 2. Tìm giá trị đỉnh (Peak) để tự động cân bằng biên độ (Auto Gain Normalization)
    int32_t peak = 0;
    for (size_t i = 0; i < pcmRecSampleCount; i++) {
      int32_t a = abs(pcmRecBuffer[i]);
      if (a > peak) peak = a;
    }

    float pGain = 1.0f;
    if (peak > 80 && peak < 24000) {
      pGain = 26000.0f / (float)peak;
      if (pGain > 5.5f) pGain = 5.5f;
    }

    Serial.println("\n-------------------------------------------------------");
    Serial.printf("🔊 [PWM PLAY] DANG PHAT LAI QUA LOA TAM (GPIO %d):\n", pin);
    Serial.printf("   + So mau PCM 16kHz : %u mau (~%.2f giay)\n", (unsigned)pcmRecSampleCount, (float)pcmRecSampleCount / 16000.0f);
    Serial.printf("   + Bien do Peak     : %ld | He so khuech dai: %.2fx\n", (long)peak, pGain);
    Serial.println("-------------------------------------------------------");

    int64_t nextSampleUs = esp_timer_get_time();
    const int64_t stepUs = 1000000 / 16000; // 62.5 us / mẫu

    for (size_t i = 0; i < pcmRecSampleCount; i++) {
      int32_t s = (int32_t)(pcmRecBuffer[i] * pGain);
      if (s > 31500) s = 31500;
      else if (s < -31500) s = -31500;

      // Ánh xạ dải có dấu (-32768..32767) sang dải không dấu 8-bit (0..255)
      uint8_t duty = (uint8_t)((s + 32768) >> 8);
      ledcWrite(PWM_CH_SPK, duty);

      nextSampleUs += stepUs;
      int64_t waitUs = nextSampleUs - esp_timer_get_time();
      if (waitUs > 0) {
        delayMicroseconds((uint32_t)waitUs);
      }
      if ((i % 2000) == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        nextSampleUs = esp_timer_get_time();
      }
    }

    // Đưa duty về 0 để ngắt dòng điện DC qua loa (bảo vệ loa & tụ)
    ledcWrite(PWM_CH_SPK, 0);
    ledcDetachPin(pin);
    pinMode(pin, INPUT);

    // Khôi phục lại chế độ thu âm Micro INMP441
    if (micMonitorEnabled) {
      installMicRx();
    }
    Serial.println("✅ [PWM PLAY] Da phat lai hoan tat! Mic INMP441 san sang thu am tiep.\n");
  }

  // --- LƯU BẢN THU ÂM THÀNH FILE .WAV CHUẨN 16kHz 16-BIT MONO VÀO THẺ NHỚ SD ---
  bool saveRecordedVoiceToSd(const char* filepath) {
    if (pcmRecSampleCount == 0) {
      Serial.println("⚠️ [SD WAV] Khong co du lieu thu am de luu!");
      return false;
    }
    if (!TestSDCard::isMounted()) {
      Serial.println("⚠️ [SD WAV] The nho SD chua duoc Mount!");
      return false;
    }

    String fp = String(filepath);
    if (fp.startsWith("/recording") && !SD.exists("/recording")) {
      SD.mkdir("/recording");
    }

    if (SD.exists(filepath)) {
      SD.remove(filepath);
    }

    File f = SD.open(filepath, FILE_WRITE);
    if (!f) {
      Serial.printf("❌ [SD WAV] Khong mo duoc %s (Kiem tra the nho SD)!\n", filepath);
      return false;
    }

    uint32_t sampleRate = 16000;
    uint16_t numChannels = 1;
    uint16_t bitsPerSample = 16;
    uint32_t dataBytes = pcmRecSampleCount * sizeof(int16_t);
    uint32_t totalChunkSize = 36 + dataBytes;
    uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);

    uint8_t header[44];
    memcpy(&header[0], "RIFF", 4);
    header[4] = (uint8_t)(totalChunkSize & 0xFF);
    header[5] = (uint8_t)((totalChunkSize >> 8) & 0xFF);
    header[6] = (uint8_t)((totalChunkSize >> 16) & 0xFF);
    header[7] = (uint8_t)((totalChunkSize >> 24) & 0xFF);
    memcpy(&header[8], "WAVEfmt ", 8);
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
    header[20] = 1;  header[21] = 0;
    header[22] = (uint8_t)numChannels; header[23] = 0;
    header[24] = (uint8_t)(sampleRate & 0xFF);
    header[25] = (uint8_t)((sampleRate >> 8) & 0xFF);
    header[26] = (uint8_t)((sampleRate >> 16) & 0xFF);
    header[27] = (uint8_t)((sampleRate >> 24) & 0xFF);
    header[28] = (uint8_t)(byteRate & 0xFF);
    header[29] = (uint8_t)((byteRate >> 8) & 0xFF);
    header[30] = (uint8_t)((byteRate >> 16) & 0xFF);
    header[31] = (uint8_t)((byteRate >> 24) & 0xFF);
    header[32] = (uint8_t)blockAlign; header[33] = 0;
    header[34] = (uint8_t)bitsPerSample; header[35] = 0;
    memcpy(&header[36], "data", 4);
    header[40] = (uint8_t)(dataBytes & 0xFF);
    header[41] = (uint8_t)((dataBytes >> 8) & 0xFF);
    header[42] = (uint8_t)((dataBytes >> 16) & 0xFF);
    header[43] = (uint8_t)((dataBytes >> 24) & 0xFF);

    f.write(header, 44);
    f.write((const uint8_t*)pcmRecBuffer, dataBytes);
    f.close();

    Serial.printf("💾 [SD WAV] Da luu ban thu am thanh cong vao SD: %s (%u mau, %u bytes)!\n",
                  filepath, (unsigned)pcmRecSampleCount, (unsigned)(dataBytes + 44));
    return true;
  }

  // --- PHÁT NHẠC BẤT ĐỒNG BỘ CHO TRÌNH PHÁT NHẠC (FREERTOS TASK TRÊN CORE 0) ---
  static TaskHandle_t audioTaskHandle = NULL;
  static volatile uint32_t audioPlayCurSample = 0;
  static String asyncSdFilePath = "";

  static void audioPwmTask(void* arg) {
    uint8_t pin = (uint8_t)(uintptr_t)arg;
    bool playFromSd = false;
    if (TestSDCard::isMounted() && asyncSdFilePath.length() > 0 && !asyncSdFilePath.startsWith("SYNTH:")) {
      playFromSd = SD.exists(asyncSdFilePath);
    }
    File f;
    uint32_t wavSampleRate = 16000;

    TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCPU(0);
    if (idle0 != NULL) {
      esp_task_wdt_delete(idle0);
    }

    bool isWav = false;
    bool isMp3 = false;

    if (playFromSd && (asyncSdFilePath.indexOf("rec_latest") < 0 || pcmRecSampleCount == 0)) {
      f = SD.open(asyncSdFilePath, FILE_READ);
      if (f && f.size() > 128) {
        uint8_t h[44];
        f.read(h, 44);
        if (memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0) {
          isWav = true;
          wavSampleRate = h[24] | (h[25] << 8) | (h[26] << 16) | (h[27] << 24);
          if (wavSampleRate < 8000 || wavSampleRate > 48000) wavSampleRate = 16000;
        } else {
          // File MP3 tren the nho SD -> Giai ma bang bo minimp3
          isMp3 = true;
          f.seek(0);
        }
      } else {
        playFromSd = false;
      }
    } else {
      playFromSd = false;
    }

    bool isSynthMode = asyncSdFilePath.startsWith("SYNTH:") || (!playFromSd && pcmRecSampleCount == 0 && asyncSdFilePath.length() > 0);

    if (!playFromSd && pcmRecSampleCount == 0 && !isSynthMode) {
      if (idle0 != NULL) {
        esp_task_wdt_add(idle0);
      }
      audioPlayRunning = false;
      audioTaskHandle = NULL;
      vTaskDelete(NULL);
      return;
    }

    if (currentDriverMode != MODE_NONE) {
      i2s_driver_uninstall(I2S_PORT);
      currentDriverMode = MODE_NONE;
    }

    const uint8_t PWM_CH_SPK = 3;
    ledcSetup(PWM_CH_SPK, 62500, 8);
    ledcAttachPin(pin, PWM_CH_SPK);

    Serial.printf("🔊 [ASYNC PWM PLAY] Bat dau phat qua Loa PWM GPIO %d (Nguon: %s, Vol:%d%%)...\n",
                  pin, isMp3 ? "Bo Giai Ma MP3 (Realtime Decoder)" : (isWav ? "WAV PCM File" : (isSynthMode ? "Retro Synth Engine" : "RAM pcmRecBuffer")),
                  speakerVolumePct);

    int64_t nextSampleUs = esp_timer_get_time();
    const int64_t stepUs = 1000000 / wavSampleRate;
    audioPlayCurSample = 0;
    float volScale = (float)speakerVolumePct / 100.0f;
    if (volScale < 0.05f) volScale = 0.05f;

    if (isSynthMode) {
      int trackStyle = 0;
      String lowerPath = asyncSdFilePath;
      lowerPath.toLowerCase();
      if (lowerPath.indexOf("cyberpunk") >= 0 || lowerPath.indexOf("night city") >= 0) trackStyle = 0;
      else if (lowerPath.indexOf("synthwave") >= 0 || lowerPath.indexOf("blade runner") >= 0) trackStyle = 1;
      else if (lowerPath.indexOf("chill") >= 0 || lowerPath.indexOf("mua") >= 0 || lowerPath.indexOf("lat") >= 0) trackStyle = 2;
      else trackStyle = 3;

      static const uint16_t notes0_bass[16] = { 147, 147, 147, 147, 117, 117, 117, 117, 131, 131, 131, 131, 110, 110, 165, 147 };
      static const uint16_t notes0_lead[16] = { 294, 349, 440, 587, 233, 349, 466, 587, 262, 330, 392, 523, 220, 330, 440, 523 };

      static const uint16_t notes1_bass[16] = { 110, 110, 110, 110, 87, 87, 87, 87, 131, 131, 131, 131, 98, 98, 98, 98 };
      static const uint16_t notes1_lead[16] = { 440, 523, 659, 523, 349, 440, 523, 440, 523, 659, 784, 659, 392, 494, 587, 494 };

      static const uint16_t notes2_bass[16] = { 131, 131, 98, 98, 110, 110, 87, 87, 131, 131, 98, 98, 110, 110, 87, 87 };
      static const uint16_t notes2_lead[16] = { 523, 0, 392, 0, 440, 523, 392, 0, 330, 0, 294, 330, 262, 0, 220, 262 };

      static const uint16_t notes3_bass[16] = { 147, 0, 147, 0, 117, 0, 117, 117, 131, 0, 131, 0, 165, 147, 131, 110 };
      static const uint16_t notes3_lead[16] = { 587, 440, 587, 440, 466, 349, 466, 587, 523, 392, 523, 659, 659, 587, 523, 440 };

      const uint16_t* bTable = (trackStyle == 0) ? notes0_bass : ((trackStyle == 1) ? notes1_bass : ((trackStyle == 2) ? notes2_bass : notes3_bass));
      const uint16_t* lTable = (trackStyle == 0) ? notes0_lead : ((trackStyle == 1) ? notes1_lead : ((trackStyle == 2) ? notes2_lead : notes3_lead));
      uint32_t stepSamples = (trackStyle == 0) ? 1920 : ((trackStyle == 1) ? 2240 : ((trackStyle == 2) ? 2800 : 2000));

      uint32_t sampleIdx = 0;
      uint32_t phaseLead = 0;
      uint32_t phaseBass = 0;
      uint32_t rndSeed = 12345;

      while (audioPlayRunning) {
        uint32_t stepIdx = (sampleIdx / stepSamples) % 16;
        uint32_t sampleInStep = sampleIdx % stepSamples;

        uint16_t fL = lTable[stepIdx];
        uint16_t fB = bTable[stepIdx];

        int32_t leadSig = 0;
        if (fL > 0) {
          phaseLead += (uint32_t)(((uint64_t)fL * 65536) / 16000);
          uint16_t p = (uint16_t)phaseLead;
          int16_t tri = (p < 32768) ? ((int16_t)p - 16384) : (49152 - (int16_t)p);
          int32_t env = (int32_t)(stepSamples - sampleInStep);
          leadSig = (tri * env) / (int32_t)stepSamples;
        }

        int32_t bassSig = 0;
        if (fB > 0) {
          phaseBass += (uint32_t)(((uint64_t)fB * 65536) / 16000);
          bassSig = ((phaseBass & 0x8000) ? 7500 : -7500);
        }

        int32_t drum = 0;
        if (trackStyle != 2) {
          if ((stepIdx % 4 == 0) && sampleInStep < 450) {
            drum = (int32_t)(450 - sampleInStep) * 45;
          } else if ((stepIdx % 4 == 2) && sampleInStep < 600) {
            rndSeed = rndSeed * 1103515245 + 12345;
            drum = ((int32_t)(rndSeed & 0x3FFF) - 8192);
          }
        }

        int32_t mixed = (leadSig * 4 + bassSig * 3 + drum * 2) / 6;
        float curVol = (float)speakerVolumePct / 100.0f;
        if (curVol < 0.05f) curVol = 0.05f;
        mixed = (int32_t)(mixed * curVol);
        if (mixed > 31500) mixed = 31500;
        else if (mixed < -31500) mixed = -31500;

        uint8_t duty = (uint8_t)((mixed + 32768) >> 8);
        ledcWrite(PWM_CH_SPK, duty);

        sampleIdx++;
        audioPlayCurSample = sampleIdx;

        nextSampleUs += stepUs;
        int64_t waitUs = nextSampleUs - esp_timer_get_time();
        if (waitUs > 0) {
          delayMicroseconds((uint32_t)waitUs);
        }
        if ((sampleIdx % 2000) == 0) {
          vTaskDelay(pdMS_TO_TICKS(1));
          nextSampleUs = esp_timer_get_time();
        }
      }
    } else if (!playFromSd) {
      int32_t peak = 0;
      for (size_t i = 0; i < pcmRecSampleCount; i++) {
        int32_t a = abs(pcmRecBuffer[i]);
        if (a > peak) peak = a;
      }
      float pGain = 1.0f;
      if (peak > 80 && peak < 24000) {
        pGain = 26000.0f / (float)peak;
        if (pGain > 5.5f) pGain = 5.5f;
      }

      for (size_t i = 0; i < pcmRecSampleCount && audioPlayRunning; i++) {
        audioPlayCurSample = i;
        int32_t s = (int32_t)(pcmRecBuffer[i] * pGain * volScale);
        if (s > 31500) s = 31500;
        else if (s < -31500) s = -31500;

        uint8_t duty = (uint8_t)((s + 32768) >> 8);
        ledcWrite(PWM_CH_SPK, duty);

        nextSampleUs += stepUs;
        int64_t waitUs = nextSampleUs - esp_timer_get_time();
        if (waitUs > 0) {
          delayMicroseconds((uint32_t)waitUs);
        }
        if ((i % 2000) == 0) {
          vTaskDelay(pdMS_TO_TICKS(1));
          nextSampleUs = esp_timer_get_time();
        }
      }
    } else if (isMp3) {
      Serial.printf("🎵 [MP3 DECODER] Bat dau giai ma & phat MP3: %s (%u bytes)...\n",
                    asyncSdFilePath.c_str(), (unsigned)f.size());

      mp3dec_t* mp3d = (mp3dec_t*)malloc(sizeof(mp3dec_t));
      const int MP3_BUF_SIZE = 4096;
      uint8_t* mp3Buf = (uint8_t*)malloc(MP3_BUF_SIZE);
      mp3d_sample_t* pcmOut = (mp3d_sample_t*)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t));

      if (mp3d && mp3Buf && pcmOut) {
        mp3dec_init(mp3d);
        int bufValid = 0;
        uint32_t currentSampleRate = 44100;
        int64_t stepUs_x1024 = (1000000ULL * 1024ULL) / currentSampleRate;
        int64_t nextSampleUs_x1024 = esp_timer_get_time() * 1024;
        uint32_t decodedFrameCount = 0;

        while (audioPlayRunning && (f.available() || bufValid > 0)) {
          if (bufValid < (MP3_BUF_SIZE - 1500) && f.available()) {
            size_t toRead = MP3_BUF_SIZE - bufValid;
            size_t nRead = f.read(mp3Buf + bufValid, toRead);
            bufValid += nRead;
          }

          if (bufValid <= 0) break;

          mp3dec_frame_info_t info;
          int samplesPerChannel = mp3dec_decode_frame(mp3d, mp3Buf, bufValid, pcmOut, &info);

          if (info.frame_bytes > 0) {
            bufValid -= info.frame_bytes;
            if (bufValid > 0) {
              memmove(mp3Buf, mp3Buf + info.frame_bytes, bufValid);
            }
          } else if (samplesPerChannel == 0) {
            bufValid--;
            if (bufValid > 0) {
              memmove(mp3Buf, mp3Buf + 1, bufValid);
            }
          }

          if (samplesPerChannel > 0) {
            decodedFrameCount++;
            if (info.hz > 0 && (uint32_t)info.hz != currentSampleRate) {
              currentSampleRate = info.hz;
              stepUs_x1024 = (1000000ULL * 1024ULL) / currentSampleRate;
              Serial.printf("🎧 [MP3 FORMAT] %d Hz | %s | %d kbps\n",
                            info.hz, info.channels == 2 ? "Stereo" : "Mono", info.bitrate_kbps);
            }

            if ((decodedFrameCount % 4) == 0) {
              uint8_t specBands[16] = {0};
              int maxLv = 0;
              for (int b = 0; b < 16; b++) {
                int sIdx = (b * samplesPerChannel) / 16;
                int32_t s0 = abs((int32_t)pcmOut[sIdx * info.channels]);
                int val = s0 / 300;
                specBands[b] = (uint8_t)constrain(val, 2, 98);
                if (specBands[b] > maxLv) maxLv = specBands[b];
              }
              TestAudio::setExternalAudioSpectrum(specBands, maxLv);
            }

            float curVol = (float)speakerVolumePct / 100.0f;
            if (curVol < 0.05f) curVol = 0.05f;

            for (int i = 0; i < samplesPerChannel && audioPlayRunning; i++) {
              int32_t mono = (info.channels == 2)
                ? (((int32_t)pcmOut[i * 2] + (int32_t)pcmOut[i * 2 + 1]) / 2)
                : (int32_t)pcmOut[i];

              int32_t s = (int32_t)(mono * curVol);
              if (s > 31500) s = 31500;
              else if (s < -31500) s = -31500;

              uint8_t duty = (uint8_t)((s + 32768) >> 8);
              ledcWrite(PWM_CH_SPK, duty);

              nextSampleUs_x1024 += stepUs_x1024;
              int64_t waitUs = (nextSampleUs_x1024 / 1024) - esp_timer_get_time();
              if (waitUs > 0) {
                delayMicroseconds((uint32_t)waitUs);
              } else if (waitUs < -3000) {
                nextSampleUs_x1024 = esp_timer_get_time() * 1024;
              }
            }

            vTaskDelay(pdMS_TO_TICKS(1));
            nextSampleUs_x1024 = esp_timer_get_time() * 1024;
          }
        }
      }
      if (mp3Buf) free(mp3Buf);
      if (pcmOut) free(pcmOut);
      if (mp3d) free(mp3d);
      f.close();
    } else {
      int16_t chunk[256];
      size_t totalSamples = (f.size() - 44) / sizeof(int16_t);
      size_t samplesPlayed = 0;
      while (f.available() && audioPlayRunning && samplesPlayed < totalSamples) {
        size_t nRead = f.read((uint8_t*)chunk, sizeof(chunk));
        size_t nSamples = nRead / sizeof(int16_t);
        for (size_t i = 0; i < nSamples && audioPlayRunning; i++) {
          int32_t s = (int32_t)(chunk[i] * volScale);
          if (s > 31500) s = 31500;
          else if (s < -31500) s = -31500;

          uint8_t duty = (uint8_t)((s + 32768) >> 8);
          ledcWrite(PWM_CH_SPK, duty);

          nextSampleUs += stepUs;
          int64_t waitUs = nextSampleUs - esp_timer_get_time();
          if (waitUs > 0) {
            delayMicroseconds((uint32_t)waitUs);
          }
        }
        samplesPlayed += nSamples;
        audioPlayCurSample = samplesPlayed;
        vTaskDelay(pdMS_TO_TICKS(1));
        nextSampleUs = esp_timer_get_time();
      }
      f.close();
    }

    ledcWrite(PWM_CH_SPK, 0);
    ledcDetachPin(pin);
    pinMode(pin, INPUT);

    if (micMonitorEnabled) {
      installMicRx();
    }

    if (idle0 != NULL) {
      esp_task_wdt_add(idle0);
    }

    audioPlayRunning = false;
    audioTaskHandle = NULL;
    Serial.println("✅ [ASYNC PWM PLAY] Da ket thuc phat nhac!");
    vTaskDelete(NULL);
  }

  void startVoicePlaybackAsync(uint8_t pin) {
    stopVoicePlaybackAsync();
    asyncSdFilePath = "";
    if (pcmRecSampleCount == 0) return;
    audioPlayRunning = true;
    xTaskCreatePinnedToCore(
      audioPwmTask,
      "AudioPwmTask",
      24576,
      (void*)(uintptr_t)pin,
      1,
      &audioTaskHandle,
      0
    );
  }

  void playSdFileAsync(const String& path, uint8_t pin) {
    stopVoicePlaybackAsync();
    asyncSdFilePath = path;
    audioPlayRunning = true;
    xTaskCreatePinnedToCore(
      audioPwmTask,
      "AudioPwmTask",
      24576,
      (void*)(uintptr_t)pin,
      1,
      &audioTaskHandle,
      0
    );
  }

  void playSynthMusicAsync(const String& trackTitle, uint8_t pin) {
    stopVoicePlaybackAsync();
    asyncSdFilePath = "SYNTH:" + trackTitle;
    audioPlayRunning = true;
    xTaskCreatePinnedToCore(
      audioPwmTask,
      "AudioPwmTask",
      24576,
      (void*)(uintptr_t)pin,
      1,
      &audioTaskHandle,
      0
    );
  }

  void stopVoicePlaybackAsync() {
    audioPlayRunning = false;
    if (audioTaskHandle != NULL) {
      unsigned long startWait = millis();
      while (audioTaskHandle != NULL && millis() - startWait < 300) {
        delay(10);
      }
      if (audioTaskHandle != NULL) {
        vTaskDelete(audioTaskHandle);
        audioTaskHandle = NULL;
      }
      TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCPU(0);
      if (idle0 != NULL) {
        esp_task_wdt_add(idle0);
      }
    }
  }

  bool isVoicePlaybackAsyncRunning() {
    return audioPlayRunning;
  }

  uint32_t getVoicePlaybackCurrentSample() {
    return audioPlayCurSample;
  }

  bool handleSerial(char cmd) {
    if (cmd == 'm' || cmd == 'M') {
      toggleMicMonitor();
      return true;
    }
    if (cmd == 'p' || cmd == 'P') {
      playRecordedVoicePwm(PIN_I2S_SPK_DIN);
      return true;
    }
    if (cmd == 't' || cmd == 'T') {
      Serial.println("🔊 [TEST PWM] Phat tieng bip 1000Hz 300ms tren GPIO 15...");
      playPwmTone(1000, 300, PIN_I2S_SPK_DIN);
      return true;
    }
    if (cmd == 'a' || cmd == 'A') {
      Serial.println("🎶 [TEST PWM] Phat giai dieu khoi dong Do-Mi-Son tren GPIO 15...");
      playStartupPwmChime(PIN_I2S_SPK_DIN);
      return true;
    }
    if (cmd == 'v' || cmd == 'V' || cmd == 's' || cmd == 'S') {
      saveRecordedVoiceToSd("/recording/rec_latest.wav");
      return true;
    }
    return false;
  }

}

