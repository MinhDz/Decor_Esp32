#pragma once

#include <ArduinoJson.h>
#include "services/PeripheralControlService.h"

namespace McpTools {
  void appendDefinitions(JsonArray tools);
  PeripheralControlResult execute(const String& name, JsonObjectConst arguments);
}
