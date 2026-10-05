#include "XiaoZhiClient.h"
#include "Config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "esp_websocket_client.h"

static Preferences sysPrefs;
static String s_lastAuthCode = "";
static esp_websocket_client_handle_t s_wsClient = nullptr;
static bool s_wsConnected = false;
static String s_wsUri = "";
static String s_wsHeaders = "";


String XiaoZhiClient::getOrCreateDeviceUuid() {
  sysPrefs.begin("sys", false);
  String uuid = sysPrefs.getString("uuid", "");
  if (uuid.length() < 32) {
    uint8_t u[16];
    esp_fill_random(u, sizeof(u));
    u[6] = (u[6] & 0x0F) | 0x40; // Version 4
    u[8] = (u[8] & 0x3F) | 0x80; // Variant 1
    char buf[37];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
      u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
    uuid = String(buf);
    sysPrefs.putString("uuid", uuid);
    Serial.printf("✨ Đã tạo UUID thiết bị mới: %s\n", uuid.c_str());
  }
  sysPrefs.end();
  return uuid;
}

String XiaoZhiClient::queryOTA(bool printToSerial) {
  if (WiFi.status() != WL_CONNECTED) {
    if (printToSerial) {
      Serial.println("⚠️ [XIAOZHI OTA] ESP32 chưa kết nối Wi-Fi, không thể kiểm tra OTA.");
    }
    return "";
  }

  String mac = WiFi.macAddress();
  String uuid = getOrCreateDeviceUuid();

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);

  HTTPClient http;
  http.begin(client, XIAOZHI_OTA_URL);
  http.setUserAgent(XIAOZHI_USER_AGENT);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Device-Id", mac);
  http.addHeader("Client-Id", uuid);
  http.addHeader("Activation-Version", "1");

  String reqBody = "{\"version\":2,\"mac_address\":\"" + mac + "\",\"uuid\":\"" + uuid + "\",\"board\":\"bread-compact-wifi\",\"board_type\":\"bread-compact-wifi\",\"flash_size\":16777216,\"psram_size\":8388608,\"chip_model_name\":\"esp32s3\",\"application\":{\"name\":\"xiaozhi\",\"version\":\"2.0.0\"}}";
  int httpCode = http.POST(reqBody);
  String payload = "";

  if (httpCode == HTTP_CODE_OK || httpCode == 200) {
    payload = http.getString();

    JsonDocument resDoc;
    DeserializationError err = deserializeJson(resDoc, payload);
    if (!err) {
      // 1. Kiểm tra mã kích hoạt
      bool isPendingActivation = false;
      const char* code = nullptr;
      const char* msg = "";
      if (resDoc["activation"]["code"].is<const char*>()) {
        code = resDoc["activation"]["code"];
        msg = resDoc["activation"]["message"] | "";
        s_lastAuthCode = String(code);
        isPendingActivation = true;
      } else {
        s_lastAuthCode = "";
      }

      // 2. Cập nhật cấu hình vào LittleFS
      JsonDocument cfgDoc;
      if (LittleFS.exists(FILE_CONFIG)) {
        File f = LittleFS.open(FILE_CONFIG, "r");
        deserializeJson(cfgDoc, f);
        f.close();
      }

      if (resDoc["websocket"]["url"].is<const char*>()) {
        cfgDoc["endpoint"] = resDoc["websocket"]["url"].as<const char*>();
      }
      cfgDoc["mac"] = mac;
      cfgDoc["uuid"] = uuid;

      if (isPendingActivation) {
        cfgDoc["bound"] = false;
        cfgDoc["token"] = "";
      } else {
        cfgDoc["bound"] = true;
        const char* token = resDoc["websocket"]["token"] | "test-token";
        cfgDoc["token"] = token;
      }

      File f = LittleFS.open(FILE_CONFIG, "w");
      if (f) {
        serializeJson(cfgDoc, f);
        f.close();
      }

      // Nếu thiết bị đã liên kết và chưa kết nối WebSocket, tự động khởi chạy WebSocket nền
      if (!isPendingActivation && s_wsClient == nullptr) {
        startWebSocket();
      }

      if (printToSerial) {
        Serial.println("\n==================================================================");
        Serial.println("🚀 [XIAOZHI OTA] PHẢN HỒI TỪ MÁY CHỦ XIAOZHI (HTTP 200 OK)");
        Serial.println("==================================================================");
        Serial.printf("📡 Device Hardware MAC : %s\n", mac.c_str());
        Serial.printf("🆔 Device Client UUID  : %s\n", uuid.c_str());
        Serial.println("------------------------------------------------------------------");
        if (isPendingActivation) {
          Serial.println("🔑 TRẠNG THÁI: CHƯA LIÊN KẾT (CẦN THÊM THIẾT BỊ TRÊN XIAOZHI.ME)");
          Serial.printf("👉 MÃ XÁC THỰC (AUTH CODE) : >>>  %s  <<<\n", code);
          if (strlen(msg) > 0) {
            Serial.printf("💬 Hướng dẫn từ XiaoZhi     : %s\n", msg);
          }
          Serial.printf("🌐 Hãy mở https://xiaozhi.me -> Thêm thiết bị -> Nhập MAC [%s] và Mã [%s]\n", mac.c_str(), code);
        } else {
          Serial.println("🎉 TRẠNG THÁI: THIẾT BỊ ĐÃ LIÊN KẾT THÀNH CÔNG VỚI TÀI KHOẢN XIAOZHI!");
          Serial.printf("🔑 Token                     : %s\n", (const char*)(resDoc["websocket"]["token"] | "test-token"));
          Serial.println("💾 Đã tự động cập nhật cờ ĐÃ LIÊN KẾT (bound=true) vào /config.json!");
        }
        if (resDoc["websocket"]["url"].is<const char*>()) {
          Serial.printf("🌐 WebSocket Server Endpoint : %s\n", resDoc["websocket"]["url"].as<const char*>());
        }
        if (resDoc["mqtt"]["endpoint"].is<const char*>()) {
          Serial.printf("⚡ MQTT Broker               : %s\n", resDoc["mqtt"]["endpoint"].as<const char*>());
          Serial.printf("📦 MQTT Client ID            : %s\n", resDoc["mqtt"]["client_id"].as<const char*>());
        }
        if (resDoc["server_time"]["timestamp"].is<long long>()) {
          Serial.printf("⏰ Server Timestamp          : %lld (Timezone Offset: %d phút)\n",
                        resDoc["server_time"]["timestamp"].as<long long>(),
                        resDoc["server_time"]["timezone_offset"].as<int>());
        }
        Serial.println("------------------------------------------------------------------");
        Serial.println("📄 [NỘI DUNG RAW JSON TỪ XIAOZHI]:");
        Serial.println(payload);
        Serial.println("==================================================================\n");
      }
    }
  } else {
    String errPayload = http.getString();
    if (printToSerial) {
      Serial.printf("❌ [XIAOZHI OTA] Lỗi gọi API (HTTP %d): %s\n", httpCode, errPayload.c_str());
    }
  }

  http.end();
  return payload;
}

