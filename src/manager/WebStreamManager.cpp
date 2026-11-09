#include "WebStreamManager.h"
#include "tests/TestDisplay.h"
#include "tests/TestAudio.h"
#include "Config.h"
#include "minimp3.h"
#include "services/PowerManagementService.h"

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
  static uint32_t s_videoFramesDropped    = 0;
  static volatile bool s_isDisplayRendering = false;

  // ---------------- PSRAM VIDEO JITTER RING BUFFER (20 FRAMES = 1.33s @ 15fps) ----------------
  static const size_t VIDEO_RING_CAPACITY = 20;            // 20 frames buffer trên PSRAM
  static const size_t MAX_FRAME_SLOT_SIZE = 80 * 1024;     // 80KB max cho mỗi frame JPEG
  static const size_t VIDEO_PREBUFFER_THRESHOLD = 8;       // Cần tích lũy >= 8 frames video mới phát

  struct QueuedVideoFrame {
    uint8_t* data;
    size_t   len;
    uint32_t pts;
    uint16_t seq;
    volatile bool valid;
  };

  static QueuedVideoFrame s_videoRing[VIDEO_RING_CAPACITY] = {0};
  static volatile size_t  s_videoHead = 0; // Vị trí ghi của Core 0
  static volatile size_t  s_videoTail = 0; // Vị trí đọc/vẽ của Core 1
  static volatile size_t  s_videoRingCount = 0;
  static portMUX_TYPE     s_videoRingMux = portMUX_INITIALIZER_UNLOCKED;

  // ---------------- MASTER CLOCK & PREBUFFERING STATE ----------------
  static volatile bool     s_isPrebuffering       = true;
  static unsigned long     s_firstPacketArrivalMs = 0;
  static unsigned long     s_playbackStartTimeMs  = 0;
  static uint32_t          s_playbackStartPts     = 0;

  static bool ensureVideoRingSlots() {
    if (!psramFound()) return false;
    for (size_t i = 0; i < VIDEO_RING_CAPACITY; i++) {
      if (!s_videoRing[i].data) {
        s_videoRing[i].data = (uint8_t*)ps_malloc(MAX_FRAME_SLOT_SIZE);
        if (!s_videoRing[i].data) {
          Serial.printf("❌ [STREAM] Không đủ PSRAM cấp phát slot video #%u\n", (unsigned)i);
          return false;
        }
      }
    }
    return true;
  }

  static void freeVideoRingSlots() {
    uint8_t* toFree[VIDEO_RING_CAPACITY] = {nullptr};

    taskENTER_CRITICAL(&s_videoRingMux);
    for (size_t i = 0; i < VIDEO_RING_CAPACITY; i++) {
      toFree[i] = s_videoRing[i].data;
      s_videoRing[i].data = nullptr;
      s_videoRing[i].len = 0;
      s_videoRing[i].pts = 0;
      s_videoRing[i].seq = 0;
      s_videoRing[i].valid = false;
    }
    s_videoHead = 0;
    s_videoTail = 0;
    s_videoRingCount = 0;
    taskEXIT_CRITICAL(&s_videoRingMux);

    // Giải phóng bộ nhớ NGOÀI critical section để tránh panic mutex trong FreeRTOS
    for (size_t i = 0; i < VIDEO_RING_CAPACITY; i++) {
      if (toFree[i]) {
        free(toFree[i]);
      }
    }
  }

  // ---------------- PSRAM AUDIO JITTER RING BUFFER (192,000 samples = 384 KB = 12.0s @ 16kHz) ----------------
  static const size_t AUDIO_RING_CAPACITY = 192000;      // 384 KB PSRAM (~12 giây âm thanh Mono 16kHz)
  static int16_t* s_audioRingBuf          = nullptr;
  static volatile size_t s_audioRingHead  = 0; // Vị trí đọc
  static volatile size_t s_audioRingTail  = 0; // Vị trí ghi
  static volatile size_t s_audioRingCount = 0;
  static portMUX_TYPE s_audioRingMux      = portMUX_INITIALIZER_UNLOCKED;

  static TaskHandle_t s_audioPumpTaskHandle = nullptr;
  static volatile bool s_audioPumpRunning   = false;

  static void audioPumpTask(void* param) {
    Serial.println("🎧 [AUDIO PUMP TASK] Đã khởi chạy trên Core 0 (PSRAM 384KB Ring Buffer Jitter Absorber)!");
    const size_t CHUNK = 256; // 16ms âm thanh ở 16kHz
    int16_t localMono[CHUNK];

    while (s_audioPumpRunning) {
      // Nếu đang trong giai đoạn Prebuffering: giữ im lặng, không xả ra I2S vội
      if (s_isPrebuffering) {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }

      size_t count = 0;
      taskENTER_CRITICAL(&s_audioRingMux);
      count = s_audioRingCount;
      taskEXIT_CRITICAL(&s_audioRingMux);

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
        // Buffer sắp cạn: nếu sau 1000ms không có thêm gói mới từ mạng, phát nốt phần còn lại
        if (millis() - s_lastAudioPcmMs > 1000) {
          size_t toPlay = count;
          taskENTER_CRITICAL(&s_audioRingMux);
          for (size_t i = 0; i < toPlay; i++) {
            localMono[i] = s_audioRingBuf[s_audioRingHead];
            s_audioRingHead = (s_audioRingHead + 1) % AUDIO_RING_CAPACITY;
          }
          s_audioRingCount -= toPlay;
          taskEXIT_CRITICAL(&s_audioRingMux);

          TestAudio::playStreamPcm(localMono, toPlay);
        } else {
          vTaskDelay(pdMS_TO_TICKS(2));
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }

    s_audioPumpTaskHandle = nullptr;
    vTaskDelete(NULL);
  }

  // ---------------- UNIFIED TCP STREAM RECEIVER TASK (Core 0, Port 12345) ----------------
  static const uint16_t UNIFIED_STREAM_PORT = 12345;
  static TaskHandle_t s_unifiedStreamTaskHandle = nullptr;
  static volatile bool s_unifiedStreamRunning = false;
  static int s_unifiedServerSock = -1;

  static bool recvAllExact(int sock, uint8_t* dest, size_t targetLen) {
    size_t got = 0;
    while (got < targetLen && s_unifiedStreamRunning) {
      int n = recv(sock, (char*)(dest + got), targetLen - got, 0);
      if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          vTaskDelay(pdMS_TO_TICKS(2));
          continue;
        }
        return false;
      }
      got += n;
    }
    return (got == targetLen);
  }

  static bool drainExact(int sock, size_t bytesToDrain) {
    uint8_t dummy[512];
    size_t rem = bytesToDrain;
    while (rem > 0 && s_unifiedStreamRunning) {
      size_t chunk = min(rem, sizeof(dummy));
      int n = recv(sock, (char*)dummy, chunk, 0);
      if (n <= 0) return false;
      rem -= n;
    }
    return true;
  }

  static void unifiedStreamTask(void* param) {
    Serial.printf("🚀 [UNIFIED STREAM] Task lắng nghe TCP đa phương tiện đã khởi tạo trên Core 0 (Port %u)...\n", UNIFIED_STREAM_PORT);

    while (s_unifiedStreamRunning) {
      if (WiFi.status() != WL_CONNECTED && WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      vTaskDelay(pdMS_TO_TICKS(200));

      int serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (serverSock < 0) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }

      int enable = 1;
      setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

      struct sockaddr_in serv_addr;
      memset(&serv_addr, 0, sizeof(serv_addr));
      serv_addr.sin_family = AF_INET;
      serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      serv_addr.sin_port = htons(UNIFIED_STREAM_PORT);

      if (bind(serverSock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(serverSock);
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }

      if (listen(serverSock, 2) < 0) {
        close(serverSock);
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }

      s_unifiedServerSock = serverSock;
      Serial.printf("✅ [UNIFIED STREAM] Đang lắng nghe trên TCP port %u (Core 0)! Sẵn sàng ghép kênh Audio + Video.\n", UNIFIED_STREAM_PORT);

      const size_t TEMP_AUDIO_MAX = 4096;
      int16_t* tempAudioBuf = (int16_t*)(psramFound() ? ps_malloc(TEMP_AUDIO_MAX) : malloc(TEMP_AUDIO_MAX));

      while (s_unifiedStreamRunning && (WiFi.status() == WL_CONNECTED || WiFi.softAPIP() != IPAddress(0, 0, 0, 0))) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int clientSock = accept(serverSock, (struct sockaddr*)&client_addr, &client_len);
        if (clientSock < 0) {
          if (!s_unifiedStreamRunning) break;
          vTaskDelay(pdMS_TO_TICKS(100));
          continue;
        }

        int nodelay = 1;
        setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        struct timeval tv;
        tv.tv_sec = 6;
        tv.tv_usec = 0;
        setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int rcvbuf = 16384; // 16KB tối ưu cho TCP Wi-Fi LAN, giải phóng 16KB SRAM quý giá chống tràn Heap
        setsockopt(clientSock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        Serial.println("⚡ [UNIFIED STREAM] Transcoder PC đã kết nối luồng SSUP ghép kênh (Audio+Video)!");
        s_streamSessionActive = true;
        s_isPrebuffering = true;
        s_firstPacketArrivalMs = millis();
        s_videoFramesDropped = 0;
        ensureVideoRingSlots();

        // Xóa sạch bộ đệm âm thanh khi bắt đầu phiên mới
        taskENTER_CRITICAL(&s_audioRingMux);
        s_audioRingHead = 0;
        s_audioRingTail = 0;
        s_audioRingCount = 0;
        taskEXIT_CRITICAL(&s_audioRingMux);

        // Xóa sạch bộ đệm video khi bắt đầu phiên mới
        taskENTER_CRITICAL(&s_videoRingMux);
        for (size_t i = 0; i < VIDEO_RING_CAPACITY; i++) {
          s_videoRing[i].valid = false;
          s_videoRing[i].len = 0;
          s_videoRing[i].pts = 0;
        }
        s_videoHead = 0;
        s_videoTail = 0;
        s_videoRingCount = 0;
        taskEXIT_CRITICAL(&s_videoRingMux);

        StreamPacketHeader hdr;
        while (s_unifiedStreamRunning) {
          // Đọc Header 16 bytes
          if (!recvAllExact(clientSock, (uint8_t*)&hdr, sizeof(StreamPacketHeader))) {
            break;
          }

          // Kiểm tra Magic "USP1"
          if (memcmp(hdr.magic, "USP1", 4) != 0) {
            Serial.println("⚠️ [UNIFIED STREAM] Mất đồng bộ byte magic, đang quét khôi phục...");
            uint8_t b = 0;
            char search[4] = { hdr.magic[1], hdr.magic[2], hdr.magic[3], 0 };
            while (s_unifiedStreamRunning) {
              if (recv(clientSock, (char*)&b, 1, 0) <= 0) break;
              search[3] = (char)b;
              if (memcmp(search, "USP1", 4) == 0) {
                if (recvAllExact(clientSock, ((uint8_t*)&hdr) + 4, sizeof(StreamPacketHeader) - 4)) {
                  break;
                }
              }
              search[0] = search[1];
              search[1] = search[2];
              search[2] = search[3];
            }
          }

          uint8_t  type = hdr.type;
          uint16_t seq = ntohs(hdr.seq);
          uint32_t pts = ntohl(hdr.pts);
          uint32_t payloadLen = ntohl(hdr.payloadLen);

          if (type == 1) {
            // --- AUDIO PACKET ---
            if (payloadLen > 0 && payloadLen <= TEMP_AUDIO_MAX && tempAudioBuf) {
              if (!recvAllExact(clientSock, (uint8_t*)tempAudioBuf, payloadLen)) break;
              WebStreamManager::playAudioPcmChunk(tempAudioBuf, payloadLen / sizeof(int16_t));
            } else {
              drainExact(clientSock, payloadLen);
            }
          } else if (type == 2) {
            // --- VIDEO JPEG PACKET ---
            if (payloadLen > 0 && payloadLen <= MAX_FRAME_SLOT_SIZE) {
              ensureVideoRingSlots();
              uint8_t* destSlot = s_videoRing[s_videoHead].data;
              if (destSlot && recvAllExact(clientSock, destSlot, payloadLen)) {
                // Kiểm tra SOI JPEG (0xFF, 0xD8)
                if (destSlot[0] == 0xFF && destSlot[1] == 0xD8) {
                  taskENTER_CRITICAL(&s_videoRingMux);
                  s_videoRing[s_videoHead].len = payloadLen;
                  s_videoRing[s_videoHead].pts = pts;
                  s_videoRing[s_videoHead].seq = seq;
                  s_videoRing[s_videoHead].valid = true;

                  size_t nextHead = (s_videoHead + 1) % VIDEO_RING_CAPACITY;
                  if (s_videoRingCount >= VIDEO_RING_CAPACITY) {
                    s_videoRing[s_videoTail].valid = false;
                    s_videoTail = (s_videoTail + 1) % VIDEO_RING_CAPACITY;
                    s_videoFramesDropped++;
                  } else {
                    s_videoRingCount++;
                  }
                  s_videoHead = nextHead;
                  taskEXIT_CRITICAL(&s_videoRingMux);

                  s_videoFrameCount++;
                  s_lastVideoFrameMs = millis();
                } else {
                  s_videoFramesDropped++;
                }
              } else {
                break;
              }
            } else {
              drainExact(clientSock, payloadLen);
            }
          } else if (type == 3) {
            // --- CLOCK SYNC / RESET ---
            if (payloadLen > 0) drainExact(clientSock, payloadLen);
            s_isPrebuffering = true;
            s_firstPacketArrivalMs = millis();
            s_videoFramesDropped = 0;

            taskENTER_CRITICAL(&s_audioRingMux);
            s_audioRingHead = 0;
            s_audioRingTail = 0;
            s_audioRingCount = 0;
            taskEXIT_CRITICAL(&s_audioRingMux);

            taskENTER_CRITICAL(&s_videoRingMux);
            for (size_t i = 0; i < VIDEO_RING_CAPACITY; i++) {
              s_videoRing[i].valid = false;
              s_videoRing[i].len = 0;
              s_videoRing[i].pts = 0;
            }
            s_videoHead = 0;
            s_videoTail = 0;
            s_videoRingCount = 0;
            taskEXIT_CRITICAL(&s_videoRingMux);
            Serial.println("🔄 [UNIFIED STREAM] Nhận lệnh CLOCK SYNC / RESET: Đã xóa sạch Ring Buffer!");
          } else if (type == 4) {
            // --- STREAM END ---
            if (payloadLen > 0) drainExact(clientSock, payloadLen);
            Serial.println("🛑 [UNIFIED STREAM] Nhận tín hiệu kết thúc luồng từ client.");
            break;
          } else {
            if (payloadLen > 0) drainExact(clientSock, payloadLen);
          }
        }

        close(clientSock);
        s_streamSessionActive = false;
        TestDisplay::stopStreamMode();
        Serial.println("⚪ [UNIFIED STREAM] Transcoder PC đã ngắt kết nối.");
      }

      if (tempAudioBuf) free(tempAudioBuf);
      close(serverSock);
      s_unifiedServerSock = -1;
      vTaskDelay(pdMS_TO_TICKS(500));
    }

    s_unifiedStreamTaskHandle = nullptr;
    vTaskDelete(NULL);
  }

  // Task xử lý luồng stream URL (Web Radio MP3 hoặc MJPEG Video) chạy nền trên Core 0
  static void urlStreamTask(void* param) {
    Serial.printf("🌐 [WEB STREAM TASK] Bắt đầu kết nối URL (%s): %s\n",
                  s_activeUrlType.c_str(), s_activeUrl.c_str());

    bool isHttps = s_activeUrl.startsWith("https://");

    if (s_activeUrlType == "audio") {
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
              int32_t m = (int32_t)(pcmOut[i * 2] * volScale);
              int16_t s = (int16_t)constrain(m, -31500, 31500);
              stereoPcm[i * 2]     = s;
              stereoPcm[i * 2 + 1] = s;
            }
          }

          size_t written = 0;
          i2s_write((i2s_port_t)1, stereoPcm, samplesPerChannel * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));

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

      uint8_t* rawJpgBuf = (uint8_t*)(psramFound() ? ps_malloc(MAX_FRAME_SLOT_SIZE) : malloc(MAX_FRAME_SLOT_SIZE));
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
              if (jpgLen < MAX_FRAME_SLOT_SIZE) {
                rawJpgBuf[jpgLen++] = b;
              }
              if (lastByte == 0xFF && b == 0xD9) { // EOI
                inFrame = false;
                if (jpgLen > 100) {
                  WebStreamManager::queueVideoFrame(rawJpgBuf, jpgLen, 0, 0);
                }
                jpgLen = 0;
              }
            }
            lastByte = b;
          }
        } else {
          vTaskDelay(pdMS_TO_TICKS(5));
        }

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
} // namespace

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
  s_isPrebuffering   = true;
  s_firstPacketArrivalMs = 0;

  // Cấp phát linh hoạt (lazy allocation) khi luồng stream thực sự kết nối,
  // tránh ngốn PSRAM khi trạm đang ở Màn hình chờ hoặc các chế độ khác.

  Serial.println("📡 [STREAM] WebStreamManager đã khởi tạo thành công với SSUP Unified Ring Buffer!");
}

