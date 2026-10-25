#include "api/ApiMedia.h"
#include "Config.h"
#include "ImageManager.h"
#include "WebStreamManager.h"
#include "WebAuth.h"
#include "tests/TestDisplay.h"
#include "tests/TestAudio.h"
#include "tests/TestSDCard.h"
#include "drivers/StorageDriver.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>

namespace ApiMedia {

  static bool s_uploadAllowed = true;
  static File s_mediaUploadFile;
  static String s_lastUploadedMusicName = "";
  static bool s_mediaUploadSuccess = false;

  static uint8_t* s_streamJpegUploadBuf = nullptr;
  static size_t s_streamJpegUploadLen = 0;
  static const size_t MAX_STREAM_JPEG_UPLOAD = 64 * 1024;
  static uint32_t s_logVideoFrameCount = 0;
  static uint32_t s_logAudioPcmChunks = 0;
  static int16_t s_audioCarryByte = -1;

  // ================================================================
  //                BỘ NHỚ & BUFFER STREAMING VIDEO
  // ================================================================

  bool hasAllocatedUploadBuffer() {
    return (s_streamJpegUploadBuf != nullptr);
  }

  size_t trimMemory() {
    if (s_streamJpegUploadBuf == nullptr) return 0;
    if (WebStreamManager::isVideoStreaming()) return 0;

    size_t freed = MAX_STREAM_JPEG_UPLOAD;
    free(s_streamJpegUploadBuf);
    s_streamJpegUploadBuf = nullptr;
    s_streamJpegUploadLen = 0;
    Serial.printf("🧹 [TRIM WEB] Đã giải phóng bộ đệm upload video (%u KB)\n", (unsigned int)(freed / 1024));
    return freed;
  }

  // ================================================================
  //                REST APIS QUẢN LÝ ẢNH (SD / LITTLEFS)
  // ================================================================

  void handleListImages(WebServer& server) {
    JsonDocument doc;
    ImageManager::listImages(doc);
    String res;
    serializeJson(doc, res);
    server.send(200, "application/json", res);
  }

  void handleImageUploadRoute(WebServer& server) {
    bool forceSd = server.hasArg("target") && (server.arg("target") == "sd");
    ImageManager::handleUpload(server.upload(), s_uploadAllowed, forceSd);
  }

  void handleSdUploadRoute(WebServer& server) {
    ImageManager::handleUpload(server.upload(), s_uploadAllowed, true);
  }

  void handleSdFileRoute(WebServer& server) {
    if (!server.hasArg("path") || !TestSDCard::isMounted() || !StorageDriver::isHealthy()) {
      server.send(404, "text/plain", "SD card not mounted or not healthy");
      return;
    }
    String path = server.arg("path");
    if (!path.startsWith("/")) path = "/" + path;

    if (!StorageDriver::lock(pdMS_TO_TICKS(100))) {
      server.send(503, "text/plain", "SD card busy");
      return;
    }

    if (!SD.exists(path)) {
      StorageDriver::unlock();
      server.send(404, "text/plain", "SD file does not exist");
      return;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) {
      StorageDriver::notifyIoError(false, "handleSdFileRoute open failed");
      StorageDriver::unlock();
      server.send(500, "text/plain", "Cannot open SD file");
      return;
    }

    String contentType = "application/octet-stream";
    String low = path;
    low.toLowerCase();
    if (low.endsWith(".png")) contentType = "image/png";
    else if (low.endsWith(".jpg") || low.endsWith(".jpeg")) contentType = "image/jpeg";
    else if (low.endsWith(".bmp")) contentType = "image/bmp";

    server.streamFile(f, contentType);
    f.close();
    StorageDriver::notifyIoSuccess();
    StorageDriver::unlock();
  }

  void handleDeleteImage(WebServer& server) {
    if (!WebAuth::isAuthenticated(server)) {
      server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
      return;
    }
    if (!server.hasArg("name")) {
      server.send(400, "application/json", "{\"error\":\"Missing name\"}");
      return;
    }
    String filename = server.arg("name");
    if (ImageManager::deleteImage(filename)) {
      server.send(200, "application/json", "{\"status\":\"success\"}");
    } else {
      server.send(404, "application/json", "{\"error\":\"File not found\"}");
    }
  }

