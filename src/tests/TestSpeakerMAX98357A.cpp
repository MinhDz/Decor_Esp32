#include "tests/TestSpeakerMAX98357A.h"
#include "Config.h"
#include <driver/i2s.h>
#include <math.h>

namespace TestSpeakerMAX98357A {

  static const i2s_port_t I2S_PORT = I2S_NUM_0;
  static const uint32_t SAMPLE_RATE = 44100; // 44.1kHz -> BCLK = 1.4112 MHz (~1.64V DC trên VOM)

  static bool driverInstalled = false;
  static uint8_t activeBclkPin = PIN_I2S_SPK_BCLK; // Mặc định GPIO 16 (I2S1 BCLK)
  static uint8_t activeWsPin   = PIN_I2S_SPK_LRC;  // Mặc định GPIO 17 (I2S1 LRC/WS)
  static uint8_t activeDinPin  = PIN_I2S_SPK_DIN;  // Mặc định GPIO 15 (I2S1 DIN/DOUT)
  // QUAN TRỌNG: Khi nuôi MAX98357A + Loa 3W (4 Ohm) bằng chân 3.3V từ cổng USB máy tính (giới hạn 500mA),
  // để âm lượng 18% giúp dòng tiêu thụ của loa chỉ ~45mA (kêu vừa đủ nghe rõ mà KHÔNG gây sụt áp 3.3V hay sập cổng USB PC!)
  static int currentVolumePct  = 18;           // Mặc định 18% (Chế độ an toàn chống sụt áp 3.3V USB)
  static bool autoBeacon       = true;         // Tự động phát chuông mỗi 3.5s
  static bool continuousSine   = false;        // [c] Phát sóng Sine 600Hz liên tục không nghỉ để đo áp bằng đồng hồ VOM
  static unsigned long lastBeaconMs = 0;
  static uint32_t beaconCount = 0;

  // Cài đặt cố định I2S ở chế độ MASTER TX (Giữ xung BCLK ~1.64V & WS ~1.64V liên tục 100%)
  static bool setupI2sTx(uint8_t bclkPin, uint8_t wsPin, uint8_t dinPin) {
    if (driverInstalled) {
      i2s_driver_uninstall(I2S_PORT);
      driverInstalled = false;
      delay(10);
    }

    // Reset hoàn toàn ma trận GPIO trước khi gắn vào bộ I2S
    pinMode(bclkPin, OUTPUT);
    pinMode(wsPin, OUTPUT);
    pinMode(dinPin, OUTPUT);

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; // Xuất đồng thời cả 2 kênh Left + Right
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;

    if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) {
      Serial.println("❌ [MAX98357A] Lỗi i2s_driver_install!");
      return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = bclkPin;
    pins.ws_io_num    = wsPin;
    pins.data_out_num = dinPin;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
      Serial.println("❌ [MAX98357A] Lỗi i2s_set_pin!");
      i2s_driver_uninstall(I2S_PORT);
      return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    activeBclkPin = bclkPin;
    activeWsPin   = wsPin;
    activeDinPin  = dinPin;
    driverInstalled = true;
    return true;
  }

  // Kiểm tra xem chân GPIO 4 (BCLK) có đang bị chạm vào 3.3V / RST hay không
  void runElectricalPinCheck() {
    if (driverInstalled) {
      i2s_driver_uninstall(I2S_PORT);
      driverInstalled = false;
    }

    Serial.println("\n🔍 [CHẨN ĐOÁN ĐIỆN ÁP CHÂN I2S TRƯỚC KHI PHÁT]:");
    uint8_t testPins[3] = { activeBclkPin, activeWsPin, activeDinPin };
    const char* pinNames[3] = { "BCLK", "LRC/WS", "DIN" };

    for (int i = 0; i < 3; i++) {
      uint8_t p = testPins[i];
      // Thử kéo xuống GND bằng trở nội (INPUT_PULLDOWN) xem có bị áp 3.3V bên ngoài ép lên HIGH không
      pinMode(p, INPUT_PULLDOWN);
      delay(5);
      int valPulldown = digitalRead(p);

      // Thử xuất mức LOW (0V)
      pinMode(p, OUTPUT);
      digitalWrite(p, LOW);
      delay(5);
      int valDrivenLow = digitalRead(p);

      if (valPulldown == HIGH || valDrivenLow == HIGH) {
        Serial.printf("  ⚠️ CẢNH BÁO [GPIO %-2d - %-6s]: Đang bị kéo lên 3.3V từ bên ngoài (Pulldown=%d, DriveLow=%d)!\n",
                      p, pinNames[i], valPulldown, valDrivenLow);
        Serial.println("     👉 Kiểm tra xem chân này trên Motherboard có bị chạm sang đường 3.3V / VIN / SD hoặc chân RST (sát trên GPIO 4) không!");
      } else {
        Serial.printf("  ✅ [GPIO %-2d - %-6s]: Đường mạch sạch, không chạm 3.3V (Pulldown=0V OK).\n",
                      p, pinNames[i]);
      }
    }

    // Khởi tạo lại I2S TX ngay sau khi kiểm tra
    setupI2sTx(activeBclkPin, activeWsPin, activeDinPin);
  }