void WebStreamManager::loop() {
  unsigned long now = millis();

  // 1. Kiểm tra trạng thái Prebuffering
  if (s_streamSessionActive && s_isPrebuffering) {
    size_t vCount = 0;
    taskENTER_CRITICAL(&s_videoRingMux);
    vCount = s_videoRingCount;
    taskEXIT_CRITICAL(&s_videoRingMux);

    size_t aCount = 0;
    taskENTER_CRITICAL(&s_audioRingMux);
    aCount = s_audioRingCount;
    taskEXIT_CRITICAL(&s_audioRingMux);

    bool ready = false;
    // Ngưỡng 1: Đủ 8 frames video và 12,000 mẫu âm thanh (~0.75s)
    if (vCount >= VIDEO_PREBUFFER_THRESHOLD && aCount >= 12000) {
      ready = true;
    }
    // Ngưỡng 2: Luồng chỉ có video (không có audio) và tích lũy >= 10 frames
    else if (vCount >= 10 && aCount == 0 && (s_firstPacketArrivalMs > 0) && (now - s_firstPacketArrivalMs > 800)) {
      ready = true;
    }
    // Ngưỡng 3: Hết thời gian chờ 1200ms và có tối thiểu 4 frames hoặc 8000 mẫu âm thanh
    else if ((s_firstPacketArrivalMs > 0) && (now - s_firstPacketArrivalMs >= 1200) && (vCount >= 4 || aCount >= 8000)) {
      ready = true;
    }

    if (ready) {
      s_isPrebuffering = false;
      s_playbackStartTimeMs = millis();
      taskENTER_CRITICAL(&s_videoRingMux);
      s_playbackStartPts = (s_videoRingCount > 0 && s_videoRing[s_videoTail].valid) ? s_videoRing[s_videoTail].pts : 0;
      taskEXIT_CRITICAL(&s_videoRingMux);

      Serial.printf("🎬 [STREAM SYNC] ✅ Prebuffering XONG! Video đệm: %u frames, Audio đệm: %u mẫu (~%.2fs) | Khởi động phát đồng bộ!\n",
                    (unsigned)vCount, (unsigned)aCount, (float)aCount / 16000.0f);
    }
  }

  // 2. Trình phát Video Frame đồng bộ PTS trên Core 1
  if (s_streamSessionActive && !s_isPrebuffering) {
    size_t vCount = 0;
    taskENTER_CRITICAL(&s_videoRingMux);
    vCount = s_videoRingCount;
    taskEXIT_CRITICAL(&s_videoRingMux);

    size_t aCount = 0;
    taskENTER_CRITICAL(&s_audioRingMux);
    aCount = s_audioRingCount;
    taskEXIT_CRITICAL(&s_audioRingMux);

    if (vCount == 0 && aCount == 0 && s_lastVideoFrameMs > 0 && (now - s_lastVideoFrameMs > 800)) {
      s_isPrebuffering = true;
      s_firstPacketArrivalMs = now;
      Serial.println("⏳ [STREAM SYNC] Bộ đệm cạn (Jitter Starvation)! Tạm dừng để Prebuffering lại...");
    } else if (vCount > 0) {
      uint32_t currentClockMs = now - s_playbackStartTimeMs;
      uint32_t targetPts = s_playbackStartPts + currentClockMs;

      taskENTER_CRITICAL(&s_videoRingMux);
      QueuedVideoFrame* cur = &s_videoRing[s_videoTail];
      uint32_t fPts = cur->pts;
      size_t   fLen = cur->len;
      uint8_t* fData = cur->data;
      bool     fValid = cur->valid;
      taskEXIT_CRITICAL(&s_videoRingMux);

      if (fValid && fData && fLen > 0) {
        if (fPts > targetPts + 10) {
          // Khung hình ở tương lai: chưa đến thời điểm vẽ -> nhường nhịp cho các task khác
        } else if (targetPts > fPts && (targetPts - fPts > 120)) {
          // Khung hình bị trễ hơn 120ms: DROP ngay để đuổi kịp nhịp âm thanh
          taskENTER_CRITICAL(&s_videoRingMux);
          cur->valid = false;
          s_videoTail = (s_videoTail + 1) % VIDEO_RING_CAPACITY;
          s_videoRingCount--;
          s_videoFramesDropped++;
          taskEXIT_CRITICAL(&s_videoRingMux);
        } else {
          // Khung hình đúng nhịp! Tiến hành giải mã và vẽ lên màn hình ST7789
          s_isDisplayRendering = true;
          TestDisplay::drawStreamJpegFrame(fData, fLen);
          s_isDisplayRendering = false;

          taskENTER_CRITICAL(&s_videoRingMux);
          cur->valid = false;
          s_videoTail = (s_videoTail + 1) % VIDEO_RING_CAPACITY;
          s_videoRingCount--;
          s_videoFrameCount++;
          s_fpsFrameCounter++;
          taskEXIT_CRITICAL(&s_videoRingMux);
        }
      } else {
        taskENTER_CRITICAL(&s_videoRingMux);
        s_videoTail = (s_videoTail + 1) % VIDEO_RING_CAPACITY;
        if (s_videoRingCount > 0) s_videoRingCount--;
        taskEXIT_CRITICAL(&s_videoRingMux);
      }
    }
  }

  // 3. Tính FPS trung bình mỗi 1000ms
  if (now - s_fpsCalcStartMs >= 1000) {
    s_currentFps = (float)s_fpsFrameCounter * 1000.0f / (float)(now - s_fpsCalcStartMs);
    s_fpsFrameCounter = 0;
    s_fpsCalcStartMs = now;
  }
}

