#include "tests/TestSDCard.h"
#include "tests/TestDisplay.h"
#include "Config.h"
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <driver/gpio.h>

namespace TestSDCard {

  static SPIClass* sdSpi = nullptr;
  static bool sdMounted = false;
  static uint32_t detectedSizeMB = 0;
  static uint32_t detectedTotalMB = 0;
  static uint32_t detectedUsedMB = 0;
  static int rootFileCount = 0;
  static String cardTypeStr = "NONE";
  static String statusSummary = "DANG DO MACH & QUET CMD0...";
  static String cmd0RawHex = "FF FF FF FF FF FF FF FF";
  static uint8_t cmd0R1Byte = 0xFF;
  static String matchedPinDesc = "D5:42(SCK) D6:41(MISO) D7:40(MOSI) D8:39(CS)";
  static String firstFilesStr = "(Chua co du lieu)";
  static uint32_t scanCounter = 0;
  static unsigned long lastAutoProbeMs = 0;

  // Bảng thông số điện học trực tiếp của 4 chân Shield SD
  static SdPinDiag pinDiags[4] = {
    { 42, "D5", "SCK ", 1, 0, 0, 3.30f, "Checking..." },
    { 41, "D6", "MISO", 1, 0, 1, 3.30f, "Checking..." },
    { 40, "D7", "MOSI", 1, 0, 1, 3.30f, "Checking..." },
    { 39, "D8", "CS  ", 1, 0, 1, 3.30f, "Checking..." }
  };

  static void detachJtagFromSdPins() {
    const uint8_t pins[4] = { 42, 41, 40, 39 };
    for (int i = 0; i < 4; i++) {
      gpio_reset_pin((gpio_num_t)pins[i]);
    }
  }

  static void measureAllPinsElectrical() {
    detachJtagFromSdPins();

    for (int i = 0; i < 4; i++) {
      pinMode(pinDiags[i].gpio, INPUT);
    }
    delay(2);

    for (int i = 0; i < 4; i++) {
      uint8_t p = pinDiags[i].gpio;

      pinMode(p, INPUT_PULLUP);
      delayMicroseconds(300);
      uint8_t pu = digitalRead(p);

      pinMode(p, INPUT_PULLDOWN);
      delayMicroseconds(300);
      uint8_t pd = digitalRead(p);

      pinMode(p, INPUT_PULLUP);

      pinDiags[i].valPullUp   = pu;
      pinDiags[i].valPullDown = pd;

      if (pu == 1 && pd == 1) {
        pinDiags[i].estVoltage = 3.28f;
        pinDiags[i].stateDesc  = "3.28V (Co tro PullUp)";
      } else if (pu == 0 && pd == 0) {
        pinDiags[i].estVoltage = 0.00f;
        pinDiags[i].stateDesc  = "0.00V (Keo GND/Thap!)";
      } else if (pu == 1 && pd == 0) {
        pinDiags[i].estVoltage = 3.26f;
        pinDiags[i].stateDesc  = "3.26V (OK voi PU noi)";
      } else {
        pinDiags[i].estVoltage = 1.65f;
        pinDiags[i].stateDesc  = "1.65V (Dao dong)";
      }
    }
  }

  static uint8_t softSpiTransferByte(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t dataOut) {
    uint8_t dataIn = 0;
    for (int bit = 7; bit >= 0; bit--) {
      digitalWrite(mosi, (dataOut & (1 << bit)) ? HIGH : LOW);
      delayMicroseconds(3);
      digitalWrite(sck, HIGH);
      delayMicroseconds(3);
      if (digitalRead(miso)) {
        dataIn |= (1 << bit);
      }
      digitalWrite(sck, LOW);
      delayMicroseconds(2);
    }
    return dataIn;
  }

  static uint8_t probeRawCmd0(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t cs, uint8_t outRxBuf[8]) {
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
    pinMode(sck, OUTPUT);
    digitalWrite(sck, LOW);
    pinMode(mosi, OUTPUT);
    digitalWrite(mosi, HIGH);
    pinMode(miso, INPUT_PULLUP);
    delay(2);

    for (int i = 0; i < 10; i++) {
      softSpiTransferByte(sck, miso, mosi, 0xFF);
    }

    digitalWrite(cs, LOW);
    delayMicroseconds(20);
    softSpiTransferByte(sck, miso, mosi, 0xFF);

    const uint8_t cmd0[6] = { 0x40, 0x00, 0x00, 0x00, 0x00, 0x95 };
    for (int i = 0; i < 6; i++) {
      softSpiTransferByte(sck, miso, mosi, cmd0[i]);
    }

    uint8_t bestR1 = 0xFF;
    for (int i = 0; i < 8; i++) {
      uint8_t r = softSpiTransferByte(sck, miso, mosi, 0xFF);
      if (outRxBuf) outRxBuf[i] = r;
      if (bestR1 == 0xFF && r != 0xFF) {
        bestR1 = r;
      }
      if (r == 0x01) {
        bestR1 = 0x01;
      }
    }

    digitalWrite(cs, HIGH);
    softSpiTransferByte(sck, miso, mosi, 0xFF);

    for (int i = 0; i < 4; i++) {
      pinDiags[i].valActiveSpi = digitalRead(pinDiags[i].gpio);
    }

    return bestR1;
  }