void XiaoZhiClient::getAIConfig(JsonDocument& doc) {
  if (LittleFS.exists(FILE_CONFIG)) {
    File file = LittleFS.open(FILE_CONFIG, "r");
    deserializeJson(doc, file);
    file.close();
  }

  if (!doc["endpoint"].is<const char*>() || strlen(doc["endpoint"]) == 0 || String(doc["endpoint"].as<const char*>()).indexOf("api.xiaozhi.me") != -1) {
    doc["endpoint"] = XIAOZHI_DEFAULT_ENDPOINT;
  }
  if (!doc["token"].is<const char*>()) {
    doc["token"] = "";
  }
  String hwMac = WiFi.macAddress();
  doc["hw_mac"] = hwMac;
  if (!doc["mac"].is<const char*>() || strlen(doc["mac"]) == 0) {
    doc["mac"] = hwMac;
  }
  doc["uuid"] = getOrCreateDeviceUuid();
}

bool XiaoZhiClient::saveAIConfig(const String& body) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) return false;

  File file = LittleFS.open(FILE_CONFIG, "w");
  if (!file) return false;
  file.print(body);
  file.close();

  Serial.println("✅ Đã cập nhật cấu hình XiaoZhi AI vào LittleFS!");
  return true;
}

String XiaoZhiClient::getLastAuthCode() {
  return s_lastAuthCode;
}

