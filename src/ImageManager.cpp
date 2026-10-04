#include "ImageManager.h"
#include "Config.h"
#include "tests/TestSDCard.h"
#include <LittleFS.h>
#include <SD.h>
#include <Preferences.h>

static File uploadFile;
static Preferences imgPrefs;
static String currentBatchStamp = "";
static String lastUploadedPath = "";

void ImageManager::ensureSdFolders() {
  if (!TestSDCard::isMounted()) return;
  if (!SD.exists("/sd_images")) {
    SD.mkdir("/sd_images");
  }
  if (!SD.exists("/sd_rgb565")) {
    SD.mkdir("/sd_rgb565");
  }
}

String ImageManager::getRgb565CompanionPath(const String& imagePath) {
  String p = imagePath;
  if (p.startsWith("sd:")) p = p.substring(3);
  if (p.endsWith(".rgb565")) return p;

  int slash = p.lastIndexOf('/');
  String base = (slash >= 0) ? p.substring(slash + 1) : p;
  int dot = base.lastIndexOf('.');
  if (dot > 0) base = base.substring(0, dot);

  String candidate = "/sd_rgb565/" + base + ".rgb565";
  if (TestSDCard::isMounted() && SD.exists(candidate)) {
    return candidate;
  }
  return "";
}

int ImageManager::countStoredImages() {
  int count = 0;
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String filename = String(file.name());
    if (filename.endsWith(".jpg") || filename.endsWith(".jpeg") || filename.endsWith(".png") || filename.endsWith(".bmp")) {
      count++;
    }
    file = root.openNextFile();
  }
  return count;
}

void ImageManager::listImages(JsonDocument& doc) {
  JsonArray array = doc["images"].to<JsonArray>();
  int fsCount = 0;
  int sdCount = 0;

  // 1. Quét thư mục /sd_images trên thẻ nhớ Micro SD trước (ưu tiên tốc độ 0ms RGB565)
  if (TestSDCard::isMounted()) {
    ensureSdFolders();
    File sdDir = SD.open("/sd_images");
    if (sdDir && sdDir.isDirectory()) {
      File f = sdDir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String fname = String(f.name());
          int sl = fname.lastIndexOf('/');
          if (sl >= 0) fname = fname.substring(sl + 1);
          String low = fname;
          low.toLowerCase();
          if (low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp")) {
            String fullSdPath = "/sd_images/" + fname;
            String companion = getRgb565CompanionPath(fullSdPath);
            JsonObject obj = array.add<JsonObject>();
            obj["name"] = fullSdPath; // Đường dẫn chuẩn HTTP /sd_images/... để mọi thẻ <img> hiển thị trực tiếp!
            obj["url"] = "/api/sd/file?path=" + fullSdPath;
            obj["size"] = (uint32_t)f.size();
            obj["storage"] = "SD";
            obj["has_rgb565"] = (companion.length() > 0);
            sdCount++;
          }
        }
        f.close();
        f = sdDir.openNextFile();
      }
      sdDir.close();
    }
  }

  // 2. Quét bộ nhớ nội LittleFS
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String filename = String(file.name());
    if (filename.endsWith(".jpg") || filename.endsWith(".jpeg") || filename.endsWith(".png") || filename.endsWith(".bmp")) {
      String normName = filename.startsWith("/") ? filename : "/" + filename;
      JsonObject obj = array.add<JsonObject>();
      obj["name"] = normName;
      obj["url"] = normName;
      obj["size"] = (uint32_t)file.size();
      obj["storage"] = "LittleFS";
      obj["has_rgb565"] = (getRgb565CompanionPath(normName).length() > 0);
      fsCount++;
    }
    file = root.openNextFile();
  }

  doc["count"] = fsCount;
  doc["sd_count"] = sdCount;
  doc["total_count"] = fsCount + sdCount;
  doc["max"] = MAX_IMAGE_COUNT;
  doc["sd_mounted"] = TestSDCard::isMounted();
  doc["sd_free_mb"] = TestSDCard::getFreeMB();
  doc["sd_total_mb"] = TestSDCard::getTotalMB();
}

