#include "services/PeripheralControlService.h"

#include "drivers/CobLedDriver.h"
#include "drivers/DisplayDriver.h"
#include "drivers/MotorDriver.h"
#include "drivers/AudioDriver.h"
#include "drivers/SensorDriver.h"
#include "tests/TestDisplay.h"

namespace PeripheralControlService {

PeripheralControlResult setWindmill(int mode, int speedPct) {
  if (mode < 0 || mode > 4 || speedPct < 0 || speedPct > 100) {
    return {false, "Chế độ hoặc tốc độ cối xay không hợp lệ."};
  }
  MotorDriver::setMode((uint8_t)mode);
  MotorDriver::setMotorSpeed((uint8_t)speedPct);
  if (mode == 0 || speedPct == 0) {
    return {true, "Đã tắt motor cối xay gió."};
  }
  return {true, "Đã đặt cối xay gió ở tốc độ " + String(speedPct) + "% (" + MotorDriver::getModeName() + ")."};
}

PeripheralControlResult setEdisonLights(int side, bool enabled, int brightnessPct, bool breathing) {
  if (side < 0 || side > 2 || brightnessPct < 0 || brightnessPct > 100) {
    return {false, "Vị trí hoặc độ sáng đèn Edison không hợp lệ."};
  }
  uint8_t brightness = enabled ? (uint8_t)brightnessPct : 0;
  if (side == 1) MotorDriver::setEdisonLeftBrightness(brightness, enabled && breathing);
  else if (side == 2) MotorDriver::setEdisonRightBrightness(brightness, enabled && breathing);
  else MotorDriver::setEdisonBrightness(brightness);

  return {true, String(enabled ? "Đã đặt đèn Edison " : "Đã tắt đèn Edison ") +
                (side == 1 ? "bên trái" : side == 2 ? "bên phải" : "hai bên") +
                (enabled ? " ở mức " + String(brightnessPct) + "%" : ".")};
}

PeripheralControlResult setCobLeds(bool enabled, bool nextMode) {
  if (nextMode) {
    CobLeds::nextMode();
    return {true, "Đã chuyển hiệu ứng đèn COB."};
  }
  if (enabled) {
    CobLeds::turnOn();
    MotorDriver::setFlowingLeds(true);
    return {true, "Đã bật đèn COB."};
  }
  CobLeds::turnOff();
  MotorDriver::setFlowingLeds(false);
  return {true, "Đã tắt đèn COB."};
}

PeripheralControlResult getTelemetry() {
  float temp = SensorDriver::getTemperatureC();
  float hum = SensorDriver::getHumidityPct();
  int bat = SensorDriver::getBatteryPercentage();
  float vbat = SensorDriver::getBatteryVoltage();
  bool charging = SensorDriver::isCharging();
  uint8_t speed = MotorDriver::getMotorSpeed();
  String message = "Nhiệt độ phòng: " + String(temp, 1) + "°C, độ ẩm: " + String(hum, 0) +
                   "%, pin: " + String(bat) + "% (" + String(vbat, 2) + "V" +
                   (charging ? ", đang sạc" : "") + "), tốc độ cối xay: " + String(speed) + "%.";
  return {true, message};
}

PeripheralControlResult switchScreen(const String& requestedMode) {
  String mode = requestedMode;
  mode.toLowerCase();
  if (mode == "standby" || mode == "home" || mode == "cho") {
    TestDisplay::setScreenMode("standby");
    return {true, "Đã chuyển về màn hình chờ."};
  }
  if (mode == "pc_status" || mode == "pc" || mode == "hud") {
    TestDisplay::setScreenMode("pchud");
    return {true, "Đã chuyển sang màn hình PC HUD."};
  }
  if (mode == "emoji" || mode == "ai") {
    TestDisplay::setScreenMode("emoji");
    return {true, "Đã chuyển sang màn hình biểu cảm."};
  }
  if (mode == "diag" || mode == "sensors" || mode == "song_am") {
    TestDisplay::setScreenMode("diag");
    return {true, "Đã chuyển sang màn hình chẩn đoán."};
  }
  return {false, "Chế độ màn hình không được hỗ trợ: " + requestedMode};
}

PeripheralControlResult setAudioVolume(int volumePct) {
  if (volumePct < 0 || volumePct > 100) return {false, "Âm lượng phải nằm trong khoảng 0 đến 100%."};
  AudioDriver::setSpeakerVolumePct(volumePct);
  AudioDriver::setXiaoZhiVolumePct(volumePct);
  return {true, "Đã đặt âm lượng loa: " + String(volumePct) + "%."};
}

PeripheralControlResult setEmojiStyle(const String& requestedStyle) {
  String style = requestedStyle;
  style.toLowerCase();
  if (style == "neon" || style == "default") {
    TestDisplay::setEmojiStyle(0);
    return {true, "Đã chọn phong cách Mắt Neon."};
  }
  if (style == "cute_girl" || style == "chibi" || style == "girl" || style == "co_be") {
    TestDisplay::setEmojiStyle(1);
    return {true, "Đã chọn phong cách Cô Bé Chibi."};
  }
  if (style == "doi_mat" || style == "robot" || style == "eyes" || style == "dong") {
    TestDisplay::setEmojiStyle(2);
    return {true, "Đã chọn phong cách Đôi Mắt Robot."};
  }
  if (style == "gome" || style == "meo" || style == "cat") {
    TestDisplay::setEmojiStyle(3);
    return {true, "Đã chọn phong cách Mèo Gome."};
  }
  return {false, "Phong cách biểu cảm không được hỗ trợ: " + requestedStyle};
}

} // namespace PeripheralControlService