bool XiaoZhiClient::isDeviceBound() {
  if (s_lastAuthCode.length() > 0) return false;
  if (!LittleFS.exists(FILE_CONFIG)) return false;
  File f = LittleFS.open(FILE_CONFIG, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  if (doc["bound"].is<bool>()) {
    return doc["bound"].as<bool>();
  }
  if (doc["token"].is<const char*>()) {
    const char* t = doc["token"];
    return (t && strlen(t) > 0);
  }
  return false;
}

String XiaoZhiClient::getStoredToken() {
  if (!LittleFS.exists(FILE_CONFIG)) return "";
  File f = LittleFS.open(FILE_CONFIG, "r");
  if (!f) return "";
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return "";
  if (!doc["token"].is<const char*>()) return "";
  return String(doc["token"].as<const char*>());
}

String XiaoZhiClient::getStoredMac() {
  if (LittleFS.exists(FILE_CONFIG)) {
    File f = LittleFS.open(FILE_CONFIG, "r");
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        f.close();
        if (doc["mac"].is<const char*>() && strlen(doc["mac"]) > 0) {
          return String(doc["mac"].as<const char*>());
        }
      } else {
        f.close();
      }
    }
  }
  return WiFi.macAddress();
}

String XiaoZhiClient::getStoredEndpoint() {
  if (LittleFS.exists(FILE_CONFIG)) {
    File f = LittleFS.open(FILE_CONFIG, "r");
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        f.close();
        if (doc["endpoint"].is<const char*>() && strlen(doc["endpoint"]) > 0) {
          return String(doc["endpoint"].as<const char*>());
        }
      } else {
        f.close();
      }
    }
  }
  return String(XIAOZHI_DEFAULT_ENDPOINT);
}

void XiaoZhiClient::unbindDevice() {
  JsonDocument doc;
  if (LittleFS.exists(FILE_CONFIG)) {
    File f = LittleFS.open(FILE_CONFIG, "r");
    deserializeJson(doc, f);
    f.close();
  }
  doc["bound"] = false;
  doc["token"] = "";
  doc["mac"] = "";
  File f = LittleFS.open(FILE_CONFIG, "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
    Serial.println("🗑️ [XIAOZHI] Đã xóa Token và cờ liên kết khỏi /config.json!");
  }

  // Xóa UUID cũ để máy chủ XiaoZhi OTA nhận diện đây là yêu cầu cấp mã OTP mới
  sysPrefs.begin("sys", false);
  sysPrefs.remove("uuid");
  sysPrefs.end();
  Serial.println("✨ [XIAOZHI] Đã làm mới UUID thiết bị.");

  s_lastAuthCode = "";
}

