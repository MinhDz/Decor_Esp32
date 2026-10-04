#pragma once
#include <Arduino.h>

namespace TestSpeakerMAX98357A {

  // Khởi tạo độc lập Module Khuếch Đại Âm Thanh I2S MAX98357A
  // Sơ đồ chân giữ nguyên: BCLK = GPIO 4, WS/LRC = GPIO 5, DIN = GPIO 15
  void init();

  // Vòng lặp phát chuông định kỳ (Auto-Beacon 3.5s) giúp kiểm tra tức thì khi đang cắm dây trên Motherboard
  void loop();

  // Phát 1 tần số âm thanh Sine (Hz, ms) chuẩn 16-bit Stereo (L+R)
  void playTone(uint16_t freqHz, uint16_t durationMs, int volumePct = -1);

  // Các bài test âm thanh chuyên sâu cho củ loa 3W + MAX98357A
  void playChime4Notes();     // [a] Chuông 4 nốt chuẩn C5-E5-G5-C6
  void playMelodyMario();     // [n] Nhạc hiệu Mario / Space Decor
  void playFrequencySweep();  // [f] Quét dải tần Bass -> Treble (150Hz -> 2400Hz)
  void playSpaceSiren();      // [s] Còi hú vũ trụ (400Hz <-> 1200Hz)

  // Kiểm tra chạm chập phần cứng (Phát hiện chân GPIO 4 BCLK bị chạm 3.3V/RST)
  void runElectricalPinCheck();

  // Điều chỉnh âm lượng, chế độ phát sóng Sine liên tục (để đo đồng hồ VOM) & chế độ lặp
  void setVolume(int pct);
  int  getVolume();
  void toggleAutoBeacon();
  bool isAutoBeaconEnabled();
  void toggleContinuousTone(); // [c] Phát sóng Sine 600Hz liên tục 100% thời gian để đo áp VOM tại loa
  void toggleDinPin();         // [p] Đổi thử chân DIN giữa GPIO 15 (chuẩn) và GPIO 6
  void toggleBclkPin();        // [b] Đổi thử chân BCLK giữa GPIO 4 (chuẩn) và GPIO 6
  uint8_t getActiveDinPin();

  // In bảng hướng dẫn đo kiểm phần cứng MAX98357A
  void printWiringAndDebugGuide();

  // Xử lý lệnh từ Serial Monitor ('a', 'n', 'f', 's', 'c', 'd', 'l', '+', '-', 'p', 'b')
  bool handleSerial(char cmd);

}
