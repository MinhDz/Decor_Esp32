#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

class WebStreamManager {
public:
  static void init();
  static void loop();

  // 1. Nhận & Xếp hàng Frame Video JPEG (Drop frame nếu Core 1 bận vẽ, chỉ vẽ frame mới nhất)
  static bool queueVideoFrame(const uint8_t* jpegData, size_t len);
  static bool renderVideoFrame(const uint8_t* jpegData, size_t len) {
    return queueVideoFrame(jpegData, len);
  }

  // 2. Nhận & Nạp Âm Thanh PCM 16-bit Mono (16,000 Hz) vào Ring Buffer PSRAM (Core 0 phát I2S - Không bao giờ drop)
  static void playAudioPcmChunk(const int16_t* pcmSamples, size_t sampleCount);
  static void startAudioUdpListener();

  // 3. Khởi chạy / Dừng Stream từ URL Mạng Trực Tiếp (MP3 Web Radio hoặc MJPEG Camera)
  static bool startUrlStream(const String& type, const String& url);
  static void stopUrlStream();

  // 4. Dừng toàn bộ luồng stream (cả video lẫn audio)
  static void stopAll();

  // 5. Kiểm tra trạng thái
  static bool isVideoStreaming();
  static bool isAudioStreaming();
  static float getFps();
  static uint32_t getDroppedFrames();
  static void getStatusJson(JsonDocument& doc);
};