String XiaoZhiClient::askXiaoZhiAI(const String& userText, String& outEmotion, float tempC, float humPct) {
  outEmotion = "happy";
  Serial.println("\n==================================================================");
  Serial.printf("🎙️ [XIAOZHI AI] ESP32 gửi nội dung lên Trợ lý: \"%s\"\n", userText.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    // Nếu chưa kiểm tra OTA / Token lần nào, thử đồng bộ nhanh với XiaoZhi OTA Server
    if (s_lastAuthCode.length() == 0 && !LittleFS.exists(FILE_CONFIG)) {
      queryOTA(false);
    }

    // Gửi truy vấn AI trực tuyến (Tương thích OpenAI JSON API - trả về tiếng Việt không dấu chuẩn ST7789)
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8000);

    HTTPClient http;
    if (http.begin(client, "https://text.pollinations.ai/openai")) {
      http.addHeader("Content-Type", "application/json");

      JsonDocument reqDoc;
      reqDoc["model"] = "openai";
      JsonArray messages = reqDoc["messages"].to<JsonArray>();

      JsonObject sysMsg = messages.add<JsonObject>();
      sysMsg["role"] = "system";
      String sysPrompt =
        "Ban la XiaoZhi, tro ly ao AI de thuong tren Tram Decor Vu Tru (ESP32-S3). "
        "QUY TAC BAT BUOC: "
        "1. Chi tra loi bang TIENG VIET KHONG DAU (ASCII) de hien thi dep tren man hinh TFT 240x320. "
        "2. Bat dau cau tra loi bang 1 the bieu cam trong ngoac vuong: [HAPPY], [WINK], [CONFUSED], [COOL], hoac [SLEEPY]. "
        "3. Tra loi tu nhien, thong minh, vui ve, do dai 2-4 cau (khoang 35-65 tu). "
        "Thong so cam bien SHT31 hien tai cua phong: Nhiet do " + String(tempC, 1) + "C, Do am " + String(humPct, 0) + "%.";
      sysMsg["content"] = sysPrompt;

      JsonObject usrMsg = messages.add<JsonObject>();
      usrMsg["role"] = "user";
      usrMsg["content"] = userText;

      String reqBody;
      serializeJson(reqDoc, reqBody);

      int code = http.POST(reqBody);
      if (code == HTTP_CODE_OK || code == 200) {
        String respStr = http.getString();
        http.end();

        JsonDocument respDoc;
        if (!deserializeJson(respDoc, respStr)) {
          const char* content = respDoc["choices"][0]["message"]["content"];
          if (content && strlen(content) > 0) {
            String reply = String(content);
            reply.trim();
            if (reply.indexOf("[WINK]") >= 0) { outEmotion = "wink-left"; reply.replace("[WINK]", ""); }
            else if (reply.indexOf("[CONFUSED]") >= 0) { outEmotion = "confused"; reply.replace("[CONFUSED]", ""); }
            else if (reply.indexOf("[COOL]") >= 0) { outEmotion = "right"; reply.replace("[COOL]", ""); }
            else if (reply.indexOf("[SLEEPY]") >= 0) { outEmotion = "sleepy"; reply.replace("[SLEEPY]", ""); }
            else if (reply.indexOf("[HAPPY]") >= 0) { outEmotion = "happy"; reply.replace("[HAPPY]", ""); }
            reply.trim();
            Serial.printf("🤖 [XIAOZHI CLOUD REPLY] (%s): %s\n", outEmotion.c_str(), reply.c_str());
            Serial.println("==================================================================\n");
            return reply;
          }
        }
      } else {
        http.end();
      }
    }
  }

  // Phản hồi thông minh dự phòng ngay trên ESP32-S3 (kèm thông số cảm biến thực tế & mã kích hoạt XiaoZhi.me)
  String q = userText;
  q.toLowerCase();
  String fallback;
  if (q.indexOf("nhiet do") >= 0 || q.indexOf("thoi tiet") >= 0 || q.indexOf("nong") >= 0) {
    outEmotion = "happy";
    fallback = "Cam bien SHT31 tren Tram Decor dang do duoc nhiet do phong la " + String(tempC, 1) +
               " do C va do am la " + String(humPct, 0) + "%. Khong khi trong phong rat de chiu, ban nho uong du nuoc nhe!";
  } else if (q.indexOf("gio") >= 0 || q.indexOf("hom nay") >= 0) {
    outEmotion = "wink-left";
    struct tm ti;
    if (getLocalTime(&ti, 10)) {
      char tbuf[32];
      snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d ngay %02d/%02d", ti.tm_hour, ti.tm_min, ti.tm_sec, ti.tm_mday, ti.tm_mon + 1);
      fallback = String("Bay gio la ") + tbuf + " (Gio chuan NTP GMT+7). Chuc ban mot ngay lam viec tran day nang luong cung Tram Decor Vu Tru!";
    } else {
      fallback = "Dong ho he thong dang hoat dong tren ESP32-S3. Ban co the xem gio lon ngay tren Man Hinh Cho Cyberpunk nhe!";
    }
  } else if (q.indexOf("chuyen") >= 0 || q.indexOf("vui") >= 0 || q.indexOf("vu tru") >= 0) {
    outEmotion = "wink-left";
    fallback = "Ngay xua co mot phi hanh gia mang theo ESP32-S3 len sao Hoa. Khi hoi AI tren tram rang sao Hoa co gi vui, XiaoZhi dap: Co wifi mien phi nhung ping hoi cao 15 phut anh sang!";
  } else if (q.indexOf("tinh nang") >= 0 || q.indexOf("tram") >= 0) {
    outEmotion = "happy";
    fallback = "Tram Decor Vu Tru chay chip ESP32-S3 N16R8 voi 9 ung dung Symbian S40: Man hinh cho NTP, Bieu cam AI, PC HUD, Quan ly Bo nho SD, Do dien ap DIO va Tro ly XiaoZhi!";
  } else {
    outEmotion = "happy";
    fallback = "Xin chao! Minh la tro ly ao XiaoZhi tren Tram Decor Vu Tru ESP32-S3. Hien tai he thong dang chay che do hoi thoai Text thong minh (khong can DAC), san sang tro chuyen cung ban!";
    if (s_lastAuthCode.length() > 0) {
      fallback += " (Ma kich hoat xiaozhi.me: " + s_lastAuthCode + ")";
    }
  }
  Serial.printf("🤖 [XIAOZHI LOCAL REPLY] (%s): %s\n", outEmotion.c_str(), fallback.c_str());
  Serial.println("==================================================================\n");
  return fallback;
}