bool WebStreamManager::queueVideoFrame(const uint8_t* jpegData, size_t len, uint32_t pts, uint16_t seq) {
  if (!jpegData || len < 4 || len > MAX_FRAME_SLOT_SIZE) return false;

  if (jpegData[0] != 0xFF || jpegData[1] != 0xD8) {
    s_videoFramesDropped++;
    return false;
  }

  if (millis() - s_lastStopMs < 1200) {
    s_videoFramesDropped++;
    return false;
  }

  if (!ensureVideoRingSlots()) {
    s_videoFramesDropped++;
    return false;
  }

  if (s_firstPacketArrivalMs == 0) {
    s_firstPacketArrivalMs = millis();
  }

  s_streamSessionActive = true;
  s_lastVideoFrameMs = millis();

  taskENTER_CRITICAL(&s_videoRingMux);
  size_t slot = s_videoHead;
  memcpy(s_videoRing[slot].data, jpegData, len);
  s_videoRing[slot].len = len;
  s_videoRing[slot].pts = pts;
  s_videoRing[slot].seq = seq;
  s_videoRing[slot].valid = true;

  s_videoHead = (s_videoHead + 1) % VIDEO_RING_CAPACITY;
  if (s_videoRingCount >= VIDEO_RING_CAPACITY) {
    s_videoRing[s_videoTail].valid = false;
    s_videoTail = (s_videoTail + 1) % VIDEO_RING_CAPACITY;
    s_videoFramesDropped++;
  } else {
    s_videoRingCount++;
  }
  taskEXIT_CRITICAL(&s_videoRingMux);

  return true;
}