  void printWiringAndDebugGuide() {
    Serial.println("\n======================================================================");
    Serial.println("   🔊 TRÌNH KIỂM TRA ĐỘC LẬP MODULE LOA I2S MAX98357A (MOTHERBOARD)   ");
    Serial.println("======================================================================");
    Serial.println("  📌 BẢNG ĐIỆN ÁP CHUẨN KHI ĐO ĐỒNG HỒ VOM (So với GND):");
    Serial.printf ("     • BCLK (GPIO %-2d)   : Phải đạt ~1.60V - 1.65V DC (Nếu = 3.29V là mất xung BCLK -> IC bị Shutdown!)\n", activeBclkPin);
    Serial.printf ("     • LRC  (GPIO %-2d)   : Phải đạt ~1.60V - 1.65V DC (Xung 44.1kHz 50%% duty)\n", activeWsPin);
    Serial.printf ("     • DIN  (GPIO %-2d)   : ~0.0V khi nghỉ, ~1.5V - 1.65V DC khi đang phát sóng Sine\n", activeDinPin);
    Serial.println("     • SD / MODE        : Phải > 1.4V (Để hở hoặc nối 3.3V/5V; nếu < 0.16V IC sẽ Shutdown)");
    Serial.println("     • Cọc Loa (+) & (-): Đo giữa [Loa +] với [GND] phải có ~1.65V (nguồn 3.3V) hoặc ~2.5V (nguồn 5V)");
    Serial.println("                          khi IC thức! (Nếu đo giữa Loa+ với GND = 0V tức là IC đang Shutdown do mất BCLK)");
    Serial.println("----------------------------------------------------------------------");
    Serial.println("  🎮 BẢNG PHÍM TẮT ĐIỀU KHIỂN TRÊN SERIAL MONITOR:");
    Serial.println("     [c] : BẬT/TẮT PHÁT SÓNG SINE 600Hz LIÊN TỤC 100%% (Để đo đồng hồ VOM không bị ngắt quãng!)");
    Serial.println("     [d] : Chạy kiểm tra chạm chập 3.3V trên các chân GPIO 4, 5, 15");
    Serial.println("     [a] : Phát Chuông 4 Nốt (C5 - E5 - G5 - C6)");
    Serial.println("     [n] : Phát Giai Điệu Nhạc Mario / Space Decor");
    Serial.println("     [f] : Quét Dải Tần Số Bass -> Treble (150Hz -> 2400Hz)");
    Serial.printf ("     [l] : Bật/Tắt Tự Động Phát Lặp mỗi 3s (Hiện tại: %s)\n", autoBeacon ? "ĐANG BẬT" : "ĐANG TẮT");
    Serial.printf ("     [b] : Đổi thử chân BCLK giữa GPIO 4 và GPIO 6 (Hiện tại: GPIO %d)\n", activeBclkPin);
    Serial.printf ("     [p] : Đổi thử chân DIN giữa GPIO 15 và GPIO 6 (Hiện tại: GPIO %d)\n", activeDinPin);
    Serial.println("======================================================================");
  }