static String stripVietnameseToAscii(const String& s) {
  String r = s;
  const char* from[] = {
    "á","à","ả","ã","ạ","ă","ắ","ằ","ẳ","ẵ","ặ","â","ấ","ầ","ẩ","ẫ","ậ",
    "Á","À","Ả","Ã","Ạ","Ă","Ắ","Ằ","Ẳ","Ẵ","Ặ","Â","Ấ","Ầ","Ẩ","Ẫ","Ậ",
    "đ","Đ",
    "é","è","ẻ","ẽ","ẹ","ê","ế","ề","ể","ễ","ệ",
    "É","È","Ẻ","Ẽ","Ẹ","Ê","Ế","Ề","Ể","Ễ","Ệ",
    "í","ì","ỉ","ĩ","ị","Í","Ì","Ỉ","Ĩ","Ị",
    "ó","ò","ỏ","õ","ọ","ô","ố","ồ","ổ","ỗ","ộ","ơ","ớ","ờ","ở","ỡ","ợ",
    "Ó","Ò","Ỏ","Õ","Ọ","Ô","Ố","Ồ","Ổ","Ỗ","Ộ","Ơ","Ớ","Ờ","Ở","Ỡ","Ợ",
    "ú","ù","ủ","ũ","ụ","ư","ứ","ừ","ử","ữ","ự",
    "Ú","Ù","Ủ","Ũ","Ụ","Ư","Ứ","Ừ","Ử","Ữ","Ự",
    "ý","ỳ","ỷ","ỹ","ỵ","Ý","Ỳ","Ỷ","Ỹ","Ỵ"
  };
  const char* to[] = {
    "a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a","a",
    "A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A",
    "d","D",
    "e","e","e","e","e","e","e","e","e","e","e",
    "E","E","E","E","E","E","E","E","E","E","E",
    "i","i","i","i","i","I","I","I","I","I",
    "o","o","o","o","o","o","o","o","o","o","o","o","o","o","o","o","o",
    "O","O","O","O","O","O","O","O","O","O","O","O","O","O","O","O","O",
    "u","u","u","u","u","u","u","u","u","u","u",
    "U","U","U","U","U","U","U","U","U","U","U",
    "y","y","y","y","y","Y","Y","Y","Y","Y"
  };
  const size_t n = sizeof(from) / sizeof(from[0]);
  for (size_t i = 0; i < n; i++) {
    r.replace(from[i], to[i]);
  }
  return r;
}