void WebStreamManager::playAudioPcmChunk(const int16_t* pcmSamples, size_t sampleCount) {
  if (!pcmSamples || sampleCount == 0) return;
  s_lastAudioPcmMs = millis();
  s_audioChunkCount++;

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
  }

  if (!s_audioRingBuf) return;

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

  taskENTER_CRITICAL(&s_audioRingMux);
  for (size_t i = 0; i < sampleCount; i++) {
    if (s_audioRingCount < AUDIO_RING_CAPACITY) {
      s_audioRingBuf[s_audioRingTail] = pcmSamples[i];
      s_audioRingTail = (s_audioRingTail + 1) % AUDIO_RING_CAPACITY;
      s_audioRingCount++;
    } else {
      s_audioRingHead = (s_audioRingHead + 1) % AUDIO_RING_CAPACITY;
      s_audioRingBuf[s_audioRingTail] = pcmSamples[i];
      s_audioRingTail = (s_audioRingTail + 1) % AUDIO_RING_CAPACITY;
    }
  }
  taskEXIT_CRITICAL(&s_audioRingMux);
}

void WebStreamManager::startUnifiedStreamListener() {
  if (!s_unifiedStreamTaskHandle) {
    s_unifiedStreamRunning = true;
    xTaskCreatePinnedToCore(
      unifiedStreamTask,
      "unifiedStreamTask",
      8192,
      nullptr,
      3,
      &s_unifiedStreamTaskHandle,
      0
    );
  }
}

