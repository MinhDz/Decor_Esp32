#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

typedef std::function<void(const String& recognizedUserText, const String& partialReply, const String& emotion, bool isFinal)> XiaoZhiProgressCallback;

class XiaoZhiClient {
public:
  static String getOrCreateDeviceUuid();
  static String queryOTA(bool printToSerial = true);
  static void getAIConfig(JsonDocument& doc);
  static bool saveAIConfig(const String& body);

  // Kiểm tra trạng thái liên kết thiết bị với XiaoZhi Cloud / Hub
  static bool isDeviceBound();
  static String getStoredToken();
  static String getStoredMac();
  static String getStoredEndpoint();
  static void unbindDevice();

  // Nhận diện giọng nói (Google STT đã được cô lập/comment theo yêu cầu người dùng)
  static String transcribeMicAudioPcm16(const int16_t* pcmSamples, size_t sampleCount, uint32_t sampleRate, String& outUtf8Transcript);
  static String stripVietnameseToAscii(const String& s);
  static String detectEmotionFromText(const String& text);

  // Giao tiếp hội thoại trực tiếp từ ESP32-S3 lên Trợ lý XiaoZhi AI (Trả về Text + Emotion cho màn hình ST7789)
  static String askXiaoZhiAI(const String& userText, String& outEmotion, float tempC = 28.0f, float humPct = 65.0f);
  static String getLastAuthCode();

  // Quản lý kết nối nền WebSocket trực tiếp tới XiaoZhi Cloud (Duy trì Online trên Hub)
  static void startWebSocket();
  static void stopWebSocket();
  static bool isWebSocketConnected();
  static void loopWebSocket();
  static String getSessionId();

  // Truyền dữ liệu âm thanh thuần (PCM) trực tiếp qua XiaoZhi Hub WebSocket (listen start -> binary audio -> listen stop -> nhận STT/LLM)
  static bool sendAudioToXiaoZhi(const int16_t* pcmSamples, size_t sampleCount, uint32_t sampleRate,
                                String& outUserText, String& outAiReply, String& outEmotion,
                                XiaoZhiProgressCallback onProgress = nullptr);
  static String getLastSttText();
  static String getLastLlmReply();
  static String getLastEmotion();
  static bool isTtsFinished();
  static uint32_t getLastAiReplyTime();
  static void clearLastTurn();
};


