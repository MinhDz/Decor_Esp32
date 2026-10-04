#pragma once
#include <Arduino.h>

// ====================================================================
//          ĐỊNH NGHĨA SƠ ĐỒ CHÂN PHẦN CỨNG CHÍNH THỨC (ESP32-S3)
// ====================================================================

// 1. Đèn RGB Onboard (WS2812 NeoPixel có sẵn trên kit)
#ifndef RGB_BUILTIN
  #define RGB_BUILTIN 48
#endif
#define PIN_RGB_ONBOARD     48

// 2. Bus I2C Phần Cứng - Cảm biến Nhiệt độ & Độ ẩm SHT31
#define PIN_I2C_SDA         8   // SDA (SHT31) -> GPIO 8
#define PIN_I2C_SCL         9   // SCL (SHT31) -> GPIO 9
#define SHT31_I2C_ADDR_1    0x44
#define SHT31_I2C_ADDR_2    0x45

// 3. Cảm biến Chạm Điện Dung TTP22
#define PIN_TOUCH_TTP223    20  // SIG (TTP223) -> GPIO 20 (Active HIGH)

// 4. Module Cầu H L298N Mini / MX1508 (Cối xay gió N20 & Đèn LED Edison 3V)
#define PIN_L298N_IN1_MOTOR 1   // IN1 (Motor cối xay) -> GPIO 1 (PWM)
#define PIN_L298N_IN3_LED   2   // IN3 (LED Edison 3V) -> GPIO 2 (PWM)

// 5. Âm thanh I2S (Trợ lý ảo XiaoZhi: Mic INMP441 & Loa DAC MAX98357A)
#define PIN_I2S_BCLK        4   // BCLK / SCK (Chung cho cả Mic & Loa) -> GPIO 4
#define PIN_I2S_WS          5   // WS / LRC   (Chung cho cả Mic & Loa) -> GPIO 5
#define PIN_I2S_LRC         5   // Alias tương thích
#define PIN_I2S_MIC_SD      6   // SD (INMP441 - Data IN thu giọng nói) -> GPIO 6
#define PIN_I2S_DIN         6   // Alias tương thích
#define PIN_I2S_SPK_DIN     15  // DIN (MAX98357A - Data OUT phát âm thanh) -> GPIO 15
#define PIN_I2S_DOUT        15  // Alias tương thích

// 6. Màn hình ST7789 SPI 2.4" (240x320 - SPI Bus tốc độ cao 40MHz-80MHz)
#define PIN_TFT_SCK         12  // SCL (SCK)  -> GPIO 12
#define PIN_TFT_MOSI        11  // SDA (MOSI) -> GPIO 11
#define PIN_TFT_RST         10  // RES (Reset)-> GPIO 10
#define PIN_TFT_DC          13  // DC         -> GPIO 13
#define PIN_TFT_CS          14  // CS         -> GPIO 14
#define PIN_TFT_BL          7   // BLK (Đèn nền LED màn hình) -> GPIO 7 (Điều khiển độ sáng PWM 0..100%)

// 7. Module Shield Thẻ nhớ Micro SD (Wemos D1 Mini Shield: D5=CLK, D6=MISO, D7=MOSI, D8=CS)
#define PIN_SD_SCK          42  // D5 (SD_SCK / CLK)  -> GPIO 42
#define PIN_SD_MISO         41  // D6 (SD_MISO / DO)  -> GPIO 41
#define PIN_SD_MOSI         40  // D7 (SD_MOSI / DI)  -> GPIO 40
#define PIN_SD_CS           39  // D8 (SD_CS / SS)    -> GPIO 39

// 8. Cụm 7 Phím Bấm Điều Hướng (Hỗ trợ Thang Điện Trở ADC 1 Dây trên GPIO 3 + Dự phòng Digital)
#define PIN_ADC_KEYPAD      3   // Chân ADC đọc cụm thang điện trở 7 nút (10k Pull-up lên 3.3V) -> GPIO 3 (ADC1_CH2)
#define PIN_BTN_UP          16  // LÊN (Up)     -> 1.0kΩ (~0.30V)
#define PIN_BTN_DOWN        17  // XUỐNG (Down) -> 2.2kΩ (~0.60V)
#define PIN_BTN_LEFT        18  // TRÁI (Left)  -> 4.7kΩ (~1.05V)
#define PIN_BTN_RIGHT       21  // PHẢI (Right) -> 10kΩ  (~1.65V)
#define PIN_BTN_OK          38  // OK (Giữa)    -> 0Ω    (0.00V - Nối thẳng GND)
#define PIN_BTN_MENU        47  // MENU         -> 20kΩ  (~2.20V - 2.27V)
#define PIN_BTN_EXIT        45  // EXIT         -> 47kΩ  (~2.72V)

// ====================================================================
//                 CẤU HÌNH HỆ THỐNG & ĐƯỜNG DẪN TỆP
// ====================================================================

#define MAX_IMAGE_COUNT 15

#define FILE_CONFIG         "/config.json"
#define FILE_STANDBY_CONFIG "/standby_config.json"
#define FILE_WIFI_KNOWN     "/wifi_known.json"

// Cấu hình Wi-Fi SoftAP cứu hộ mặc định
#define DEFAULT_AP_SSID     "TramVuTru-Config"
#define DEFAULT_AP_PASS     "12345678"
#define MDNS_HOSTNAME       "tramvutru"

// Cấu hình XiaoZhi Cloud AI
#define XIAOZHI_DEFAULT_ENDPOINT "wss://api.tenclass.net/xiaozhi/v1/"
#define XIAOZHI_OTA_URL          "https://api.tenclass.net/xiaozhi/ota/"
#define XIAOZHI_USER_AGENT       "bread-compact-wifi/2.0.0"

