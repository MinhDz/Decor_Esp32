#include "XiaoZhiClient.h"
#include "Config.h"
#include "tests/TestAudio.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "esp_websocket_client.h"
#include <opus.h>

static Preferences sysPrefs;
static String s_lastAuthCode = "";
static esp_websocket_client_handle_t s_wsClient = nullptr;
static bool s_wsConnected = false;
static uint32_t s_lastWsDisconnectMs = 0;
static uint32_t s_wsReconnectBackoffMs = 15000;
static volatile bool s_isRestartingWs = false;
static String s_wsUri = "";
static String s_wsHeaders = "";
static String s_sessionId = "";
static String s_lastSttText = "";
static String s_lastLlmText = "";
static String s_lastTtsSentence = "";
static String s_lastAiFullReply = "";
static String s_lastEmotion = "happy";
static volatile bool s_sttReceived = false;
static volatile bool s_llmReceived = false;
static volatile bool s_ttsReceived = false;
static volatile bool s_ttsFinished = false;
static volatile uint32_t s_lastAiReplyTime = 0;

static OpusDecoder* s_opusDecoder = nullptr;
static int16_t s_pcmDecodeBuf[1920]; // Tối đa 120ms âm thanh @ 16000Hz Mono

static void ensureOpusDecoder() {
  if (s_opusDecoder == nullptr) {
    int err = OPUS_OK;
    s_opusDecoder = opus_decoder_create(16000, 1, &err);
    if (err == OPUS_OK && s_opusDecoder != nullptr) {
      Serial.println("✅ [OPUS] Đã khởi tạo Opus Decoder 16000Hz Mono cho XiaoZhi Cloud TTS!");
    } else {
      Serial.printf("❌ [OPUS] Lỗi khởi tạo Opus Decoder (err=%d)\n", err);
    }
  }
}


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
  mac.toLowerCase();
  String uuid = getOrCreateDeviceUuid();

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2500);

  HTTPClient http;
  http.setTimeout(2500);
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
          String m = String(doc["mac"].as<const char*>());
          m.toLowerCase();
          return m;
        }
      } else {
        f.close();
      }
    }
  }
  String m = WiFi.macAddress();
  m.toLowerCase();
  return m;
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
  stopWebSocket();
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
    Serial.println("🗑️ [XIAOZHI] Đã ngắt kết nối WebSocket và xóa Token, cờ liên kết khỏi /config.json!");
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

String XiaoZhiClient::stripVietnameseToAscii(const String& s) {
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

  // Lọc sạch toàn bộ byte ngoài bảng ASCII chuẩn (32..126, '\n') để tránh lỗi font màn hình ST7789
  String clean = "";
  clean.reserve(r.length());
  for (size_t i = 0; i < r.length(); i++) {
    uint8_t c = (uint8_t)r[i];
    if (c == '\n' || (c >= 32 && c <= 126)) {
      clean += (char)c;
    }
  }
  return clean;
}

