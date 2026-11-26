#include "client/McpTools.h"
#include "drivers/CobLedDriver.h"
#include "XiaoZhiClient.h"

namespace {
void addTool(JsonArray tools, const char* name, const char* description) {
  JsonObject tool = tools.add<JsonObject>();
  tool["name"] = name;
  tool["description"] = description;
  tool["inputSchema"]["type"] = "object";
  tool["inputSchema"]["properties"].to<JsonObject>();
}

void addProperty(JsonObject tool, const char* key, const char* type, const char* description) {
  JsonObject property = tool["inputSchema"]["properties"][key].to<JsonObject>();
  property["type"] = type;
  property["description"] = description;
}

PeripheralControlResult invalidArguments(const char* message) {
  return {false, String("Tham số không hợp lệ: ") + message};
}
}

namespace McpTools {

void appendDefinitions(JsonArray tools) {
  JsonObject tool;

  addTool(tools, "self.windmill.set", "Bật, tắt hoặc điều chỉnh tốc độ cối xay gió.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "state", "boolean", "Bật hoặc tắt motor.");
  addProperty(tool, "speed", "integer", "Tốc độ từ 0 đến 100 phần trăm.");
  addProperty(tool, "mode", "string", "Chế độ: off, low, medium, high hoặc breath.");

  addTool(tools, "self.edison_lights.set", "Điều khiển độ sáng hai đèn Edison.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "state", "boolean", "Bật hoặc tắt đèn.");
  addProperty(tool, "side", "string", "Vị trí: both, left hoặc right.");
  addProperty(tool, "brightness", "integer", "Độ sáng từ 0 đến 100 phần trăm.");
  addProperty(tool, "breathing", "boolean", "Bật hiệu ứng thở.");

  addTool(tools, "self.cob_leds.set", "Bật, tắt hoặc đổi hiệu ứng hai thanh LED COB.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "state", "boolean", "Bật hoặc tắt đèn.");
  addProperty(tool, "action", "string", "Thao tác: toggle hoặc next_mode.");

  addTool(tools, "self.telemetry.get", "Đọc nhiệt độ, độ ẩm, pin, trạng thái sạc và tốc độ motor.");

  addTool(tools, "self.screen.switch_mode", "Chuyển màn hình sang standby, pc_status, emoji hoặc diag. TUYỆT ĐỐI KHÔNG tự ý chuyển sang emoji khi người dùng đang ở trò chơi hoặc ứng dụng khác.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "mode", "string", "Chế độ: standby, pc_status, emoji hoặc diag.");

  addTool(tools, "self.audio.set_volume", "Đặt âm lượng phát từ 0 đến 100 phần trăm.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "volume", "integer", "Âm lượng từ 0 đến 100 phần trăm.");

  addTool(tools, "self.screen.set_emoji_style", "Chi goi khi nguoi dung yeu cau doi phong cach bieu cam khuon mat: neon, cute_girl, doi_mat hoac gome. Tuyet doi khong tu y goi khi dang doc tin tuc hoac tra loi binh thuong.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "style", "string", "neon, cute_girl, doi_mat hoac gome.");

  addTool(tools, "self.power.set_profile", "Đặt cấu hình quản lý nguồn: performance (tối đa), balanced (cân bằng) hoặc saver (tiết kiệm pin).");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "profile", "string", "performance, balanced hoặc saver.");
  addProperty(tool, "auto_switch", "boolean", "Tự động chuyển cấu hình theo trạng thái sạc và pin (tùy chọn).");

  addTool(tools, "self.power.get_status", "Lấy thông tin quản lý nguồn: cấu hình, tần số CPU, pin, trạng thái sạc và bão hòa.");

  addTool(tools, "get_daily_news", "Lấy danh sách các bản tin tức mới nhất trong ngày từ báo điện tử để tóm tắt và đọc cho người dùng nghe (tin tức hôm nay, thời sự, điểm tin buổi sáng/tối, tin mới nhất).");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "limit", "integer", "Số lượng tin tức muốn lấy (mặc định 5, từ 1 đến 5).");

  // --- CÔNG CỤ QUẢN TRỊ TRẠM VŨ TRỤ CHO THUYỀN TRƯỞNG (SCI-FI AI COMMANDS) ---
  addTool(tools, "self.battery.get", "Kiểm tra chi tiết dung lượng pin (phần trăm, điện áp vôn, trạng thái sạc nguồn ngoài hay pin dự phòng) để đọc cho người dùng.");
  addTool(tools, "self.wifi.get", "Kiểm tra trạng thái mạng Wi-Fi (tên mạng SSID, cường độ sóng, IP, độ ổn định đường truyền) để đọc cho người dùng.");
  addTool(tools, "self.storage.get", "Kiểm tra dung lượng thẻ nhớ ngoài SD, bộ nhớ Flash LittleFS và dung lượng RAM của trạm vũ trụ.");
  addTool(tools, "self.station.get_report", "Lập báo cáo toàn diện tình trạng Trạm Vũ Trụ (nhiệt độ, độ ẩm, pin, wifi, thẻ nhớ, đèn, cối xay, CPU) theo phong cách AI Chỉ huy Trạm Vũ Trụ báo cáo cho Thuyền trưởng.");

  addTool(tools, "self.screen.next_wallpaper", "Chuyển sang hình nền tiếp theo trong thẻ nhớ SD hoặc bộ nhớ trạm.");
  addTool(tools, "self.screen.open_app", "Mở ứng dụng hoặc chức năng trên màn hình trạm: games (trò chơi), media (máy nghe nhạc), standby (màn hình chờ), pchud (thông số PC), diag (chẩn đoán cảm biến), clock (đồng hồ), calendar (lịch vạn niên). Giữ nguyên màn hình ứng dụng sau khi mở, tuyệt đối không tự ý chuyển về emoji.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "app", "string", "Tên ứng dụng: games, media, standby, pchud, diag, clock hoặc calendar.");

  addTool(tools, "self.media.control", "Điều khiển trình phát nhạc của trạm vũ trụ: play (phát), pause (tạm dừng), next (bài kế), prev (bài trước), open (mở màn hình nghe nhạc).");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "action", "string", "Thao tác: play, pause, next, prev hoặc open.");

  addTool(tools, "self.screen.set_brightness", "Điều chỉnh độ sáng đèn nền màn hình TFT của trạm từ 5 đến 100 phần trăm.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "brightness", "integer", "Độ sáng màn hình từ 5 đến 100 phần trăm.");

  addTool(tools, "self.ai.set_identity", "Đặt tên mới cho Trợ lý AI trên trạm hoặc đổi câu nói/từ khóa đánh thức AI (Wake Word) khi người dùng yêu cầu đổi tên AI.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "ai_name", "string", "Tên mới của trợ lý AI trên trạm (ví dụ: Jarvis, Gome, Na, XiaoZhi).");
  addProperty(tool, "wake_word", "string", "Câu nói đánh thức AI từ màn hình chờ/AOD (ví dụ: Jarvis ơi, Gome ơi).");

  // --- CÔNG CỤ ĐIỀU KHIỂN MÁY TÍNH PC THÔNG QUA DECOR SPACE COMPANION ---
  addTool(tools, "pc.system.schedule_shutdown", "Hẹn giờ tắt máy tính PC hoặc hủy hẹn giờ tắt máy tính PC (Windows).");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "minutes", "integer", "Số phút hẹn giờ trước khi tắt máy (ví dụ: 15, 30, 60). Đặt 0 nếu muốn tắt ngay.");
  addProperty(tool, "cancel", "boolean", "Đặt true nếu muốn hủy lệnh hẹn giờ tắt máy đang chạy.");

  addTool(tools, "pc.system.power", "Thực hiện thao tác nguồn hoặc khóa màn hình trên máy tính PC: lock (khóa máy), sleep (ngủ), restart (khởi động lại), cancel_shutdown (hủy tắt máy).");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "action", "string", "Thao tác: lock, sleep, restart hoặc cancel_shutdown.");

  addTool(tools, "pc.clipboard.get", "Đọc nội dung văn bản hiện đang lưu trong Clipboard (bộ nhớ tạm) của máy tính PC để báo cáo hoặc tóm tắt cho người dùng.");

  addTool(tools, "pc.clipboard.set", "Ghi hoặc sao chép một đoạn văn bản vào Clipboard (bộ nhớ tạm) của máy tính PC.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "text", "string", "Nội dung văn bản cần chép vào Clipboard của PC.");

  addTool(tools, "pc.app.open", "Mở một ứng dụng hoặc phần mềm trên máy tính PC (ví dụ: notepad, calculator, paint, explorer, cmd, powershell, taskmgr, settings, chrome, edge, coccoc, vscode, spotify, discord, zalo, word, excel).");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "app_name", "string", "Tên ứng dụng hoặc đường dẫn chương trình cần mở trên PC.");

  addTool(tools, "pc.browser.open_youtube", "Mở YouTube trên trình duyệt máy tính PC để tìm kiếm hoặc phát video/nhạc theo yêu cầu (hoặc mở trang chủ YouTube nếu không có từ khóa).");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "query", "string", "Từ khóa tìm kiếm video/bài hát trên YouTube hoặc URL YouTube.");

  addTool(tools, "pc.browser.open_url", "Mở một trang web bất kỳ hoặc tìm kiếm Google trên trình duyệt máy tính PC.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "url", "string", "Địa chỉ URL trang web (ví dụ: https://vnexpress.net, facebook.com) hoặc từ khóa tìm kiếm Google.");
}

PeripheralControlResult execute(const String& name, JsonObjectConst args) {
  if (name == "self.windmill.set") {
    if (args["state"].is<bool>() && !args["state"].as<bool>()) return PeripheralControlService::setWindmill(0, 0);
    int mode = 2;
    int speed = 60;
    if (!args["mode"].isUnbound()) {
      if (!args["mode"].is<const char*>()) return invalidArguments("mode phải là chuỗi.");
      String value = args["mode"].as<String>();
      value.toLowerCase();
      if (value == "off") { mode = 0; speed = 0; }
      else if (value == "low") { mode = 1; speed = 35; }
      else if (value == "medium") { mode = 2; speed = 60; }
      else if (value == "high") { mode = 3; speed = 85; }
      else if (value == "breath") { mode = 4; speed = 50; }
      else return invalidArguments("mode phải là off, low, medium, high hoặc breath.");
    }
    if (args["speed"].is<int>()) {
      speed = args["speed"].as<int>();
      if (speed == 0) mode = 0;
      else if (mode == 0) mode = 2;
    } else if (!args["speed"].isUnbound()) return invalidArguments("speed phải là số nguyên.");
    if (!args["state"].isUnbound() && !args["state"].is<bool>()) return invalidArguments("state phải là boolean.");
    return PeripheralControlService::setWindmill(mode, speed);
  }

  if (name == "self.edison_lights.set") {
    bool enabled = true;
    int side = 0;
    int brightness = 80;
    bool breathing = false;
    if (args["state"].is<bool>()) enabled = args["state"].as<bool>();
    else if (!args["state"].isUnbound()) return invalidArguments("state phải là boolean.");
    if (args["side"].is<const char*>()) {
      String value = args["side"].as<String>(); value.toLowerCase();
      if (value == "left") side = 1;
      else if (value == "right") side = 2;
      else if (value != "both") return invalidArguments("side phải là both, left hoặc right.");
    } else if (!args["side"].isUnbound()) return invalidArguments("side phải là chuỗi.");
    if (args["brightness"].is<int>()) brightness = args["brightness"].as<int>();
    else if (!args["brightness"].isUnbound()) return invalidArguments("brightness phải là số nguyên.");
    if (args["breathing"].is<bool>()) breathing = args["breathing"].as<bool>();
    else if (!args["breathing"].isUnbound()) return invalidArguments("breathing phải là boolean.");
    return PeripheralControlService::setEdisonLights(side, enabled, brightness, breathing);
  }

  if (name == "self.cob_leds.set") {
    bool enabled = true;
    bool nextMode = false;
    if (args["state"].is<bool>()) enabled = args["state"].as<bool>();
    else if (!args["state"].isUnbound()) return invalidArguments("state phải là boolean.");
    if (args["action"].is<const char*>()) {
      String action = args["action"].as<String>(); action.toLowerCase();
      if (action == "next_mode") nextMode = true;
      else if (action == "toggle") enabled = !CobLeds::isOn();
      else return invalidArguments("action phải là toggle hoặc next_mode.");
    } else if (!args["action"].isUnbound()) return invalidArguments("action phải là chuỗi.");
    return PeripheralControlService::setCobLeds(enabled, nextMode);
  }

  if (name == "self.telemetry.get") return PeripheralControlService::getTelemetry();

  if (name == "self.screen.switch_mode") {
    if (!args["mode"].is<const char*>()) return invalidArguments("cần mode dạng chuỗi.");
    return PeripheralControlService::switchScreen(args["mode"].as<String>());
  }

  if (name == "self.audio.set_volume") {
    if (!args["volume"].is<int>()) return invalidArguments("cần volume dạng số nguyên.");
    return PeripheralControlService::setAudioVolume(args["volume"].as<int>());
  }

  if (name == "self.screen.set_emoji_style") {
    if (!args["style"].is<const char*>()) return invalidArguments("cần style dạng chuỗi.");
    return PeripheralControlService::setEmojiStyle(args["style"].as<String>());
  }

  if (name == "self.power.set_profile") {
    if (!args["profile"].is<const char*>()) return invalidArguments("cần profile dạng chuỗi (performance, balanced hoặc saver).");
    bool autoSw = args["auto_switch"].is<bool>() ? args["auto_switch"].as<bool>() : false;
    return PeripheralControlService::setPowerProfile(args["profile"].as<String>(), autoSw);
  }

  if (name == "self.power.get_status") {
    return PeripheralControlService::getPowerStatus();
  }

  if (name == "get_daily_news" || name == "self.get_daily_news" || name == "self.news.get_daily" || name == "self.news.get") {
    int limit = 5;
    if (args["limit"].is<int>()) limit = args["limit"].as<int>();
    return PeripheralControlService::getDailyNews(limit);
  }

  // Quản trị trạm vũ trụ theo lệnh Thuyền trưởng
  if (name == "self.battery.get") return PeripheralControlService::getBatteryStatus();
  if (name == "self.wifi.get") return PeripheralControlService::getWifiStatus();
  if (name == "self.storage.get") return PeripheralControlService::getStorageStatus();
  if (name == "self.station.get_report") return PeripheralControlService::getStationFullReport();

  if (name == "self.screen.next_wallpaper") return PeripheralControlService::cycleWallpaper(1);
  if (name == "self.screen.open_app") {
    if (!args["app"].is<const char*>()) return invalidArguments("cần app dạng chuỗi.");
    return PeripheralControlService::switchScreen(args["app"].as<String>());
  }
  if (name == "self.media.control") {
    String action = "open";
    if (args["action"].is<const char*>()) action = args["action"].as<String>();
    return PeripheralControlService::controlMedia(action);
  }

  if (name == "self.screen.set_brightness") {
    if (!args["brightness"].is<int>()) return invalidArguments("cần brightness dạng số nguyên (5-100).");
    return PeripheralControlService::setScreenBrightness(args["brightness"].as<int>());
  }

  if (name == "self.ai.set_identity") {
    String newName = args["ai_name"] | "";
    String newWake = args["wake_word"] | "";
    newName.trim();
    newWake.trim();
    if (newName.length() == 0 && newWake.length() == 0) {
      return invalidArguments("cần cung cấp ai_name hoặc wake_word.");
    }
    if (newName.length() > 0) {
      XiaoZhiClient::setAiName(newName);
      if (newWake.length() == 0) {
        XiaoZhiClient::setWakeWord(newName + " ơi");
      }
    }
    if (newWake.length() > 0) {
      XiaoZhiClient::setWakeWord(newWake);
    }
    return {true, "Đã cập nhật tên Trợ lý AI thành \"" + XiaoZhiClient::getAiName() + "\" và câu đánh thức là \"" + XiaoZhiClient::getWakeWord() + "\"."};
  }

  // Thực thi nhóm công cụ điều khiển máy tính PC qua DecorSpaceCompanion
  if (name == "pc.system.schedule_shutdown") {
    return PeripheralControlService::controlPcAction("schedule_shutdown", args);
  }
  if (name == "pc.system.power") {
    return PeripheralControlService::controlPcAction("power_control", args);
  }
  if (name == "pc.clipboard.get") {
    return PeripheralControlService::controlPcAction("get_clipboard", args);
  }
  if (name == "pc.clipboard.set") {
    return PeripheralControlService::controlPcAction("set_clipboard", args);
  }
  if (name == "pc.app.open") {
    return PeripheralControlService::controlPcAction("open_app", args);
  }
  if (name == "pc.browser.open_youtube") {
    return PeripheralControlService::controlPcAction("open_youtube", args);
  }
  if (name == "pc.browser.open_url") {
    return PeripheralControlService::controlPcAction("open_url", args);
  }

  return {false, "Không tìm thấy công cụ MCP: " + name};
}

} // namespace McpTools
