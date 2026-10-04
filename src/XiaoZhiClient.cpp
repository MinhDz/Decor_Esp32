#include "XiaoZhiClient.h"
#include "Config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <LittleFS.h>

static Preferences sysPrefs;
static String s_lastAuthCode = "";


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

    if (printToSerial) {
      Serial.println("\n==================================================================");
      Serial.println("🚀 [XIAOZHI OTA] PHẢN HỒI TỪ MÁY CHỦ XIAOZHI (HTTP 200 OK)");
      Serial.println("==================================================================");
      Serial.printf("📡 Device Hardware MAC : %s\n", mac.c_str());
      Serial.printf("🆔 Device Client UUID  : %s\n", uuid.c_str());
      Serial.println("------------------------------------------------------------------");

      JsonDocument resDoc;
      DeserializationError err = deserializeJson(resDoc, payload);
      if (!err) {
        // 1. Kiểm tra mã kích hoạt
        if (resDoc["activation"]["code"].is<const char*>()) {
          const char* code = resDoc["activation"]["code"];
          const char* msg = resDoc["activation"]["message"] | "";
          s_lastAuthCode = String(code);
          Serial.println("🔑 TRẠNG THÁI: CHƯA LIÊN KẾT (CẦN THÊM THIẾT BỊ TRÊN XIAOZHI.ME)");
          Serial.printf("👉 MÃ XÁC THỰC (AUTH CODE) : >>>  %s  <<<\n", code);
          if (strlen(msg) > 0) {
            Serial.printf("💬 Hướng dẫn từ XiaoZhi     : %s\n", msg);
          }
          Serial.printf("🌐 Hãy mở https://xiaozhi.me -> Thêm thiết bị -> Nhập MAC [%s] và Mã [%s]\n", mac.c_str(), code);
        } else {
          s_lastAuthCode = "";
          Serial.println("🎉 TRẠNG THÁI: THIẾT BỊ ĐÃ LIÊN KẾT THÀNH CÔNG VỚI TÀI KHOẢN XIAOZHI!");
        }

        Serial.println("------------------------------------------------------------------");

        // 2. Thông tin WebSocket
        if (resDoc["websocket"]["url"].is<const char*>()) {
          const char* wsUrl = resDoc["websocket"]["url"];
          const char* token = resDoc["websocket"]["token"] | "";
          Serial.printf("🌐 WebSocket Server Endpoint : %s\n", wsUrl);
          Serial.printf("🔑 Token                     : %s\n", (token && strlen(token) > 0) ? token : "(Chưa có)");

          // Tự động lưu Token vào config.json nếu nhận được token
          if (token && strlen(token) > 0) {
            JsonDocument cfgDoc;
            if (LittleFS.exists(FILE_CONFIG)) {
              File f = LittleFS.open(FILE_CONFIG, "r");
              deserializeJson(cfgDoc, f);
              f.close();
            }
            cfgDoc["endpoint"] = wsUrl;
            cfgDoc["token"] = token;
            cfgDoc["mac"] = mac;
            cfgDoc["uuid"] = uuid;
            File f = LittleFS.open(FILE_CONFIG, "w");
            if (f) {
              serializeJson(cfgDoc, f);
              f.close();
              Serial.println("💾 Đã tự động cập nhật Token và Endpoint vào /config.json!");
            }
          }
        }

        // 3. Thông tin MQTT
        if (resDoc["mqtt"]["endpoint"].is<const char*>()) {
          Serial.printf("⚡ MQTT Broker               : %s\n", resDoc["mqtt"]["endpoint"].as<const char*>());
          Serial.printf("📦 MQTT Client ID            : %s\n", resDoc["mqtt"]["client_id"].as<const char*>());
        }

        // 4. Server Time
        if (resDoc["server_time"]["timestamp"].is<long long>()) {
          Serial.printf("⏰ Server Timestamp          : %lld (Timezone Offset: %d phút)\n",
                        resDoc["server_time"]["timestamp"].as<long long>(),
                        resDoc["server_time"]["timezone_offset"].as<int>());
        }
      }

      Serial.println("------------------------------------------------------------------");
      Serial.println("📄 [NỘI DUNG RAW JSON TỪ XIAOZHI]:");
      Serial.println(payload);
      Serial.println("==================================================================\n");
    }
  } else {
    String errPayload = http.getString();
    Serial.printf("❌ [XIAOZHI OTA] Lỗi gọi API (HTTP %d): %s\n", httpCode, errPayload.c_str());
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
  Serial.printf("🎙️ [STT] Bắt đầu gửi %u mẫu PCM (%u byte @ %u Hz) | Peak=%u, Avg=%u\n",
                (unsigned)sampleCount, (unsigned)(sampleCount * 2), (unsigned)sampleRate,
                (unsigned)peak, (unsigned)avgAbs);

  // Public Chromium Speech API Key cho Google Speech-to-Text v2 (nhận diện trực tiếp PCM 16-bit signed little-endian)
  const char* sttUrl = "http://www.google.com/speech-api/v2/recognize?output=json&lang=vi-VN&key=AIzaSyBo8_3E6-G1_n7T4C2P9p_hE-sW9Y4n_X0";
  HTTPClient http;
  http.begin(sttUrl);
  http.setTimeout(7500);
  String contentType = "audio/l16; rate=" + String(sampleRate) + "; channels=1";
  http.addHeader("Content-Type", contentType);

  int code = http.POST((uint8_t*)pcmSamples, sampleCount * sizeof(int16_t));
  String resp = (code > 0) ? http.getString() : "";
  http.end();

  Serial.printf("🎙️ [STT] HTTP Code=%d, Response=%s\n", code, resp.c_str());

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

  return "";
}