  void handleSelectImage(WebServer& server) {
    if (!WebAuth::isAuthenticated(server)) {
      server.send(401, "application/json", "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
      return;
    }
    if (!server.hasArg("name")) {
      server.send(400, "application/json", "{\"error\":\"Missing name\"}");
      return;
    }
    String filename = server.arg("name");
    if (!filename.startsWith("sd:") && !filename.startsWith("/")) filename = "/" + filename;
    ImageManager::selectActiveImage(filename);

    // Đồng bộ luôn vào /standby_config.json và đẩy lên màn hình ST7789
    JsonDocument doc;
    if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
      File rFile = LittleFS.open(FILE_STANDBY_CONFIG, "r");
      if (rFile) {
        deserializeJson(doc, rFile);
        rFile.close();
      }
    }
    doc["bg_mode"] = "image";
    doc["bg_image"] = filename;
    File wFile = LittleFS.open(FILE_STANDBY_CONFIG, "w");
    if (wFile) {
      serializeJson(doc, wFile);
      wFile.close();
    }
    TestDisplay::reloadStandbyConfig();

    server.send(200, "application/json", "{\"status\":\"success\"}");
  }

  void finishUploadSync(WebServer& server) {
    if (s_uploadAllowed) {
      String activeImg = ImageManager::getActiveImage();
      if (activeImg.length() > 0) {
        JsonDocument doc;
        if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
          File rFile = LittleFS.open(FILE_STANDBY_CONFIG, "r");
          if (rFile) {
            deserializeJson(doc, rFile);
            rFile.close();
          }
        }
        doc["bg_mode"] = "image";
        doc["bg_image"] = activeImg;
        File wFile = LittleFS.open(FILE_STANDBY_CONFIG, "w");
        if (wFile) {
          serializeJson(doc, wFile);
          wFile.close();
        }
      }
      TestDisplay::reloadStandbyConfig();
      server.send(200, "application/json", "{\"status\":\"success\",\"active_img\":\"" + activeImg + "\",\"message\":\"Đã convert & lưu ảnh vào Thẻ nhớ SD (/sd_images + /sd_rgb565) và đẩy lên màn hình ST7789!\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Đã đạt giới hạn tối đa 15 ảnh trên LittleFS. Hãy bật lưu sang Thẻ nhớ SD hoặc xóa bớt ảnh cũ!\"}");
    }
  }

  void handleLittleFsUpload(WebServer& server) {
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Uploaded to LittleFS\"}");
  }

  void handleLittleFsUploadChunk(WebServer& server) {
    HTTPUpload& upload = server.upload();
    static File f;
    if (upload.status == UPLOAD_FILE_START) {
      String filename = upload.filename;
      if (!filename.startsWith("/")) filename = "/" + filename;
      Serial.printf("📁 [LITTLEFS UPLOAD] Bắt đầu ghi file: %s\n", filename.c_str());
      if (LittleFS.exists(filename)) LittleFS.remove(filename);
      f = LittleFS.open(filename, FILE_WRITE);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (f) f.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (f) {
        f.close();
        Serial.printf("✅ [LITTLEFS UPLOAD] Ghi hoàn tất: %u bytes\n", upload.totalSize);
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      if (f) f.close();
      Serial.println("⚠️ [LITTLEFS UPLOAD] Hủy tải lên LittleFS");
    }
  }

  // ================================================================
  //         REST APIS TRÌNH PHÁT NHẠC (/Musics TRÊN THẺ NHỚ SD)
  // ================================================================

  void handleMediaStatus(WebServer& server) {
    String trackName;
    int trackIdx = 0, trackCount = 0, volumePct = 75;
    bool playing = true, fromSd = false;
    uint8_t visMode = 0, repeatMode = 0;
    uint16_t curSec = 0, totalSec = 180;

    TestDisplay::getMediaPlayerState(trackName, trackIdx, trackCount, playing,
                                     visMode, repeatMode, curSec, totalSec, volumePct, fromSd);

    JsonDocument doc;
    doc["playing"]     = playing;
    doc["track_idx"]   = trackIdx;
    doc["track_name"]  = trackName;
    doc["track_count"] = trackCount;
    doc["cur_sec"]     = curSec;
    doc["total_sec"]   = totalSec;
    doc["volume"]      = volumePct;
    doc["vis_mode"]    = visMode;
    doc["repeat_mode"] = repeatMode;
    doc["from_sd"]     = fromSd;
    doc["sd_mounted"]  = TestSDCard::isMounted();
    doc["mic_level"]   = TestAudio::getLastMicLevelPct();
    doc["ui_theme"]    = TestDisplay::getUiThemeIdx();

    JsonArray specArr = doc["spectrum"].to<JsonArray>();
    const uint8_t* spec16 = TestAudio::getMicSpectrum16();
    for (int i = 0; i < 16; i++) {
      specArr.add(spec16 ? spec16[i] : 0);
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void handleMediaList(WebServer& server) {
    String names[24];
    uint16_t durs[24];
    bool fromSd[24];
    size_t sizes[24];
    int cnt = TestDisplay::getMediaPlaylistItems(names, durs, fromSd, sizes, 24);

    JsonDocument doc;
    doc["sd_mounted"]  = TestSDCard::isMounted();
    doc["sd_total_mb"] = (uint32_t)TestSDCard::getTotalMB();
    doc["sd_used_mb"]  = (uint32_t)TestSDCard::getUsedMB();

    JsonArray arr = doc["tracks"].to<JsonArray>();
    for (int i = 0; i < cnt; i++) {
      JsonObject obj = arr.add<JsonObject>();
      obj["index"]    = i;
      obj["name"]     = names[i];
      obj["duration"] = durs[i];
      obj["from_sd"]  = fromSd[i];
      obj["size_kb"]  = (uint32_t)(sizes[i] / 1024);
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void handleMediaControl(WebServer& server) {
    if (server.hasArg("plain")) {
      JsonDocument doc;
      if (!deserializeJson(doc, server.arg("plain"))) {
        // 1. Phổ âm thanh thực FFT (spectrum[16]) từ web
        if (doc["spectrum"].is<JsonArray>()) {
          JsonArray sp = doc["spectrum"].as<JsonArray>();
          uint8_t bands[16] = {0};
          for (int i = 0; i < 16 && i < (int)sp.size(); i++) {
            bands[i] = (uint8_t)constrain(sp[i].as<int>(), 0, 100);
          }
          int lv = doc["mic_level"] | 25;
          TestAudio::setExternalAudioSpectrum(bands, lv);
        }

        // 2. Lệnh điều khiển trình phát
        if (!doc["action"].isNull()) {
          String action = doc["action"].as<String>();
          int val       = doc["value"] | 0;
          String name   = doc["name"] | "";
          TestDisplay::controlMediaPlayer(action, val, name);
        }
      }
    }
    handleMediaStatus(server);
  }

  void handleMediaUploadChunk(WebServer& server) {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      s_mediaUploadSuccess = false;
      s_lastUploadedMusicName = upload.filename;
      int sl = s_lastUploadedMusicName.lastIndexOf('/');
      if (sl >= 0) s_lastUploadedMusicName = s_lastUploadedMusicName.substring(sl + 1);
      int bsl = s_lastUploadedMusicName.lastIndexOf('\\');
      if (bsl >= 0) s_lastUploadedMusicName = s_lastUploadedMusicName.substring(bsl + 1);

      if (TestSDCard::isMounted() && StorageDriver::isHealthy()) {
        if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
          if (!SD.exists("/Musics")) {
            SD.mkdir("/Musics");
          }
          String fullPath = "/Musics/" + s_lastUploadedMusicName;
          if (SD.exists(fullPath)) SD.remove(fullPath);
          s_mediaUploadFile = SD.open(fullPath, FILE_WRITE);
          StorageDriver::unlock();
          Serial.printf("🎵 [SD /Musics] Đang nhận file nhạc: %s\n", fullPath.c_str());
        }
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (s_mediaUploadFile && upload.currentSize > 0) {
        if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
          size_t written = s_mediaUploadFile.write(upload.buf, upload.currentSize);
          if (written != upload.currentSize) {
            StorageDriver::notifyIoError(true, "handleMediaUploadChunk write error");
          } else {
            StorageDriver::notifyIoSuccess();
          }
          StorageDriver::unlock();
        }
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (s_mediaUploadFile) {
        if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
          s_mediaUploadFile.close();
          StorageDriver::unlock();
        }
        s_mediaUploadSuccess = true;
        Serial.printf("✅ [SD /Musics] Đã lưu xong: /Musics/%s (%u bytes)\n",
                      s_lastUploadedMusicName.c_str(), (unsigned)upload.totalSize);
        TestDisplay::controlMediaPlayer("select", 0, s_lastUploadedMusicName);
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      if (s_mediaUploadFile) {
        if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
          s_mediaUploadFile.close();
          StorageDriver::unlock();
        }
      }
      s_mediaUploadSuccess = false;
    }
  }

  void handleMediaUploadFinish(WebServer& server) {
    if (s_mediaUploadSuccess) {
      server.send(200, "application/json", "{\"status\":\"ok\",\"file\":\"" + s_lastUploadedMusicName + "\",\"message\":\"Đã lưu vào /Musics trên Thẻ SD!\"}");
    } else {
      server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Không thể ghi vào Thẻ nhớ SD (/Musics)!\"}");
    }
  }

  void handleMediaDelete(WebServer& server) {
    String name = server.arg("name");
    int sl = name.lastIndexOf('/');
    if (sl >= 0) name = name.substring(sl + 1);
    if (name.length() > 0 && TestSDCard::isMounted() && StorageDriver::isHealthy()) {
      if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
        String p1 = "/Musics/" + name;
        String p2 = "/" + name;
        bool removed = false;
        if (SD.exists(p1)) removed = SD.remove(p1);
        else if (SD.exists(p2)) removed = SD.remove(p2);
        StorageDriver::unlock();

        TestDisplay::controlMediaPlayer("refresh", 0);
        if (removed) {
          server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Đã xóa bài hát khỏi thư mục /Musics trên Thẻ SD!\"}");
          return;
        }
      }
    }
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Không tìm thấy tệp nhạc trên Thẻ SD!\"}");
  }

  void handleMediaStream(WebServer& server) {
    String name = server.arg("name");
    int sl = name.lastIndexOf('/');
    if (sl >= 0) name = name.substring(sl + 1);
    if (name.length() > 0 && TestSDCard::isMounted() && StorageDriver::isHealthy()) {
      String path = "/Musics/" + name;
      if (StorageDriver::lock(pdMS_TO_TICKS(100))) {
        if (!SD.exists(path)) path = "/" + name;
        if (SD.exists(path)) {
          File f = SD.open(path, FILE_READ);
          if (f) {
            String low = name;
            low.toLowerCase();
            String mime = "audio/mpeg";
            if (low.endsWith(".wav")) mime = "audio/wav";
            else if (low.endsWith(".ogg")) mime = "audio/ogg";
            else if (low.endsWith(".flac")) mime = "audio/flac";
            server.streamFile(f, mime);
            f.close();
            StorageDriver::notifyIoSuccess();
            StorageDriver::unlock();
            return;
          }
        }
        StorageDriver::unlock();
      }
    }
    server.send(404, "text/plain", "Music file not found on SD");
  }

  // ================================================================
  //          REST APIS STREAM ĐA PHƯƠNG TIỆN (VIDEO & AUDIO)
  // ================================================================

  void handleStreamVideoUpload(WebServer& server) {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      if (!s_streamJpegUploadBuf) {
        if (psramFound()) s_streamJpegUploadBuf = (uint8_t*)ps_malloc(MAX_STREAM_JPEG_UPLOAD);
        if (!s_streamJpegUploadBuf) s_streamJpegUploadBuf = (uint8_t*)malloc(MAX_STREAM_JPEG_UPLOAD);
      }
      s_streamJpegUploadLen = 0;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (s_streamJpegUploadBuf && s_streamJpegUploadLen + upload.currentSize <= MAX_STREAM_JPEG_UPLOAD) {
        memcpy(s_streamJpegUploadBuf + s_streamJpegUploadLen, upload.buf, upload.currentSize);
        s_streamJpegUploadLen += upload.currentSize;
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (s_streamJpegUploadBuf && s_streamJpegUploadLen > 100) {
        s_logVideoFrameCount++;
        if (s_logVideoFrameCount == 1) {
          Serial.printf("🎬 [STREAM VIDEO] Bắt đầu nhận frame JPEG (%u bytes)\n", (unsigned)s_streamJpegUploadLen);
        } else if (s_logVideoFrameCount % 30 == 0) {
          Serial.printf("🎬 [STREAM VIDEO] Đang hiển thị mượt mà: frame #%u (%u bytes)\n",
                        (unsigned)s_logVideoFrameCount, (unsigned)s_streamJpegUploadLen);
        }
        WebStreamManager::queueVideoFrame(s_streamJpegUploadBuf, s_streamJpegUploadLen);
      }
    }
  }

  void handleStreamVideoFinish(WebServer& server) {
    if (server.hasArg("plain")) {
      const String& body = server.arg("plain");
      if (body.length() > 100) {
        s_logVideoFrameCount++;
        if (s_logVideoFrameCount == 1 || s_logVideoFrameCount % 30 == 0) {
          Serial.printf("🎬 [STREAM VIDEO (RAW)] Frame #%u (%u bytes)\n", (unsigned)s_logVideoFrameCount, (unsigned)body.length());
        }
        WebStreamManager::queueVideoFrame((const uint8_t*)body.c_str(), body.length());
      }
    }
    server.send(200, "text/plain", "1");
  }

  void handleStreamAudioUpload(WebServer& server) {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      s_audioCarryByte = -1;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (upload.currentSize == 0) return;

      const uint8_t* p = upload.buf;
      size_t len = upload.currentSize;
      static int16_t s_alignedBuf[1024];
      size_t samplesInBuf = 0;

      // 1. Ghép byte lẻ nếu có
      if (s_audioCarryByte >= 0 && len > 0) {
        int16_t sample = (int16_t)(((uint16_t)(*p) << 8) | (uint8_t)s_audioCarryByte);
        s_alignedBuf[samplesInBuf++] = sample;
        p++;
        len--;
        s_audioCarryByte = -1;
      }

      // 2. Chuyển đổi các cặp byte 16-bit
      size_t pairs = len / 2;
      for (size_t i = 0; i < pairs; i++) {
        int16_t sample = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        p += 2;
        s_alignedBuf[samplesInBuf++] = sample;
        if (samplesInBuf >= 1024) {
          s_logAudioPcmChunks++;
          if (s_logAudioPcmChunks == 1 || s_logAudioPcmChunks % 50 == 0) {
            Serial.printf("🎵 [STREAM AUDIO] Đang nạp PCM 16kHz vào RingBuffer (Chunk #%u, %u mẫu)\n",
                          (unsigned)s_logAudioPcmChunks, (unsigned)samplesInBuf);
          }
          WebStreamManager::playAudioPcmChunk(s_alignedBuf, samplesInBuf);
          samplesInBuf = 0;
        }
      }

      if (samplesInBuf > 0) {
        s_logAudioPcmChunks++;
        if (s_logAudioPcmChunks == 1 || s_logAudioPcmChunks % 50 == 0) {
          Serial.printf("🎵 [STREAM AUDIO] Đang nạp PCM 16kHz vào RingBuffer (Chunk #%u, %u mẫu)\n",
                        (unsigned)s_logAudioPcmChunks, (unsigned)samplesInBuf);
        }
        WebStreamManager::playAudioPcmChunk(s_alignedBuf, samplesInBuf);
        samplesInBuf = 0;
      }

      // 3. Nếu còn thừa 1 byte lẻ ở cuối gói
      if (len % 2 != 0) {
        s_audioCarryByte = *p;
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      s_audioCarryByte = -1;
    }
  }

  void handleStreamAudioFinish(WebServer& server) {
    server.send(200, "text/plain", "1");
  }

  void handleStreamStartUrl(WebServer& server) {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    String type = doc["type"] | "audio";
    String url = doc["url"] | "";
    bool ok = WebStreamManager::startUrlStream(type, url);
    server.send(ok ? 200 : 500, "application/json", ok ? "{\"status\":\"ok\"}" : "{\"error\":\"Failed to start URL stream\"}");
  }

  void handleStreamStop(WebServer& server) {
    Serial.println("🛑 [STREAM] Đã nhận lệnh dừng toàn bộ luồng stream từ Web / Transcoder!");
    s_logVideoFrameCount = 0;
    s_logAudioPcmChunks = 0;
    WebStreamManager::stopAll();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }

  void handleStreamStatus(WebServer& server) {
    JsonDocument doc;
    WebStreamManager::getStatusJson(doc);
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }

  void handleStreamProxyStart(WebServer& server) {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    String url = doc["url"] | "";
    String pcHost = doc["host"] | "";
    if (pcHost.length() == 0 || pcHost == "localhost" || pcHost == "127.0.0.1") {
      pcHost = server.client().remoteIP().toString();
    }
    int fps = doc["fps"] | 15;
    String resolution = doc["resolution"] | "240x240";
    int volume = doc["volume"] | 100;

    Serial.printf("⚡ [PROXY START] Chuyển tiếp yêu cầu stream tới PC Transcoder: %s:5050 (url: %s)\n",
                  pcHost.c_str(), url.c_str());

    HTTPClient http;
    String targetUrl = "http://" + pcHost + ":5050/api/start";
    http.begin(targetUrl);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(18000);

    JsonDocument postDoc;
    postDoc["url"] = url;
    postDoc["esp_ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "192.168.4.1";
    postDoc["fps"] = fps;
    postDoc["resolution"] = resolution;
    postDoc["volume"] = volume;
    String body;
    serializeJson(postDoc, body);

    int code = http.POST(body);
    if (code == 200) {
      String resp = http.getString();
      http.end();
      server.send(200, "application/json", resp);
    } else {
      http.end();
      Serial.printf("❌ [PROXY START] Kết nối PC Transcoder thất bại: mã %d\n", code);
      server.send(code > 0 ? code : 500, "application/json",
                  "{\"status\":\"error\",\"error\":\"Khong the ket noi toi PC Transcoder (" + targetUrl + ")\"}");
    }
  }

  void handleStreamProxyStop(WebServer& server) {
    String pcHost = "";
    if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      pcHost = doc["host"] | "";
    }
    if (pcHost.length() == 0 || pcHost == "localhost" || pcHost == "127.0.0.1") {
      pcHost = server.client().remoteIP().toString();
    }
    HTTPClient http;
    String targetUrl = "http://" + pcHost + ":5050/api/stop";
    http.begin(targetUrl);
    http.setTimeout(3000);
    http.POST("");
    http.end();

    WebStreamManager::stopAll();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }

  // ================================================================
  //                    ĐĂNG KÝ ROUTE TỔNG HỢP
  // ================================================================

  void registerRoutes(WebServer& server) {
    // 1. Quản lý Ảnh (SD / LittleFS)
    server.on("/api/images", HTTP_GET, [&server]() { handleListImages(server); });
    server.on("/api/sd/file", HTTP_GET, [&server]() { handleSdFileRoute(server); });
    server.on("/api/images/upload", HTTP_POST, [&server]() { finishUploadSync(server); }, [&server]() { handleImageUploadRoute(server); });
    server.on("/api/sd/upload", HTTP_POST, [&server]() { finishUploadSync(server); }, [&server]() { handleSdUploadRoute(server); });
    server.on("/api/images/delete", HTTP_POST, [&server]() { handleDeleteImage(server); });
    server.on("/api/images/select", HTTP_POST, [&server]() { handleSelectImage(server); });
    server.on("/api/littlefs/upload", HTTP_POST, [&server]() { handleLittleFsUpload(server); }, [&server]() { handleLittleFsUploadChunk(server); });

    // 2. Trình Phát Nhạc (/Musics trên Thẻ SD)
    server.on("/api/media/status", HTTP_GET, [&server]() { handleMediaStatus(server); });
    server.on("/api/media/list", HTTP_GET, [&server]() { handleMediaList(server); });
    server.on("/api/media/control", HTTP_POST, [&server]() { handleMediaControl(server); });
    server.on("/api/media/upload", HTTP_POST, [&server]() { handleMediaUploadFinish(server); }, [&server]() { handleMediaUploadChunk(server); });
    server.on("/api/media/delete", HTTP_POST, [&server]() { handleMediaDelete(server); });
    server.on("/api/media/stream", HTTP_GET, [&server]() { handleMediaStream(server); });

    // 3. Web Browser / URL Video & Audio Stream
    server.on("/api/stream/video_frame", HTTP_POST, [&server]() { handleStreamVideoFinish(server); }, [&server]() { handleStreamVideoUpload(server); });
    server.on("/api/stream/audio_pcm", HTTP_POST, [&server]() { handleStreamAudioFinish(server); }, [&server]() { handleStreamAudioUpload(server); });
    server.on("/api/stream/start_url", HTTP_POST, [&server]() { handleStreamStartUrl(server); });
    server.on("/api/stream/stop", HTTP_POST, [&server]() { handleStreamStop(server); });
    server.on("/api/stream/status", HTTP_GET, [&server]() { handleStreamStatus(server); });
    server.on("/api/stream/proxy_start", HTTP_POST, [&server]() { handleStreamProxyStart(server); });
    server.on("/api/stream/proxy_stop", HTTP_POST, [&server]() { handleStreamProxyStop(server); });
  }

} // namespace ApiMedia

