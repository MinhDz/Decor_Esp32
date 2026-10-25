#include "drivers/AudioDriver.h"
#include "tests/TestDisplay.h"
#include "tests/TestSDCard.h"
#include "drivers/StorageDriver.h"
#include "WebStreamManager.h"
#include "Config.h"
#include <driver/i2s.h>
#include <math.h>
#include <SD.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace AudioDriver {

  static const i2s_port_t I2S_PORT_MIC = I2S_NUM_0;
  static const i2s_port_t I2S_PORT_SPK = I2S_NUM_1;
  static const uint32_t SAMPLE_RATE = 16000;

  static bool micDriverInstalled = false;
  static bool spkDriverInstalled = false;
  static uint8_t currentTxPin = PIN_I2S_SPK_DIN;

  // Mặc định BẬT sẵn thu âm Mic INMP441 để màn hình Test hiển thị sóng âm ngay lập tức!
  static bool micMonitorEnabled = true;

  // Quản lý âm lượng chuyên biệt 3 phân hệ (0 - 100%)
  static int notifyVolumePct  = 70; // Âm thông báo / Chuông / Phím bấm (Mặc định 70%)
  static int musicVolumePct   = 65; // Âm phát nhạc đa phương tiện (Mặc định 65%)
  static int xiaozhiVolumePct = 80; // Âm thoại Trợ lý XiaoZhi AI (Mặc định 80%)
  static int speakerVolumePct = 70; // Đồng bộ tương thích ngược với notifyVolumePct
  static bool volumePrefsLoaded = false;

  static unsigned long lastMicSampleMs = 0;
  static unsigned long lastSerialPrintMs = 0;
  static volatile bool audioPlayRunning = false;

  void loadVolumePrefs() {
    if (volumePrefsLoaded) return;
    volumePrefsLoaded = true;
    Preferences p;
    if (p.begin("audio_vol", true)) {
      notifyVolumePct  = p.getInt("notify", 70);
      musicVolumePct   = p.getInt("music", 65);
      xiaozhiVolumePct = p.getInt("ai", 80);
      p.end();
    }
    speakerVolumePct = notifyVolumePct;
    Serial.printf("🔊 [AUDIO VOL LOAD] Thông báo: %d%% | Phát nhạc: %d%% | XiaoZhi AI: %d%%\n",
                  notifyVolumePct, musicVolumePct, xiaozhiVolumePct);
  }

  void saveVolumePrefs() {
    Preferences p;
    if (p.begin("audio_vol", false)) {
      p.putInt("notify", notifyVolumePct);
      p.putInt("music", musicVolumePct);
      p.putInt("ai", xiaozhiVolumePct);
      p.end();
    }
  }

  int getNotifyVolumePct() {
    if (!volumePrefsLoaded) loadVolumePrefs();
    return notifyVolumePct;
  }

  void setNotifyVolumePct(int pct) {
    if (!volumePrefsLoaded) loadVolumePrefs();
    notifyVolumePct = constrain(pct, 0, 100);
    speakerVolumePct = notifyVolumePct;
    saveVolumePrefs();
  }

  int getMusicVolumePct() {
    if (!volumePrefsLoaded) loadVolumePrefs();
    return musicVolumePct;
  }

  void setMusicVolumePct(int pct) {
    if (!volumePrefsLoaded) loadVolumePrefs();
    musicVolumePct = constrain(pct, 0, 100);
    saveVolumePrefs();
  }

  int getXiaoZhiVolumePct() {
    if (!volumePrefsLoaded) loadVolumePrefs();
    return xiaozhiVolumePct;
  }

  void setXiaoZhiVolumePct(int pct) {
    if (!volumePrefsLoaded) loadVolumePrefs();
    xiaozhiVolumePct = constrain(pct, 0, 100);
    saveVolumePrefs();
  }

  int getSpeakerVolumePct() {
    if (!volumePrefsLoaded) loadVolumePrefs();
    return speakerVolumePct;
  }

  void setSpeakerVolumePct(int pct) {
    setNotifyVolumePct(pct);
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

  // Khởi tạo I2S1 TX 16-bit Stereo (32 BCLK/frame) chuyên biệt cho Loa MAX98357A
  // (Độc lập 100% với Mic I2S0, không bao giờ bị xung đột xung hay tranh chấp DMA)
  static bool installSpeakerTx(uint32_t sampleRate = 16000) {
    if (spkDriverInstalled) return true;

    i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = sampleRate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // 16-bit Stereo (xuất cả 2 kênh để hỗ trợ mọi cấu hình SD_MODE)
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,  // 8 bộ đệm DMA
      .dma_buf_len = 256,  // 256 mẫu mỗi đệm -> 8 * 256 = 2048 mẫu = 128ms buffer phần cứng tối ưu độ trễ thấp
      .use_apll = false,
      .tx_desc_auto_clear = true, // Tự động xóa DMA buffer bằng silence để triệt tiêu tiếng rè/ù
      .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
      .bck_io_num = PIN_I2S_SPK_BCLK, // GPIO 16
      .ws_io_num = PIN_I2S_SPK_LRC,   // GPIO 17
      .data_out_num = PIN_I2S_SPK_DIN,// GPIO 15
      .data_in_num = I2S_PIN_NO_CHANGE
    };

    if (i2s_driver_install(I2S_PORT_SPK, &i2s_config, 0, NULL) != ESP_OK) {
      Serial.println("❌ [MAX98357A] Lỗi i2s_driver_install I2S1!");
      return false;
    }
    if (i2s_set_pin(I2S_PORT_SPK, &pin_config) != ESP_OK) {
      Serial.println("❌ [MAX98357A] Lỗi i2s_set_pin I2S1!");
      i2s_driver_uninstall(I2S_PORT_SPK);
      return false;
    }
    i2s_zero_dma_buffer(I2S_PORT_SPK);
    spkDriverInstalled = true;
    currentTxPin = PIN_I2S_SPK_DIN;
    Serial.printf("✅ [MAX98357A] Đã khởi tạo I2S1 TX DAC (BCLK=GPIO %d, LRC=GPIO %d, DIN=GPIO %d) @ %u Hz!\n",
                  PIN_I2S_SPK_BCLK, PIN_I2S_SPK_LRC, PIN_I2S_SPK_DIN, sampleRate);
    return true;
  }

  // Khởi tạo I2S0 RX 32-bit Stereo (64 BCLK/frame) chuyên biệt cho Mic INMP441
  static bool installMicRx() {
    if (micDriverInstalled) return true;

    i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
      .bck_io_num = PIN_I2S_MIC_BCLK, // GPIO 4
      .ws_io_num = PIN_I2S_MIC_WS,    // GPIO 5
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = PIN_I2S_MIC_SD   // GPIO 6
    };

    if (i2s_driver_install(I2S_PORT_MIC, &i2s_config, 0, NULL) != ESP_OK) {
      Serial.println("❌ [INMP441] Lỗi i2s_driver_install I2S0!");
      return false;
    }
    if (i2s_set_pin(I2S_PORT_MIC, &pin_config) != ESP_OK) {
      Serial.println("❌ [INMP441] Lỗi i2s_set_pin I2S0!");
      i2s_driver_uninstall(I2S_PORT_MIC);
      return false;
    }
    micDriverInstalled = true;
    Serial.printf("✅ [INMP441] Đã khởi tạo I2S0 RX (BCLK=GPIO %d, WS=GPIO %d, SD=GPIO %d) @ %u Hz!\n",
                  PIN_I2S_MIC_BCLK, PIN_I2S_MIC_WS, PIN_I2S_MIC_SD, SAMPLE_RATE);
    return true;
  }

  // Phát sóng Sine chuẩn 16-bit Stereo mượt mà ra Loa MAX98357A (có sườn Attack/Release triệt tiêu tiếng tách/click)
  void playTone(uint16_t freqHz, uint16_t durationMs, uint8_t volumePct) {
    if (freqHz == 0 || durationMs == 0) return;
    int vol = (volumePct > 0) ? volumePct : speakerVolumePct;
    if (vol <= 0) return; // Đang tắt tiếng (Mute)
    vol = constrain(vol, 0, 100);

    if (!installSpeakerTx(16000)) return;
    if (audioPlayRunning) return; // Không can thiệp nếu đang chạy tác vụ phát nhạc nền
    if (WebStreamManager::isAudioStreaming()) return; // Tuyệt đối không chen tiếng bíp phá vỡ luồng stream I2S

    i2s_set_sample_rates(I2S_PORT_SPK, 16000);

    const size_t CHUNK_SIZE = 128;
    int16_t stereoChunk[CHUNK_SIZE * 2];
    uint32_t totalFrames = (16000ULL * durationMs) / 1000;
    uint32_t framesSent = 0;

    float amplitude = (28000.0f * (float)vol) / 100.0f;
    float phaseIncrement = (2.0f * (float)M_PI * (float)freqHz) / 16000.0f;
    float phase = 0.0f;

    uint32_t attackFrames = min((uint32_t)64, totalFrames / 6);
    uint32_t releaseFrames = min((uint32_t)96, totalFrames / 4);

    while (framesSent < totalFrames) {
      size_t n = min((uint32_t)CHUNK_SIZE, totalFrames - framesSent);
      for (size_t i = 0; i < n; i++) {
        uint32_t curFrame = framesSent + i;
        float env = 1.0f;
        if (curFrame < attackFrames && attackFrames > 0) {
          env = (float)curFrame / (float)attackFrames;
        } else if (totalFrames - curFrame < releaseFrames && releaseFrames > 0) {
          env = (float)(totalFrames - curFrame) / (float)releaseFrames;
        }

        int16_t sample = (int16_t)(sinf(phase) * amplitude * env);
        phase += phaseIncrement;
        if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

        stereoChunk[i * 2]     = sample; // Left
        stereoChunk[i * 2 + 1] = sample; // Right
      }

      size_t written = 0;
      i2s_write(I2S_PORT_SPK, stereoChunk, n * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
      framesSent += n;
    }
  }

  // Các hàm bọc tương thích ngược
  void playPwmTone(uint16_t freqHz, uint16_t durationMs, uint8_t pin) {
    playTone(freqHz, durationMs, speakerVolumePct);
  }

  void playStartupChime() {
    Serial.println("🎶 [MAX98357A] Đang phát âm thanh khởi động Trạm Không Gian...");
    playTone(523, 90, speakerVolumePct);  // C5
    delay(15);
    playTone(659, 90, speakerVolumePct);  // E5
    delay(15);
    playTone(784, 90, speakerVolumePct);  // G5
    delay(20);
    playTone(1046, 220, speakerVolumePct); // C6
  }

  void playStartupPwmChime(uint8_t pin) {
    playStartupChime();
  }

  void playClickBeep(uint16_t freqHz) {
    playTone(freqHz, 20, speakerVolumePct);
  }

  static bool audioInitialized = false;
  void init() {
    if (audioInitialized) return;
    audioInitialized = true;

    loadVolumePrefs();

    Serial.println("\n-------------------------------------------------------");
    Serial.println("🔊 [AUDIO I2S FULL-DUPLEX DÉP KÉP] HỆ THỐNG ÂM THANH");
    Serial.printf("   + MIC (INMP441 I2S0) : BCLK=GPIO %d | WS=GPIO %d  | SD=GPIO %d\n", PIN_I2S_MIC_BCLK, PIN_I2S_MIC_WS, PIN_I2S_MIC_SD);
    Serial.printf("   + LOA (MAX98357A I2S1): BCLK=GPIO %d | LRC=GPIO %d | DIN=GPIO %d\n", PIN_I2S_SPK_BCLK, PIN_I2S_SPK_LRC, PIN_I2S_SPK_DIN);
    Serial.println("-------------------------------------------------------");

    // Khởi tạo độc lập cả 2 kênh I2S phần cứng
    installMicRx();
    installSpeakerTx(16000);

    // Phát chuông khởi động kiểm tra Loa MAX98357A
    playStartupChime();
  }

  static void playToneOnPin(uint8_t doutPin, uint16_t freqHz, uint16_t durationMs, uint8_t volumePct) {
    playTone(freqHz, durationMs, volumePct);
  }

  // --- BỘ ÂM BÁO & NHẠC CHUÔNG MONO CỔ ĐIỂN NOKIA SYMBIAN S40 (I2S DAC) ---
  void playKeyBeep() {
    // Tiếng bíp bàn phím mono siêu ngắn dứt khoát (~16ms, C7 2093Hz) chuẩn Nokia S40
    playTone(2093, 16, speakerVolumePct);
  }

  void playOkChime() {
    // Âm xác nhận OK / Lưu thành công (E6 1318Hz -> B6 1975Hz vui tươi, dứt khoát)
    playTone(1318, 40, speakerVolumePct);
    delay(12);
    playTone(1975, 80, speakerVolumePct);
  }

  void playDeleteChime() {
    // Âm xóa tệp / Hủy bỏ / Cảnh báo (G5 784Hz -> D5 587Hz âm trầm dứt khoát)
    playTone(784, 55, speakerVolumePct);
    delay(15);
    playTone(587, 95, speakerVolumePct);
  }

  void playSmsSpecialTone() {
    // Âm tin nhắn Nokia Morse SMS kinh điển: "... -- ..." (S-M-S)
    for (int i = 0; i < 3; i++) {
      playTone(1350, 40, speakerVolumePct);
      delay(35);
    }
    delay(75);
    for (int i = 0; i < 2; i++) {
      playTone(1350, 110, speakerVolumePct);
      delay(45);
    }
    delay(75);
    for (int i = 0; i < 3; i++) {
      playTone(1350, 40, speakerVolumePct);
      delay(35);
    }
  }

  void playNokiaTune() {
    // Nhạc chuông Nokia Tune huyền thoại (Grande Valse Francisco Tárrega)
    struct Note { uint16_t freq; uint16_t dur; };
    const Note melody[] = {
      { 1318, 110 }, // E6
      { 1175, 110 }, // D6
      { 740,  220 }, // F#5
      { 831,  220 }, // G#5
      { 1109, 110 }, // C#6
      { 988,  110 }, // B5
      { 587,  220 }, // D5
      { 659,  220 }, // E5
      { 988,  110 }, // B5
      { 880,  110 }, // A5
      { 554,  220 }, // C#5
      { 659,  220 }, // E5
      { 880,  440 }  // A5
    };
    for (size_t i = 0; i < sizeof(melody) / sizeof(melody[0]); i++) {
      playTone(melody[i].freq, melody[i].dur, speakerVolumePct);
      delay(15);
    }
  }

  static const char* S40_ALARM_TUNE_NAMES[4] = {
    "1. Bíp Dồn Dập (Standard)",
    "2. Nokia Tune Cổ Điển",
    "3. Tin Nhắn SMS (Morse)",
    "4. Chuông 4 Nốt (C5-C6)"
  };

  const char* getAlarmTuneName(int tuneIdx) {
    if (tuneIdx < 0 || tuneIdx >= 4) return S40_ALARM_TUNE_NAMES[0];
    return S40_ALARM_TUNE_NAMES[tuneIdx];
  }

  int getAlarmTuneCount() { return 4; }

  void playAlarmTuneStep(int tuneIdx, int step) {
    if (speakerVolumePct <= 0) return;
    switch (tuneIdx) {
      case 1: {
        // Nokia Tune: Chạy từng cụm 4 nốt theo chu kỳ hoạt họa báo thức
        struct Note { uint16_t f; uint16_t d; };
        const Note p0[] = { { 1318, 95 }, { 1175, 95 }, { 740, 190 }, { 831, 190 } };
        const Note p1[] = { { 1109, 95 }, { 988, 95 }, { 587, 190 }, { 659, 190 } };
        const Note p2[] = { { 988, 95 }, { 880, 95 }, { 554, 190 }, { 659, 190 } };
        const Note p3[] = { { 880, 360 } };
        const Note* phrases[4] = { p0, p1, p2, p3 };
        const int phraseLens[4] = { 4, 4, 4, 1 };
        int pIdx = step % 4;
        for (int i = 0; i < phraseLens[pIdx]; i++) {
          playTone(phrases[pIdx][i].f, phrases[pIdx][i].d, speakerVolumePct);
          delay(12);
        }
        break;
      }
      case 2: {
        // Tin nhắn SMS Morse (... -- ...)
        for (int i = 0; i < 3; i++) {
          playTone(1350, 35, speakerVolumePct);
          delay(25);
        }
        delay(60);
        for (int i = 0; i < 2; i++) {
          playTone(1350, 95, speakerVolumePct);
          delay(35);
        }
        delay(60);
        for (int i = 0; i < 3; i++) {
          playTone(1350, 35, speakerVolumePct);
          delay(25);
        }
        break;
      }
      case 3: {
        // Chuông 4 Nốt (Ascending Chime C5-E5-G5-C6)
        playTone(523, 70, speakerVolumePct);
        delay(15);
        playTone(659, 70, speakerVolumePct);
        delay(15);
        playTone(784, 70, speakerVolumePct);
        delay(15);
        playTone(1046, 130, speakerVolumePct);
        break;
      }
      case 0:
      default: {
        // Bíp dồn dập Nokia Standard (Beep-Beep dồn nhịp)
        int rep = (step % 2 == 0) ? 2 : 3;
        for (int i = 0; i < rep; i++) {
          playTone(1760, 50, speakerVolumePct);
          delay(25);
          playTone(2349, 70, speakerVolumePct);
          delay(30);
        }
        break;
      }
    }
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

  // Theo dõi hoạt động luồng âm thanh thời gian thực (TTS XiaoZhi & Nhạc)
  static volatile unsigned long s_lastStreamPcmMs = 0;
  static volatile uint8_t s_streamVoiceEnergy = 0;

  bool isVoiceStreamActive() {
    return (s_lastStreamPcmMs > 0 && (millis() - s_lastStreamPcmMs < 600));
  }

  uint8_t getVoiceStreamEnergy() {
    if (!isVoiceStreamActive()) return 0;
    return s_streamVoiceEnergy;
  }

  bool isMusicPlaying() {
    return isVoicePlaybackAsyncRunning() || (millis() - lastExtSpecMs < 1200 && !isVoiceStreamActive());
  }

  void getAudioSpectrumLevels(uint8_t& bass, uint8_t& treble) {
    const uint8_t* spec = getMicSpectrum16();
    int bMax = 0;
    for (int i = 0; i < 6; i++) {
      if (spec[i] > bMax) bMax = spec[i];
    }
    int tMax = 0;
    for (int i = 6; i < 16; i++) {
      if (spec[i] > tMax) tMax = spec[i];
    }
    bass = (uint8_t)constrain(bMax, 0, 100);
    treble = (uint8_t)constrain(tMax, 0, 100);
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
  static float preEmphPrev = 0.0f;
  static bool firstSampleFiltered = false;

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
    // Xả sạch dữ liệu DMA cũ còn tồn đọng trong hàng đợi I2S trước khi bắt đầu phiên thu mới
    size_t discarded = 0;
    int32_t dumpBuf[128 * 2];
    for (int d = 0; d < 8; d++) {
      if (i2s_read(I2S_PORT_MIC, dumpBuf, sizeof(dumpBuf), &discarded, 0) != ESP_OK || discarded == 0) break;
    }
    i2s_zero_dma_buffer(I2S_PORT_MIC);

    pcmRecSampleCount = 0;
    recMaxVuPct = 0;
    lockedChannel = 0; // INMP441 có L/R nối đất (GND) -> Kênh thu chuẩn 100% là Kênh Trái (LEFT)
    dcPrevX = 0.0f;
    dcPrevY = 0.0f;
    preEmphPrev = 0.0f;
    firstSampleFiltered = false;
    voiceRecordingActive = true;
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
    if (!micDriverInstalled) {
      if (!installMicRx()) return;
    }

    // Đọc liên tục các khung dữ liệu I2S 16kHz Stereo từ DMA
    int32_t stereoRaw[128 * 2];
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT_MIC, stereoRaw, sizeof(stereoRaw), &bytesRead, pdMS_TO_TICKS(5));
    if (err != ESP_OK || bytesRead < 16) return;

    int frames = bytesRead / (2 * sizeof(int32_t));
    if (frames <= 0) return;

    // 1. Kênh thu chuẩn từ INMP441 (Chân L/R nối GND nên dữ liệu chuẩn luôn nằm ở kênh LEFT)
    int chIdx = 0;

    // 2. Hệ số khuếch đại (AGC Gain) theo cài đặt người dùng
    float gain = 3.5f;
    if (micSensMode == 0)      gain = 1.8f; // Thấp: phòng ồn hoặc nói sát mic
    else if (micSensMode == 2) gain = 5.5f; // Cao: nói xa cách mic 40-70cm

    float blockSumSq = 0.0f;

    // 3. Xử lý DSP từng mẫu âm thanh ở tốc độ gốc 16,000 Hz
    for (int i = 0; i < frames && pcmRecSampleCount < pcmRecAllocatedSamples; i++) {
      int32_t raw24 = (stereoRaw[i * 2 + chIdx] >> 8);

      // Khởi tạo mẫu đầu tiên mượt mà, triệt tiêu 100% cú sốc DC nhảy bước ở t=0ms
      if (!firstSampleFiltered) {
        dcPrevX = (float)raw24;
        dcPrevY = 0.0f;
        preEmphPrev = 0.0f;
        firstSampleFiltered = true;
        continue;
      }

      // A. Bộ lọc High-Pass IIR chuẩn thoại 80Hz (alpha = 0.9695f @ 16kHz)
      // Cắt bỏ 100% DC trôi, rung chấn cơ học và tiếng ù 50Hz, bảo toàn 100% âm sắc tự nhiên
      float y = 0.9695f * (dcPrevY + (float)raw24 - dcPrevX);
      dcPrevX = (float)raw24;
      dcPrevY = y;

      // B. Bộ lọc Pre-emphasis giọng nói: z[n] = y[n] - 0.40 * y[n-1]
      // Nâng dải tần số phụ âm 1.5kHz - 4.5kHz (+3.5dB), giúp AI nhận diện cực chuẩn âm đầu ("A", "B", "T", "CH")
      float pe = y - 0.40f * preEmphPrev;
      preEmphPrev = y;

      // C. Chuẩn hóa sang 16-bit signed và nhân hệ số khuếch đại giọng nói
      float s16 = (pe / 256.0f) * gain;

      // D. Bộ kẹp đỉnh mềm (Soft Limiter): Tránh méo tiếng (No Clipping) khi nói to
      if (s16 > 31000.0f)       s16 = 31000.0f;
      else if (s16 < -31000.0f) s16 = -31000.0f;

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
    if (voiceRecordingActive) {
      pollVoiceRecording();
      return;
    }
    if (!micMonitorEnabled) return;
    if (!micDriverInstalled) {
      if (!installMicRx()) return;
    }

    unsigned long now = millis();
    if (now - lastMicSampleMs < 40) return; // ~25 FPS lấy mẫu sóng âm mượt mà
    lastMicSampleMs = now;

    // Đọc 96 khung Stereo 32-bit (96 Left + 96 Right)
    int32_t stereoRaw[96 * 2];
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT_MIC, stereoRaw, sizeof(stereoRaw), &bytesRead, pdMS_TO_TICKS(10));
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

  // --- PHÁT LẠI ÂM THANH VỪA THU TỪ MIC QUA I2S DAC MAX98357A (GPIO 15/16/17) ---
  void playRecordedVoice(uint8_t volumePct) {
    if (pcmRecSampleCount == 0 || !pcmRecBuffer) {
      Serial.println("⚠️ [MAX98357A] Chưa có bản thu âm nào trong bộ nhớ để phát!");
      return;
    }
    if (!installSpeakerTx(16000)) return;
    i2s_set_sample_rates(I2S_PORT_SPK, 16000);

    // 1. Tìm giá trị đỉnh (Peak) để tự động cân bằng biên độ (Auto Gain Normalization)
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

    float volScale = (float)constrain((int)volumePct, 0, 100) / 100.0f;
    Serial.println("\n-------------------------------------------------------");
    Serial.println("🔊 [MAX98357A I2S DAC] ĐANG PHÁT LẠI GIỌNG NÓI VỪA THU:");
    Serial.printf("   + Số mẫu PCM 16kHz : %u mẫu (~%.2f giây)\n", (unsigned)pcmRecSampleCount, (float)pcmRecSampleCount / 16000.0f);
    Serial.printf("   + Biên độ Peak     : %ld | He so Gain: %.2fx | Âm lượng: %d%%\n", (long)peak, pGain, volumePct);
    Serial.println("-------------------------------------------------------");

    const size_t CHUNK = 128;
    int16_t stereoChunk[CHUNK * 2];
    size_t i = 0;
    while (i < pcmRecSampleCount) {
      size_t n = min(CHUNK, pcmRecSampleCount - i);
      for (size_t j = 0; j < n; j++) {
        int32_t s = (int32_t)(pcmRecBuffer[i + j] * pGain * volScale);
        if (s > 31500) s = 31500;
        else if (s < -31500) s = -31500;
        stereoChunk[j * 2]     = (int16_t)s;
        stereoChunk[j * 2 + 1] = (int16_t)s;
      }
      size_t written = 0;
      i2s_write(I2S_PORT_SPK, stereoChunk, n * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
      i += n;
    }
    i2s_zero_dma_buffer(I2S_PORT_SPK);
    Serial.println("✅ [MAX98357A] Đã phát lại giọng nói hoàn tất!\n");
  }

  void playRecordedVoicePwm(uint8_t pin) {
    playRecordedVoice(speakerVolumePct);
  }

  // --- LƯU BẢN THU ÂM THÀNH FILE .WAV CHUẨN 16kHz 16-BIT MONO VÀO THẺ NHỚ SD ---
  bool saveRecordedVoiceToSd(const char* filepath) {
    if (pcmRecSampleCount == 0 || !pcmRecBuffer) {
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

    // --- TÁC VỤ PHÁT NHẠC ĐƠN TUYẾN TRỰC TIẾP I2S DMA (FREERTOS TASK TRÊN CORE 0) ---
  static TaskHandle_t audioTaskHandle = NULL;
  static volatile uint32_t audioPlayCurSample = 0;
  static String asyncSdFilePath = "";

  static void audioPwmTask(void* arg) {
    uint8_t pin = (uint8_t)(uintptr_t)arg;
    bool playFromSd = false;
    if (StorageDriver::isMounted() && asyncSdFilePath.length() > 0 && !asyncSdFilePath.startsWith("SYNTH:")) {
      if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
        playFromSd = SD.exists(asyncSdFilePath);
        StorageDriver::unlock();
      }
    }
    File f;
    uint32_t wavSampleRate = 16000;
    uint16_t wavChannels = 1;

    bool isWav = false;
    bool isMp3 = false;

    if (playFromSd && (asyncSdFilePath.indexOf("rec_latest") < 0 || pcmRecSampleCount == 0)) {
      if (StorageDriver::lock(pdMS_TO_TICKS(150))) {
        f = SD.open(asyncSdFilePath, FILE_READ);
        if (f && f.size() > 128) {
          uint8_t h[44];
          f.read(h, 44);
          if (memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0) {
            isWav = true;
            wavSampleRate = h[24] | (h[25] << 8) | (h[26] << 16) | (h[27] << 24);
            wavChannels = h[22] | (h[23] << 8);
            if (wavSampleRate < 8000 || wavSampleRate > 48000) wavSampleRate = 16000;
            if (wavChannels != 1 && wavChannels != 2) wavChannels = 1;
          } else {
            // File MP3 trên thẻ nhớ SD -> Giải mã bằng bộ minimp3
            isMp3 = true;
            f.seek(0);
          }
        } else {
          playFromSd = false;
        }
        StorageDriver::unlock();
      } else {
        playFromSd = false;
      }
    } else {
      playFromSd = false;
    }

    bool isSynthMode = asyncSdFilePath.startsWith("SYNTH:") || (!playFromSd && pcmRecSampleCount == 0 && asyncSdFilePath.length() > 0);

    if (!playFromSd && pcmRecSampleCount == 0 && !isSynthMode) {
      audioPlayRunning = false;
      audioTaskHandle = NULL;
      vTaskDelete(NULL);
      return;
    }

    uint32_t currentSampleRate = isWav ? wavSampleRate : 44100;
    installSpeakerTx(currentSampleRate);
    i2s_set_sample_rates(I2S_PORT_SPK, currentSampleRate);
    audioPlayCurSample = 0;

    Serial.printf("🔊 [ASYNC I2S PLAY] Bắt đầu phát qua Loa MAX98357A I2S DAC (Nguồn: %s, Vol: %d%%)...\n",
                  isMp3 ? "Bộ Giải Mã MP3 (minimp3)" : (isWav ? "WAV PCM File" : (isSynthMode ? "Retro Synth Engine" : "RAM pcmRecBuffer")),
                  musicVolumePct);

    if (isSynthMode) {
      i2s_set_sample_rates(I2S_PORT_SPK, 16000);
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

      const size_t CHUNK = 128;
      int16_t stereoChunk[CHUNK * 2];

      while (audioPlayRunning) {
        float volScale = (musicVolumePct > 0) ? ((float)musicVolumePct / 100.0f) : 0.0f;
        for (size_t c = 0; c < CHUNK && audioPlayRunning; c++) {
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
          mixed = (int32_t)(mixed * volScale);
          if (mixed > 31500) mixed = 31500;
          else if (mixed < -31500) mixed = -31500;

          stereoChunk[c * 2]     = (int16_t)mixed;
          stereoChunk[c * 2 + 1] = (int16_t)mixed;

          sampleIdx++;
        }

        size_t written = 0;
        i2s_write(I2S_PORT_SPK, stereoChunk, CHUNK * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(50));
        audioPlayCurSample += CHUNK;

        // Cập nhật 16 cột phổ âm thanh cho màn hình ST7789
        uint8_t specBands[16] = {0};
        int maxLv = 0;
        for (int b = 0; b < 16; b++) {
          int sIdx = (b * CHUNK) / 16;
          int32_t val = abs((int32_t)stereoChunk[sIdx * 2]) / 300;
          specBands[b] = (uint8_t)constrain(val, 2, 98);
          if (specBands[b] > maxLv) maxLv = specBands[b];
        }
        TestAudio::setExternalAudioSpectrum(specBands, maxLv);

        vTaskDelay(pdMS_TO_TICKS(1));
      }
    } else if (!playFromSd) {
      i2s_set_sample_rates(I2S_PORT_SPK, 16000);
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

      const size_t CHUNK = 128;
      int16_t stereoChunk[CHUNK * 2];
      size_t i = 0;
      while (i < pcmRecSampleCount && audioPlayRunning) {
        float volScale = (musicVolumePct > 0) ? ((float)musicVolumePct / 100.0f) : 0.0f;
        size_t n = min(CHUNK, pcmRecSampleCount - i);
        for (size_t j = 0; j < n; j++) {
          int32_t s = (int32_t)(pcmRecBuffer[i + j] * pGain * volScale);
          if (s > 31500) s = 31500;
          else if (s < -31500) s = -31500;
          stereoChunk[j * 2]     = (int16_t)s;
          stereoChunk[j * 2 + 1] = (int16_t)s;
        }
        size_t written = 0;
        i2s_write(I2S_PORT_SPK, stereoChunk, n * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(50));
        audioPlayCurSample += n;
        i += n;

        uint8_t specBands[16] = {0};
        int maxLv = 0;
        for (int b = 0; b < 16; b++) {
          int sIdx = (b * n) / 16;
          int32_t val = abs((int32_t)stereoChunk[sIdx * 2]) / 300;
          specBands[b] = (uint8_t)constrain(val, 2, 98);
          if (specBands[b] > maxLv) maxLv = specBands[b];
        }
        TestAudio::setExternalAudioSpectrum(specBands, maxLv);

        vTaskDelay(pdMS_TO_TICKS(1));
      }
    } else if (isMp3) {
      Serial.printf("🎵 [MP3 DECODER] Bắt đầu giải mã & phát MP3: %s (%u bytes)...\n",
                    asyncSdFilePath.c_str(), (unsigned)f.size());

      // Cấp phát trong DRAM nội bộ (SRAM) để tránh 100% lỗi DMA và căn lề bộ nhớ của Octal PSRAM
      mp3dec_t* mp3d = (mp3dec_t*)malloc(sizeof(mp3dec_t));
      const int MP3_BUF_SIZE = 8192; // 8KB đệm DRAM an toàn tuyệt đối
      uint8_t* mp3Buf = (uint8_t*)malloc(MP3_BUF_SIZE);
      mp3d_sample_t* pcmOut = (mp3d_sample_t*)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t));
      int16_t* stereoPcm = (int16_t*)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * 2 * sizeof(int16_t));

      if (mp3d && mp3Buf && pcmOut && stereoPcm) {
        mp3dec_init(mp3d);
        int bufValid = 0;
        currentSampleRate = 44100;
        i2s_set_sample_rates(I2S_PORT_SPK, currentSampleRate);

        while (audioPlayRunning && (f.available() || bufValid > 0)) {
          if (bufValid < (MP3_BUF_SIZE - 2048) && f.available()) {
            if (StorageDriver::lock(pdMS_TO_TICKS(120))) {
              size_t toRead = MP3_BUF_SIZE - bufValid;
              size_t nRead = f.read(mp3Buf + bufValid, toRead);
              bufValid += nRead;
              StorageDriver::unlock();
            } else {
              vTaskDelay(pdMS_TO_TICKS(3));
            }
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
            if (info.hz > 0 && (uint32_t)info.hz != currentSampleRate) {
              currentSampleRate = info.hz;
              i2s_set_sample_rates(I2S_PORT_SPK, currentSampleRate);
              Serial.printf("🎧 [MP3 FORMAT] %d Hz | %s | %d kbps\n",
                            info.hz, info.channels == 2 ? "Stereo" : "Mono", info.bitrate_kbps);
            }

            float volScale = (musicVolumePct > 0) ? ((float)musicVolumePct / 100.0f) : 0.0f;
            for (int i = 0; i < samplesPerChannel; i++) {
              if (info.channels == 2) {
                int32_t l = (int32_t)(pcmOut[i * 2] * volScale);
                int32_t r = (int32_t)(pcmOut[i * 2 + 1] * volScale);
                stereoPcm[i * 2]     = (int16_t)constrain(l, -31500, 31500);
                stereoPcm[i * 2 + 1] = (int16_t)constrain(r, -31500, 31500);
              } else {
                int32_t m = (int32_t)(pcmOut[i] * volScale);
                int16_t s = (int16_t)constrain(m, -31500, 31500);
                stereoPcm[i * 2]     = s;
                stereoPcm[i * 2 + 1] = s;
              }
            }

            size_t written = 0;
            i2s_write(I2S_PORT_SPK, stereoPcm, samplesPerChannel * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
            audioPlayCurSample += samplesPerChannel;

            // Cập nhật phổ âm thanh cho ST7789
            if (samplesPerChannel >= 16) {
              uint8_t specBands[16] = {0};
              int maxLv = 0;
              for (int b = 0; b < 16; b++) {
                int sIdx = (b * samplesPerChannel) / 16;
                int32_t val = abs((int32_t)stereoPcm[sIdx * 2]) / 300;
                specBands[b] = (uint8_t)constrain(val, 2, 98);
                if (specBands[b] > maxLv) maxLv = specBands[b];
              }
              TestAudio::setExternalAudioSpectrum(specBands, maxLv);
            }
          }

          // Nhường CPU cho IDLE task trên Core 0 để nuôi Task Watchdog Timer
          vTaskDelay(pdMS_TO_TICKS(1));
        }
      }
      if (stereoPcm) free(stereoPcm);
      if (mp3Buf) free(mp3Buf);
      if (pcmOut) free(pcmOut);
      if (mp3d) free(mp3d);

      if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
        f.close();
        StorageDriver::unlock();
      }
    } else {
      currentSampleRate = wavSampleRate;
      i2s_set_sample_rates(I2S_PORT_SPK, currentSampleRate);
      const size_t WAV_CHUNK = 512;
      int16_t* chunk = (int16_t*)malloc(WAV_CHUNK * sizeof(int16_t));
      int16_t* stereoChunk = (int16_t*)malloc(WAV_CHUNK * 2 * sizeof(int16_t));
      size_t totalBytes = (f.size() > 44) ? (f.size() - 44) : 0;
      size_t bytesRead = 0;

      if (chunk && stereoChunk) {
        while (audioPlayRunning && bytesRead < totalBytes && f.available()) {
          size_t nReadBytes = 0;
          if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
            if (f.available()) {
              nReadBytes = f.read((uint8_t*)chunk, WAV_CHUNK * sizeof(int16_t));
            }
            StorageDriver::unlock();
          } else {
            vTaskDelay(pdMS_TO_TICKS(3));
            continue;
          }

          if (nReadBytes == 0) break;

          bytesRead += nReadBytes;
          float volScale = (musicVolumePct > 0) ? ((float)musicVolumePct / 100.0f) : 0.0f;

          if (wavChannels == 2) {
            size_t nPairs = nReadBytes / (2 * sizeof(int16_t));
            for (size_t j = 0; j < nPairs; j++) {
              int32_t l = (int32_t)(chunk[j * 2] * volScale);
              int32_t r = (int32_t)(chunk[j * 2 + 1] * volScale);
              stereoChunk[j * 2]     = (int16_t)constrain(l, -31500, 31500);
              stereoChunk[j * 2 + 1] = (int16_t)constrain(r, -31500, 31500);
            }
            size_t written = 0;
            i2s_write(I2S_PORT_SPK, stereoChunk, nPairs * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
            audioPlayCurSample += nPairs;
          } else {
            size_t nSamples = nReadBytes / sizeof(int16_t);
            for (size_t j = 0; j < nSamples; j++) {
              int32_t s = (int32_t)(chunk[j] * volScale);
              if (s > 31500) s = 31500;
              else if (s < -31500) s = -31500;
              stereoChunk[j * 2]     = (int16_t)s;
              stereoChunk[j * 2 + 1] = (int16_t)s;
            }
            size_t written = 0;
            i2s_write(I2S_PORT_SPK, stereoChunk, nSamples * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
            audioPlayCurSample += nSamples;
          }

          uint8_t specBands[16] = {0};
          int maxLv = 0;
          for (int b = 0; b < 16; b++) {
            int sIdx = (b * (nReadBytes / sizeof(int16_t))) / 16;
            int32_t val = abs((int32_t)stereoChunk[sIdx * 2]) / 300;
            specBands[b] = (uint8_t)constrain(val, 2, 98);
            if (specBands[b] > maxLv) maxLv = specBands[b];
          }
          TestAudio::setExternalAudioSpectrum(specBands, maxLv);

          vTaskDelay(pdMS_TO_TICKS(1));
        }
      }
      if (chunk) free(chunk);
      if (stereoChunk) free(stereoChunk);

      if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
        f.close();
        StorageDriver::unlock();
      }
    }

    if (spkDriverInstalled) {
      i2s_zero_dma_buffer(I2S_PORT_SPK);
      i2s_set_sample_rates(I2S_PORT_SPK, 16000);
    }

    audioPlayRunning = false;
    audioTaskHandle = NULL;
    Serial.println("✅ [ASYNC I2S PLAY] Đã kết thúc phát nhạc qua MAX98357A!");
    vTaskDelete(NULL);
  }

  // --- BỘ ĐỆM BẤT ĐỒNG BỘ STREAM (XIAOZHI TTS / WEB STREAM) TRÊN CORE 0 ---
  static int16_t* s_asyncStreamRingBuf = nullptr;
  static const size_t ASYNC_STREAM_RING_CAPACITY = 64000; // 128KB PSRAM (~4 giây Mono 16kHz)
  static size_t s_asyncStreamHead = 0;
  static size_t s_asyncStreamTail = 0;
  static size_t s_asyncStreamCount = 0;
  static portMUX_TYPE s_asyncStreamMux = portMUX_INITIALIZER_UNLOCKED;
  static TaskHandle_t s_asyncStreamPumpHandle = nullptr;
  static volatile bool s_asyncStreamPumpRunning = false;

  static void asyncStreamPumpTask(void* param) {
    const size_t CHUNK = 256;
    int16_t stereoChunk[CHUNK * 2];
    int16_t monoChunk[CHUNK];

    while (s_asyncStreamPumpRunning) {
      size_t count = 0;
      taskENTER_CRITICAL(&s_asyncStreamMux);
      count = s_asyncStreamCount;
      taskEXIT_CRITICAL(&s_asyncStreamMux);

      if (count == 0) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        continue;
      }

      size_t toRead = (count > CHUNK) ? CHUNK : count;
      taskENTER_CRITICAL(&s_asyncStreamMux);
      for (size_t i = 0; i < toRead; i++) {
        monoChunk[i] = s_asyncStreamRingBuf[s_asyncStreamHead];
        s_asyncStreamHead = (s_asyncStreamHead + 1) % ASYNC_STREAM_RING_CAPACITY;
      }
      s_asyncStreamCount -= toRead;
      taskEXIT_CRITICAL(&s_asyncStreamMux);

      s_lastStreamPcmMs = millis();

      if (!spkDriverInstalled) {
        if (!installSpeakerTx(16000)) {
          vTaskDelay(pdMS_TO_TICKS(10));
          continue;
        }
      }

      int64_t sumAmp = 0;
      for (size_t k = 0; k < toRead; k += 8) {
        int32_t val = monoChunk[k];
        if (val < 0) val = -val;
        sumAmp += val;
      }
      int avgAmp = (toRead > 0) ? (int)(sumAmp / (toRead / 8 + 1)) : 0;
      s_streamVoiceEnergy = (uint8_t)constrain(avgAmp / 110, 0, 100);

      int vol = max(musicVolumePct, xiaozhiVolumePct);
      float volScale = (vol > 0) ? ((float)vol / 100.0f) : 0.0f;
      for (size_t j = 0; j < toRead; j++) {
        int32_t s = (int32_t)(monoChunk[j] * volScale);
        if (s > 31500) s = 31500;
        else if (s < -31500) s = -31500;
        stereoChunk[j * 2]     = (int16_t)s;
        stereoChunk[j * 2 + 1] = (int16_t)s;
      }

      size_t written = 0;
      i2s_write(I2S_PORT_SPK, stereoChunk, toRead * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(50));

      if (toRead >= 16) {
        uint8_t specBands[16] = {0};
        int maxLv = 0;
        for (int b = 0; b < 16; b++) {
          int sIdx = (b * toRead) / 16;
          int val = abs((int)monoChunk[sIdx]) / 280;
          specBands[b] = (uint8_t)constrain(val, 2, 98);
          if (specBands[b] > maxLv) maxLv = specBands[b];
        }
        TestAudio::setExternalAudioSpectrum(specBands, maxLv);
      }
    }

    s_asyncStreamPumpHandle = nullptr;
    vTaskDelete(NULL);
  }

  void playStreamPcmAsync(const int16_t* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0) return;

    if (!s_asyncStreamRingBuf) {
      if (psramFound()) {
        s_asyncStreamRingBuf = (int16_t*)ps_malloc(ASYNC_STREAM_RING_CAPACITY * sizeof(int16_t));
      }
      if (!s_asyncStreamRingBuf) {
        s_asyncStreamRingBuf = (int16_t*)malloc(8000 * sizeof(int16_t));
      }
      s_asyncStreamHead = 0;
      s_asyncStreamTail = 0;
      s_asyncStreamCount = 0;
    }
    if (!s_asyncStreamRingBuf) return;

    size_t cap = psramFound() ? ASYNC_STREAM_RING_CAPACITY : 8000;

    if (!s_asyncStreamPumpHandle) {
      s_asyncStreamPumpRunning = true;
      xTaskCreatePinnedToCore(
        asyncStreamPumpTask,
        "asyncStreamPump",
        4096,
        nullptr,
        4,
        &s_asyncStreamPumpHandle,
        0
      );
    }

    taskENTER_CRITICAL(&s_asyncStreamMux);
    for (size_t i = 0; i < sampleCount; i++) {
      if (s_asyncStreamCount < cap) {
        s_asyncStreamRingBuf[s_asyncStreamTail] = samples[i];
        s_asyncStreamTail = (s_asyncStreamTail + 1) % cap;
        s_asyncStreamCount++;
      } else {
        s_asyncStreamHead = (s_asyncStreamHead + 1) % cap;
        s_asyncStreamRingBuf[s_asyncStreamTail] = samples[i];
        s_asyncStreamTail = (s_asyncStreamTail + 1) % cap;
      }
    }
    taskEXIT_CRITICAL(&s_asyncStreamMux);

    if (s_asyncStreamPumpHandle) {
      xTaskNotifyGive(s_asyncStreamPumpHandle);
    }
  }

  void stopStreamPcmAsync() {
    taskENTER_CRITICAL(&s_asyncStreamMux);
    s_asyncStreamHead = 0;
    s_asyncStreamTail = 0;
    s_asyncStreamCount = 0;
    taskEXIT_CRITICAL(&s_asyncStreamMux);
    s_lastStreamPcmMs = 0;
    s_streamVoiceEnergy = 0;
    if (spkDriverInstalled) {
      i2s_zero_dma_buffer(I2S_PORT_SPK);
    }
  }

  bool isStreamPcmAsyncPlaying() {
    bool hasData = false;
    taskENTER_CRITICAL(&s_asyncStreamMux);
    hasData = (s_asyncStreamCount > 0);
    taskEXIT_CRITICAL(&s_asyncStreamMux);
    return hasData || isVoiceStreamActive();
  }

  // --- PHÁT DỮ LIỆU ÂM THANH THEO LUỒNG THỜI GIAN THỰC (TTS XIAOZHI) ---
  void resetStreamPcm() {
    stopStreamPcmAsync();
    s_lastStreamPcmMs = 0;
    s_streamVoiceEnergy = 0;
    if (spkDriverInstalled) {
      i2s_zero_dma_buffer(I2S_PORT_SPK);
    }
  }

  void playStreamPcm(const int16_t* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0) return;
    s_lastStreamPcmMs = millis();
    {
      int64_t sumAmp = 0;
      size_t step = max((size_t)1, sampleCount / 32);
      size_t count = 0;
      for (size_t k = 0; k < sampleCount; k += step) {
        int32_t val = samples[k];
        if (val < 0) val = -val;
        sumAmp += val;
        count++;
      }
      int avgAmp = (count > 0) ? (int)(sumAmp / count) : 0;
      s_streamVoiceEnergy = (uint8_t)constrain(avgAmp / 110, 0, 100);
    }
    if (!spkDriverInstalled) {
      if (!installSpeakerTx(16000)) return;
    } else {
      static uint32_t s_lastStreamHz = 0;
      if (s_lastStreamHz != 16000) {
        i2s_set_sample_rates(I2S_PORT_SPK, 16000);
        s_lastStreamHz = 16000;
      }
    }

    if (audioPlayRunning) {
      stopVoicePlaybackAsync();
    }

    int vol = max(musicVolumePct, xiaozhiVolumePct);
    float volScale = (vol > 0) ? ((float)vol / 100.0f) : 0.0f;
    const size_t CHUNK = 256;
    int16_t stereoChunk[CHUNK * 2];

    size_t offset = 0;
    while (offset < sampleCount) {
      size_t n = min(CHUNK, sampleCount - offset);
      for (size_t j = 0; j < n; j++) {
        int32_t s = (int32_t)(samples[offset + j] * volScale);
        if (s > 31500) s = 31500;
        else if (s < -31500) s = -31500;
        stereoChunk[j * 2]     = (int16_t)s;
        stereoChunk[j * 2 + 1] = (int16_t)s;
      }
      size_t toWriteBytes = n * 2 * sizeof(int16_t);
      size_t written = 0;
      // Dùng timeout 100ms để giữ nhịp đồng hồ phần cứng I2S (Hardware Clock Pacing) mượt mà
      // đồng thời tránh tình trạng treo cứng vĩnh viễn (deadlock) nếu bus I2S gặp sự cố
      esp_err_t res = i2s_write(I2S_PORT_SPK, stereoChunk, toWriteBytes, &written, pdMS_TO_TICKS(100));
      if (res == ESP_OK && written > 0) {
        offset += written / (2 * sizeof(int16_t));
      } else {
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    }

    // Cập nhật phổ âm thanh cho màn hình ST7789
    if (sampleCount >= 16) {
      uint8_t specBands[16] = {0};
      int maxLv = 0;
      for (int b = 0; b < 16; b++) {
        int sIdx = (b * sampleCount) / 16;
        int val = abs((int)samples[sIdx]) / 280;
        specBands[b] = (uint8_t)constrain(val, 2, 98);
        if (specBands[b] > maxLv) maxLv = specBands[b];
      }
      TestAudio::setExternalAudioSpectrum(specBands, maxLv);
    }
  }

  void startVoicePlaybackAsync(uint8_t pin) {
    stopVoicePlaybackAsync();
    asyncSdFilePath = "";
    if (pcmRecSampleCount == 0) return;
    audioPlayRunning = true;
    xTaskCreatePinnedToCore(
      audioPwmTask,
      "AudioPwmTask",
      16384,
      (void*)(uintptr_t)pin,
      2,
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
      16384,
      (void*)(uintptr_t)pin,
      2,
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
      16384,
      (void*)(uintptr_t)pin,
      2,
      &audioTaskHandle,
      0
    );
  }

  void stopVoicePlaybackAsync() {
    audioPlayRunning = false;
    if (audioTaskHandle != NULL) {
      unsigned long startWait = millis();
      while (audioTaskHandle != NULL && millis() - startWait < 300) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      if (audioTaskHandle != NULL) {
        audioTaskHandle = NULL;
      }
    }

    if (spkDriverInstalled) {
      i2s_zero_dma_buffer(I2S_PORT_SPK);
      i2s_set_sample_rates(I2S_PORT_SPK, 16000);
    }
  }

  bool isVoicePlaybackAsyncRunning() {
    return audioPlayRunning;
  }

  uint32_t getVoicePlaybackCurrentSample() {
    return audioPlayCurSample;
  }

  // --- THU HỒI BỘ NHỚ AN TOÀN (MEMORY SCAVENGER / STRICT MODE) ---
  bool hasAllocatedRecBuffer() {
    return (pcmRecBuffer != nullptr || s_asyncStreamRingBuf != nullptr);
  }

  size_t trimMemory() {
    size_t totalFreed = 0;
    // 1. Giải phóng bộ đệm thu âm khi không dùng
    if (!voiceRecordingActive && !audioPlayRunning && pcmRecBuffer != nullptr) {
      size_t freedBytes = pcmRecAllocatedSamples * sizeof(int16_t);
      free(pcmRecBuffer);
      pcmRecBuffer = nullptr;
      pcmRecAllocatedSamples = 0;
      pcmRecSampleCount = 0;
      totalFreed += freedBytes;
      Serial.printf("🧹 [TRIM AUDIO] Đã giải phóng bộ đệm thu âm (%u bytes / %u KB)\n",
                    (unsigned int)freedBytes, (unsigned int)(freedBytes / 1024));
    }

    // 2. Giải phóng bộ đệm stream bất đồng bộ khi không phát
    if (!isStreamPcmAsyncPlaying() && s_asyncStreamRingBuf != nullptr) {
      taskENTER_CRITICAL(&s_asyncStreamMux);
      free(s_asyncStreamRingBuf);
      s_asyncStreamRingBuf = nullptr;
      s_asyncStreamHead = 0;
      s_asyncStreamTail = 0;
      s_asyncStreamCount = 0;
      taskEXIT_CRITICAL(&s_asyncStreamMux);
      size_t freedStream = ASYNC_STREAM_RING_CAPACITY * sizeof(int16_t);
      totalFreed += freedStream;
      Serial.printf("🧹 [TRIM AUDIO] Đã giải phóng bộ đệm stream RingBuffer (%u KB)\n", (unsigned int)(freedStream / 1024));
    }

    

    return totalFreed;
  }

  bool handleSerial(char cmd) {
    if (cmd == 'm' || cmd == 'M') {
      toggleMicMonitor();
      return true;
    }
    if (cmd == 'p' || cmd == 'P') {
      playRecordedVoice(speakerVolumePct);
      return true;
    }
    if (cmd == 't' || cmd == 'T') {
      Serial.printf("🔊 [TEST MAX98357A] Phát tiếng bíp 1000Hz 300ms qua Loa I2S DAC (Vol=%d%%)...\n", speakerVolumePct);
      playTone(1000, 300, speakerVolumePct);
      return true;
    }
    if (cmd == 'a' || cmd == 'A') {
      Serial.printf("🎶 [TEST MAX98357A] Phát âm thanh khởi động Trạm Không Gian qua Loa I2S DAC (Vol=%d%%)...\n", speakerVolumePct);
      playStartupChime();
      return true;
    }
    if (cmd == 'n' || cmd == 'N') {
      Serial.printf("🎵 [S40 TUNE] Đang phát Nokia Tune kinh điển qua Loa I2S DAC (Vol=%d%%)...\n", speakerVolumePct);
      playNokiaTune();
      return true;
    }
    if (cmd == 'o' || cmd == 'O') {
      Serial.println("🔔 [S40 TUNE] Đang phát âm báo OK...");
      playOkChime();
      return true;
    }
    if (cmd == 'd' || cmd == 'D') {
      Serial.println("🔕 [S40 TUNE] Đang phát âm báo Delete...");
      playDeleteChime();
      return true;
    }
    if (cmd == 'r' || cmd == 'R') {
      Serial.println("📩 [S40 TUNE] Đang phát âm tin nhắn Nokia SMS Morse (... -- ...)...");
      playSmsSpecialTone();
      return true;
    }
    if (cmd == '+' || cmd == '=') {
      speakerVolumePct = constrain(speakerVolumePct + 5, 0, 100);
      Serial.printf("🔊 [VOL] Tăng âm lượng Loa MAX98357A lên: %d%%\n", speakerVolumePct);
      playClickBeep();
      return true;
    }
    if (cmd == '-' || cmd == '_') {
      speakerVolumePct = constrain(speakerVolumePct - 5, 0, 100);
      Serial.printf("🔉 [VOL] Giảm âm lượng Loa MAX98357A xuống: %d%%\n", speakerVolumePct);
      playClickBeep();
      return true;
    }
    if (cmd == 'v' || cmd == 'V' || cmd == 's' || cmd == 'S') {
      saveRecordedVoiceToSd("/recording/rec_latest.wav");
      return true;
    }
    return false;
  }

}