  static void refreshSdCapacityAndFiles() {
    if (!sdMounted) return;
    detectedSizeMB  = (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));
    detectedTotalMB = (uint32_t)(SD.totalBytes() / (1024ULL * 1024ULL));
    if (detectedTotalMB == 0) detectedTotalMB = detectedSizeMB;
    detectedUsedMB  = (uint32_t)(SD.usedBytes() / (1024ULL * 1024ULL));

    rootFileCount = 0;
    firstFilesStr = "";
    File root = SD.open("/");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        if (!file.isDirectory()) {
          rootFileCount++;
          if (firstFilesStr.length() < 30) {
            if (firstFilesStr.length() > 0) firstFilesStr += ", ";
            String fn = String(file.name());
            if (fn.startsWith("/")) fn = fn.substring(1);
            firstFilesStr += fn;
          }
        }
        file = root.openNextFile();
      }
      root.close();
    }
    if (firstFilesStr.length() == 0) firstFilesStr = "(The trong / 0 file)";
  }

  static bool tryMountFatFilesystem(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t cs) {
    SD.end();
    if (sdSpi) {
      sdSpi->end();
    } else {
      sdSpi = new SPIClass(HSPI);
    }

    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
    pinMode(miso, INPUT_PULLUP);
    pinMode(mosi, INPUT_PULLUP);
    pinMode(sck, OUTPUT);
    delay(5);

    sdSpi->begin(sck, miso, mosi, cs);
    delay(5);

    if (!SD.begin(cs, *sdSpi, 1000000)) {
      SD.end();
      if (!SD.begin(cs, *sdSpi, 400000)) {
        return false;
      }
    }

    uint8_t ct = SD.cardType();
    if (ct == CARD_NONE) {
      SD.end();
      return false;
    }

    cardTypeStr = (ct == CARD_MMC)  ? "MMC" :
                  (ct == CARD_SD)   ? "SDSC" :
                  (ct == CARD_SDHC) ? "SDHC/XC" : "SD";

    sdMounted = true;

    File wf = SD.open("/sd_test_ok.txt", FILE_WRITE);
    if (wf) {
      wf.printf("ESP32-S3 SD Shield OK! Size=%uMB\n", (unsigned)(SD.cardSize() / (1024ULL * 1024ULL)));
      wf.close();
    }

    refreshSdCapacityAndFiles();
    return true;
  }

  void runFullElectricalAndSpiProbe() {
    scanCounter++;

    if (sdMounted && SD.cardType() != CARD_NONE) {
      for (int i = 0; i < 4; i++) {
        pinDiags[i].valActiveSpi = digitalRead(pinDiags[i].gpio);
      }
      refreshSdCapacityAndFiles();
      return;
    }

    sdMounted = false;
    measureAllPinsElectrical();

    uint8_t rxBytes[8] = {0};
    uint8_t r1 = probeRawCmd0(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS, rxBytes);
    cmd0R1Byte = r1;

    char hexBuf[32];
    snprintf(hexBuf, sizeof(hexBuf), "%02X %02X %02X %02X %02X %02X %02X %02X",
             rxBytes[0], rxBytes[1], rxBytes[2], rxBytes[3],
             rxBytes[4], rxBytes[5], rxBytes[6], rxBytes[7]);
    cmd0RawHex = String(hexBuf);

    uint8_t winSck  = PIN_SD_SCK;
    uint8_t winMiso = PIN_SD_MISO;
    uint8_t winMosi = PIN_SD_MOSI;
    uint8_t winCs   = PIN_SD_CS;
    matchedPinDesc  = "D5:42(SCK) D6:41(MISO) D7:40(MOSI) D8:39(CS)";

    if (r1 != 0x01) {
      const uint8_t pList[4] = { 42, 41, 40, 39 };
      bool foundAlt = false;
      for (int a = 0; a < 4 && !foundAlt; a++) {
        for (int b = 0; b < 4 && !foundAlt; b++) {
          if (b == a) continue;
          for (int c = 0; c < 4 && !foundAlt; c++) {
            if (c == a || c == b) continue;
            for (int d = 0; d < 4 && !foundAlt; d++) {
              if (d == a || d == b || d == c) continue;
              uint8_t testRx[8];
              uint8_t testR1 = probeRawCmd0(pList[a], pList[b], pList[c], pList[d], testRx);
              if (testR1 == 0x01) {
                winSck  = pList[a];
                winMiso = pList[b];
                winMosi = pList[c];
                winCs   = pList[d];
                cmd0R1Byte = 0x01;
                snprintf(hexBuf, sizeof(hexBuf), "%02X %02X %02X %02X %02X %02X %02X %02X",
                         testRx[0], testRx[1], testRx[2], testRx[3],
                         testRx[4], testRx[5], testRx[6], testRx[7]);
                cmd0RawHex = String(hexBuf);
                char mapBuf[48];
                snprintf(mapBuf, sizeof(mapBuf), "AUTO-DETECT: SCK=%d MISO=%d MOSI=%d CS=%d",
                         winSck, winMiso, winMosi, winCs);
                matchedPinDesc = String(mapBuf);
                foundAlt = true;
              }
            }
          }
        }
      }
    }

    if (tryMountFatFilesystem(winSck, winMiso, winMosi, winCs)) {
      sdMounted = true;
      char sumBuf[48];
      snprintf(sumBuf, sizeof(sumBuf), "OK %s %uMB (%d file)",
               cardTypeStr.c_str(), (unsigned)detectedSizeMB, rootFileCount);
      statusSummary = String(sumBuf);
    } else {
      sdMounted = false;
      if (cmd0R1Byte == 0x01) {
        statusSummary = "CMD0 OK (0x01)! Loi FAT32";
      } else if (cmd0R1Byte == 0x00) {
        statusSummary = "MISO=0x00 (Bi keo GND/Mat 3V3)";
      } else {
        statusSummary = "MISO=0xFF (The chua tra loi CMD0)";
      }
    }

    Serial.printf("\n================ [SD SHIELD PROBE #%lu] ================\n", (unsigned long)scanCounter);
    for (int i = 0; i < 4; i++) {
      Serial.printf("  Pin %s -> GPIO %2d [%-4s] | PU:%d PD:%d SPI:%d | ~%.2fV | %s\n",
                    pinDiags[i].shieldPin, pinDiags[i].gpio, pinDiags[i].spiRole,
                    pinDiags[i].valPullUp, pinDiags[i].valPullDown, pinDiags[i].valActiveSpi,
                    pinDiags[i].estVoltage, pinDiags[i].stateDesc);
    }
    Serial.printf("  CMD0 (0x40 00 00 00 00 95) -> MISO RX: [%s] (R1=0x%02X)\n",
                  cmd0RawHex.c_str(), cmd0R1Byte);
    Serial.printf("  Map: %s\n", matchedPinDesc.c_str());
    Serial.printf("  Status: %s\n", statusSummary.c_str());
    Serial.println("=======================================================");
  }

  // Liệt kê danh sách tệp trong Thẻ nhớ SD (hoặc LittleFS dự phòng nếu chưa cắm thẻ) cho App Bộ Nhớ
  int listSdFiles(String outNames[], size_t outSizes[], bool outIsDir[], int maxItems) {
    int count = 0;
    if (sdMounted) {
      // 1. Ưu tiên liệt kê các file ảnh trong thư mục /sd_images trên Thẻ nhớ SD để bấm OK xem trước ngay!
      if (SD.exists("/sd_images")) {
        File imgDir = SD.open("/sd_images");
        if (imgDir && imgDir.isDirectory()) {
          File f = imgDir.openNextFile();
          while (f && count < maxItems) {
            if (!f.isDirectory()) {
              String fn = String(f.name());
              int sl = fn.lastIndexOf('/');
              if (sl >= 0) fn = fn.substring(sl + 1);
              outNames[count] = "/sd_images/" + fn;
              outSizes[count] = f.size();
              outIsDir[count] = false;
              count++;
            }
            f.close();
            f = imgDir.openNextFile();
          }
          imgDir.close();
        }
      }

      // 2. Quét tiếp các tệp và thư mục ở gốc Thẻ nhớ SD ("/")
      File root = SD.open("/");
      if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f && count < maxItems) {
          String fn = String(f.name());
          if (!fn.startsWith("/")) fn = "/" + fn;
          if (fn != "/sd_images" && fn != "/sd_rgb565" && fn != "/System Volume Information") {
            outNames[count] = fn;
            outSizes[count] = f.size();
            outIsDir[count] = f.isDirectory();
            count++;
          }
          f.close();
          f = root.openNextFile();
        }
        root.close();
      }
    }
    // 3. Bổ sung các file ảnh/cấu hình trong bộ nhớ trong LittleFS (gắn tiền tố [FS])
    File fsRoot = LittleFS.open("/");
    if (fsRoot) {
      File f = fsRoot.openNextFile();
      while (f && count < maxItems) {
        String fn = String(f.name());
        if (!fn.startsWith("/")) fn = "/" + fn;
        outNames[count] = "[FS]" + fn;
        outSizes[count] = f.size();
        outIsDir[count] = false;
        count++;
        f = fsRoot.openNextFile();
      }
      fsRoot.close();
    }
    return count;
  }

  // Đọc nội dung văn bản (tối đa 220 ký tự) của 1 file trên Thẻ nhớ SD hoặc LittleFS
  String readSdFilePreview(const String& path) {
    File f;
    if (path.startsWith("[FS]")) {
      String realP = path.substring(4);
      f = LittleFS.open(realP, "r");
    } else if (sdMounted) {
      f = SD.open(path, "r");
    }
    if (!f) return "(Khong the mo file nay)";

    String content = "";
    while (f.available() && content.length() < 210) {
      char c = (char)f.read();
      if (c == '\r') continue;
      if (c == '\n') content += ' ';
      else if (c >= 32 && c <= 126) content += c;
    }
    f.close();
    if (content.length() == 0) content = "(File trong / Du lieu nhi phan)";
    return content;
  }

  // Chép file ảnh .jpg từ Thẻ nhớ SD sang LittleFS để đặt làm Hình nền Màn hình chờ
  bool copySdJpgToLittleFS(const String& sdPath) {
    if (sdPath.startsWith("[FS]")) return true; // Đã nằm sẵn trong LittleFS
    if (!sdMounted) return false;

    File src = SD.open(sdPath, FILE_READ);
    if (!src) return false;

    File dst = LittleFS.open(sdPath, "w");
    if (!dst) {
      src.close();
      return false;
    }

    uint8_t buf[512];
    while (src.available()) {
      size_t n = src.read(buf, sizeof(buf));
      if (n == 0) break;
      dst.write(buf, n);
    }
    dst.close();
    src.close();
    return true;
  }

  // Xóa sạch toàn bộ file trên thẻ nhớ (Format nhanh trên thẻ SD)
  bool formatSdCard() {
    if (!sdMounted) {
      runFullElectricalAndSpiProbe();
      if (!sdMounted) return false;
    }

    File root = SD.open("/");
    if (!root || !root.isDirectory()) return false;

    String toDelete[40];
    int delCount = 0;
    File f = root.openNextFile();
    while (f && delCount < 40) {
      String fn = String(f.name());
      if (!fn.startsWith("/")) fn = "/" + fn;
      if (!f.isDirectory()) {
        toDelete[delCount++] = fn;
      }
      f = root.openNextFile();
    }
    root.close();

    for (int i = 0; i < delCount; i++) {
      SD.remove(toDelete[i]);
    }

    // Tạo lại file đánh dấu đã Format sạch
    File wf = SD.open("/sd_formatted.txt", FILE_WRITE);
    if (wf) {
      wf.println("MicroSD Formatted by Tram Decor Symbian S40 OS!");
      wf.close();
    }

    refreshSdCapacityAndFiles();
    return true;
  }

  static bool sdInitialized = false;
  bool init() {
    if (sdInitialized) return sdMounted;
    sdInitialized = true;
    runFullElectricalAndSpiProbe();
    lastAutoProbeMs = millis();
    return sdMounted;
  }

  void loop() {
    unsigned long now = millis();
    // Chỉ tự động quét lặp lại mỗi 2 giây khi người dùng đang mở trực tiếp Màn hình Chẩn đoán SD (Mode 9)
    if (now - lastAutoProbeMs >= 2000) {
      lastAutoProbeMs = now;
      if (!sdMounted && TestDisplay::getScreenModeName() == "sd_test") {
        runFullElectricalAndSpiProbe();
      }
    }
  }

  bool isMounted() { return sdMounted; }
  uint32_t getCardSizeMB() { return detectedSizeMB; }
  uint32_t getTotalMB() { return detectedTotalMB > 0 ? detectedTotalMB : detectedSizeMB; }
  uint32_t getUsedMB() { return detectedUsedMB; }
  uint32_t getFreeMB() {
    uint32_t tot = getTotalMB();
    return (tot >= detectedUsedMB) ? (tot - detectedUsedMB) : 0;
  }
  int getFileCount() { return rootFileCount; }
  String getCardTypeName() { return cardTypeStr; }
  String getStatusSummary() { return statusSummary; }
  String getCmd0RawHexStr() { return cmd0RawHex; }
  uint8_t getCmd0R1Byte() { return cmd0R1Byte; }
  String getMatchedPinMapDesc() { return matchedPinDesc; }
  String getFirstFileNames() { return firstFilesStr; }
  uint32_t getScanCount() { return scanCounter; }
  const SdPinDiag* getPinDiags() { return pinDiags; }

  bool handleSerial(char cmd) {
    if (cmd == 'k' || cmd == 'K') {
      sdMounted = false;
      runFullElectricalAndSpiProbe();
      return true;
    }
    return false;
  }

}