String XiaoZhiClient::transcribeMicAudioPcm16(const int16_t* pcmSamples, size_t sampleCount, uint32_t sampleRate, String& outUtf8Transcript) {
  outUtf8Transcript = "";
  if (!pcmSamples || sampleCount < 800) {
    return "";
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ [STT] Chưa kết nối WiFi, không thể gửi âm thanh nhận diện giọng nói.");
    return "";
  }

  // Tính mức năng lượng âm thanh (RMS / Peak) của đoạn thu
  uint32_t peak = 0;
  uint64_t sumAbs = 0;
  for (size_t i = 0; i < sampleCount; i++) {
    int32_t v = pcmSamples[i];
    uint32_t av = (uint32_t)(v < 0 ? -v : v);
    if (av > peak) peak = av;
    sumAbs += av;
  }
  uint32_t avgAbs = (uint32_t)(sumAbs / sampleCount);
  Serial.printf("🎙️ [STT] Bắt đầu xử lý %u mẫu PCM (%u byte @ %u Hz) | Peak=%u, Avg=%u\n",
                (unsigned)sampleCount, (unsigned)(sampleCount * 2), (unsigned)sampleRate,
                (unsigned)peak, (unsigned)avgAbs);

  // Đọc cấu hình token STT từ /config.json (Hỗ trợ Wit.ai và Google Speech API)
  String witToken = "";
  String googleKey = "";
  if (LittleFS.exists(FILE_CONFIG)) {
    File f = LittleFS.open(FILE_CONFIG, "r");
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        if (doc["wit_token"].is<const char*>()) {
          witToken = doc["wit_token"].as<String>();
        } else if (doc["stt_key"].is<const char*>()) {
          witToken = doc["stt_key"].as<String>();
        }
        if (doc["google_stt_key"].is<const char*>()) {
          googleKey = doc["google_stt_key"].as<String>();
        }
      }
      f.close();
    }
  }

  // 1. Ưu tiên Wit.ai Speech API nếu có token (Hỗ trợ tiếng Việt xuất sắc, miễn phí, nhận diện trực tiếp PCM 16kHz Little-Endian)
  if (witToken.length() > 0) {
    Serial.println("🎙️ [STT] Đang gửi PCM lên Wit.ai Speech-to-Text API...");
    WiFiClientSecure witClient;
    witClient.setInsecure();
    witClient.setTimeout(8000);
    HTTPClient httpWit;
    if (httpWit.begin(witClient, "https://api.wit.ai/speech?v=20230215")) {
      httpWit.addHeader("Authorization", "Bearer " + witToken);
      httpWit.addHeader("Content-Type", "audio/raw; encoding=signed-integer; bits=16; rate=" + String(sampleRate) + "; endian=little");
      int code = httpWit.POST((uint8_t*)pcmSamples, sampleCount * sizeof(int16_t));
      if (code == 200) {
        String resp = httpWit.getString();
        httpWit.end();
        Serial.printf("✅ [STT WIT.AI]: %s\n", resp.c_str());
        JsonDocument witDoc;
        if (!deserializeJson(witDoc, resp)) {
          const char* txt = witDoc["text"] | "";
          if (strlen(txt) > 0) {
            outUtf8Transcript = String(txt);
            outUtf8Transcript.trim();
            String asciiText = stripVietnameseToAscii(outUtf8Transcript);
            Serial.printf("✅ [STT THÀNH CÔNG] UTF-8: \"%s\" -> ASCII: \"%s\"\n",
                          outUtf8Transcript.c_str(), asciiText.c_str());
            return asciiText;
          }
        }
      } else {
        Serial.printf("⚠️ [STT WIT.AI] HTTP %d: %s\n", code, httpWit.getString().c_str());
        httpWit.end();
      }
    }
  }

  // 2. Google Speech API v2 nếu có custom key
  if (googleKey.length() > 0) {
    String sttUrl = "http://www.google.com/speech-api/v2/recognize?output=json&lang=vi-VN&key=" + googleKey;
    HTTPClient http;
    http.begin(sttUrl);
    http.setTimeout(7500);
    String contentType = "audio/l16; rate=" + String(sampleRate) + "; channels=1";
    http.addHeader("Content-Type", contentType);

    int code = http.POST((uint8_t*)pcmSamples, sampleCount * sizeof(int16_t));
    String resp = (code > 0) ? http.getString() : "";
    http.end();

    Serial.printf("🎙️ [STT] HTTP Code=%d\n", code);
    if (code == 200 && resp.length() > 0) {
      int idx = resp.indexOf("\"transcript\":\"");
      if (idx >= 0) {
        idx += 14;
        int endIdx = resp.indexOf("\"", idx);
        if (endIdx > idx) {
          outUtf8Transcript = resp.substring(idx, endIdx);
          outUtf8Transcript.trim();
          String asciiText = stripVietnameseToAscii(outUtf8Transcript);
          Serial.printf("✅ [STT THÀNH CÔNG] UTF-8: \"%s\" -> ASCII: \"%s\"\n",
                        outUtf8Transcript.c_str(), asciiText.c_str());
          return asciiText;
        }
      }
    }
  } else if (witToken.length() == 0) {
    Serial.println("ℹ️ [STT] Khóa Google STT mặc định đã hết hạn (Google trả về 403 Forbidden).");
    Serial.println("💡 Để dùng STT từ Mic INMP441, hãy thêm \"wit_token\" (Wit.ai miễn phí) hoặc \"google_stt_key\" vào /config.json");
  }

  return "";
}