void WebStreamManager::startAudioUdpListener() {
  startUnifiedStreamListener();
}

void WebStreamManager::startVideoTcpListener() {
  startUnifiedStreamListener();
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
}

void WebStreamManager::stopAll() {
  s_streamSessionActive = false;
  s_lastStopMs = millis();
  stopUrlStream();
  TestDisplay::stopStreamMode();
  s_lastVideoFrameMs = 0;
  s_lastAudioPcmMs = 0;
  s_currentFps = 0.0f;
  s_videoFramesDropped = 0;
  s_isPrebuffering = true;
  s_firstPacketArrivalMs = 0;

  // Xóa sạch Ring Buffer âm thanh
  taskENTER_CRITICAL(&s_audioRingMux);
  s_audioRingHead = 0;
  s_audioRingTail = 0;
  s_audioRingCount = 0;
  taskEXIT_CRITICAL(&s_audioRingMux);

  // Xóa sạch Ring Buffer video
  taskENTER_CRITICAL(&s_videoRingMux);
  for (size_t i = 0; i < VIDEO_RING_CAPACITY; i++) {
    s_videoRing[i].valid = false;
    s_videoRing[i].len = 0;
  }
  s_videoHead = 0;
  s_videoTail = 0;
  s_videoRingCount = 0;
  taskEXIT_CRITICAL(&s_videoRingMux);
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

uint32_t WebStreamManager::getQueuedVideoFrames() {
  return s_videoRingCount;
}

void WebStreamManager::getStatusJson(JsonDocument& doc) {
  doc["video_active"]   = isVideoStreaming();
  doc["audio_active"]   = isAudioStreaming();
  doc["fps"]            = serialized(String(getFps(), 1));
  doc["video_frames"]   = s_videoFrameCount;
  doc["video_dropped"]  = s_videoFramesDropped;
  doc["video_queued"]   = s_videoRingCount;
  doc["audio_chunks"]   = s_audioChunkCount;
  doc["audio_buf_ms"]   = (uint32_t)((s_audioRingCount * 1000ULL) / 16000ULL);
  doc["prebuffering"]   = (bool)s_isPrebuffering;
  doc["url_running"]    = (bool)s_urlStreamRunning;
  doc["url_type"]       = s_activeUrlType;
  doc["url"]            = s_activeUrl;
  doc["vol_music"]      = TestAudio::getMusicVolumePct();
}

bool WebStreamManager::hasAllocatedBuffers() {
  return (s_videoRing[0].data != nullptr || s_audioRingBuf != nullptr);
}

size_t WebStreamManager::trimMemory() {
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
    totalFreed += AUDIO_RING_CAPACITY * sizeof(int16_t);
  }
  taskEXIT_CRITICAL(&s_audioRingMux);

  if (pAudio != nullptr) {
    free(pAudio);
  }

  // 3. Thu hồi toàn bộ 20 slot Video Ring Buffer trong PSRAM (1.6 MB)
  uint8_t* toFree[VIDEO_RING_CAPACITY] = {nullptr};

  taskENTER_CRITICAL(&s_videoRingMux);
  for (size_t i = 0; i < VIDEO_RING_CAPACITY; i++) {
    if (s_videoRing[i].data != nullptr) {
      toFree[i] = s_videoRing[i].data;
      s_videoRing[i].data = nullptr;
      totalFreed += MAX_FRAME_SLOT_SIZE;
    }
    s_videoRing[i].len = 0;
    s_videoRing[i].pts = 0;
    s_videoRing[i].seq = 0;
    s_videoRing[i].valid = false;
  }
  s_videoHead = 0;
  s_videoTail = 0;
  s_videoRingCount = 0;
  taskEXIT_CRITICAL(&s_videoRingMux);

  // Giải phóng ngoài critical section để đảm bảo an toàn tuyệt đối cho heap mutex
  for (size_t i = 0; i < VIDEO_RING_CAPACITY; i++) {
    if (toFree[i] != nullptr) {
      free(toFree[i]);
    }
  }

  if (totalFreed > 0) {
    Serial.printf("🧹 [TRIM STREAM] Đã giải phóng toàn bộ bộ đệm SSUP (%u bytes / %u KB)\n",
                  (unsigned int)totalFreed, (unsigned int)(totalFreed / 1024));
  }
  return totalFreed;
}
