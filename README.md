# 🛸 SpaceOS v3.2.9 - Trạm Decor Bàn Làm Việc Thông Minh (ESP32-S3)
> **Dự án Nghệ Thuật Điêu Khắc Mạch Đồng Thủ Công (Freeform Circuit Sculpture) Tích Hợp Hệ Điều Hành Không Gian & Trợ Lý Ảo AI**

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Supported-orange.svg)](https://platformio.org/)
[![Framework](https://img.shields.io/badge/Framework-Arduino%20ESP32-blue.svg)](https://github.com/espressif/arduino-esp32)
[![Chip](https://img.shields.io/badge/MCU-ESP32--S3--WROOM--1%20N16R8-red.svg)](https://www.espressif.com/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![OTA Ready](https://img.shields.io/badge/OTA-Dual%20Partition%20Safe-blueviolet.svg)](#hệ-thống-cập-nhật-ota-từ-xa)

---

## 📖 Giới Thiệu Dự Án

**Decor_Esp32 (SpaceOS)** là dự án trạm để bàn không gian phong cách Cyberpunk, được chế tác thủ công hoàn toàn bằng kỹ thuật hàn điêu khắc khung dây đồng nguyên khối (**Freeform Copper Circuit Sculpture 1.2mm**). 

Hệ thống được điều khiển bởi vi điều khiển hiệu năng cao **ESP32-S3 DevKitC-1 N16R8** (16MB Flash, 8MB Octal PSRAM), vận hành hệ điều hành nhúng đa nhiệm **SpaceOS v3.2.9** mang lại khả năng xử lý thời gian thực mượt mà cho màn hình màu IPS, âm thanh kép I2S Full-Duplex, trợ lý ảo giọng nói XiaoZhi AI, cảm biến môi trường và hệ thống hiệu ứng ánh sáng decor sống động.

---

## ✨ Tính Năng Nổi Bật

### 1. 🤖 Trợ Lý Ảo XiaoZhi Cloud AI & Âm Thanh Kép Full-Duplex
- **Giao thức WebSocket thời gian thực**: Kết nối đám mây XiaoZhi AI độ trễ cực thấp, đàm thoại tự nhiên hai chiều.
- **Micro Kỹ Thuật Số I2S (INMP441 trên `I2S0`)**: BCLK (GPIO 4), WS (GPIO 5), SD (GPIO 6) - thu âm mono chất lượng phòng thu, lọc ồn phần cứng.
- **Mạch Giải Mã Âm Thanh I2S DAC (MAX98357A trên `I2S1`)**: DIN (GPIO 15), BCLK (GPIO 16), LRC (GPIO 17) - công suất loa 3W sống động.
- **Kiến trúc Full-Duplex độc lập**: Không chia sẻ xung clock giữa Mic và Loa, loại bỏ hoàn toàn hiện tượng dội âm (acoustic echo).

### 2. 🖥️ Màn Hình Màu IPS 2.4" & Sẵn Sàng Cho Màn Hình Cảm Ứng
- **Màn hình ST7789 2.4 inch (240x320 IPS)**: Bus SPI phần cứng tốc độ cao (40MHz – 80MHz) hiển thị giao diện trạm không gian, đồng hồ, thông tin thời tiết, phổ âm thanh sống động.
- **Sẵn sàng Nâng cấp Màn hình 2.4" Cảm ứng**: Đã cấu hình và dự phòng chân **`GPIO 47`** làm chân ngắt chạm cảm ứng (`PIN_TOUCH_SCREEN_INT / IRQ / CS`), hỗ trợ cả màn hình cảm ứng I2C (CST816S, FT6236) và SPI (XPT2046).

### 3. 🌪️ Cối Xay Gió Cyberpunk & Đèn Trang Trí Không Gian
- **Cối xay gió N20**: Động cơ giảm tốc N20 và vòng LED cánh cối xay đấu song song qua mạch cầu H L298N Mini / MX1508, điều khiển băm xung PWM mềm mượt trên **`GPIO 1`**.
- **Cặp đèn ống năng lượng Edison filament 30mm**: Đặt hai bên sườn trạm ($7.7\,\text{cm}$), điều khiển băm xung tần số cao 5000Hz trên **`GPIO 47`** (sườn Trái) và **`GPIO 21`** (sườn Phải) với 11 hiệu ứng ánh sáng (Lò phản ứng, Ngọn nến lung linh, Nhịp tim Cyber, Sấm sét, Thở lệch pha, Năng lượng sạc...).
- **Cặp LED nước chảy sao băng mặt lưng**: 2 thanh LED COB 3V tích hợp IC quét tự động đặt đối xứng hai bên lưng trạm, điều khiển độc lập qua **`GPIO 38`** và **`GPIO 18`**.
- **Đèn RGB NeoPixel Onboard**: WS2812 trên **`GPIO 48`** hiển thị trạng thái kết nối mạng và hoạt động của hệ thống.

### 4. 🌡️ Cảm Biến Môi Trường & Giám Sát Nguồn Điện Thông Minh
- **Cảm biến SHT31 (I2C phần cứng)**: SDA (`GPIO 8`), SCL (`GPIO 9`) - đo nhiệt độ, độ ẩm phòng với độ chính xác cao.
- **Cảm biến chạm điện dung TTP223**: Nối vào chân RTC **`GPIO 20`** (Active HIGH) hàn áp vào vỏ sườn đồng, hỗ trợ chạm chuyển đổi hiệu ứng đèn, đánh thức trợ lý ảo và đánh thức hệ thống từ chế độ Deep Sleep.
- **Giám sát trạng thái sạc bo nguồn MH-CD42**: Đọc cờ High/Low từ ngõ vào Vin bo sạc qua chân an toàn **`GPIO 46`** (Input-Only).
- **Đo điện áp pin Lithium 18650**: Đọc qua cầu phân áp $100\,\text{k}\Omega / 100\,\text{k}\Omega$ trên chân **`GPIO 2`** (kênh ADC1_CH1, không xung đột với bộ thu phát WiFi).
- **Bàn phím 7 nút điều hướng**: Đọc thang trở 1 dây trên **`GPIO 3`** (ADC1_CH2).

### 5. 🌐 Giao Diện Quản Trị Web Nhúng & Cập Nhật Firmware OTA
- **Web Server tích hợp**: Cho phép giám sát trạng thái trạm, điều khiển đèn decor, tốc độ cối xay, chat trực tiếp với AI và cấu hình mạng Wi-Fi ngay trên trình duyệt điện thoại/máy tính.
- **Hệ thống phân vùng 16MB Flash Dual OTA**: Phân vùng `ota_0` và `ota_1` dung lượng lớn (hơn 3MB mỗi phân vùng), cho phép cập nhật phần mềm từ xa an toàn tuyệt đối, tự động chuyển đổi phân vùng khi có bản cập nhật mới.

---

## 🔌 Sơ Đồ Đấu Nối Chân Phần Cứng (Hardware Pinout)

Chi tiết đầy đủ về sơ đồ mạch và kinh nghiệm thi công xem tại [PINOUT.md](file:///c:/Users/admin/Documents/GitHub/Decor_Esp32/PINOUT.md).

### Bảng phân bổ chân ESP32-S3 DevKitC-1 (N16R8)

| GPIO | Tên Ngoại Vi / Tín Hiệu | Chức Năng Chi Tiết |
| :---: | :--- | :--- |
| **GPIO 1** | **Cối Xay Gió IN1** | PWM điều tốc động cơ N20 & Vành đèn cánh cối xay (Cầu H L298N) |
| **GPIO 2** | **BAT_ADC** | Đo điện áp pin Lithium 18650 qua cầu phân áp $100\,\text{k}\Omega / 100\,\text{k}\Omega$ (ADC1_CH1) |
| **GPIO 3** | **KEY_ADC** | Đọc bàn phím thang trở 7 nút điều hướng 1 dây (ADC1_CH2) |
| **GPIO 4** | **Mic INMP441 SCK** | Xung bit thu âm I2S0 BCLK |
| **GPIO 5** | **Mic INMP441 WS** | Xung chọn kênh thu âm I2S0 WS / LRC |
| **GPIO 6** | **Mic INMP441 SD** | Dữ liệu giọng nói gửi về MCU (I2S0 Data In) |
| **GPIO 7** | **Màn hình ST7789 BLK** | Đèn nền LCD màn hình 2.4" (PWM 0..100%) |
| **GPIO 8** | **Cảm biến SHT31 SDA** | Dữ liệu I2C Data (Dùng chung cho cảm ứng I2C nếu nâng cấp) |
| **GPIO 9** | **Cảm biến SHT31 SCL** | Xung nhịp I2C Clock (Dùng chung cho cảm ứng I2C nếu nâng cấp) |
| **GPIO 10**| **Màn hình ST7789 RST** | Reset phần cứng LCD ST7789 |
| **GPIO 11**| **Màn hình ST7789 MOSI**| SPI Data Out (SDA / SDI) |
| **GPIO 12**| **Màn hình ST7789 SCK** | SPI Clock (SCL) |
| **GPIO 13**| **Màn hình ST7789 DC**  | SPI Data / Command |
| **GPIO 14**| **Màn hình ST7789 CS**  | SPI Chip Select LCD |
| **GPIO 15**| **Loa MAX98357A DIN**   | Dữ liệu âm thanh phát loa (I2S1 Data Out) |
| **GPIO 16**| **Loa MAX98357A BCLK**  | Xung bit phát loa (I2S1 Bit Clock) |
| **GPIO 17**| **Loa MAX98357A LRC**   | Xung chọn kênh phát loa (I2S1 Word Select) |
| **GPIO 18**| **LED Nước Chảy Phải**  | Cực (+) thanh LED sao băng bên phải mặt lưng qua trở $22\,\Omega$ |
| **GPIO 20**| **Cảm Biến Chạm TTP223**| Tín hiệu chạm Digital SIG (Active HIGH, RTC_GPIO20 đánh thức Deep Sleep) |
| **GPIO 21**| **LED Edison Sườn Phải**| Cực (+) cặp LED Edison sườn phải qua trở $47\,\Omega$ (PWM CH5) |
| **GPIO 38**| **LED Nước Chảy Trái**  | Cực (+) thanh LED sao băng bên trái mặt lưng qua trở $22\,\Omega$ |
| **GPIO 39**| **Thẻ Micro SD CS**     | SPI Chip Select thẻ nhớ SD |
| **GPIO 40**| **Thẻ Micro SD MOSI**   | SPI Data Out thẻ nhớ SD |
| **GPIO 41**| **Thẻ Micro SD MISO**   | SPI Data In thẻ nhớ SD (Dùng chung cho Touch SPI nếu nâng cấp) |
| **GPIO 42**| **Thẻ Micro SD SCK**    | SPI Clock thẻ nhớ SD |
| **GPIO 46**| **Trạng Thái Sạc Vin**  | Đọc mức High/Low trạng thái cắm sạc pin từ bo MH-CD42 (Input-Only) |
| **GPIO 47**| **Nâng Cấp Cảm Ứng 2.4" / LED Edison Trái** | **Chân ngắt IRQ/INT hoặc CS cho màn hình 2.4" cảm ứng** / Cặp LED Edison Trái |
| **GPIO 48**| **RGB WS2812 Onboard**  | Đèn LED RGB đa sắc tích hợp trên kit ESP32-S3 |

> [!WARNING]
> Tuyệt đối **KHÔNG SỬ DỤNG các chân từ GPIO 26 đến GPIO 37**! Các chân này kết nối nội bộ cho 8MB Octal PSRAM và 16MB Flash của ESP32-S3. Chân **GPIO 46** là chân *Input-Only* (chỉ nhận tín hiệu vào).

---

## 🛠️ Hướng Dẫn Nâng Cấp Màn Hình 2.4" Cảm Ứng (Touch Screen)

Để nâng cấp từ màn hình thường lên màn hình 2.4" cảm ứng:
1. **Màn hình Cảm Ứng Chuẩn I2C (CST816S / FT6236 / FT5436)**:
   - `SDA` $\to$ **GPIO 8** (nối chung bus I2C với SHT31).
   - `SCL` $\to$ **GPIO 9** (nối chung bus I2C với SHT31).
   - `INT / IRQ` $\to$ **GPIO 47** (`PIN_TOUCH_SCREEN_INT` trong [Config.h](file:///c:/Users/admin/Documents/GitHub/Decor_Esp32/include/Config.h)).
   - `RST` $\to$ **GPIO 10** (chung LCD RST) hoặc cấp 3.3V.
2. **Màn hình Cảm Ứng Chuẩn SPI (XPT2046)**:
   - `T_CLK` $\to$ **GPIO 12** (chung SPI SCK).
   - `T_DIN` $\to$ **GPIO 11** (chung SPI MOSI).
   - `T_DO` $\to$ **GPIO 41** (chung SPI MISO của thẻ nhớ SD).
   - `T_CS` hoặc `T_IRQ` $\to$ **GPIO 47** (`PIN_TOUCH_SCREEN_CS` / `PIN_TOUCH_SCREEN_IRQ`).

---

## 💻 Hướng Dẫn Cài Đặt & Nạp Firmware

### 1. Yêu Cầu Môi Trường
- [Visual Studio Code](https://code.visualstudio.com/) với tiện ích mở rộng **PlatformIO IDE**.
- Driver nạp chip ESP32-S3: CH343 / CP210x hoặc Native USB CDC.
- Python 3.9+ (để sử dụng công cụ nạp OTA từ xa).

### 2. Biên Dịch Dự Án
Mở thư mục dự án trong VS Code và chạy lệnh:
```bash
# Biên dịch mã nguồn firmware
pio run -e esp32-s3-devkitc-1

# Nạp qua cáp USB Type-C (cổng COM)
pio run -t upload -e esp32-s3-devkitc-1

# Mở Serial Monitor theo dõi log
pio device monitor -b 115200
```

### 3. Cập Nhật Firmware Qua Mạng (OTA)
Hệ thống hỗ trợ nạp firmware từ xa không cần cắm cáp:
```bash
# Nạp trực tiếp qua script Python OTA
python scripts/ota_deploy.py --ip 192.168.1.233 --bin .pio/build/esp32-s3-devkitc-1/firmware.bin
```
Hoặc truy cập trực tiếp trang quản trị Web trạm tại `http://192.168.1.233` (hoặc `http://tramvutru.local`) và tải tệp `firmware.bin` lên mục Cập nhật OTA.

---

## 📁 Cấu Trúc Thư Mục Dự Án

```
Decor_Esp32/
├── .pio/                  # Thư mục build và thư viện cache của PlatformIO
├── data/                  # Tài nguyên LittleFS (Giao diện Web HTML/JS/CSS, Icon, Font)
├── hardware/              # Thiết kế phần cứng PCB phím bấm (KiCad: Key_pad)
├── include/               # Các tệp Header cấu hình hệ thống
│   └── Config.h           # Sơ đồ chân phần cứng chính thức & thông số hệ thống
├── src/                   # Mã nguồn chính của SpaceOS
│   ├── main.cpp           # Điểm khởi chạy hệ thống, quản lý TaskScheduler & vòng lặp
│   ├── AudioTask.cpp      # Xử lý âm thanh I2S kép (INMP441 & MAX98357A)
│   ├── DisplayTask.cpp    # Quản lý đồ họa màn hình ST7789 2.4"
│   ├── LightTask.cpp      # Điều khiển PWM cối xay gió, LED Edison & LED nước chảy
│   ├── SensorTask.cpp     # Đọc cảm biến SHT31, pin BAT_ADC, bàn phím ADC, sạc CD42
│   ├── WebServerTask.cpp  # Máy chủ Web giao diện điều khiển & OTA
│   └── XiaoZhiTask.cpp    # Tương tác giao thức WebSocket XiaoZhi Cloud AI
├── scripts/               # Kịch bản Python hỗ trợ nạp OTA và công cụ kiểm thử
├── partitions_16mb.csv    # Bảng phân vùng bộ nhớ Flash 16MB (Dual OTA 3.2MB)
├── platformio.ini         # Tệp cấu hình dự án PlatformIO
├── PINOUT.md              # Tài liệu chi tiết sơ đồ chân và kinh nghiệm thi công mạch đồng
└── README.md              # Tài liệu hướng dẫn chính thức của dự án
```

---

## 🤝 Liên Kết Các Dự Án Con Liên Quan

- **[Decor_AI_Station](file:///c:/Users/admin/Documents/GitHub/Decor_AI_Station)**: Dự án phát triển thử nghiệm trạm AI độc lập kết nối đa mô hình lớn (Google Gemini 2.0 Flash / Pro, Hybrid Edge-Cloud TTS và Gateway hỗ trợ đọc báo, nội dung phân tích chuyên sâu).

---

## 📄 Bản Quyền & Giấy Phép (License)

Dự án phát hành dưới giấy phép [MIT License](LICENSE).  
Thiết kế phần cứng và nghệ thuật điêu khắc mạch đồng thủ công thuộc bản quyền nhóm phát triển **SpaceOS Team**.
