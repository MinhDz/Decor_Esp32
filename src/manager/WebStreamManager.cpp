#include "WebStreamManager.h"
#include "tests/TestDisplay.h"
#include "tests/TestAudio.h"
#include "Config.h"
#include "minimp3.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <errno.h>

namespace {
  static unsigned long s_lastVideoFrameMs = 0;
  static unsigned long s_lastAudioPcmMs   = 0;
  static uint32_t s_videoFrameCount       = 0;
  static uint32_t s_audioChunkCount       = 0;
  static float s_currentFps               = 0.0f;
  static unsigned long s_fpsCalcStartMs   = 0;
  static uint32_t s_fpsFrameCounter       = 0;

  static String s_activeUrl               = "";
  static String s_activeUrlType           = ""; // "audio" hoặc "video"
  static volatile bool s_urlStreamRunning = false;
  static TaskHandle_t s_urlStreamTaskHandle = nullptr;
  static volatile bool s_streamSessionActive = false;
  static volatile unsigned long s_lastStopMs = 0;

  // Buffer video truyền không đồng bộ an toàn từ Core 0 sang Core 1 (tránh xung đột bus SPI ST7789)
  static const size_t MAX_STREAM_JPG_SIZE = 64 * 1024;
  static uint8_t* s_asyncVideoBuf         = nullptr;
  static size_t   s_asyncVideoLen         = 0;
  static volatile bool s_asyncVideoReady  = false;
  static volatile bool s_isDisplayRendering = false;
  static uint32_t s_videoFramesDropped     = 0;
  static portMUX_TYPE s_streamMux         = portMUX_INITIALIZER_UNLOCKED;

