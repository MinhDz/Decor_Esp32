#pragma once
#include <Arduino.h>
#include <WebServer.h>

namespace ApiMedia {
  // Đăng ký toàn bộ REST APIs Quản lý ảnh (SD/LittleFS), Trình phát nhạc (/Musics) & Streaming Video/Audio
  void registerRoutes(WebServer& server);

  // Quản lý bộ đệm streaming video để giải phóng RAM cho các tác vụ ưu tiên khác
  bool hasAllocatedUploadBuffer();
  size_t trimMemory();

  // Handlers ảnh & lưu trữ
  void handleListImages(WebServer& server);
  void handleImageUploadRoute(WebServer& server);
  void handleSdUploadRoute(WebServer& server);
  void handleSdFileRoute(WebServer& server);
  void handleDeleteImage(WebServer& server);
  void handleSelectImage(WebServer& server);
  void handleLittleFsUpload(WebServer& server);
  void handleLittleFsUploadChunk(WebServer& server);
  void finishUploadSync(WebServer& server);

  // Handlers Media Player (/Musics)
  void handleMediaStatus(WebServer& server);
  void handleMediaList(WebServer& server);
  void handleMediaControl(WebServer& server);
  void handleMediaUploadChunk(WebServer& server);
  void handleMediaUploadFinish(WebServer& server);
  void handleMediaDelete(WebServer& server);
  void handleMediaStream(WebServer& server);

  // Handlers Web Stream (Video JPEG / Audio PCM 16kHz)
  void handleStreamVideoUpload(WebServer& server);
  void handleStreamVideoFinish(WebServer& server);
  void handleStreamAudioUpload(WebServer& server);
  void handleStreamAudioFinish(WebServer& server);
  void handleStreamStartUrl(WebServer& server);
  void handleStreamStop(WebServer& server);
  void handleStreamStatus(WebServer& server);
}

