#pragma once
#define TEST_AUDIO_H_INCLUDED 1
#include <Arduino.h>

namespace TestAudio {
  // Khởi tạo Bus I2S 32-bit Stereo chuẩn cho Loa MAX98357A (BCLK=4, WS=5, DIN=15) & Mic INMP441 (SD=6)
  void init();

  // Vòng lặp đọc mẫu âm thanh 24-bit/32-bit từ Mic INMP441 và tạo mảng sóng âm Oscilloscope
  void loop();

  // Phát âm báo (Hz, ms) chuẩn 32-bit Stereo (L+R) ra loa MAX98357A
  void playTone(uint16_t freqHz, uint16_t durationMs, uint8_t volumePct = 75);

  // Phát giai điệu kiểm tra Loa MAX98357A (GPIO 15, kèm thử dự phòng GPIO 6 nếu đấu nhầm)
  void playStartupChime();
  void playClickBeep(uint16_t freqHz = 1480);
  int getSpeakerVolumePct();
  void setSpeakerVolumePct(int pct);

  // --- BỘ ÂM BÁO & NHẠC CHUÔNG MONO CỔ ĐIỂN NOKIA SYMBIAN S40 ---
  void playKeyBeep();                         // Tiếng bíp bàn phím mono dứt khoát (~16ms, 2093Hz)
  void playOkChime();                          // Âm xác nhận OK / Lưu thành công (E6 -> B6)
  void playDeleteChime();                      // Âm xóa tệp / Hủy bỏ / Cảnh báo (G5 -> D5)
  void playSmsSpecialTone();                   // Âm tin nhắn Nokia Morse SMS kinh điển (... -- ...)
  void playNokiaTune();                        // Nhạc chuông Nokia Tune kinh điển (Grande Valse)
  void playAlarmTuneStep(int tuneIdx, int step = 0); // Phát từng nhịp chuông báo thức theo giai điệu
  const char* getAlarmTuneName(int tuneIdx);
  int getAlarmTuneCount();

  // Bật/tắt chế độ thu âm trực tiếp Mic INMP441 & Hiển thị sóng âm lên ST7789
  void enableMicMonitor(bool enable);
  void toggleMicMonitor();
  bool isMicMonitorActive();

  // Lấy thông số sóng âm & Dữ liệu thô (Raw SD Data) thời gian thực để vẽ lên màn hình ST7789
  int getLastMicLevelPct();
  int32_t getLastMicPeak();
  uint32_t getLastRaw32Left();
  int32_t getLastRaw24Left();
  int32_t getLastRaw24Right();
  int32_t getLastRawAc();
  int32_t getLastRms();
  const char* getActiveMicChannelName(); // "LEFT (GND)", "RIGHT (3V3)", hoặc "NO SIGNAL"
  const int8_t* getWaveformBuffer();     // Mảng 48 điểm biên độ (-28 .. +28 px)
  const uint8_t* getMicSpectrum16();     // Mảng 16 cột phổ âm thanh thực (0 .. 100%) từ Mic INMP441 / Audio Data
  void setExternalAudioSpectrum(const uint8_t bands[16], int levelPct);

  // --- THU ÂM GIỌNG NÓI THỰC TẾ TỪ MIC INMP441 (16,000Hz 16-bit Mono PCM ĐÃ QUA DSP) ---
  bool startVoiceRecording();
  void pollVoiceRecording();
  void stopVoiceRecording();
  bool isVoiceRecording();
  const int16_t* getRecordedPcmBuffer();
  size_t getRecordedSampleCount();
  uint32_t getRecordSampleRate(); // Luôn là 16,000 Hz chuẩn Google STT & AI
  float getRecordedDurationSec();
  int getRecordedMaxVuPct();
  void setMicSensitivityMode(int mode); // 0: Thấp (1.8x), 1: Tiêu chuẩn (3.5x), 2: Cao (5.5x)

  // --- PHÁT ÂM THANH MÔ PHỎNG QUA PWM (GPIO 15 - KHÔNG CẦN DAC MAX98357A) ---
  void playPwmTone(uint16_t freqHz, uint16_t durationMs, uint8_t pin = 15);
  void playStartupPwmChime(uint8_t pin = 15);
  void playRecordedVoicePwm(uint8_t pin = 15);
  bool saveRecordedVoiceToSd(const char* filepath = "/recording/rec_latest.wav");

  // Phát nhạc bất đồng bộ (Non-blocking Task trên Core 0) cho Trình phát nhạc (Mode 12)
  void startVoicePlaybackAsync(uint8_t pin = 15);
  void playSdFileAsync(const String& path, uint8_t pin = 15);
  void playSynthMusicAsync(const String& trackTitle, uint8_t pin = 15);
  void stopVoicePlaybackAsync();
  bool isVoicePlaybackAsyncRunning();
  uint32_t getVoicePlaybackCurrentSample();

  // Xử lý phím tắt từ Serial Monitor ('m': Bật/Tắt Raw Stream, 'p': Phát lại PWM, 't': Test bíp, 's': Lưu SD)
  bool handleSerial(char cmd);
}