  // Task xử lý luồng stream URL (Web Radio MP3 hoặc MJPEG Video) chạy nền trên Core 0
  static void urlStreamTask(void* param) {
    Serial.printf("🌐 [WEB STREAM TASK] Bắt đầu kết nối URL (%s): %s\n",
                  s_activeUrlType.c_str(), s_activeUrl.c_str());

    bool isHttps = s_activeUrl.startsWith("https://");

    if (s_activeUrlType == "audio") {
      // Dừng triệt để các nguồn phát âm thanh nền khác trước khi mở stream
      TestAudio::stopVoicePlaybackAsync();
      TestDisplay::controlMediaPlayer("stop");
      i2s_zero_dma_buffer((i2s_port_t)1);

      HTTPClient* http = new HTTPClient();
      WiFiClient* plainClient = nullptr;
      WiFiClientSecure* secureClient = nullptr;

      if (isHttps) {
        secureClient = new WiFiClientSecure();
        secureClient->setInsecure();
        http->begin(*secureClient, s_activeUrl);
      } else {
        plainClient = new WiFiClient();
        http->begin(*plainClient, s_activeUrl);
      }

      http->setTimeout(8000);
      http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http->addHeader("User-Agent", "Decor_Esp32_Radio/3.0");
      http->addHeader("Icy-MetaData", "0");
      http->addHeader("Connection", "keep-alive");

      int httpCode = http->GET();
      if (httpCode <= 0 || (httpCode != HTTP_CODE_OK && httpCode != 206)) {
        Serial.printf("❌ [AUDIO STREAM] Kết nối thất bại, mã HTTP: %d\n", httpCode);
        http->end();
        delete http;
        if (plainClient) delete plainClient;
        if (secureClient) delete secureClient;
        s_urlStreamRunning = false;
        s_urlStreamTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
      }

      WiFiClient* stream = http->getStreamPtr();
      if (!stream) {
        http->end();
        delete http;
        if (plainClient) delete plainClient;
        if (secureClient) delete secureClient;
        s_urlStreamRunning = false;
        s_urlStreamTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
      }

      // Cấp phát bộ nhớ giải mã MP3 ưu tiên trên PSRAM (8MB)
      mp3dec_t* mp3d = (mp3dec_t*)(psramFound() ? ps_malloc(sizeof(mp3dec_t)) : malloc(sizeof(mp3dec_t)));
      const int MP3_BUF_SIZE = 4096;
      uint8_t* mp3Buf = (uint8_t*)(psramFound() ? ps_malloc(MP3_BUF_SIZE) : malloc(MP3_BUF_SIZE));
      mp3d_sample_t* pcmOut = (mp3d_sample_t*)(psramFound() ? ps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t)) : malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t)));
      int16_t* stereoPcm = (int16_t*)(psramFound() ? ps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * 2 * sizeof(int16_t)) : malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * 2 * sizeof(int16_t)));
      size_t scratchSz = mp3dec_scratch_size();
      mp3dec_scratch_t* scratch = (mp3dec_scratch_t*)(psramFound() ? ps_malloc(scratchSz) : malloc(scratchSz));

      if (!mp3d || !mp3Buf || !pcmOut || !stereoPcm || !scratch) {
        Serial.println("❌ [AUDIO STREAM] Không đủ bộ nhớ cấp phát cho bộ giải mã MP3!");
        if (scratch) free(scratch);
        if (mp3d) free(mp3d);
        if (mp3Buf) free(mp3Buf);
        if (pcmOut) free(pcmOut);
        if (stereoPcm) free(stereoPcm);
        http->end();
        delete http;
        if (plainClient) delete plainClient;
        if (secureClient) delete secureClient;
        s_urlStreamRunning = false;
        s_urlStreamTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
      }

      mp3dec_init(mp3d);
      int bufValid = 0;
      uint32_t currentSampleRate = 44100;

      Serial.println("🎧 [AUDIO STREAM] Bắt đầu giải mã & phát MP3 trực tuyến...");

      while (s_urlStreamRunning && http->connected()) {
        // Nạp dữ liệu từ stream mạng vào buffer
        if (bufValid < (MP3_BUF_SIZE - 1500) && stream->available()) {
          size_t want = MP3_BUF_SIZE - bufValid;
          size_t nRead = stream->read(mp3Buf + bufValid, want);
          if (nRead > 0) bufValid += nRead;
        }

        if (bufValid <= 0) {
          vTaskDelay(pdMS_TO_TICKS(10));
          continue;
        }

        mp3dec_frame_info_t info;
        int samplesPerChannel = mp3dec_decode_frame_scratch(mp3d, mp3Buf, bufValid, pcmOut, &info, scratch);

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
          s_lastAudioPcmMs = millis();
          s_audioChunkCount++;

          // Cập nhật tần số lấy mẫu I2S1 nếu luồng đổi định dạng
          if (info.hz > 0 && (uint32_t)info.hz != currentSampleRate) {
            currentSampleRate = info.hz;
            i2s_set_sample_rates((i2s_port_t)1, currentSampleRate);
            Serial.printf("🎧 [AUDIO STREAM] %d Hz | %s | %d kbps\n",
                          info.hz, info.channels == 2 ? "Stereo" : "Mono", info.bitrate_kbps);
          }

          int vol = max(TestAudio::getMusicVolumePct(), TestAudio::getXiaoZhiVolumePct());
          float volScale = (vol > 0) ? ((float)vol / 100.0f) : 0.0f;

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
          i2s_write((i2s_port_t)1, stereoPcm, samplesPerChannel * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));

          // Cập nhật phổ âm thanh ST7789
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

        // Nhường CPU cho Core 0 IDLE task (nuôi Watchdog)
        vTaskDelay(pdMS_TO_TICKS(1));
      }

      free(scratch);
      free(mp3d);
      free(mp3Buf);
      free(pcmOut);
      free(stereoPcm);
      http->end();
      delete http;
      if (plainClient) delete plainClient;
      if (secureClient) delete secureClient;
      Serial.println("🛑 [AUDIO STREAM] Đã kết thúc luồng MP3 Radio.");

    } else if (s_activeUrlType == "video") {
      HTTPClient* http = new HTTPClient();
      WiFiClient* plainClient = nullptr;
      WiFiClientSecure* secureClient = nullptr;

      if (isHttps) {
        secureClient = new WiFiClientSecure();
        secureClient->setInsecure();
        http->begin(*secureClient, s_activeUrl);
      } else {
        plainClient = new WiFiClient();
        http->begin(*plainClient, s_activeUrl);
      }

      http->setTimeout(8000);
      http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http->addHeader("User-Agent", "Decor_Esp32_Cam/3.0");
      http->addHeader("Connection", "keep-alive");

      int httpCode = http->GET();
      if (httpCode <= 0 || (httpCode != HTTP_CODE_OK && httpCode != 206)) {
        Serial.printf("❌ [VIDEO STREAM] Kết nối thất bại, mã HTTP: %d\n", httpCode);
        http->end();
        delete http;
        if (plainClient) delete plainClient;
        if (secureClient) delete secureClient;
        s_urlStreamRunning = false;
        s_urlStreamTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
      }

      WiFiClient* stream = http->getStreamPtr();
      if (!stream) {
        http->end();
        delete http;
        if (plainClient) delete plainClient;
        if (secureClient) delete secureClient;
        s_urlStreamRunning = false;
        s_urlStreamTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
      }

      uint8_t* rawJpgBuf = (uint8_t*)(psramFound() ? ps_malloc(MAX_STREAM_JPG_SIZE) : malloc(MAX_STREAM_JPG_SIZE));
      if (!rawJpgBuf) {
        Serial.println("❌ [VIDEO STREAM] Không đủ bộ nhớ cấp phát cho Frame JPEG!");
        http->end();
        delete http;
        if (plainClient) delete plainClient;
        if (secureClient) delete secureClient;
        s_urlStreamRunning = false;
        s_urlStreamTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
      }

      // Tự động chuyển màn hình sang chế độ Stream để hiển thị ngay luồng video
      TestDisplay::setScreenMode("stream");

      size_t jpgLen = 0;
      bool inFrame = false;
      uint8_t lastByte = 0;
      uint8_t readChunk[512];

      Serial.println("📹 [VIDEO STREAM] Bắt đầu nhận luồng MJPEG...");

      while (s_urlStreamRunning && http->connected()) {
        int avail = stream->available();
        if (avail > 0) {
          int toRead = min((int)sizeof(readChunk), avail);
          int bytesRead = stream->read(readChunk, toRead);

          for (int i = 0; i < bytesRead; i++) {
            uint8_t b = readChunk[i];

            if (!inFrame) {
              if (lastByte == 0xFF && b == 0xD8) { // SOI
                inFrame = true;
                rawJpgBuf[0] = 0xFF;
                rawJpgBuf[1] = 0xD8;
                jpgLen = 2;
              }
            } else {
              if (jpgLen < MAX_STREAM_JPG_SIZE) {
                rawJpgBuf[jpgLen++] = b;
              }
              if (lastByte == 0xFF && b == 0xD9) { // EOI
                inFrame = false;
                if (jpgLen > 100) {
                  WebStreamManager::queueVideoFrame(rawJpgBuf, jpgLen);
                }
                jpgLen = 0;
              }
            }
            lastByte = b;
          }
        } else {
          vTaskDelay(pdMS_TO_TICKS(5));
        }

        // Nhường CPU cho Core 0 IDLE task (nuôi Watchdog)
        vTaskDelay(pdMS_TO_TICKS(1));
      }

      free(rawJpgBuf);
      http->end();
      delete http;
      if (plainClient) delete plainClient;
      if (secureClient) delete secureClient;
      Serial.println("🛑 [VIDEO STREAM] Đã kết thúc luồng MJPEG.");
    }

    s_urlStreamRunning = false;
    s_urlStreamTaskHandle = nullptr;
    vTaskDelete(nullptr);
  }
}

