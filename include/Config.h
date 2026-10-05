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

// 4. Cối Xay Gió & Đèn LED Decor Trạm Không Gian
#define PIN_L298N_IN1_MOTOR 1   // IN1: Motor cối xay N20 & LED vành cánh (PWM) -> GPIO 1
#define PIN_EDISON_TUBE_LED 2   // 2 LED ống Edison 30mm sườn trạm (qua trở 47Ω, chung GND) -> GPIO 2 (PWM)
#define PIN_L298N_IN3_LED   2   // Alias tương thích (nếu kích qua L298N IN3) -> GPIO 2
#define PIN_FLOWING_LED_L   18  // LED nước chảy sao băng mặt lưng (Thanh Trái) -> GPIO 18 (Nối trực tiếp ESP32)
#define PIN_FLOWING_LED_R   38  // LED nước chảy sao băng mặt lưng (Thanh Phải) -> GPIO 38 (Nối trực tiếp ESP32)
#define PIN_FLOWING_LED_LEFT  PIN_FLOWING_LED_L
#define PIN_FLOWING_LED_RIGHT PIN_FLOWING_LED_R

// 5. Hệ Thống Âm Thanh I2S Độc Lập Kép (Mic INMP441 trên I2S0 & Loa MAX98357A trên I2S1)
// Cụm Microphone Kỹ Thuật Số INMP441 (I2S_NUM_0 - Chuyên Thu Âm & Trợ Lý Ảo XiaoZhi)
#define PIN_I2S_MIC_BCLK    4   // SCK / BCLK (Mic Clock) -> GPIO 4
#define PIN_I2S_MIC_WS      5   // WS  / LRC  (Mic Word Select) -> GPIO 5
#define PIN_I2S_MIC_SD      6   // SD  (Mic Data In) -> GPIO 6

// Cụm Khuếch Đại Loa I2S DAC MAX98357A (I2S_NUM_1 - Độc lập 100%, không chung BCLK/WS)
#define PIN_I2S_SPK_BCLK    16  // BCLK (Speaker Bit Clock) -> GPIO 16
#define PIN_I2S_SPK_LRC     17  // LRC / WS (Speaker Word Select) -> GPIO 17
#define PIN_I2S_SPK_DIN     15  // DIN / DOUT (Speaker Data Out) -> GPIO 15

// Alias tương thích ngược cho mã nguồn hiện tại
#define PIN_I2S_BCLK        PIN_I2S_MIC_BCLK
#define PIN_I2S_WS          PIN_I2S_MIC_WS
#define PIN_I2S_LRC         PIN_I2S_MIC_WS
#define PIN_I2S_DIN         PIN_I2S_MIC_SD
#define PIN_I2S_DOUT        PIN_I2S_SPK_DIN

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

// 8. Cụm Bàn Phím Điều Hướng & Giám Sát Bo Nguồn Keypad_Power_Shield
#define PIN_ADC_KEYPAD      3   // Chân ADC đọc cụm thang điện trở 7 nút (10k Pull-up lên 3.3V) -> GPIO 3 (ADC1_CH2)
#define PIN_CHG_STAT        46  // Đọc cờ trạng thái sạc TP4056 từ bo nguồn -> GPIO 46 (Input-Only)
#define PIN_BAT_ADC         2   // Chân ADC1_CH1 đọc điện áp pin 18650 qua cầu phân áp (Dự phòng) -> GPIO 2
#define PIN_BTN_UP          16  // [Dự phòng phím số] LÊN (Up)
#define PIN_BTN_DOWN        17  // [Dự phòng phím số] XUỐNG (Down)
#define PIN_BTN_LEFT        18  // [Dự phòng phím số] TRÁI (Left)
#define PIN_BTN_RIGHT       21  // [Dự phòng phím số] PHẢI (Right)
#define PIN_BTN_OK          38  // [Dự phòng phím số] OK (Giữa)
#define PIN_BTN_MENU        47  // [Dự phòng phím số] MENU
#define PIN_BTN_EXIT        45  // [Dự phòng phím số] EXIT

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