  void playTone(uint16_t freqHz, uint16_t durationMs, int volumePct) {
    if (!driverInstalled && !setupI2sTx(activeBclkPin, activeWsPin, activeDinPin)) return;

    int vol = (volumePct >= 0) ? volumePct : currentVolumePct;
    if (vol > 100) vol = 100;
    if (vol < 0) vol = 0;

    const size_t FRAMES_PER_CHUNK = 128;
    int16_t stereoChunk[FRAMES_PER_CHUNK * 2]; // [Left, Right] 16-bit
    uint32_t totalFrames = (SAMPLE_RATE * durationMs) / 1000;
    uint32_t framesSent = 0;

    float amplitude = (30000.0f * (float)vol) / 100.0f;
    float phaseIncrement = (2.0f * (float)M_PI * (float)freqHz) / (float)SAMPLE_RATE;
    float phase = 0.0f;

    while (framesSent < totalFrames) {
      size_t n = (totalFrames - framesSent > FRAMES_PER_CHUNK)
                   ? FRAMES_PER_CHUNK
                   : (totalFrames - framesSent);

      for (size_t i = 0; i < n; i++) {
        uint32_t curFrame = framesSent + i;
        float env = 1.0f;
        uint32_t attackFrames = (SAMPLE_RATE * 3) / 1000;
        uint32_t releaseFrames = (SAMPLE_RATE * 5) / 1000;
        if (curFrame < attackFrames) {
          env = (float)curFrame / (float)attackFrames;
        } else if (totalFrames - curFrame < releaseFrames) {
          env = (float)(totalFrames - curFrame) / (float)releaseFrames;
        }

        int16_t sample = (freqHz > 0) ? (int16_t)(sinf(phase) * amplitude * env) : 0;
        phase += phaseIncrement;
        if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

        stereoChunk[i * 2]     = sample; // Left Channel
        stereoChunk[i * 2 + 1] = sample; // Right Channel
      }

      size_t bytesWritten = 0;
      i2s_write(I2S_PORT, stereoChunk, n * 2 * sizeof(int16_t), &bytesWritten, pdMS_TO_TICKS(200));
      framesSent += n;
    }

    if (!continuousSine) {
      i2s_zero_dma_buffer(I2S_PORT);
    }
  }

  void playChime4Notes() {
    Serial.printf("🎵 [MAX98357A] Đang phát Chuông 4 Nốt (C5-E5-G5-C6) | Vol=%d%% | BCLK=%d, WS=%d, DIN=%d\n",
                  currentVolumePct, activeBclkPin, activeWsPin, activeDinPin);
    playTone(523, 180); // C5
    delay(25);
    playTone(659, 180); // E5
    delay(25);
    playTone(784, 180); // G5
    delay(25);
    playTone(1046, 320); // C6
  }

  void playMelodyMario() {
    Serial.printf("🎶 [MAX98357A] Đang phát Giai Điệu Nhạc Hiệu (Vol=%d%%, DIN=GPIO %d)...\n",
                  currentVolumePct, activeDinPin);
    const uint16_t notes[] = { 659, 659, 0, 659, 0, 523, 659, 0, 784, 0, 392 };
    const uint16_t durs[]  = { 120, 120, 80, 120, 80, 120, 140, 80, 240, 150, 240 };
    for (size_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
      if (notes[i] == 0) {
        delay(durs[i]);
      } else {
        playTone(notes[i], durs[i]);
        delay(20);
      }
    }
    Serial.println("✅ [MAX98357A] Hoàn tất phát giai điệu!");
  }

  void playFrequencySweep() {
    Serial.printf("📈 [MAX98357A] Đang quét dải tần số Bass -> Treble (150Hz -> 2400Hz) | Vol=%d%%...\n",
                  currentVolumePct);
    for (uint16_t f = 150; f <= 2400; f += 75) {
      playTone(f, 40);
    }
    Serial.println("✅ [MAX98357A] Hoàn tất quét tần số!");
  }

  void playSpaceSiren() {
    Serial.printf("🚨 [MAX98357A] Đang phát Còi Hú Vũ Trụ (Space Siren) | Vol=%d%%...\n",
                  currentVolumePct);
    for (int cycle = 0; cycle < 2; cycle++) {
      for (uint16_t f = 450; f <= 1200; f += 60) playTone(f, 20);
      for (uint16_t f = 1200; f >= 450; f -= 60) playTone(f, 20);
    }
    Serial.println("✅ [MAX98357A] Hoàn tất Space Siren!");
  }

  void init() {
    Serial.println("\n🔊 [TestSpeakerMAX98357A] Khởi tạo chuyên biệt cho MAX98357A...");
    runElectricalPinCheck();
    printWiringAndDebugGuide();
    playChime4Notes();
    lastBeaconMs = millis();
  }