void WebStreamManager::init() {
  s_lastVideoFrameMs = 0;
  s_lastAudioPcmMs   = 0;
  s_videoFrameCount  = 0;
  s_audioChunkCount  = 0;
  s_currentFps       = 0.0f;
  s_fpsCalcStartMs   = millis();
  s_fpsFrameCounter  = 0;
  s_urlStreamRunning = false;
  s_urlStreamTaskHandle = nullptr;

  if (!s_asyncVideoBuf) {
    if (psramFound()) {
      s_asyncVideoBuf = (uint8_t*)ps_malloc(MAX_STREAM_JPG_SIZE);
    }
    if (!s_asyncVideoBuf) {
      s_asyncVideoBuf = (uint8_t*)malloc(MAX_STREAM_JPG_SIZE);
    }
  }
  s_asyncVideoLen = 0;
  s_asyncVideoReady = false;

  Serial.println("📡 [STREAM] WebStreamManager đã khởi tạo thành công với PSRAM Double Buffer!");
}

void WebStreamManager::loop() {
  unsigned long now = millis();

  // 1. Kiểm tra nếu có Video Frame mới nhất, vẽ ra màn hình ST7789 trên Core 1
  if (s_streamSessionActive && s_asyncVideoReady && s_asyncVideoBuf) {
    taskENTER_CRITICAL(&s_streamMux);
    size_t len = s_asyncVideoLen;
    s_asyncVideoReady = false;
    s_isDisplayRendering = true;
    taskEXIT_CRITICAL(&s_streamMux);

    if (len > 0) {
      TestDisplay::drawStreamJpegFrame(s_asyncVideoBuf, len);
      s_fpsFrameCounter++;
    }

    s_isDisplayRendering = false;
  } else if (!s_streamSessionActive && s_asyncVideoReady) {
    taskENTER_CRITICAL(&s_streamMux);
    s_asyncVideoReady = false;
    s_asyncVideoLen = 0;
    taskEXIT_CRITICAL(&s_streamMux);
  }

  // 2. Tính FPS trung bình mỗi 1000ms
  if (now - s_fpsCalcStartMs >= 1000) {
    s_currentFps = (float)s_fpsFrameCounter * 1000.0f / (float)(now - s_fpsCalcStartMs);
    s_fpsFrameCounter = 0;
    s_fpsCalcStartMs = now;
  }
}

