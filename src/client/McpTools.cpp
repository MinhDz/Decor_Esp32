#include "client/McpTools.h"
#include "drivers/CobLedDriver.h"

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

  addTool(tools, "self.screen.switch_mode", "Chuyển màn hình sang standby, pc_status, emoji hoặc diag.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "mode", "string", "Chế độ: standby, pc_status, emoji hoặc diag.");

  addTool(tools, "self.audio.set_volume", "Đặt âm lượng phát từ 0 đến 100 phần trăm.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "volume", "integer", "Âm lượng từ 0 đến 100 phần trăm.");

  addTool(tools, "self.screen.set_emoji_style", "Đổi phong cách biểu cảm: neon, cute_girl, doi_mat hoặc gome.");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "style", "string", "neon, cute_girl, doi_mat hoặc gome.");

  addTool(tools, "self.power.set_profile", "Đặt cấu hình quản lý nguồn: performance (tối đa), balanced (cân bằng) hoặc saver (tiết kiệm pin).");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "profile", "string", "performance, balanced hoặc saver.");
  addProperty(tool, "auto_switch", "boolean", "Tự động chuyển cấu hình theo trạng thái sạc và pin (tùy chọn).");

  addTool(tools, "self.power.get_status", "Lấy thông tin quản lý nguồn: cấu hình, tần số CPU, pin, trạng thái sạc và bão hòa.");

  addTool(tools, "get_daily_news", "Lấy danh sách các bản tin tức mới nhất trong ngày từ báo điện tử để tóm tắt và đọc cho người dùng nghe (tin tức hôm nay, thời sự, điểm tin buổi sáng/tối, tin mới nhất).");
  tool = tools[tools.size() - 1].as<JsonObject>();
  addProperty(tool, "limit", "integer", "Số lượng tin tức muốn lấy (mặc định 5, từ 1 đến 5).");
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

  return {false, "Không tìm thấy công cụ MCP: " + name};
}

} // namespace McpTools