  void loop() {
    if (!driverInstalled) return;

    // Nếu đang bật chế độ phát sóng Sine 600Hz liên tục (để người dùng đo đồng hồ VOM)
    if (continuousSine) {
      playTone(600, 100);
      return;
    }

    if (!autoBeacon) return;

    unsigned long now = millis();
    if (now - lastBeaconMs >= 3000) {
      lastBeaconMs = now;
      beaconCount++;
      Serial.printf("🔔 [Auto-Beacon #%lu] Phát chuông 800ms (BCLK=%d, WS=%d, DIN=%d, Vol=%d%%) | Gõ 'c' để phát liên tục đo VOM, 'd' để kiểm tra chạm áp\n",
                    (unsigned long)beaconCount, activeBclkPin, activeWsPin, activeDinPin, currentVolumePct);
      // Phát nốt dài 800ms để đồng hồ VOM kịp lấy mẫu điện áp AC/DC
      playTone(587, 350);  // D5
      delay(20);
      playTone(880, 450);  // A5
    }
  }

  void setVolume(int pct) {
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    currentVolumePct = pct;
    Serial.printf("🔊 [MAX98357A] Âm lượng hiện tại: %d%%\n", currentVolumePct);
    playTone(880, 120);
  }

  int getVolume() { return currentVolumePct; }

  void toggleAutoBeacon() {
    autoBeacon = !autoBeacon;
    lastBeaconMs = millis();
    Serial.printf("🔁 [MAX98357A] Chế độ Tự Động Phát Lặp mỗi 3s (Auto-Beacon): %s\n",
                  autoBeacon ? "ĐÃ BẬT" : "ĐÃ TẮT");
  }

  bool isAutoBeaconEnabled() { return autoBeacon; }

  void toggleContinuousTone() {
    continuousSine = !continuousSine;
    Serial.printf("〰️ [MAX98357A] Chế độ Phát Sóng Sine 600Hz LIÊN TỤC 100%% (Đo VOM): %s\n",
                  continuousSine ? "ĐANG BẬT (Sóng Sine 600Hz phát không nghỉ để bạn đo áp VOM!)" : "ĐÃ TẮT");
  }

  void toggleDinPin() {
    uint8_t nextPin = (activeDinPin == PIN_I2S_DOUT) ? PIN_I2S_DIN : PIN_I2S_DOUT;
    Serial.printf("🔀 [MAX98357A] Đang chuyển chân DIN sang GPIO %d...\n", nextPin);
    if (setupI2sTx(activeBclkPin, activeWsPin, nextPin)) {
      Serial.printf("✅ [MAX98357A] Đã chuyển chân DIN sang GPIO %d! Đang phát thử chuông...\n", activeDinPin);
      playChime4Notes();
    }
  }

  void toggleBclkPin() {
    uint8_t nextBclk = (activeBclkPin == PIN_I2S_BCLK) ? 6 : PIN_I2S_BCLK;
    Serial.printf("🔀 [MAX98357A] Đang đổi thử chân BCLK sang GPIO %d (để kiểm tra nếu GPIO 4 bị chạm 3.3V)...\n", nextBclk);
    if (setupI2sTx(nextBclk, activeWsPin, activeDinPin)) {
      Serial.printf("✅ [MAX98357A] Đã chuyển BCLK sang GPIO %d! Hãy đo áp trên GPIO %d (sẽ thấy ~1.64V DC)!\n", activeBclkPin, activeBclkPin);
      playChime4Notes();
    }
  }

  uint8_t getActiveDinPin() { return activeDinPin; }

  bool handleSerial(char cmd) {
    switch (cmd) {
      case 'c':
      case 'C':
        toggleContinuousTone();
        return true;

      case 'd':
      case 'D':
        runElectricalPinCheck();
        return true;

      case 'b':
      case 'B':
        toggleBclkPin();
        return true;

      case 'a':
      case 'A':
        playChime4Notes();
        lastBeaconMs = millis();
        return true;

      case 'n':
      case 'N':
        playMelodyMario();
        lastBeaconMs = millis();
        return true;

      case 'f':
      case 'F':
        playFrequencySweep();
        lastBeaconMs = millis();
        return true;

      case 's':
      case 'S':
        playSpaceSiren();
        lastBeaconMs = millis();
        return true;

      case 'l':
      case 'L':
        toggleAutoBeacon();
        return true;

      case '+':
      case '=':
        setVolume(currentVolumePct + 5);
        return true;

      case '-':
      case '_':
        setVolume(currentVolumePct - 5);
        return true;

      case 'p':
      case 'P':
        toggleDinPin();
        return true;

      default:
        return false;
    }
  }

}