bool WebStreamManager::queueVideoFrame(const uint8_t* jpegData, size_t len) {
  if (!jpegData || len == 0 || len > MAX_STREAM_JPG_SIZE) return false;

  // Chặn frame video tàn dư trong vòng 1.2s sau khi người dùng bấm dừng stream
  if (millis() - s_lastStopMs < 1200) {
    s_videoFramesDropped++;
    return false;
  }

  s_streamSessionActive = true;
  s_lastVideoFrameMs = millis();
  s_videoFrameCount++;

  // Ưu tiên âm thanh - Cho phép Video Drop Frame:
  // Nếu Core 1 đang giải mã/vẽ hoặc đã có 1 frame mới hơn chờ vẽ:
  // Hủy (drop) khung hình này ngay lập tức! Chỉ vẽ khung hình mới nhất.
  if (s_isDisplayRendering || s_asyncVideoReady) {
    s_videoFramesDropped++;
    return false;
  }

  if (!s_asyncVideoBuf) {
    if (psramFound()) {
      s_asyncVideoBuf = (uint8_t*)ps_malloc(MAX_STREAM_JPG_SIZE);
    }
    if (!s_asyncVideoBuf) {
      s_asyncVideoBuf = (uint8_t*)malloc(MAX_STREAM_JPG_SIZE);
    }
  }
  if (!s_asyncVideoBuf) return false;

  taskENTER_CRITICAL(&s_streamMux);
  memcpy(s_asyncVideoBuf, jpegData, len);
  s_asyncVideoLen = len;
  s_asyncVideoReady = true;
  taskEXIT_CRITICAL(&s_streamMux);

  return true;
}

