#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// Dinh nghia Pin SPI TFT ILI9341
#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  14
#define TFT_MOSI 11
#define TFT_SCK  12

// Dinh nghia Pin Nut bam
#define BTN_LEFT  4
#define BTN_RIGHT 5

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);
WebServer server(80);

// Enum quan ly Page Màn hinh
enum DisplayPage { PAGE_STANDBY, PAGE_PC_HUD, PAGE_EMOJI };
DisplayPage currentPage = PAGE_EMOJI;

// Enum quan ly Trang thai Bieu cam Emoji
enum EmojiState { IDLE, LOOK_LEFT, LOOK_RIGHT, BLINK, ANGRY, CONFUSED };
EmojiState currentEmoji = IDLE;

// Bien ho tro nut bam & debounce
bool lastBtnLeft = HIGH;
bool lastBtnRight = HIGH;
unsigned long lastDebounce = 0;

// Dynamic HTML Web Portal
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8"><title>Cyberpunk Assistant Control</title>
  <style>
    body { font-family: Arial, sans-serif; background: #121212; color: #fff; text-align: center; padding: 20px; }
    button { background: #00adb5; color: white; border: none; padding: 10px 20px; margin: 5px; border-radius: 5px; cursor: pointer; }
    button:hover { background: #393e46; }
    .card { background: #1e1e1e; padding: 15px; margin: 15px auto; max-width: 400px; border-radius: 10px; }
  </style>
</head>
<body>
  <h2>Cyberpunk Desk Assistant Portal</h2>
  <div class="card">
    <h3>Upload Ảnh Nền (Simulated)</h3>
    <form action="/upload" method="POST" enctype="multipart/form-data">
      <input type="file" name="upload" accept="image/*"><br><br>
      <input type="submit" value="Tải lên & Nén (200-300KB)">
    </form>
  </div>
  <div class="card">
    <h3>Đổi Biểu Cảm Emoji</h3>
    <button onclick="fetch('/emoji?type=0')">Idle</button>
    <button onclick="fetch('/emoji?type=1')">Liếc Trái</button>
    <button onclick="fetch('/emoji?type=2')">Liếc Phải</button>
    <button onclick="fetch('/emoji?type=3')">Nháy Mắt</button>
    <button onclick="fetch('/emoji?type=4')">Giận</button>
    <button onclick="fetch('/emoji?type=5')">Bối Rối (@@)</button>
  </div>
</body>
</html>
)rawliteral";

// --- HOÀN THIỆN ĐỒ HỌA EMOJI ---
void drawEmoji(EmojiState state) {
  tft.fillScreen(ILI9341_BLACK);
  int eyeR = 35;
  int leftEyeX = 90, rightEyeX = 230, eyeY = 120;

  switch (state) {
    case IDLE:
      tft.fillCircle(leftEyeX, eyeY, eyeR, ILI9341_CYAN);
      tft.fillCircle(rightEyeX, eyeY, eyeR, ILI9341_CYAN);
      break;

    case LOOK_LEFT:
      tft.fillCircle(leftEyeX - 15, eyeY, eyeR, ILI9341_CYAN);
      tft.fillCircle(rightEyeX - 15, eyeY, eyeR, ILI9341_CYAN);
      break;

    case LOOK_RIGHT:
      tft.fillCircle(leftEyeX + 15, eyeY, eyeR, ILI9341_CYAN);
      tft.fillCircle(rightEyeX + 15, eyeY, eyeR, ILI9341_CYAN);
      break;

    case BLINK:
      tft.fillRect(leftEyeX - eyeR, eyeY - 5, eyeR * 2, 10, ILI9341_CYAN);
      tft.fillRect(rightEyeX - eyeR, eyeY - 5, eyeR * 2, 10, ILI9341_CYAN);
      break;

    case ANGRY:
      tft.fillCircle(leftEyeX, eyeY, eyeR, ILI9341_RED);
      tft.fillCircle(rightEyeX, eyeY, eyeR, ILI9341_RED);
      // Lông mày xếch
      tft.fillTriangle(leftEyeX - 40, eyeY - 45, leftEyeX + 30, eyeY - 20, leftEyeX - 40, eyeY - 20, ILI9341_BLACK);
      tft.fillTriangle(rightEyeX + 40, eyeY - 45, rightEyeX - 30, eyeY - 20, rightEyeX + 40, eyeY - 20, ILI9341_BLACK);
      break;

    case CONFUSED:
      tft.setTextColor(ILI9341_YELLOW);
      tft.setTextSize(6);
      tft.setCursor(leftEyeX - 25, eyeY - 20);
      tft.print("@");
      tft.setCursor(rightEyeX - 25, eyeY - 20);
      tft.print("@");
      break;
  }
}

// --- TRANG MÀN HÌNH CHỜ (STANDBY) ---
void drawStandbyPage() {
  tft.fillScreen(ILI9341_NAVY);
  tft.drawRect(5, 5, 310, 230, ILI9341_WHITE);
  
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(5);
  tft.setCursor(80, 70);
  tft.print("15:30"); // Đồng hồ giả lập

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(65, 140);
  tft.print("TEMP: 28.5C | HUM: 65%");
  tft.setCursor(75, 180);
  tft.print("[ Standby Wallpaper ]");
}

// --- TRANG TRẠNG THÁI PC (PC HUD) ---
void drawPCHudPage() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_MAGENTA);
  tft.setTextSize(3);
  tft.setCursor(50, 15);
  tft.print("PC TELEMETRY");

  // Thanh CPU
  tft.drawRect(20, 60, 200, 20, ILI9341_WHITE);
  tft.fillRect(20, 60, 140, 20, ILI9341_CYAN); // 70%
  tft.setCursor(230, 60); tft.setTextSize(2); tft.setTextColor(ILI9341_CYAN); tft.print("CPU 70%");

  // Thanh GPU
  tft.drawRect(20, 110, 200, 20, ILI9341_WHITE);
  tft.fillRect(20, 110, 110, 20, ILI9341_GREEN); // 55%
  tft.setCursor(230, 110); tft.setTextColor(ILI9341_GREEN); tft.print("GPU 55%");

  // Thanh RAM
  tft.drawRect(20, 160, 200, 20, ILI9341_WHITE);
  tft.fillRect(20, 160, 160, 20, ILI9341_YELLOW); // 80%
  tft.setCursor(230, 160); tft.setTextColor(ILI9341_YELLOW); tft.print("RAM 80%");
}

// Cập nhật hiển thị theo Page hiện tại
void renderPage() {
  if (currentPage == PAGE_EMOJI) {
    drawEmoji(currentEmoji);
  } else if (currentPage == PAGE_STANDBY) {
    drawStandbyPage();
  } else if (currentPage == PAGE_PC_HUD) {
    drawPCHudPage();
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  tft.begin();
  tft.setRotation(1); // Màn hình nằm ngang 320x240
  
  // Kết nối WiFi Wokwi-GUEST
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.print("Connecting WiFi...");

  WiFi.begin("Wokwi-GUEST", "");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  // Khởi tạo các Route WebServer
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", index_html); });
  
  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/plain", "Upload OK! Reduced image size to ~250KB (Simulated)");
  });

  server.on("/emoji", HTTP_GET, []() {
    if (server.hasArg("type")) {
      currentEmoji = (EmojiState)server.arg("type").toInt();
      currentPage = PAGE_EMOJI;
      renderPage();
      server.send(200, "text/plain", "Emoji Changed");
    }
  });

  server.begin();
  renderPage();
}

void loop() {
  server.handleClient();

  // Xử lý đọc nút bấm có Debounce
  if (millis() - lastDebounce > 200) {
    bool btnL = digitalRead(BTN_LEFT);
    bool btnR = digitalRead(BTN_RIGHT);

    // Nút RIGHT: Chuyển đổi Trang (Standby -> PC HUD -> Emoji)
    if (btnR == LOW && lastBtnRight == HIGH) {
      currentPage = (DisplayPage)((currentPage + 1) % 3);
      renderPage();
      lastDebounce = millis();
    }

    // Nút LEFT: Nếu đang ở Page Emoji thì đổi liên tục 6 biểu cảm
    if (btnL == LOW && lastBtnLeft == HIGH) {
      if (currentPage == PAGE_EMOJI) {
        currentEmoji = (EmojiState)((currentEmoji + 1) % 6);
      } else {
        currentPage = PAGE_EMOJI; // Chuyển nhanh về Emoji
      }
      renderPage();
      lastDebounce = millis();
    }

    lastBtnLeft = btnL;
    lastBtnRight = btnR;
  }
}