#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

class ImageManager {
public:
  static void ensureSdFolders();
  static int countStoredImages();
  static void listImages(JsonDocument& doc);
  static void handleUpload(HTTPUpload& upload, bool& uploadAllowed, bool forceSdCard = false);
  static bool deleteImage(const String& filename);
  static bool selectActiveImage(const String& filename);
  static String getActiveImage();
  static String getRgb565CompanionPath(const String& imagePath);
};