// -------------------------------------------------------------
// Triển khai WebSocket Client nền kết nối XiaoZhi Cloud
// -------------------------------------------------------------

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED: {
      s_wsConnected = true;
      Serial.println("\n=======================================================");
      Serial.println("🟢 [XIAOZHI WS] ĐÃ KẾT NỐI THÀNH CÔNG VỚI XIAOZHI CLOUD!");
      Serial.println("🌐 [XIAOZHI HUB] Thiết bị hiện đang ONLINE trên Hub (xiaozhi.me)!");
      Serial.println("=======================================================");
      // Gửi bản tin hello theo chuẩn giao thức XiaoZhi Cloud v2
      const char* hello = "{\"type\":\"hello\",\"version\":2,\"transport\":\"websocket\",\"audio_params\":{\"format\":\"pcm\",\"sample_rate\":16000,\"channels\":1}}";
      esp_websocket_client_send_text(s_wsClient, hello, strlen(hello), portMAX_DELAY);
      break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
      s_wsConnected = false;
      Serial.println("🟡 [XIAOZHI WS] Mất kết nối WebSocket tới XiaoZhi Cloud. Đang tự động kết nối lại...");
      break;
    case WEBSOCKET_EVENT_DATA:
      if (data && data->data_len > 0) {
        if (data->op_code == 0x01) { // Text frame
          String msg = String(data->data_ptr).substring(0, data->data_len);
          Serial.printf("📩 [XIAOZHI WS RCV]: %s\n", msg.c_str());
        }
      }
      break;
    case WEBSOCKET_EVENT_ERROR:
      Serial.println("⚠️ [XIAOZHI WS] Sự kiện lỗi WebSocket.");
      break;
    default:
      break;
  }
}

