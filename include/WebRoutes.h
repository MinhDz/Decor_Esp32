#pragma once
#include <Arduino.h>
#include <WebServer.h>

class WebRoutes {
public:
  static void begin();
  static void handleClient();
  static WebServer& getServer();
};