void ImageManager::handleUpload(HTTPUpload& upload, bool& uploadAllowed, bool forceSdCard) {
  String origName = upload.filename;
  String lowName = origName;
  lowName.toLowerCase();
  bool isRgb565 = lowName.endsWith(".rgb565");
  bool isPng = lowName.endsWith(".png");
  bool useSd = TestSDCard::isMounted() && (forceSdCard || isRgb565 || isPng || origName.startsWith("sd_"));

  if (upload.status == UPLOAD_FILE_START) {
    if (!useSd && countStoredImages() >= MAX_IMAGE_COUNT) {
      Serial.println("❌ Đã đạt giới hạn 15 ảnh LittleFS, từ chối lưu ảnh mới!");
      uploadAllowed = false;
      return;
    }
    uploadAllowed = true;

    // Trích xuất tên gốc hoặc tạo mã batch đồng bộ giữa file .png và .rgb565
    int slashIdx = origName.lastIndexOf('/');
    String cleanName = (slashIdx >= 0) ? origName.substring(slashIdx + 1) : origName;
    int extIdx = cleanName.lastIndexOf('.');
    String baseName = (extIdx > 0) ? cleanName.substring(0, extIdx) : cleanName;
    String ext = (extIdx != -1) ? cleanName.substring(extIdx) : ".png";
    ext.toLowerCase();

    if (baseName.length() == 0 || baseName == "wallpaper") {
      if (currentBatchStamp.length() == 0) {
        currentBatchStamp = "img_" + String(millis() % 100000);
      }
      baseName = currentBatchStamp;
    }

    if (useSd) {
      ensureSdFolders();
      String sdTarget = isRgb565 ? ("/sd_rgb565/" + baseName + ".rgb565")
                                 : ("/sd_images/" + baseName + ext);
      Serial.printf("💾 [SD UPLOAD] Đang ghi vào Thẻ nhớ SD: %s\n", sdTarget.c_str());
      if (SD.exists(sdTarget)) SD.remove(sdTarget);
      uploadFile = SD.open(sdTarget, FILE_WRITE);
      if (!isRgb565) {
        lastUploadedPath = sdTarget;
      } else if (lastUploadedPath.length() == 0) {
        lastUploadedPath = "/sd_images/" + baseName + ".png";
      }
    } else {
      String newFilename = "/" + baseName + ext;
      lastUploadedPath = newFilename;
      Serial.printf("📸 [LittleFS UPLOAD] Lưu ảnh mới: %s\n", newFilename.c_str());
      if (LittleFS.exists(newFilename)) LittleFS.remove(newFilename);
      uploadFile = LittleFS.open(newFilename, FILE_WRITE);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadAllowed && uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadAllowed && uploadFile) {
      uploadFile.close();
      if (lastUploadedPath.length() > 0) {
        selectActiveImage(lastUploadedPath);
      }
      if (!isRgb565) {
        currentBatchStamp = "";
      }
      Serial.printf("✅ Đã ghi xong tệp (%u bytes)! Active=%s\n",
                    (unsigned)upload.totalSize, lastUploadedPath.c_str());
    }
  }
}

bool ImageManager::deleteImage(const String& filename) {
  String p = filename;
  if (p.startsWith("sd:")) p = p.substring(3);
  if (!p.startsWith("/")) p = "/" + p;

  if (p.startsWith("/sd_images/") || p.startsWith("/sd_rgb565/")) {
    if (!TestSDCard::isMounted()) return false;
    String companion = getRgb565CompanionPath(p);
    if (companion.length() > 0 && SD.exists(companion)) {
      SD.remove(companion);
    }
    if (SD.exists(p)) {
      return SD.remove(p);
    }
    return false;
  }

  String companion = getRgb565CompanionPath(p);
  if (companion.length() > 0 && TestSDCard::isMounted() && SD.exists(companion)) {
    SD.remove(companion);
  }
  if (LittleFS.exists(p)) {
    return LittleFS.remove(p);
  }
  return false;
}

bool ImageManager::selectActiveImage(const String& filename) {
  String path = filename;
  if (path.startsWith("sd:")) path = path.substring(3);
  if (!path.startsWith("/")) path = "/" + path;

  imgPrefs.begin("sys", false);
  imgPrefs.putString("active_img", path);
  imgPrefs.end();
  return true;
}

String ImageManager::getActiveImage() {
  imgPrefs.begin("sys", true);
  String active = imgPrefs.getString("active_img", "/wallpaper.jpg");
  imgPrefs.end();
  if (active.startsWith("sd:")) active = active.substring(3);
  return active;
}


