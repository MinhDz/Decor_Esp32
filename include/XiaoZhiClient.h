#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

class XiaoZhiClient {
public:
  static String getOrCreateDeviceUuid();
  static String queryOTA(bool printToSerial = true);
  static void getAIConfig(JsonDocument& doc);
  static bool saveAIConfig(const String& body);

  // Nhận diện giọng nói thực tế từ bộ đệm PCM 16-bit 8000Hz của Mic INMP441 (Google Speech-to-Text vi-VN)
  static String transcribeMicAudioPcm16(const int16_t* pcmSamples, size_t sampleCount, uint32_t sampleRate, String& outUtf8Transcript);

  // Giao tiếp hội thoại trực tiếp từ ESP32-S3 lên Trợ lý XiaoZhi AI (Trả về Text + Emotion cho màn hình ST7789, không cần DAC)
  static String askXiaoZhiAI(const String& userText, String& outEmotion, float tempC = 28.0f, float humPct = 65.0f);
  static String getLastAuthCode();
};


