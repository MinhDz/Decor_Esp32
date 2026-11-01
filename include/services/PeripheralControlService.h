#pragma once

#include <Arduino.h>

struct PeripheralControlResult {
  bool success;
  String message;
};

namespace PeripheralControlService {
  PeripheralControlResult setWindmill(int mode, int speedPct);
  PeripheralControlResult setEdisonLights(int side, bool enabled, int brightnessPct, bool breathing);
  PeripheralControlResult setCobLeds(bool enabled, bool nextMode);
  PeripheralControlResult getTelemetry();
  PeripheralControlResult switchScreen(const String& mode);
  PeripheralControlResult setAudioVolume(int volumePct);
  PeripheralControlResult setEmojiStyle(const String& style);
  PeripheralControlResult setPowerProfile(const String& profile, bool autoSwitch = false);
  PeripheralControlResult getPowerStatus();
}