// ---------------- PSRAM AUDIO JITTER RING BUFFER (192,000 samples = 384 KB = 12.0s @ 16kHz) ----------------
static const size_t AUDIO_RING_CAPACITY = 192000;      // 384 KB PSRAM (~12 giây âm thanh Mono 16kHz)
static const size_t AUDIO_PREBUFFER_THRESHOLD = 8000;  // 0.50 giây âm thanh tải trước (Hấp thụ hoàn toàn Jitter Wi-Fi)
static int16_t* s_audioRingBuf          = nullptr;
static volatile size_t s_audioRingHead  = 0; // Vị trí đọc
static volatile size_t s_audioRingTail  = 0; // Vị trí ghi
static volatile size_t s_audioRingCount = 0;
static volatile bool   s_isPrebuffering = true;
static portMUX_TYPE s_audioRingMux      = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t s_audioPumpTaskHandle = nullptr;
static volatile bool s_audioPumpRunning   = false;

// ---------------- UDP AUDIO RECEIVER TASK (Core 0, Port 12345) ----------------
static const uint16_t AUDIO_UDP_PORT      = 12345;
static TaskHandle_t s_audioUdpTaskHandle  = nullptr;
static volatile bool s_audioUdpRunning    = false;
static int s_audioUdpSock                 = -1;
static uint32_t s_udpPacketsReceived      = 0;