void XiaoZhiClient::startWebSocket() {
  if (s_wsClient != nullptr) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!isDeviceBound()) {
    return;
  }

  String endpoint = getStoredEndpoint();
  if (endpoint.length() == 0 || !endpoint.startsWith("ws")) {
    endpoint = XIAOZHI_DEFAULT_ENDPOINT;
  }
  String token = getStoredToken();
  if (token.length() == 0) {
    token = "test-token";
  }
  String mac = getStoredMac();
  String uuid = getOrCreateDeviceUuid();

  s_wsUri = endpoint;
  s_wsHeaders = "Authorization: Bearer " + token + "\r\n"
              + "Device-Id: " + mac + "\r\n"
              + "Client-Id: " + uuid + "\r\n"
              + "Protocol-Version: 2\r\n";

  // DigiCert Global Root G2 CA PEM (xác thực chứng chỉ SSL api.tenclass.net)
  static const char XIAOZHI_CA_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
    "MrY=\n"
    "-----END CERTIFICATE-----\n";

  esp_websocket_client_config_t ws_cfg = {};
  ws_cfg.uri = s_wsUri.c_str();
  ws_cfg.headers = s_wsHeaders.c_str();
  ws_cfg.user_agent = XIAOZHI_USER_AGENT;
  ws_cfg.cert_pem = XIAOZHI_CA_PEM;
  ws_cfg.skip_cert_common_name_check = true;
  ws_cfg.ping_interval_sec = 10;
  ws_cfg.pingpong_timeout_sec = 10;
  ws_cfg.task_stack = 6144;
  ws_cfg.buffer_size = 2048;

  Serial.println("\n🚀 [XIAOZHI WS] Đang mở kết nối nền WebSocket tới XiaoZhi Cloud...");
  Serial.printf("🌐 Endpoint : %s\n", s_wsUri.c_str());
  Serial.printf("📡 Device-Id: %s\n", mac.c_str());

  s_wsClient = esp_websocket_client_init(&ws_cfg);
  if (!s_wsClient) {
    Serial.println("❌ [XIAOZHI WS] Không thể khởi tạo esp_websocket_client!");
    return;
  }

  esp_websocket_register_events(s_wsClient, WEBSOCKET_EVENT_ANY, ws_event_handler, (void*)s_wsClient);

  esp_err_t err = esp_websocket_client_start(s_wsClient);
  if (err != ESP_OK) {
    Serial.printf("❌ [XIAOZHI WS] Lỗi khởi động WebSocket (0x%x)\n", err);
    esp_websocket_client_destroy(s_wsClient);
    s_wsClient = nullptr;
    s_wsConnected = false;
  }
}

void XiaoZhiClient::stopWebSocket() {
  if (s_wsClient != nullptr) {
    Serial.println("⏹️ [XIAOZHI WS] Đang đóng kết nối WebSocket...");
    esp_websocket_client_stop(s_wsClient);
    esp_websocket_client_destroy(s_wsClient);
    s_wsClient = nullptr;
    s_wsConnected = false;
  }
}

bool XiaoZhiClient::isWebSocketConnected() {
  return (s_wsClient != nullptr && s_wsConnected);
}

void XiaoZhiClient::loopWebSocket() {
  static uint32_t s_lastWsCheck = 0;
  uint32_t now = millis();
  if (now - s_lastWsCheck < 3000) return;
  s_lastWsCheck = now;

  if (WiFi.status() == WL_CONNECTED && isDeviceBound()) {
    if (!s_wsClient) {
      startWebSocket();
    }
  } else {
    if (s_wsClient) {
      stopWebSocket();
    }
  }
}