String XiaoZhiClient::detectEmotionFromText(const String& text) {
  if (text.length() == 0) return "";

  // 1. Kiểm tra các mã Emoji UTF-8 phổ biến trả về từ mô hình LLM XiaoZhi:
  if (text.indexOf("😊") >= 0 || text.indexOf("😄") >= 0 || text.indexOf("😃") >= 0 ||
      text.indexOf("😀") >= 0 || text.indexOf("😆") >= 0 || text.indexOf("🥰") >= 0 ||
      text.indexOf("🤗") >= 0 || text.indexOf("❤️") >= 0 || text.indexOf("💖") >= 0) {
    return "happy";
  }
  if (text.indexOf("😂") >= 0 || text.indexOf("🤣") >= 0 || text.indexOf("😹") >= 0) {
    return "laughing";
  }
  if (text.indexOf("😉") >= 0 || text.indexOf("😜") >= 0 || text.indexOf("😝") >= 0 || text.indexOf("😋") >= 0) {
    return "winking";
  }
  if (text.indexOf("😎") >= 0 || text.indexOf("😏") >= 0) {
    return "cool";
  }
  if (text.indexOf("🤔") >= 0 || text.indexOf("🧐") >= 0 || text.indexOf("🤨") >= 0) {
    return "thinking";
  }
  if (text.indexOf("😮") >= 0 || text.indexOf("😲") >= 0 || text.indexOf("😱") >= 0 || text.indexOf("😳") >= 0) {
    return "confused";
  }
  if (text.indexOf("😴") >= 0 || text.indexOf("🥱") >= 0 || text.indexOf("💤") >= 0) {
    return "sleepy";
  }
  if (text.indexOf("😢") >= 0 || text.indexOf("😭") >= 0 || text.indexOf("🥺") >= 0 || text.indexOf("😞") >= 0) {
    return "sad";
  }
  if (text.indexOf("😡") >= 0 || text.indexOf("😠") >= 0 || text.indexOf("🤬") >= 0) {
    return "angry";
  }

  // 2. Kiểm tra các thẻ cảm xúc dạng văn bản [EMOTION]
  if (text.indexOf("[HAPPY]") >= 0) return "happy";
  if (text.indexOf("[WINK]") >= 0) return "winking";
  if (text.indexOf("[COOL]") >= 0) return "cool";
  if (text.indexOf("[CONFUSED]") >= 0) return "confused";
  if (text.indexOf("[SLEEPY]") >= 0) return "sleepy";
  if (text.indexOf("[ANGRY]") >= 0) return "angry";
  if (text.indexOf("[THINKING]") >= 0) return "thinking";

  return "";
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

  // Đọc cấu hình token STT từ /config.json (Chỉ dùng Wit.ai nếu có, cô lập hoàn toàn Google STT)
  String witToken = "";
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
      }
      f.close();
    }
  }

  // 1. Dùng Wit.ai Speech API nếu có cấu hình wit_token
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

  // 2. Google Speech API v2 (ĐÃ CÔ LẬP / COMMENT THEO YÊU CẦU NGƯỜI DÙNG: Loại bỏ Google STT)
  /*
  if (googleKey.length() > 0) {
    String sttUrl = "http://www.google.com/speech-api/v2/recognize?output=json&lang=vi-VN&key=" + googleKey;
    HTTPClient http;
    http.begin(sttUrl);
    ...
  }
  */

  Serial.println("ℹ️ [STT] Google STT đã được cô lập. Âm thanh sẽ được truyền trực tiếp qua XiaoZhi Hub WebSocket!");
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
      s_lastWsDisconnectMs = 0;
      s_wsReconnectBackoffMs = 15000;
      ensureOpusDecoder();
      if (s_opusDecoder) {
        opus_decoder_ctl(s_opusDecoder, OPUS_RESET_STATE);
      }
      Serial.println("\n=======================================================");
      Serial.println("🟢 [XIAOZHI WS] ĐÃ KẾT NỐI THÀNH CÔNG VỚI XIAOZHI CLOUD!");
      Serial.println("🌐 [XIAOZHI HUB] Thiết bị hiện đang ONLINE trên Hub (xiaozhi.me)!");
      Serial.println("=======================================================");
      // Gửi bản tin hello theo chuẩn giao thức XiaoZhi Cloud Protocol v1 (Hỗ trợ Audio format PCM/Opus)
      const char* hello = "{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\",\"audio_params\":{\"format\":\"pcm\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}";
      esp_websocket_client_send_text(s_wsClient, hello, strlen(hello), pdMS_TO_TICKS(1500));
      break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
      s_wsConnected = false;
      if (s_lastWsDisconnectMs == 0) s_lastWsDisconnectMs = millis();
      Serial.println("🟡 [XIAOZHI WS] Mất kết nối WebSocket tới XiaoZhi Cloud.");
      break;
    case WEBSOCKET_EVENT_DATA:
      if (data && data->data_len > 0) {
        if (data->op_code == 0x01) { // Text frame JSON
          String msg = String(data->data_ptr).substring(0, data->data_len);
          Serial.printf("📩 [XIAOZHI WS RCV]: %s\n", msg.c_str());

          JsonDocument doc;
          DeserializationError err = deserializeJson(doc, msg);
          if (!err) {
            // 1. Trích xuất thuộc tính emotion / emoji tổng quát từ MỌI frame JSON
            if (doc["emotion"].is<const char*>()) {
              String em = doc["emotion"].as<String>();
              em.toLowerCase();
              em.trim();
              if (em.length() > 0) {
                s_lastEmotion = em;
                Serial.printf("🎭 [XIAOZHI WS EMOTION]: %s\n", s_lastEmotion.c_str());
              }
            } else if (doc["emoji"].is<const char*>()) {
              String em = doc["emoji"].as<String>();
              em.toLowerCase();
              em.trim();
              if (em.length() > 0) {
                s_lastEmotion = em;
                Serial.printf("🎭 [XIAOZHI WS EMOJI]: %s\n", s_lastEmotion.c_str());
              }
            }

            const char* type = doc["type"] | "";
            if (strcmp(type, "hello") == 0) {
              if (doc["session_id"].is<const char*>()) {
                s_sessionId = doc["session_id"].as<String>();
                Serial.printf("🤝 [XIAOZHI WS HELLO ACK] Session ID: %s\n", s_sessionId.c_str());
              }
              if (doc["audio_params"]["format"].is<const char*>()) {
                Serial.printf("🎧 [XIAOZHI SERVER AUDIO]: Format=%s | Rate=%d Hz\n",
                              doc["audio_params"]["format"].as<const char*>(),
                              doc["audio_params"]["sample_rate"].as<int>());
              }
            } else if (strcmp(type, "stt") == 0) {
              const char* txt = doc["text"] | "";
              if (strlen(txt) > 0) {
                s_lastSttText = String(txt);
                s_sttReceived = true;
                Serial.printf("🎤 [XIAOZHI WS STT]: \"%s\"\n", s_lastSttText.c_str());
              }
            } else if (strcmp(type, "llm") == 0) {
              if (doc["text"].is<const char*>()) {
                const char* txt = doc["text"].as<const char*>();
                if (strlen(txt) > 0) {
                  s_lastLlmText = String(txt);
                  s_llmReceived = true;
                  s_lastAiReplyTime = millis();
                  Serial.printf("🤖 [XIAOZHI WS LLM TEXT]: \"%s\"\n", s_lastLlmText.c_str());
                  String emoFromTxt = XiaoZhiClient::detectEmotionFromText(s_lastLlmText);
                  if (emoFromTxt.length() > 0) {
                    s_lastEmotion = emoFromTxt;
                    Serial.printf("🎭 [XIAOZHI EMOTION FROM LLM TEXT]: %s\n", s_lastEmotion.c_str());
                  }
                }
              }
            } else if (strcmp(type, "tts") == 0) {
              const char* st = doc["state"] | "";
              if (doc["text"].is<const char*>()) {
                const char* txt = doc["text"].as<const char*>();
                if (strlen(txt) > 0) {
                  // Tránh trùng lặp câu nếu server gửi lại cùng đoạn text trong các frame liên tiếp
                  if (String(txt) != s_lastTtsSentence || (millis() - s_lastAiReplyTime > 1500)) {
                    s_lastTtsSentence = String(txt);
                    if (s_lastAiFullReply.length() > 0) {
                      s_lastAiFullReply += " ";
                    }
                    s_lastAiFullReply += String(txt);
                    s_ttsReceived = true;
                    s_lastAiReplyTime = millis();
                    Serial.printf("🔊 [XIAOZHI WS TTS ACCUM]: \"%s\" (Toàn bộ: \"%s\")\n", txt, s_lastAiFullReply.c_str());
                    String emoFromTxt = XiaoZhiClient::detectEmotionFromText(String(txt));
                    if (emoFromTxt.length() > 0) {
                      s_lastEmotion = emoFromTxt;
                      Serial.printf("🎭 [XIAOZHI EMOTION FROM TTS]: %s\n", s_lastEmotion.c_str());
                    }
                  }
                }
              }
              if (strcmp(st, "start") == 0 || strcmp(st, "sentence_start") == 0) {
                if (s_opusDecoder) {
                  opus_decoder_ctl(s_opusDecoder, OPUS_RESET_STATE);
                }
                TestAudio::resetStreamPcm();
              }
              if (strcmp(st, "stop") == 0) {
                s_ttsFinished = true;
                Serial.println("⏹️ [XIAOZHI WS TTS STOP]: Máy chủ đã gửi xong toàn bộ chuỗi câu thoại TTS.");
              }
            }
          }
        } else if (data->op_code == 0x02) { // Binary frame: Opus compressed audio từ XiaoZhi TTS
          if (data->data_len > 0 && data->data_ptr != nullptr) {
            static uint32_t s_binFrames = 0;
            s_binFrames++;

            ensureOpusDecoder();
            int decodedSamples = -1;
            if (s_opusDecoder != nullptr) {
              decodedSamples = opus_decode(
                s_opusDecoder,
                (const unsigned char*)data->data_ptr,
                data->data_len,
                s_pcmDecodeBuf,
                1920,
                0
              );
            }

            if (decodedSamples > 0) {
              if (s_binFrames <= 3 || s_binFrames % 25 == 0) {
                Serial.printf("🔊 [XIAOZHI OPUS RX] Frame #%u | Opus: %d bytes -> Giải mã %d mẫu PCM 16kHz -> MAX98357A\n",
                              s_binFrames, data->data_len, decodedSamples);
              }
              TestAudio::playStreamPcmAsync(s_pcmDecodeBuf, (size_t)decodedSamples);
            } else {
              // Fallback dự phòng nếu khung nhị phân là PCM 16-bit thô
              size_t sampleCount = data->data_len / sizeof(int16_t);
              if (s_binFrames <= 3 || s_binFrames % 25 == 0) {
                Serial.printf("🔊 [XIAOZHI PCM RX] Frame #%u | %d bytes (Opus err=%d) -> %u mẫu PCM -> MAX98357A\n",
                              s_binFrames, data->data_len, decodedSamples, (unsigned)sampleCount);
              }
              TestAudio::playStreamPcmAsync((const int16_t*)data->data_ptr, sampleCount);
            }
          }
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
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!isDeviceBound()) {
    return;
  }

  // Nếu client cũ đã tồn tại: kiểm tra xem còn kết nối không
  if (s_wsClient != nullptr) {
    if (esp_websocket_client_is_connected(s_wsClient) && s_wsConnected) {
      return; // Vẫn đang kết nối tốt, không tạo lại
    }
    // Socket bị ngắt hoặc mất kết nối -> Dọn dẹp triệt để trước khi tạo mới
    stopWebSocket();
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
  mac.toLowerCase(); // Bắt buộc viết thường để khớp với cơ sở dữ liệu XiaoZhi Hub!
  String uuid = getOrCreateDeviceUuid();

  s_wsUri = endpoint;
  s_wsHeaders = "Authorization: Bearer " + token + "\r\n"
              + "Device-Id: " + mac + "\r\n"
              + "Client-Id: " + uuid + "\r\n"
              + "Protocol-Version: 1\r\n";

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
  ws_cfg.ping_interval_sec = 15;
  ws_cfg.pingpong_timeout_sec = 60;
  ws_cfg.disable_pingpong_discon = true; // Tránh tự động ngắt kết nối do độ trễ Ping/Pong
  ws_cfg.task_stack = 12288;
  ws_cfg.buffer_size = 4096;

  Serial.println("\n🚀 [XIAOZHI WS] Đang mở kết nối nền WebSocket tới XiaoZhi Cloud...");
  Serial.printf("🌐 Endpoint : %s\n", s_wsUri.c_str());
  Serial.printf("📡 Device-Id: %s (Lowercase)\n", mac.c_str());

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
  if (s_opusDecoder != nullptr) {
    opus_decoder_destroy(s_opusDecoder);
    s_opusDecoder = nullptr;
  }
}

bool XiaoZhiClient::isWebSocketConnected() {
  if (s_wsClient == nullptr) return false;
  return (esp_websocket_client_is_connected(s_wsClient) && s_wsConnected);
}

static void wsRestartTask(void* pv) {
  XiaoZhiClient::stopWebSocket();
  vTaskDelay(pdMS_TO_TICKS(500));
  XiaoZhiClient::startWebSocket();
  s_isRestartingWs = false;
  vTaskDelete(NULL);
}

static void restartWebSocketAsync() {
  if (s_isRestartingWs) return;
  s_isRestartingWs = true;
  BaseType_t res = xTaskCreatePinnedToCore(wsRestartTask, "WsRestartTask", 4096, NULL, 1, NULL, 0);
  if (res != pdPASS) {
    s_isRestartingWs = false;
  }
}

void XiaoZhiClient::loopWebSocket() {
  static uint32_t s_lastWsCheck = 0;
  uint32_t now = millis();
  if (now - s_lastWsCheck < 5000) return;
  s_lastWsCheck = now;

  if (WiFi.status() == WL_CONNECTED && isDeviceBound()) {
    if (s_wsClient == nullptr && !s_isRestartingWs) {
      Serial.println("🚀 [XIAOZHI WS] Khởi chạy WebSocket kết nối lần đầu...");
      startWebSocket();
    } else if (!isWebSocketConnected() && !s_isRestartingWs) {
      // esp_websocket_client tự động reconnect ngầm mỗi 10s.
      // Chỉ khi mất kết nối liên tục vượt quá backoff (15s -> 30s -> 60s -> max 120s) mới restart async
      if (s_lastWsDisconnectMs > 0 && (now - s_lastWsDisconnectMs >= s_wsReconnectBackoffMs)) {
        Serial.printf("🔄 [XIAOZHI WS] Mất kết nối >%u s -> Khởi động lại WebSocket (Async Non-blocking)...\n",
                      (unsigned)(s_wsReconnectBackoffMs / 1000));
        uint32_t nextBackoff = s_wsReconnectBackoffMs * 2;
        if (nextBackoff > 120000) nextBackoff = 120000;
        s_wsReconnectBackoffMs = nextBackoff;
        s_lastWsDisconnectMs = now;
        restartWebSocketAsync();
      }
    }
  } else {
    if (s_wsClient != nullptr && !s_isRestartingWs) {
      restartWebSocketAsync();
    }
  }
}

String XiaoZhiClient::getSessionId() {
  return s_sessionId;
}

String XiaoZhiClient::getLastSttText() {
  return s_lastSttText;
}

String XiaoZhiClient::getLastLlmReply() {
  if (s_lastAiFullReply.length() > 0) {
    return s_lastAiFullReply;
  }
  if (s_lastLlmText.length() > 0 && s_lastLlmText != "😊") {
    return s_lastLlmText;
  }
  if (s_lastTtsSentence.length() > 0) {
    return s_lastTtsSentence;
  }
  return s_lastLlmText;
}

String XiaoZhiClient::getLastEmotion() {
  return s_lastEmotion;
}

bool XiaoZhiClient::isTtsFinished() {
  return s_ttsFinished;
}

uint32_t XiaoZhiClient::getLastAiReplyTime() {
  return s_lastAiReplyTime;
}

void XiaoZhiClient::clearLastTurn() {
  s_lastSttText = "";
  s_lastLlmText = "";
  s_lastTtsSentence = "";
  s_lastAiFullReply = "";
  s_lastEmotion = "";
  s_sttReceived = false;
  s_llmReceived = false;
  s_ttsReceived = false;
  s_ttsFinished = false;
  s_lastAiReplyTime = 0;
}

bool XiaoZhiClient::sendAudioToXiaoZhi(const int16_t* pcmSamples, size_t sampleCount, uint32_t sampleRate,
                                      String& outUserText, String& outAiReply, String& outEmotion,
                                      XiaoZhiProgressCallback onProgress) {
  outUserText = "";
  outAiReply = "";
  outEmotion = "happy";

  if (!pcmSamples || sampleCount < 800) {
    Serial.println("⚠️ [XIAOZHI AUDIO] Dữ liệu PCM quá ngắn, bỏ qua.");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ [XIAOZHI AUDIO] Chưa kết nối Wi-Fi.");
    return false;
  }

  // Khởi động lại WebSocket nếu chưa kết nối
  if (!isWebSocketConnected()) {
    Serial.println("🔄 [XIAOZHI AUDIO] WebSocket chưa kết nối, đang thiết lập lại kết nối tới XiaoZhi Cloud...");
    stopWebSocket();
    startWebSocket();
    uint32_t t0 = millis();
    while (!isWebSocketConnected() && (millis() - t0 < 4000)) {
      delay(50);
    }
  }

  if (!isWebSocketConnected() || !s_wsClient) {
    Serial.println("❌ [XIAOZHI AUDIO] Không thể kết nối tới XiaoZhi WebSocket!");
    return false;
  }

  // Đặt lại dữ liệu hội thoại cho lượt mới
  clearLastTurn();

  // 1. Gửi listen start
  String sid = s_sessionId.length() > 0 ? s_sessionId : "";
  String startMsg = "{\"session_id\":\"" + sid + "\",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"manual\"}";
  int retStart = esp_websocket_client_send_text(s_wsClient, startMsg.c_str(), startMsg.length(), pdMS_TO_TICKS(1500));
  if (retStart < 0) {
    Serial.println("❌ [XIAOZHI AUDIO] Gửi listen start thất bại!");
    return false;
  }
  Serial.printf("🎤 [XIAOZHI AUDIO] Bắt đầu truyền %u mẫu PCM (%.1f giây @ %u Hz) qua WebSocket...\n",
                (unsigned)sampleCount, (float)sampleCount / sampleRate, (unsigned)sampleRate);

  // 2. Truyền các khung âm thanh PCM (960 mẫu = 1920 byte @ 16kHz tương đương 60ms)
  const uint8_t* bytePtr = (const uint8_t*)pcmSamples;
  size_t totalBytes = sampleCount * sizeof(int16_t);
  const size_t FRAME_BYTES = 960 * sizeof(int16_t); // 1920 byte

  size_t offset = 0;
  bool sendOk = true;
  while (offset < totalBytes && isWebSocketConnected()) {
    size_t chunk = (totalBytes - offset > FRAME_BYTES) ? FRAME_BYTES : (totalBytes - offset);
    int bSent = esp_websocket_client_send_bin(s_wsClient, (const char*)(bytePtr + offset), chunk, pdMS_TO_TICKS(1000));
    if (bSent < 0) {
      Serial.printf("❌ [XIAOZHI AUDIO] Gửi khung nhị phân thất bại tại offset %u!\n", (unsigned)offset);
      sendOk = false;
      break;
    }
    offset += chunk;
    delay(5);
  }

  // 3. Gửi listen stop
  if (sendOk && isWebSocketConnected()) {
    String stopMsg = "{\"session_id\":\"" + sid + "\",\"type\":\"listen\",\"state\":\"stop\"}";
    esp_websocket_client_send_text(s_wsClient, stopMsg.c_str(), stopMsg.length(), pdMS_TO_TICKS(1500));
    Serial.println("⏹️ [XIAOZHI AUDIO] Đã kết thúc truyền âm thanh. Đang chờ XiaoZhi Hub phản hồi...");
  } else {
    Serial.println("⚠️ [XIAOZHI AUDIO] Quá trình gửi âm thanh bị gián đoạn do mất kết nối socket.");
    return false;
  }

  // 4. Chờ nhận phản hồi STT và LLM/TTS từ server (tối đa 12.0s để nhận trọn vẹn toàn bộ các câu thoại)
  uint32_t waitStart = millis();
  while ((millis() - waitStart < 12000) && isWebSocketConnected()) {
    String currentPartial = (s_lastAiFullReply.length() > 0) ? s_lastAiFullReply : s_lastLlmText;
    if (onProgress) {
      onProgress(s_lastSttText, currentPartial, s_lastEmotion, false);
    }

    // Nếu máy chủ đã phát tín hiệu kết thúc TTS (type: tts, state: stop) -> đã hoàn tất trọn vẹn toàn bộ chuỗi câu
    if ((s_ttsReceived || s_llmReceived) && s_ttsFinished) {
      Serial.println("✅ [XIAOZHI AUDIO] Máy chủ đã gửi xong toàn bộ câu thoại (TTS STOP). Hoàn tất nhận phản hồi.");
      break;
    }
    // Nếu đã nhận được câu trả lời nhưng chưa có tín hiệu stop, chờ tối thiểu 3.2 giây không có câu mới từ server
    if ((s_ttsReceived || s_llmReceived) && s_lastAiReplyTime > 0 && (millis() - s_lastAiReplyTime >= 3200)) {
      Serial.println("⏱️ [XIAOZHI AUDIO] Đã qua 3.2s không có thêm câu thoại mới từ máy chủ. Kết thúc chờ đồng bộ.");
      break;
    }

    delay(30);
  }

  if (s_sttReceived && s_lastSttText.length() > 0) {
    outUserText = s_lastSttText;
  }

  if (s_lastAiFullReply.length() > 0) {
    outAiReply = s_lastAiFullReply;
  } else if (s_lastLlmText.length() > 0 && s_lastLlmText != "😊") {
    outAiReply = s_lastLlmText;
  } else if (s_lastTtsSentence.length() > 0) {
    outAiReply = s_lastTtsSentence;
  }

  outEmotion = (s_lastEmotion.length() > 0) ? s_lastEmotion : "happy";

  if (onProgress) {
    onProgress(outUserText, outAiReply, outEmotion, true);
  }

  Serial.printf("✨ [XIAOZHI RESULT] STT: \"%s\" | AI: \"%s\" | Emotion: %s\n",
                outUserText.c_str(), outAiReply.c_str(), outEmotion.c_str());

  return (outUserText.length() > 0 || outAiReply.length() > 0);
}

// --- TÁC VỤ ĐÀM THOẠI BẤT ĐỒNG BỘ FREERTOS (CORE 0) ---
static int16_t* s_asyncConvPcm = nullptr;
static size_t s_asyncConvMaxCapacity = 160000; // 320 KB PSRAM (~10s âm thanh Mono 16kHz)
static size_t s_asyncConvSampleCount = 0;
static uint32_t s_asyncConvSampleRate = 16000;
static TaskHandle_t s_asyncConvTaskHandle = nullptr;
static volatile bool s_asyncConvRunning = false;
static volatile bool s_asyncConvHasResult = false;
static String s_asyncResultUserText = "";
static String s_asyncResultAiReply = "";
static String s_asyncResultEmotion = "happy";
static portMUX_TYPE s_asyncConvMux = portMUX_INITIALIZER_UNLOCKED;

static void asyncConversationTask(void* param) {
  Serial.println("🤖 [XIAOZHI CONV TASK] Bắt đầu tác vụ đàm thoại bất đồng bộ trên Core 0...");
  String userText = "";
  String aiReply = "";
  String emotion = "happy";

  bool ok = XiaoZhiClient::sendAudioToXiaoZhi(
    s_asyncConvPcm,
    s_asyncConvSampleCount,
    s_asyncConvSampleRate,
    userText,
    aiReply,
    emotion,
    nullptr
  );

  taskENTER_CRITICAL(&s_asyncConvMux);
  s_asyncResultUserText = userText;
  s_asyncResultAiReply = aiReply;
  s_asyncResultEmotion = emotion;
  s_asyncConvHasResult = ok;
  s_asyncConvRunning = false;
  taskEXIT_CRITICAL(&s_asyncConvMux);

  Serial.printf("🤖 [XIAOZHI CONV TASK] Hoàn tất đàm thoại (ok=%d). Giải phóng task Core 0.\n", ok);
  s_asyncConvTaskHandle = nullptr;
  vTaskDelete(NULL);
}

bool XiaoZhiClient::startAudioConversationAsync(const int16_t* pcmSamples, size_t sampleCount, uint32_t sampleRate) {
  if (!pcmSamples || sampleCount < 800) {
    Serial.println("⚠️ [XIAOZHI ASYNC] Dữ liệu PCM quá ngắn, bỏ qua.");
    return false;
  }

  abortConversation();

  if (!s_asyncConvPcm) {
    if (psramFound()) {
      s_asyncConvPcm = (int16_t*)ps_malloc(s_asyncConvMaxCapacity * sizeof(int16_t));
    }
    if (!s_asyncConvPcm) {
      s_asyncConvPcm = (int16_t*)malloc(16000 * sizeof(int16_t)); // Fallback 32KB SRAM (~1s)
      s_asyncConvMaxCapacity = 16000;
    }
  }

  if (!s_asyncConvPcm) {
    Serial.println("❌ [XIAOZHI ASYNC] Không thể cấp phát bộ đệm PCM!");
    return false;
  }

  size_t toCopy = (sampleCount < s_asyncConvMaxCapacity) ? sampleCount : s_asyncConvMaxCapacity;
  memcpy(s_asyncConvPcm, pcmSamples, toCopy * sizeof(int16_t));
  s_asyncConvSampleCount = toCopy;
  s_asyncConvSampleRate = sampleRate;

  taskENTER_CRITICAL(&s_asyncConvMux);
  s_asyncConvRunning = true;
  s_asyncConvHasResult = false;
  s_asyncResultUserText = "";
  s_asyncResultAiReply = "";
  s_asyncResultEmotion = "happy";
  taskEXIT_CRITICAL(&s_asyncConvMux);

  BaseType_t res = xTaskCreatePinnedToCore(
    asyncConversationTask,
    "XiaoZhiConvTask",
    8192,
    nullptr,
    2, // Priority 2 trên Core 0 (không nghẽn Core 1 UI)
    &s_asyncConvTaskHandle,
    0  // Pin to Core 0
  );

  return (res == pdPASS);
}

bool XiaoZhiClient::isConversationActive() {
  bool active = false;
  taskENTER_CRITICAL(&s_asyncConvMux);
  active = s_asyncConvRunning;
  taskEXIT_CRITICAL(&s_asyncConvMux);
  return active;
}

bool XiaoZhiClient::hasNewTurnResult() {
  bool hasRes = false;
  taskENTER_CRITICAL(&s_asyncConvMux);
  hasRes = s_asyncConvHasResult;
  taskEXIT_CRITICAL(&s_asyncConvMux);
  return hasRes;
}

void XiaoZhiClient::getLatestTurnResult(String& outUserText, String& outAiReply, String& outEmotion) {
  taskENTER_CRITICAL(&s_asyncConvMux);
  outUserText = s_asyncResultUserText;
  outAiReply = s_asyncResultAiReply;
  outEmotion = s_asyncResultEmotion;
  s_asyncConvHasResult = false;
  taskEXIT_CRITICAL(&s_asyncConvMux);
}

void XiaoZhiClient::abortConversation() {
  if (s_asyncConvTaskHandle != nullptr) {
    vTaskDelete(s_asyncConvTaskHandle);
    s_asyncConvTaskHandle = nullptr;
  }
  taskENTER_CRITICAL(&s_asyncConvMux);
  s_asyncConvRunning = false;
  s_asyncConvHasResult = false;
  taskEXIT_CRITICAL(&s_asyncConvMux);
}

bool XiaoZhiClient::hasAllocatedDecoder() {
  return (s_opusDecoder != nullptr);
}

size_t XiaoZhiClient::trimMemory() {
  size_t totalFreed = 0;
  if (!isConversationActive() && s_asyncConvPcm != nullptr) {
    free(s_asyncConvPcm);
    s_asyncConvPcm = nullptr;
    totalFreed += s_asyncConvMaxCapacity * sizeof(int16_t);
    Serial.println("🧹 [TRIM XIAOZHI] Đã giải phóng bộ đệm async PCM (320 KB PSRAM)");
  }

  if (s_opusDecoder != nullptr && !TestAudio::isVoiceStreamActive()) {
    opus_decoder_destroy(s_opusDecoder);
    s_opusDecoder = nullptr;
    Serial.println("🧹 [TRIM XIAOZHI] Đã giải phóng Opus Decoder (~25 KB)");
    totalFreed += 25 * 1024;
  }
  return totalFreed;
}