static void audioPumpTask(void* param) {
  Serial.println("🎧 [AUDIO PUMP TASK] Đã khởi chạy trên Core 0 (PSRAM 384KB Ring Buffer Jitter Absorber)!");
  const size_t CHUNK = 256; // 16ms âm thanh ở 16kHz
  int16_t localMono[CHUNK];

  while (s_audioPumpRunning) {
    size_t count = 0;
    taskENTER_CRITICAL(&s_audioRingMux);
    count = s_audioRingCount;
    taskEXIT_CRITICAL(&s_audioRingMux);

    // Pre-buffering: Khi mới bắt đầu hoặc sau khi cạn dữ liệu, nạp đầy 0.50s (~8,000 mẫu) mới kích hoạt loa
    if (s_isPrebuffering) {
      if (count >= AUDIO_PREBUFFER_THRESHOLD) {
        s_isPrebuffering = false;
        Serial.printf("🎧 [AUDIO PUMP] ✅ Pre-buffering hoàn tất (%u mẫu ~ %.2fs)! Kích hoạt phát loa I2S.\n",
                      (unsigned)count, (float)count / 16000.0f);
      } else {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
    }

    if (count >= CHUNK) {
      taskENTER_CRITICAL(&s_audioRingMux);
      for (size_t i = 0; i < CHUNK; i++) {
        localMono[i] = s_audioRingBuf[s_audioRingHead];
        s_audioRingHead = (s_audioRingHead + 1) % AUDIO_RING_CAPACITY;
      }
      s_audioRingCount -= CHUNK;
      taskEXIT_CRITICAL(&s_audioRingMux);

      TestAudio::playStreamPcm(localMono, CHUNK);
      taskYIELD();
    } else if (count > 0) {
      // Buffer sắp cạn: nếu sau 1500ms không có thêm gói mới từ mạng, phát nốt phần còn lại rồi vào chế độ pre-buffering
      if (millis() - s_lastAudioPcmMs > 1500) {
        size_t toPlay = count;
        taskENTER_CRITICAL(&s_audioRingMux);
        for (size_t i = 0; i < toPlay; i++) {
          localMono[i] = s_audioRingBuf[s_audioRingHead];
          s_audioRingHead = (s_audioRingHead + 1) % AUDIO_RING_CAPACITY;
        }
        s_audioRingCount -= toPlay;
        taskEXIT_CRITICAL(&s_audioRingMux);

        TestAudio::playStreamPcm(localMono, toPlay);
        s_isPrebuffering = true;
      } else {
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    } else {
      // count == 0: Chờ nhẹ; chỉ bật lại cờ prebuffering nếu đã quá 1500ms không có dữ liệu mới
      if (millis() - s_lastAudioPcmMs > 1500) {
        s_isPrebuffering = true;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  s_audioPumpTaskHandle = nullptr;
  vTaskDelete(NULL);
}

static void audioUdpTask(void* param) {
  Serial.println("🚀 [UDP AUDIO] Task lắng nghe UDP đã khởi tạo trên Core 0 (chờ mạng Wi-Fi)...");

  while (s_audioUdpRunning) {
    // 1. Chờ đến khi Wi-Fi có IP (STA kết nối hoặc SoftAP đã bật)
    if (WiFi.status() != WL_CONNECTED && WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // Đệm thêm 200ms sau khi có IP để chắc chắn stack LWIP sẵn sàng 100%
    vTaskDelay(pdMS_TO_TICKS(200));

    Serial.printf("🚀 [UDP AUDIO] Đang mở socket UDP trên port %u (Core 0)...\n", AUDIO_UDP_PORT);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
      Serial.printf("❌ [UDP AUDIO] Không thể tạo socket UDP (errno %d)\n", errno);
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // Tăng buffer nhận LWIP lên 32KB để hấp thụ jitter tối đa
    int rcvbuf = 32768;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    // Timeout 1 giây để kiểm tra cờ s_audioUdpRunning
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(AUDIO_UDP_PORT);

    if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
      Serial.printf("❌ [UDP AUDIO] Không thể bind UDP port %u (errno %d)\n", AUDIO_UDP_PORT, errno);
      close(sock);
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    s_audioUdpSock = sock;
    Serial.printf("✅ [UDP AUDIO] Đang lắng nghe UDP port %u trên Core 0! Sẵn sàng nhận PCM 16kHz.\n", AUDIO_UDP_PORT);

    const size_t UDP_BUF_MAX = 2048;
    int16_t* udpBuf = (int16_t*)(psramFound() ? ps_malloc(UDP_BUF_MAX) : malloc(UDP_BUF_MAX));
    if (!udpBuf) {
      Serial.println("❌ [UDP AUDIO] Không đủ bộ nhớ cấp phát cho buffer UDP!");
      close(sock);
      s_audioUdpSock = -1;
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (s_audioUdpRunning && (WiFi.status() == WL_CONNECTED || WiFi.softAPIP() != IPAddress(0, 0, 0, 0))) {
      int len = recvfrom(sock, (char*)udpBuf, UDP_BUF_MAX, 0, (struct sockaddr*)&client_addr, &client_len);
      if (len <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }

      size_t samples = len / sizeof(int16_t);
      if (samples > 0) {
        s_udpPacketsReceived++;
        WebStreamManager::playAudioPcmChunk(udpBuf, samples);
      }
    }

    if (udpBuf) free(udpBuf);
    close(sock);
    s_audioUdpSock = -1;
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  s_audioUdpTaskHandle = nullptr;
  Serial.println("🛑 [UDP AUDIO] Task UDP đã dừng.");
  vTaskDelete(NULL);
}

void WebStreamManager::startAudioUdpListener() {
  if (!s_audioUdpTaskHandle) {
    s_audioUdpRunning = true;
    xTaskCreatePinnedToCore(
      audioUdpTask,
      "audioUdpTask",
      4096,
      nullptr,
      3,
      &s_audioUdpTaskHandle,
      0
    );
  }
}

void WebStreamManager::playAudioPcmChunk(const int16_t* pcmSamples, size_t sampleCount) {
  if (!pcmSamples || sampleCount == 0) return;
  s_lastAudioPcmMs = millis();
  s_audioChunkCount++;

  // Khởi tạo Ring Buffer 384KB trên PSRAM nếu chưa có
  if (!s_audioRingBuf) {
    if (psramFound()) {
      s_audioRingBuf = (int16_t*)ps_malloc(AUDIO_RING_CAPACITY * sizeof(int16_t));
    }
    if (!s_audioRingBuf) {
      s_audioRingBuf = (int16_t*)malloc(AUDIO_RING_CAPACITY * sizeof(int16_t));
    }
    s_audioRingHead = 0;
    s_audioRingTail = 0;
    s_audioRingCount = 0;
    s_isPrebuffering = true;
  }

  if (!s_audioRingBuf) return;

  // Khởi động Task phát ngầm trên Core 0 với Priority 4 nếu chưa chạy
  if (!s_audioPumpTaskHandle) {
    s_audioPumpRunning = true;
    xTaskCreatePinnedToCore(
      audioPumpTask,
      "audioPumpTask",
      4096,
      nullptr,
      4,
      &s_audioPumpTaskHandle,
      0
    );
  }

  // Đẩy toàn bộ mẫu vào Ring Buffer (cực nhanh: ~0.02ms, không bao giờ drop, không block WebServer)
  taskENTER_CRITICAL(&s_audioRingMux);
  for (size_t i = 0; i < sampleCount; i++) {
    if (s_audioRingCount < AUDIO_RING_CAPACITY) {
      s_audioRingBuf[s_audioRingTail] = pcmSamples[i];
      s_audioRingTail = (s_audioRingTail + 1) % AUDIO_RING_CAPACITY;
      s_audioRingCount++;
    } else {
      // Bể chứa 384KB (~12 giây) nếu đầy tràn do client gửi quá nhanh: trượt mẫu cũ nhất
      s_audioRingHead = (s_audioRingHead + 1) % AUDIO_RING_CAPACITY;
      s_audioRingBuf[s_audioRingTail] = pcmSamples[i];
      s_audioRingTail = (s_audioRingTail + 1) % AUDIO_RING_CAPACITY;
    }
  }
  taskEXIT_CRITICAL(&s_audioRingMux);

  if (s_audioChunkCount == 1 || s_audioChunkCount % 50 == 0) {
    Serial.printf("🔊 [STREAM AUDIO] Nhận %u mẫu PCM (chunk #%u | đệm: %u mẫu ~ %.2fs | drop video: %u)\n",
                  (unsigned)sampleCount, (unsigned)s_audioChunkCount,
                  (unsigned)s_audioRingCount, (float)s_audioRingCount / 16000.0f,
                  (unsigned)s_videoFramesDropped);
  }
}

bool WebStreamManager::startUrlStream(const String& type, const String& url) {
  stopUrlStream();
  if (url.length() == 0) return false;

  if (ESP.getFreeHeap() < 32000) {
    Serial.printf("⚠️ [STREAM] Bộ nhớ SRAM khả dụng quá thấp (%u bytes), không thể khởi chạy task stream URL an toàn!\n",
                  (unsigned)ESP.getFreeHeap());
    return false;
  }

  s_activeUrlType = type;
  s_activeUrl = url;
  s_urlStreamRunning = true;
  s_streamSessionActive = true;

  // Tạo Task trên Core 0 với Stack 12KB (an toàn cho minimp3 scratch và HTTP Client)
  BaseType_t res = xTaskCreatePinnedToCore(
    urlStreamTask,
    "urlStreamTask",
    12288,
    nullptr,
    1,
    &s_urlStreamTaskHandle,
    0
  );

  return (res == pdPASS);
}

void WebStreamManager::stopUrlStream() {
  if (s_urlStreamRunning) {
    s_urlStreamRunning = false;
    unsigned long waitStart = millis();
    while (s_urlStreamTaskHandle != nullptr && (millis() - waitStart < 1500)) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
  s_activeUrl = "";
  s_activeUrlType = "";
  s_asyncVideoReady = false;
  s_asyncVideoLen = 0;
}

void WebStreamManager::stopAll() {
  s_streamSessionActive = false;
  s_lastStopMs = millis();
  stopUrlStream();
  TestDisplay::stopStreamMode();
  s_lastVideoFrameMs = 0;
  s_lastAudioPcmMs = 0;
  s_currentFps = 0.0f;
  s_asyncVideoReady = false;
  s_asyncVideoLen = 0;
  s_videoFramesDropped = 0;
  s_isPrebuffering = true;

  // Xóa sạch Ring Buffer âm thanh
  taskENTER_CRITICAL(&s_audioRingMux);
  s_audioRingHead = 0;
  s_audioRingTail = 0;
  s_audioRingCount = 0;
  taskEXIT_CRITICAL(&s_audioRingMux);
}

bool WebStreamManager::isStreamSessionActive() {
  return s_streamSessionActive;
}

void WebStreamManager::setStreamSessionActive(bool active) {
  s_streamSessionActive = active;
  if (!active) {
    s_lastStopMs = millis();
  }
}

bool WebStreamManager::isVideoStreaming() {
  return s_streamSessionActive && ((s_lastVideoFrameMs > 0 && millis() - s_lastVideoFrameMs < 4000) || (s_urlStreamRunning && s_activeUrlType == "video"));
}

bool WebStreamManager::isAudioStreaming() {
  return (s_lastAudioPcmMs > 0 && millis() - s_lastAudioPcmMs < 3000) || (s_urlStreamRunning && s_activeUrlType == "audio");
}

float WebStreamManager::getFps() {
  if (millis() - s_lastVideoFrameMs > 3000) return 0.0f;
  return s_currentFps;
}

uint32_t WebStreamManager::getDroppedFrames() {
  return s_videoFramesDropped;
}

void WebStreamManager::getStatusJson(JsonDocument& doc) {
  doc["video_active"]   = isVideoStreaming();
  doc["audio_active"]   = isAudioStreaming();
  doc["fps"]            = serialized(String(getFps(), 1));
  doc["video_frames"]   = s_videoFrameCount;
  doc["video_dropped"]  = s_videoFramesDropped;
  doc["audio_chunks"]   = s_audioChunkCount;
  doc["audio_udp_pkts"] = s_udpPacketsReceived;
  doc["audio_buf_ms"]   = (uint32_t)((s_audioRingCount * 1000ULL) / 16000ULL);
  doc["prebuffering"]   = (bool)s_isPrebuffering;
  doc["url_running"]    = (bool)s_urlStreamRunning;
  doc["url_type"]       = s_activeUrlType;
  doc["url"]            = s_activeUrl;
  doc["vol_music"]      = TestAudio::getMusicVolumePct();
}

bool WebStreamManager::hasAllocatedBuffers() {
  return (s_asyncVideoBuf != nullptr || s_audioRingBuf != nullptr);
}

size_t WebStreamManager::trimMemory() {
  // Không thu hồi nếu đang stream video hoặc audio
  if (isVideoStreaming() || isAudioStreaming() || s_urlStreamRunning) {
    return 0;
  }

  size_t totalFreed = 0;

  // 1. Dừng task Audio Pump nếu đang chạy ngầm
  if (s_audioPumpTaskHandle != nullptr) {
    s_audioPumpRunning = false;
    unsigned long startWait = millis();
    while (s_audioPumpTaskHandle != nullptr && millis() - startWait < 150) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  // 2. Thu hồi s_audioRingBuf (384 KB)
  int16_t* pAudio = nullptr;
  taskENTER_CRITICAL(&s_audioRingMux);
  if (s_audioRingBuf != nullptr) {
    pAudio = s_audioRingBuf;
    s_audioRingBuf = nullptr;
    s_audioRingHead = 0;
    s_audioRingTail = 0;
    s_audioRingCount = 0;
    s_isPrebuffering = true;
    totalFreed += AUDIO_RING_CAPACITY * sizeof(int16_t);
  }
  taskEXIT_CRITICAL(&s_audioRingMux);

  if (pAudio != nullptr) {
    free(pAudio);
  }

  // 3. Thu hồi s_asyncVideoBuf (64 KB)
  uint8_t* pVideo = nullptr;
  taskENTER_CRITICAL(&s_streamMux);
  if (s_asyncVideoBuf != nullptr) {
    pVideo = s_asyncVideoBuf;
    s_asyncVideoBuf = nullptr;
    s_asyncVideoLen = 0;
    s_asyncVideoReady = false;
    totalFreed += MAX_STREAM_JPG_SIZE;
  }
  taskEXIT_CRITICAL(&s_streamMux);

  if (pVideo != nullptr) {
    free(pVideo);
  }

  if (totalFreed > 0) {
    Serial.printf("🧹 [TRIM STREAM] Đã giải phóng bộ đệm đa phương tiện (%u bytes / %u KB)\n",
                  (unsigned int)totalFreed, (unsigned int)(totalFreed / 1024));
  }
  return totalFreed;
}
