#define REAL_DISPLAY_DRIVER_CPP 1
#include "tests/TestDisplay.h"
#include "tests/TestSensors.h"
#include "tests/TestAudio.h"
#include "tests/TestButtons.h"
#include "tests/TestSDCard.h"
#include "tests/TestWindmill.h"
#include "Config.h"
#include "WifiManager.h"
#include "PcStatsManager.h"
#include "ImageManager.h"
#include "XiaoZhiClient.h"
#include "GomeIcons.h"
#include "OtaManager.h"
#include <SPI.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ILI9341.h>
#include <TJpg_Decoder.h>
#include <Preferences.h>
#include <qrcode.h>

// Bảng màu 16-bit RGB565 gốc ( hằng số cố định )
#define C_BLACK       0x0000
#define C_WHITE       0xFFFF
#define C_RED         0xF800
#define C_GREEN       0x07E0
#define C_BLUE        0x001F
#define C_CYAN        0x07FF
#define C_MAGENTA     0xF81F
#define C_YELLOW      0xFFE0
#define C_ORANGE      0xFD20
#define C_SLATE       0x9534  // #94a3b8
#define C_GRAY        0x52AA

namespace TestDisplay {

  // ============================================================================
  // BỘ KẾT XUẤT FONT TIẾNG VIỆT UTF-8 CÓ DẤU (VIETNAMESE DIACRITIC GFX ENGINE)
  // Hỗ trợ đầy đủ 134 ký tự Tiếng Việt có dấu (U+00C0 .. U+1EF9) trên ST7789 & Canvas
  // Giữ nguyên kích thước ô chữ 6x8 / 12x16, không tràn khung, không để lại bóng ma!
  // ============================================================================
  enum VnModType : uint8_t {
    VN_MOD_NONE = 0,
    VN_MOD_CIRC = 1, // ^ (â, ê, ô)
    VN_MOD_BREV = 2, // ˘ (ă)
    VN_MOD_HORN = 3, // ơ, ư
    VN_MOD_CBAR = 4  // đ, Đ
  };

  enum VnToneType : uint8_t {
    VN_TONE_NONE  = 0,
    VN_TONE_ACUTE = 1, // ´ (Sắc)
    VN_TONE_GRAVE = 2, // ` (Huyền)
    VN_TONE_HOOK  = 3, // ? (Hỏi)
    VN_TONE_TILDE = 4, // ~ (Ngã)
    VN_TONE_DOT   = 5  // . (Nặng)
  };

  struct VnExtEntry {
    char    upper;
    uint8_t mod;
    uint8_t tone;
  };

  // Bảng ánh xạ 45 cặp chữ Hoa/Thường trong dải Unicode U+1EA0 .. U+1EF9 (90 ký tự)
  static const VnExtEntry VN_EXT_TABLE[45] = {
    {'A', VN_MOD_NONE, VN_TONE_DOT},   // 1EA0/1: Ạ/ạ
    {'A', VN_MOD_NONE, VN_TONE_HOOK},  // 1EA2/3: Ả/ả
    {'A', VN_MOD_CIRC, VN_TONE_ACUTE}, // 1EA4/5: Ấ/ấ
    {'A', VN_MOD_CIRC, VN_TONE_GRAVE}, // 1EA6/7: Ầ/ầ
    {'A', VN_MOD_CIRC, VN_TONE_HOOK},  // 1EA8/9: Ẩ/ẩ
    {'A', VN_MOD_CIRC, VN_TONE_TILDE}, // 1EAA/B: Ẫ/ẫ
    {'A', VN_MOD_CIRC, VN_TONE_DOT},   // 1EAC/D: Ậ/ậ
    {'A', VN_MOD_BREV, VN_TONE_ACUTE}, // 1EAE/F: Ắ/ắ
    {'A', VN_MOD_BREV, VN_TONE_GRAVE}, // 1EB0/1: Ằ/ằ
    {'A', VN_MOD_BREV, VN_TONE_HOOK},  // 1EB2/3: Ẳ/ẳ
    {'A', VN_MOD_BREV, VN_TONE_TILDE}, // 1EB4/5: Ẵ/ẵ
    {'A', VN_MOD_BREV, VN_TONE_DOT},   // 1EB6/7: Ặ/ặ
    {'E', VN_MOD_NONE, VN_TONE_DOT},   // 1EB8/9: Ẹ/ẹ
    {'E', VN_MOD_NONE, VN_TONE_HOOK},  // 1EBA/B: Ẻ/ẻ
    {'E', VN_MOD_NONE, VN_TONE_TILDE}, // 1EBC/D: Ẽ/ẽ
    {'E', VN_MOD_CIRC, VN_TONE_ACUTE}, // 1EBE/F: Ế/ế
    {'E', VN_MOD_CIRC, VN_TONE_GRAVE}, // 1EC0/1: Ề/ề
    {'E', VN_MOD_CIRC, VN_TONE_HOOK},  // 1EC2/3: Ể/ể
    {'E', VN_MOD_CIRC, VN_TONE_TILDE}, // 1EC4/5: Ễ/ễ
    {'E', VN_MOD_CIRC, VN_TONE_DOT},   // 1EC6/7: Ệ/ệ
    {'I', VN_MOD_NONE, VN_TONE_HOOK},  // 1EC8/9: Ỉ/ỉ
    {'I', VN_MOD_NONE, VN_TONE_DOT},   // 1ECA/B: Ị/ị
    {'O', VN_MOD_NONE, VN_TONE_DOT},   // 1ECC/D: Ọ/ọ
    {'O', VN_MOD_NONE, VN_TONE_HOOK},  // 1ECE/F: Ỏ/ỏ
    {'O', VN_MOD_CIRC, VN_TONE_ACUTE}, // 1ED0/1: Ố/ố
    {'O', VN_MOD_CIRC, VN_TONE_GRAVE}, // 1ED2/3: Ồ/ồ
    {'O', VN_MOD_CIRC, VN_TONE_HOOK},  // 1ED4/5: ổ/ổ
    {'O', VN_MOD_CIRC, VN_TONE_TILDE}, // 1ED6/7: Ỗ/ỗ
    {'O', VN_MOD_CIRC, VN_TONE_DOT},   // 1ED8/9: Ộ/ộ
    {'O', VN_MOD_HORN, VN_TONE_ACUTE}, // 1EDA/B: Ớ/ớ
    {'O', VN_MOD_HORN, VN_TONE_GRAVE}, // 1EDC/D: Ờ/ờ
    {'O', VN_MOD_HORN, VN_TONE_HOOK},  // 1EDE/F: Ở/ở
    {'O', VN_MOD_HORN, VN_TONE_TILDE}, // 1EE0/1: Ỡ/ỡ
    {'O', VN_MOD_HORN, VN_TONE_DOT},   // 1EE2/3: Ợ/ợ
    {'U', VN_MOD_NONE, VN_TONE_DOT},   // 1EE4/5: Ụ/ụ
    {'U', VN_MOD_NONE, VN_TONE_HOOK},  // 1EE6/7: Ủ/ủ
    {'U', VN_MOD_HORN, VN_TONE_ACUTE}, // 1EE8/9: Ứ/ứ
    {'U', VN_MOD_HORN, VN_TONE_GRAVE}, // 1EEA/B: Ừ/ừ
    {'U', VN_MOD_HORN, VN_TONE_HOOK},  // 1EEC/D: Ử/ử
    {'U', VN_MOD_HORN, VN_TONE_TILDE}, // 1EEE/F: Ữ/ữ
    {'U', VN_MOD_HORN, VN_TONE_DOT},   // 1EF0/1: Ự/ự
    {'Y', VN_MOD_NONE, VN_TONE_GRAVE}, // 1EF2/3: Ỳ/ỳ
    {'Y', VN_MOD_NONE, VN_TONE_DOT},   // 1EF4/5: Ỵ/ỵ
    {'Y', VN_MOD_NONE, VN_TONE_HOOK},  // 1EF6/7: Ỷ/ỷ
    {'Y', VN_MOD_NONE, VN_TONE_TILDE}  // 1EF8/9: Ỹ/ỹ
  };

  static bool decodeVnCodepoint(uint32_t cp, char& baseOut, uint8_t& modOut, uint8_t& toneOut) {
    if (cp >= 0x1EA0 && cp <= 0x1EF9) {
      int idx = (int)(cp - 0x1EA0) >> 1;
      bool isLower = (cp & 1) != 0;
      baseOut = isLower ? (char)(VN_EXT_TABLE[idx].upper + 32) : VN_EXT_TABLE[idx].upper;
      modOut  = VN_EXT_TABLE[idx].mod;
      toneOut = VN_EXT_TABLE[idx].tone;
      return true;
    }
    modOut  = VN_MOD_NONE;
    toneOut = VN_TONE_NONE;
    switch (cp) {
      case 0x00C0: baseOut = 'A'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00C1: baseOut = 'A'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00C2: baseOut = 'A'; modOut  = VN_MOD_CIRC;   return true;
      case 0x00C3: baseOut = 'A'; toneOut = VN_TONE_TILDE; return true;
      case 0x00C8: baseOut = 'E'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00C9: baseOut = 'E'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00CA: baseOut = 'E'; modOut  = VN_MOD_CIRC;   return true;
      case 0x00CC: baseOut = 'I'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00CD: baseOut = 'I'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00D2: baseOut = 'O'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00D3: baseOut = 'O'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00D4: baseOut = 'O'; modOut  = VN_MOD_CIRC;   return true;
      case 0x00D5: baseOut = 'O'; toneOut = VN_TONE_TILDE; return true;
      case 0x00D9: baseOut = 'U'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00DA: baseOut = 'U'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00DD: baseOut = 'Y'; toneOut = VN_TONE_ACUTE; return true;

      case 0x00E0: baseOut = 'a'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00E1: baseOut = 'a'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00E2: baseOut = 'a'; modOut  = VN_MOD_CIRC;   return true;
      case 0x00E3: baseOut = 'a'; toneOut = VN_TONE_TILDE; return true;
      case 0x00E8: baseOut = 'e'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00E9: baseOut = 'e'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00EA: baseOut = 'e'; modOut  = VN_MOD_CIRC;   return true;
      case 0x00EC: baseOut = 'i'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00ED: baseOut = 'i'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00F2: baseOut = 'o'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00F3: baseOut = 'o'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00F4: baseOut = 'o'; modOut  = VN_MOD_CIRC;   return true;
      case 0x00F5: baseOut = 'o'; toneOut = VN_TONE_TILDE; return true;
      case 0x00F9: baseOut = 'u'; toneOut = VN_TONE_GRAVE; return true;
      case 0x00FA: baseOut = 'u'; toneOut = VN_TONE_ACUTE; return true;
      case 0x00FD: baseOut = 'y'; toneOut = VN_TONE_ACUTE; return true;

      case 0x0102: baseOut = 'A'; modOut  = VN_MOD_BREV;   return true;
      case 0x0103: baseOut = 'a'; modOut  = VN_MOD_BREV;   return true;
      case 0x0110: baseOut = 'D'; modOut  = VN_MOD_CBAR;   return true;
      case 0x0111: baseOut = 'd'; modOut  = VN_MOD_CBAR;   return true;
      case 0x0128: baseOut = 'I'; toneOut = VN_TONE_TILDE; return true;
      case 0x0129: baseOut = 'i'; toneOut = VN_TONE_TILDE; return true;
      case 0x0168: baseOut = 'U'; toneOut = VN_TONE_TILDE; return true;
      case 0x0169: baseOut = 'u'; toneOut = VN_TONE_TILDE; return true;
      case 0x01A0: baseOut = 'O'; modOut  = VN_MOD_HORN;   return true;
      case 0x01A1: baseOut = 'o'; modOut  = VN_MOD_HORN;   return true;
      case 0x01AF: baseOut = 'U'; modOut  = VN_MOD_HORN;   return true;
      case 0x01B0: baseOut = 'u'; modOut  = VN_MOD_HORN;   return true;
      default: return false;
    }
  }

  template <typename BaseGfx>
  class VnGfxAdapter : public BaseGfx {
  private:
    uint32_t utf8Code = 0;
    uint8_t  utf8Remain = 0;

    inline void drawScaledDot(int16_t dx0, int16_t dy0, int px, int py, uint8_t sx, uint8_t sy, uint16_t col) {
      if (sx == 1 && sy == 1) {
        this->drawPixel(dx0 + px, dy0 + py, col);
      } else {
        this->fillRect(dx0 + px * sx, dy0 + py * sy, sx, sy, col);
      }
    }

  public:
    using BaseGfx::BaseGfx;

    virtual size_t write(uint8_t c) override {
      if (c < 0x80) {
        utf8Remain = 0;
        return BaseGfx::write(c);
      }
      if ((c & 0xE0) == 0xC0) {
        utf8Code = c & 0x1F;
        utf8Remain = 1;
        return 1;
      }
      if ((c & 0xF0) == 0xE0) {
        utf8Code = c & 0x0F;
        utf8Remain = 2;
        return 1;
      }
      if ((c & 0xF8) == 0xF0) {
        utf8Code = c & 0x07;
        utf8Remain = 3;
        return 1;
      }
      if ((c & 0xC0) == 0x80 && utf8Remain > 0) {
        utf8Code = (utf8Code << 6) | (c & 0x3F);
        utf8Remain--;
        if (utf8Remain > 0) return 1;

        uint32_t cp = utf8Code;
        char baseChar = '?';
        uint8_t mod = VN_MOD_NONE, tone = VN_TONE_NONE;

        if (!decodeVnCodepoint(cp, baseChar, mod, tone)) {
          if (cp == 0x00B0) { // Ký hiệu độ (°)
            uint8_t sx = this->textsize_x ? this->textsize_x : 1;
            uint8_t sy = this->textsize_y ? this->textsize_y : 1;
            int16_t dx0 = this->cursor_x, dy0 = this->cursor_y;
            if (this->textcolor != this->textbgcolor) {
              this->fillRect(dx0, dy0, 6 * sx, 8 * sy, this->textbgcolor);
            }
            drawScaledDot(dx0, dy0, 1, 0, sx, sy, this->textcolor);
            drawScaledDot(dx0, dy0, 2, 0, sx, sy, this->textcolor);
            drawScaledDot(dx0, dy0, 1, 1, sx, sy, this->textcolor);
            drawScaledDot(dx0, dy0, 2, 1, sx, sy, this->textcolor);
            this->cursor_x += 4 * sx;
            return 1;
          }
          return BaseGfx::write((uint8_t)'*');
        }

        uint8_t sx = this->textsize_x ? this->textsize_x : 1;
        uint8_t sy = this->textsize_y ? this->textsize_y : 1;
        uint16_t fg = this->textcolor;
        uint16_t bg = this->textbgcolor;

        // Trường hợp chữ đ / Đ: vẽ chữ d / D gốc rồi thêm thanh ngang
        if (mod == VN_MOD_CBAR) {
          BaseGfx::write((uint8_t)baseChar);
          int16_t dx0 = this->cursor_x - 6 * sx;
          int16_t dy0 = this->cursor_y;
          if (baseChar == 'd') {
            for (int px = 1; px <= 4; px++) drawScaledDot(dx0, dy0, px, 1, sx, sy, fg);
          } else {
            for (int px = 0; px <= 3; px++) drawScaledDot(dx0, dy0, px, 3, sx, sy, fg);
          }
          return 1;
        }

        // Các nguyên âm có dấu (a/A, e/E, i/I, o/O, u/U, y/Y):
        // Dành trọn dòng py=0..1 cho dấu mũ & thanh điệu, py=2..6 (hoặc 2..5 khi có dấu nặng ở py=7) cho thân chữ!
        if (this->wrap && ((this->cursor_x + sx * 6) > this->_width)) {
          this->cursor_x = 0;
          this->cursor_y += sy * 8;
        }
        int16_t dx0 = this->cursor_x;
        int16_t dy0 = this->cursor_y;
        this->cursor_x += sx * 6;

        if (fg != bg) {
          this->fillRect(dx0, dy0, 6 * sx, 8 * sy, bg);
        }

        // Bảng bitmap thân nguyên âm 5 dòng (py=2..6) và 4 dòng (py=2..5 khi có dấu nặng . ở py=7)
        bool hasBottomDot = (tone == VN_TONE_DOT);
        uint8_t rows5[5] = {0, 0, 0, 0, 0};
        uint8_t rows4[4] = {0, 0, 0, 0};
        switch (baseChar) {
          case 'A':
            rows5[0]=0x0E; rows5[1]=0x11; rows5[2]=0x1F; rows5[3]=0x11; rows5[4]=0x11;
            rows4[0]=0x0E; rows4[1]=0x11; rows4[2]=0x1F; rows4[3]=0x11;
            break;
          case 'a':
            rows5[0]=0x0E; rows5[1]=0x01; rows5[2]=0x0F; rows5[3]=0x11; rows5[4]=0x0F;
            rows4[0]=0x0E; rows4[1]=0x0F; rows4[2]=0x11; rows4[3]=0x0F;
            break;
          case 'E':
            rows5[0]=0x1F; rows5[1]=0x10; rows5[2]=0x1E; rows5[3]=0x10; rows5[4]=0x1F;
            rows4[0]=0x1F; rows4[1]=0x1E; rows4[2]=0x10; rows4[3]=0x1F;
            break;
          case 'e':
            rows5[0]=0x0E; rows5[1]=0x11; rows5[2]=0x1F; rows5[3]=0x10; rows5[4]=0x0E;
            rows4[0]=0x0E; rows4[1]=0x1F; rows4[2]=0x10; rows4[3]=0x0E;
            break;
          case 'I':
            rows5[0]=0x0E; rows5[1]=0x04; rows5[2]=0x04; rows5[3]=0x04; rows5[4]=0x0E;
            rows4[0]=0x0E; rows4[1]=0x04; rows4[2]=0x04; rows4[3]=0x0E;
            break;
          case 'i':
            rows5[0]=0x0C; rows5[1]=0x04; rows5[2]=0x04; rows5[3]=0x04; rows5[4]=0x0E;
            rows4[0]=0x0C; rows4[1]=0x04; rows4[2]=0x04; rows4[3]=0x0E;
            break;
          case 'O':
          case 'o':
            rows5[0]=0x0E; rows5[1]=0x11; rows5[2]=0x11; rows5[3]=0x11; rows5[4]=0x0E;
            rows4[0]=0x0E; rows4[1]=0x11; rows4[2]=0x11; rows4[3]=0x0E;
            break;
          case 'U':
          case 'u':
            rows5[0]=0x11; rows5[1]=0x11; rows5[2]=0x11; rows5[3]=0x11; rows5[4]=0x0E;
            rows4[0]=0x11; rows4[1]=0x11; rows4[2]=0x11; rows4[3]=0x0E;
            break;
          case 'Y':
          case 'y':
          default:
            rows5[0]=0x11; rows5[1]=0x11; rows5[2]=0x0E; rows5[3]=0x04; rows5[4]=0x08;
            rows4[0]=0x11; rows4[1]=0x0E; rows4[2]=0x04; rows4[3]=0x04;
            break;
        }

        int nRows = hasBottomDot ? 4 : 5;
        const uint8_t* bmp = hasBottomDot ? rows4 : rows5;
        for (int r = 0; r < nRows; r++) {
          uint8_t bits = bmp[r];
          for (int b = 0; b < 5; b++) {
            if (bits & (0x10 >> b)) {
              drawScaledDot(dx0, dy0, b, 2 + r, sx, sy, fg);
            }
          }
        }

        // Chữ 'ị' có chấm trên ở (2, 0) và chấm nặng ở (2, 7)
        if (baseChar == 'i' && hasBottomDot) {
          drawScaledDot(dx0, dy0, 2, 0, sx, sy, fg);
        }

        // Vẽ dấu móc ơ / ư (VN_MOD_HORN) ở góc trên phải của thân chữ
        if (mod == VN_MOD_HORN) {
          drawScaledDot(dx0, dy0, 4, 1, sx, sy, fg);
          drawScaledDot(dx0, dy0, 5, 2, sx, sy, fg);
        }

        // Vẽ dấu Nặng (.) ở py=7 (cách đáy chữ py=5 đúng 1 dòng trống py=6!)
        if (hasBottomDot) {
          drawScaledDot(dx0, dy0, 2, 7, sx, sy, fg);
        }

        // Vẽ dấu Mũ (^, ˘) kết hợp cùng dấu Thanh (Sắc, Huyền, Hỏi, Ngã) trên dòng py=0..1
        if (mod == VN_MOD_CIRC) {
          if (tone == VN_TONE_ACUTE) {
            drawScaledDot(dx0, dy0, 0, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 1, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 3, 1, sx, sy, fg);
          } else if (tone == VN_TONE_GRAVE) {
            drawScaledDot(dx0, dy0, 0, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 1, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 3, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 1, sx, sy, fg);
          } else if (tone == VN_TONE_HOOK) {
            drawScaledDot(dx0, dy0, 0, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 1, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 1, sx, sy, fg);
          } else if (tone == VN_TONE_TILDE) {
            drawScaledDot(dx0, dy0, 1, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 3, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 0, sx, sy, fg);
          } else {
            drawScaledDot(dx0, dy0, 1, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 3, 1, sx, sy, fg);
          }
        } else if (mod == VN_MOD_BREV) {
          if (tone == VN_TONE_ACUTE) {
            drawScaledDot(dx0, dy0, 0, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 1, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 0, sx, sy, fg);
          } else if (tone == VN_TONE_GRAVE) {
            drawScaledDot(dx0, dy0, 0, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 3, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 0, sx, sy, fg);
          } else if (tone == VN_TONE_HOOK) {
            drawScaledDot(dx0, dy0, 0, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 1, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 1, sx, sy, fg);
          } else if (tone == VN_TONE_TILDE) {
            drawScaledDot(dx0, dy0, 0, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 1, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 0, sx, sy, fg);
          } else {
            drawScaledDot(dx0, dy0, 1, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 3, 0, sx, sy, fg);
          }
        } else {
          if (tone == VN_TONE_ACUTE) {
            drawScaledDot(dx0, dy0, 3, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 1, sx, sy, fg);
          } else if (tone == VN_TONE_GRAVE) {
            drawScaledDot(dx0, dy0, 1, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 1, sx, sy, fg);
          } else if (tone == VN_TONE_HOOK) {
            drawScaledDot(dx0, dy0, 2, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 3, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 1, sx, sy, fg);
          } else if (tone == VN_TONE_TILDE) {
            drawScaledDot(dx0, dy0, 1, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 2, 0, sx, sy, fg);
            drawScaledDot(dx0, dy0, 3, 1, sx, sy, fg);
            drawScaledDot(dx0, dy0, 4, 0, sx, sy, fg);
          }
        }
        return 1;
      }
      utf8Remain = 0;
      return 1;
    }
  };

  using VnST7789   = VnGfxAdapter<Adafruit_ST7789>;
  using VnILI9341  = VnGfxAdapter<Adafruit_ILI9341>;
  using VnCanvas16 = VnGfxAdapter<GFXcanvas16>;

  // Đếm số ký tự hiển thị thực tế của chuỗi UTF-8 (mỗi chữ Tiếng Việt có dấu tính đúng 1 ô 6px)
  static int vnStrLen(const char* s) {
    if (!s) return 0;
    int count = 0;
    while (*s) {
      uint8_t c = (uint8_t)(*s++);
      if ((c & 0xC0) != 0x80) count++;
    }
    return count;
  }
  static inline int vnStrLen(const String& s) {
    return vnStrLen(s.c_str());
  }

  // Cắt chuỗi UTF-8 theo số lượng ký tự hiển thị (không bao giờ cắt đứt giữa byte UTF-8)
  static String vnSubstr(const String& s, int startChar, int maxChars) {
    if (maxChars <= 0) return "";
    int curChar = 0;
    int startByte = -1;
    int endByte = s.length();
    for (int i = 0; i < (int)s.length(); i++) {
      uint8_t c = (uint8_t)s[i];
      if ((c & 0xC0) != 0x80) {
        if (curChar == startChar && startByte < 0) startByte = i;
        if (curChar == startChar + maxChars) {
          endByte = i;
          break;
        }
        curChar++;
      }
    }
    if (startByte < 0) return "";
    return s.substring(startByte, endByte);
  }

  // ============================================================================
  // HỆ THỐNG 6 PHONG CÁCH GIAO DIỆN CHÍNH THỨC (CHỦ ĐỀ / SYMBIAN S40 UI THEMES)
  // ============================================================================
  #define S40_THEME_COUNT 6

  struct S40ThemeSpec {
    const char* id;
    const char* name;         // Tên hiển thị trong Cài đặt
    const char* headerTag;    // Nhãn phong cách
    uint16_t darkBg;          // Màu nền chính của hệ thống
    uint16_t cardBg;          // Màu nền thẻ / ô Menu
    uint16_t cardSelBg;       // Màu nền ô đang chọn
    uint16_t cardBorder;      // Màu viền thẻ bình thường
    uint16_t headerBg;        // Màu thanh trạng thái & Softkey
    uint16_t neonCyan;        // Màu chủ đạo 1 (Primary Accent)
    uint16_t neonGreen;       // Màu chủ đạo 2 (Secondary / Positive)
    uint16_t neonPink;        // Màu điểm nhấn (Highlight / Alert)
    uint16_t neonAmber;       // Màu cảnh báo / Vàng ấm
    uint16_t neonPurple;      // Màu phụ trợ
    uint8_t  boxStyle;        // 0: Vũ trụ, 1: Chill, 2: Mecha, 3: Lofi, 4: Nokia S40, 5: Mèo Gome Chibi
  };

  static const S40ThemeSpec S40_THEMES[S40_THEME_COUNT] = {
    // 0: TRẠM VŨ TRỤ DECOR (Sáng tạo, hiện đại, Cyberpunk Neon)
    {
      "cyberpunk", "1. Trạm Vũ Trụ (Cyber)", "VŨ TRỤ",
      0x0842, 0x10A4, 0x1949, 0x2188, 0x10A4,
      0x3DF7, 0x15EC, 0xF292, 0xFBE1, 0xA2B7, 0
    },
    // 1: CHILL THẢO MỘC (Nhẹ nhàng, thư giãn mắt, Mint Forest / Matcha Zen)
    {
      "chill_mint", "2. Chill Thảo Mộc (Zen)", "THẢO MỘC",
      0x0903, 0x11C5, 0x1AC8, 0x2AE9, 0x1184,
      0x6FE9, 0x9FF3, 0xFD97, 0xFF53, 0x8799, 1
    },
    // 2: MECHA MẠNH MẼ (Cứng cáp, góc cạnh cơ khí, khoẻ khoắn, Tactical Armor)
    {
      "mecha_armor", "3. Mecha Mạnh Mẽ", "CƠ KHÍ",
      0x1082, 0x2104, 0x3986, 0x4A49, 0x2945,
      0xFD20, 0xFFE0, 0xF800, 0xFBE0, 0xFB00, 2
    },
    // 3: HOÀNG HÔN LOFI (Nhẹ nhàng chill chiều tà, Ấm áp, Cozy Dusk)
    {
      "sunset_lofi", "4. Hoàng Hôn Lofi", "HOÀNG HÔN",
      0x1865, 0x28E8, 0x416B, 0x51EC, 0x28A7,
      0xFB56, 0xFD68, 0xC41F, 0xFFE8, 0xE39B, 3
    },
    // 4: NOKIA S40 CỔ ĐIỂN (Hoài niệm chuẩn Nokia 6300 / 5310 XpressMusic)
    {
      "nokia_s40", "5. Cổ Điển Nokia S40", "CỔ ĐIỂN",
      0x00A6, 0x0170, 0x02B6, 0x3B9F, 0x01D2,
      0x7FFF, 0xFFFF, 0xFD20, 0xFFE0, 0x5DFF, 4
    },
    // 5: MÈO GOME CHIBI (Anime Kawaii - Xanh Pastel Sky Blue, Trắng Tuyết, Hồng Chibi)
    {
      "gome_chibi", "6. Mèo Gome Chibi", "GOME CHIBI",
      0x0863, 0x10C6, 0x218A, 0x3C19, 0x10C6,
      0x3DF7, 0xFFFF, 0xFDF4, 0xFFE0, 0xCE7F, 5
    }
  };

  static int s40UiThemeIdx = 0; // 0..5 tương ứng 6 phong cách giao diện
  static inline const S40ThemeSpec& curTheme() {
    return S40_THEMES[((s40UiThemeIdx % S40_THEME_COUNT) + S40_THEME_COUNT) % S40_THEME_COUNT];
  }

  // Ánh xạ tự động bảng màu hệ thống theo Chủ đề (Theme) đang chọn
  #define C_DARK_BG     (curTheme().darkBg)
  #define C_CARD_BG     (curTheme().cardBg)
  #define C_CARD_SEL    (curTheme().cardSelBg)
  #define C_CARD_BORDER (curTheme().cardBorder)
  #define C_HEADER_BG   (curTheme().headerBg)
  #define C_NEON_BLUE   (curTheme().neonCyan)
  #define C_NEON_CYAN   (curTheme().neonCyan)
  #define C_NEON_GREEN  (curTheme().neonGreen)
  #define C_NEON_PINK   (curTheme().neonPink)
  #define C_NEON_AMBER  (curTheme().neonAmber)
  #define C_NEON_PURPLE (curTheme().neonPurple)

  static SPIClass* spiBus = nullptr;
  static VnST7789* tft7789 = nullptr;
  static VnILI9341* tft9341 = nullptr;
  static Adafruit_SPITFT* tft = nullptr;

  static bool useST7789 = true;
  static bool inverted = false;
  static uint8_t rotation = 0;      // 0: Dọc (240x320)

  // Chế độ hiển thị:
  // 0: Màn Hình Chờ (Standby Clock + Wallpaper - MẶC ĐỊNH)
  // 1: Biểu Cảm XiaoZhi AI (Emoji + Phụ đề)
  // 2: PC Status HUD (Thông số máy tính + Cảnh báo Offline)
  // 3: Bảng Diagnostic Phần Cứng (SHT31 + Máy Hiện Sóng INMP441 + 7-Key ADC)
  // 4: Giao diện Điều Khiển Symbian S40 (Lưới 12 Biểu Tượng 4x3)
  // 5: App Symbian - Hình Nền
  // 6: App Symbian - Cài Đặt
  // 7: App Symbian - Thư Viện
  // 8: App Symbian - About
  // 9: Màn Hình Test Thẻ SD (Kiểu DIO)
  // 10: App Symbian - Bộ Nhớ (Memory & MicroSD Manager)
  // 11: App Symbian - Gọi Trợ Lý XiaoZhi AI (2/3 Biểu cảm + 1/3 Khung Hội thoại tròn dòng & tự cuộn)
  static uint8_t currentMode = 0;
  static uint8_t hudStyle = 1;

  static int s40MenuCursor = 0;       // 0..11 trên lưới 4x3
  static int s40WallpaperCursor = 0;
  static int s40SettingsCursor = 0;
  static int s40GalleryIndex = 0;
  static int s40AboutPage = 0;
  static uint8_t s40QrModeTab = 0; // 0: Kết nối Wi-Fi SoftAP, 1: Mở Web Cài Đặt (http://192.168.4.1/#wifi)
  static bool s40AboutMenuOpen = false;
  static int  s40AboutMenuCursor = 0;
  static int  s40OtaCheckState = 0; // 0: Bình thường, 1: Đang kiểm tra, 2: Có bản mới, 3: Bản mới nhất, 4: Lỗi mạng
  static OtaUpdateInfo s40OtaUpdateInfo;
  static int s40MemSubState = 0;
  static int s40MemFileCursor = 0;
  static int s40MemOptionCursor = 0;

  // Trạng thái Ứng dụng "Gọi Trợ Lý XiaoZhi AI" (Mode 11)
  enum AiConvState {
    AI_STATE_IDLE = 0,
    AI_STATE_LISTENING = 1,
    AI_STATE_THINKING = 2,
    AI_STATE_REPLYING = 3,
    AI_STATE_BINDING = 4,         // Chờ liên kết XiaoZhi Hub qua mã OTP
    AI_STATE_BINDING_CHECKING = 5 // Đang kết nối server kiểm tra trạng thái liên kết
  };
  static AiConvState s40AiState = AI_STATE_IDLE;
  static int s40AiPromptCursor = 0;
  static String s40AiUserSpeechText = "Xin chào XiaoZhi, giới thiệu về bạn và nhiệt độ phòng hiện tại đi!";
  static String s40AiReplyText = "Nhấn phím [OK] để bắt đầu trò chuyện với XiaoZhi qua Micro.";
  static String s40AiWrappedLines[28];
  static int s40AiTotalLines = 0;
  static int s40AiScrollLine = 0;
  static unsigned long s40AiStateStartMs = 0;
  static unsigned long s40AiLastVoiceMs = 0;
  static unsigned long s40AiLastScrollMs = 0;
  static bool s40AiHeardSpeech = false;
  static int s40AiMaxMicLevel = 0;
  static String s40AiOtpCode = "...";
  static String s40AiMacStr = "";
  static String s40AiBindStatus = "Dang dong bo voi XiaoZhi Cloud...";
  static unsigned long s40AiLastOtpPollMs = 0;

  static String s40ToastMsg = "";
  static unsigned long s40ToastExpireMs = 0;

  // Cấu hình Độ sáng màn hình, Thời gian tắt màn hình & Always On Display (AOD)
  static int screenBrightnessPct = 100; // 10% -> 100%
  static int screenTimeoutIdx = 2;      // 0: Luôn sáng, 1: 15s, 2: 30s (MẶC ĐỊNH), 3: 1p, 4: 5p, 5: 10p
  static const uint16_t TIMEOUT_SECONDS_LIST[6] = { 0, 15, 30, 60, 300, 600 };
  static const char* TIMEOUT_LABELS[6] = { "Luôn sáng", "15 giây", "30 giây", "1 phút", "5 phút", "10 phút" };
  static unsigned long lastUserActivityMs = 0;
  static bool isScreenSleeping = false;
  static bool alwaysOnDisplayEnabled = true; // true = Hiện đồng hồ mờ nền đen (AOD), false = Tắt hẳn màn hình (Sleep)
  static int aodClockStyle = 1;              // 0: Tắt màn hình, 1: Thường (Digital), 2: Lật số (Retro Flip), 3: Đồng hồ kim (Analog)
  static int lastAodDrawnMinute = -1;
  static bool lastTtp223State = false;
  static unsigned long lastTtp223ChangeMs = 0;
  static bool keyBeepEnabled = true;
  static int s40AlarmTuneIdx = 1; // 0: Bíp Dồn Dập, 1: Nokia Tune Cổ Điển, 2: SMS Morse, 3: Chuông 4 Nốt
  static int micSensitivityMode = 1; // 0: Thấp, 1: Tiêu chuẩn, 2: Cao
  static bool lastPcLiveState = false;

  static bool s40SystemPrefsLoaded = false;
  static void loadSystemSettingsPrefsIfNeeded() {
    if (s40SystemPrefsLoaded) return;
    s40SystemPrefsLoaded = true;
    Preferences p;
    if (p.begin("s40_sys", true)) {
      screenBrightnessPct    = p.getInt("bright", screenBrightnessPct);
      screenTimeoutIdx       = p.getInt("tout", 2) % 6; // Mặc định 30s tự vào AOD
      aodClockStyle          = p.getInt("aod_sty", 1) % 4;
      alwaysOnDisplayEnabled = (aodClockStyle > 0);
      keyBeepEnabled         = p.getBool("kbeep", keyBeepEnabled);
      micSensitivityMode     = p.getInt("msens", 1) % 3;
      TestAudio::setMicSensitivityMode(micSensitivityMode);
      int wmMod              = p.getInt("wm_mod", 0) % 5;
      TestWindmill::setMode(wmMod);
      p.end();
    }
  }

  static void saveSystemSettingsPrefs() {
    Preferences p;
    if (p.begin("s40_sys", false)) {
      p.putInt("bright", screenBrightnessPct);
      p.putInt("tout", screenTimeoutIdx);
      p.putInt("aod_sty", aodClockStyle);
      p.putBool("kbeep", keyBeepEnabled);
      p.putInt("msens", micSensitivityMode);
      p.putInt("wm_mod", TestWindmill::getMode());
      p.end();
    }
  }

  // Cấu hình Màn Hình Chờ đọc từ /standby_config.json (đồng bộ 100% với Web UI)
  struct StandbyConfig {
    String theme       = "cyberpunk";
    String clockStyle  = "digital";
    uint16_t clockColor = 0x3DF7;
    String clockFormat = "24h";
    bool showSeconds   = true;
    String clockPos    = "center"; // "top", "center", "bottom"
    bool showDate      = true;
    String dateFormat  = "vi";
    bool showWeather   = true;
    float temp         = 28.5f;
    int humidity       = 65;
    String customText  = "Trạm Decor Vũ Trụ *";
    uint16_t textColor = C_WHITE;
    uint16_t bgColor   = 0x0842;
    String bgMode      = "image";  // "image", "gradient-cyber", "gradient-nebula", "gradient-sunset", "solid-black"
    String bgImage     = "";
    int dimOverlay     = 35;       // Độ tối lớp phủ nền (0..85%)
  };

  static StandbyConfig stCfg;
  static bool ntpConfigured = false;
  static unsigned long lastSecondTick = 0;
  static unsigned long lastAnimTick = 0;
  static int lastDrawnMinute = -1;
  static bool lastWifiState = false;
  static uint8_t eyeState = 0;
  static unsigned long lastExternalEmojiSync = 0;
  static bool isAutoBlinkActive = false;
  static String customSubtitle = "Xin chào! Mình là trợ lý ảo XiaoZhi trên Trạm Decor Vũ Trụ!";
  static uint8_t graphHistoryCpu[20] = {0};
  static uint8_t graphHistoryGpu[20] = {0};

  String getEmojiStateName() {
    const char* names[] = {
      "Bình thường", "Chớp mắt", "Vui vẻ", "Nhìn trái", "Nhìn phải", "Nhìn lên", "Nhìn xuống",
      "Giận dỗi", "Ngạc nhiên", "Nháy trái", "Nháy phải", "Buồn ngủ"
    };
    return names[eyeState % 12];
  }

  // Bộ đệm lưu lát cắt nền phía sau đồng hồ (240 x 44 px = 21KB) giúp vẽ chữ nổi trong suốt trên nền ảnh JPEG mà không nháy hình
  static const int CLOCK_STRIP_W = 240;
  static const int CLOCK_STRIP_H = 44;
  static uint16_t* clockBgBuf = nullptr;
  static VnCanvas16* clockCanvas = nullptr;
  static int activeClockStripY = 112;

  // Chuyển mã màu Hex "#RRGGBB" sang 16-bit RGB565
  static uint16_t hexToRgb565(const String& hexStr, uint16_t fallback = 0x3DF7) {
    String s = hexStr;
    s.trim();
    if (s.startsWith("#")) s = s.substring(1);
    if (s.length() != 6) return fallback;
    long val = strtol(s.c_str(), nullptr, 16);
    uint8_t r = (val >> 16) & 0xFF;
    uint8_t g = (val >> 8) & 0xFF;
    uint8_t b = val & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  // Làm tối một pixel RGB565 theo tỷ lệ keep256 (0 = đen tuyền, 255 = giữ nguyên)
  static inline uint16_t dimRgb565(uint16_t col, uint16_t keep256) {
    uint16_t r = ((col >> 11) & 0x1F);
    uint16_t g = ((col >> 5) & 0x3F);
    uint16_t b = (col & 0x1F);
    r = (r * keep256) >> 8;
    g = (g * keep256) >> 8;
    b = (b * keep256) >> 8;
    return (r << 11) | (g << 5) | b;
  }

  // Nội suy tuyến tính giữa 2 màu RGB565 (t: 0..255) để vẽ nền Gradient mượt mà
  static uint16_t lerpRgb565(uint16_t c1, uint16_t c2, uint8_t t) {
    uint16_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    uint16_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
    uint16_t r = r1 + (((int)r2 - (int)r1) * t) / 255;
    uint16_t g = g1 + (((int)g2 - (int)g1) * t) / 255;
    uint16_t b = b1 + (((int)b2 - (int)b1) * t) / 255;
    return (r << 11) | (g << 5) | b;
  }

  // Kiểm tra xem tọa độ (x, y) có nằm trong các khung kính mờ (Frosted Glass Pills) giống Web UI hay không
  static inline bool isInsideGlassPill(int x, int y) {
    // 1. Pill Wi-Fi góc trên trái (x: 8..92, y: 8..25)
    if (y >= 8 && y <= 25 && x >= 8 && x <= 92) return true;
    // 2. Pill ESP32-S3 góc trên phải (x: 164..231, y: 8..25)
    if (y >= 8 && y <= 25 && x >= 164 && x <= 231) return true;
    // 3. Pill Thời tiết ở đáy (x: 42..198, y: 262..283)
    if (stCfg.showWeather && y >= 262 && y <= 283 && x >= 42 && x <= 198) return true;
    return false;
  }

  // Giữ nguyên Tiếng Việt UTF-8 có dấu để hiển thị qua VnGfxAdapter, chỉ lọc các biểu tượng Emoji 4-byte
  static String toCleanAscii(const String& input) {
    String s = input;
    s.replace("✨", "*");
    s.replace("🌡️", "");
    s.replace("💧", "");
    s.replace("⏰", "");
    s.replace("😊", "");
    s.replace("📊", "");
    s.replace("🎵", "");
    s.replace("–", "-");
    s.replace("—", "-");
    s.replace("“", "\"");
    s.replace("”", "\"");
    s.trim();
    return s;
  }

  // Tìm đường dẫn file ảnh nền thực tế (Ưu tiên Thẻ nhớ SD /sd_images + /sd_rgb565 0ms, sau đó đến LittleFS)
  static String resolveWallpaperPath() {
    auto checkPathExists = [](const String& raw) -> String {
      if (raw.length() == 0) return "";
      if (raw.startsWith("sd:")) {
        String sp = raw.substring(3);
        if (TestSDCard::isMounted() && (SD.exists(sp) || ImageManager::getRgb565CompanionPath(sp).length() > 0)) {
          return raw;
        }
      }
      String p = raw.startsWith("/") ? raw : ("/" + raw);
      if (TestSDCard::isMounted() && (p.startsWith("/sd_images/") || p.startsWith("/sd_rgb565/"))) {
        if (SD.exists(p) || ImageManager::getRgb565CompanionPath(p).length() > 0) {
          return "sd:" + p;
        }
      }
      if (LittleFS.exists(p)) return p;
      return "";
    };

    // 1. Ưu tiên ảnh khai báo trong /standby_config.json
    String p1 = checkPathExists(stCfg.bgImage);
    if (p1.length() > 0) return p1;

    // 2. Tiếp theo kiểm tra ảnh đang active trong ImageManager (Preferences)
    String p2 = checkPathExists(ImageManager::getActiveImage());
    if (p2.length() > 0) return p2;

    // 3. Quét thư mục /sd_images trên Thẻ nhớ SD trước
    if (TestSDCard::isMounted()) {
      ImageManager::ensureSdFolders();
      File sdDir = SD.open("/sd_images");
      if (sdDir && sdDir.isDirectory()) {
        String lastSd = "";
        File f = sdDir.openNextFile();
        while (f) {
          if (!f.isDirectory()) {
            String fn = String(f.name());
            int sl = fn.lastIndexOf('/');
            if (sl >= 0) fn = fn.substring(sl + 1);
            String low = fn;
            low.toLowerCase();
            if (low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp")) {
              lastSd = "sd:/sd_images/" + fn;
            }
          }
          f.close();
          f = sdDir.openNextFile();
        }
        sdDir.close();
        if (lastSd.length() > 0) return lastSd;
      }
    }

    // 4. Quét LittleFS tìm ảnh .jpg / .jpeg mới nhất mà người dùng đã tải lên
    String found = "";
    File root = LittleFS.open("/");
    if (root) {
      File file = root.openNextFile();
      while (file) {
        String fname = String(file.name());
        if (!fname.startsWith("/")) fname = "/" + fname;
        if (fname.endsWith(".jpg") || fname.endsWith(".jpeg")) {
          found = fname;
        }
        file = root.openNextFile();
      }
    }
    return found;
  }

  // Đọc cấu hình từ /standby_config.json trên LittleFS
  static void loadConfigFromFile() {
    if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
      File f = LittleFS.open(FILE_STANDBY_CONFIG, "r");
      if (f) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (!err) {
          stCfg.theme       = doc["theme"] | "cyberpunk";
          stCfg.clockStyle  = doc["clock_style"] | "digital";
          stCfg.clockColor  = hexToRgb565(doc["clock_color"] | "#38bdf8", C_NEON_CYAN);
          stCfg.clockFormat = doc["clock_format"] | "24h";
          stCfg.showSeconds = doc["show_seconds"] | true;
          stCfg.clockPos    = doc["clock_pos"] | "center";
          stCfg.showDate    = doc["show_date"] | true;
          stCfg.dateFormat  = doc["date_format"] | "vi";
          stCfg.showWeather = doc["show_weather"] | true;
          stCfg.temp        = doc["temp"] | 28.5f;
          stCfg.humidity    = doc["humidity"] | 65;
          stCfg.customText  = toCleanAscii(doc["custom_text"] | "Tram Decor Vu Tru *");
          if (stCfg.customText.length() == 0) stCfg.customText = "Tram Decor Vu Tru *";
          stCfg.textColor   = hexToRgb565(doc["text_color"] | "#f8fafc", C_WHITE);
          stCfg.bgColor     = hexToRgb565(doc["bg_color"] | "#0a0f1d", C_DARK_BG);
          stCfg.bgMode      = doc["bg_mode"] | "image";
          stCfg.bgImage     = doc["bg_image"] | "";
          if (stCfg.bgImage.startsWith("sd:")) stCfg.bgImage = stCfg.bgImage.substring(3);
          stCfg.dimOverlay  = doc["dim_overlay"] | 35;
          if (stCfg.bgColor == C_WHITE) stCfg.bgColor = C_DARK_BG;
          if (!doc["ui_theme_idx"].isNull()) {
            s40UiThemeIdx = ((doc["ui_theme_idx"].as<int>() % S40_THEME_COUNT) + S40_THEME_COUNT) % S40_THEME_COUNT;
          } else {
            for (int ti = 0; ti < S40_THEME_COUNT; ti++) {
              if (stCfg.theme == S40_THEMES[ti].id) { s40UiThemeIdx = ti; break; }
            }
          }
        }
      }
    }
    // Tự động đồng bộ đường dẫn ảnh nếu đã có ảnh trong Thẻ nhớ SD hoặc LittleFS, nhưng KHÔNG ghi đè nếu bgMode đang là Gradient / Nền đen!
    String validImg = resolveWallpaperPath();
    if (validImg.startsWith("sd:")) validImg = validImg.substring(3);
    if (validImg.length() > 0 && stCfg.bgImage.length() == 0) {
      stCfg.bgImage = validImg;
    }
  }

  // Chuyển màu RGB565 sang mã Hex "#RRGGBB" để lưu xuống /standby_config.json
  static String rgb565ToHex(uint16_t col) {
    uint8_t r = ((col >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((col >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (col & 0x1F) * 255 / 31;
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return String(buf);
  }

  static bool standbyCacheValid = false;
  static String lastStandbyCacheSig = "";

  // Lưu cấu hình Màn Hình Chờ hiện tại xuống /standby_config.json (đồng bộ 2 chiều với Web UI)
  static void saveConfigToFile() {
    if (stCfg.bgImage.startsWith("sd:")) {
      stCfg.bgImage = stCfg.bgImage.substring(3);
    }
    if (stCfg.bgImage.length() > 0 && !stCfg.bgImage.startsWith("/")) {
      stCfg.bgImage = "/" + stCfg.bgImage;
    }
    // Bắt buộc xóa hiệu lực Cache RAM để khi quay ra Màn Hình Chờ sẽ vẽ lại đúng chế độ nền mới (Gradient / Nền đen / Ảnh mới)
    standbyCacheValid = false;
    lastStandbyCacheSig = "";

    JsonDocument doc;
    if (LittleFS.exists(FILE_STANDBY_CONFIG)) {
      File rf = LittleFS.open(FILE_STANDBY_CONFIG, "r");
      if (rf) {
        deserializeJson(doc, rf);
        rf.close();
      }
    }
    doc["theme"]        = stCfg.theme;
    doc["ui_theme_idx"] = s40UiThemeIdx;
    doc["clock_style"]  = stCfg.clockStyle;
    doc["clock_color"]  = rgb565ToHex(stCfg.clockColor);
    doc["clock_format"] = stCfg.clockFormat;
    doc["show_seconds"] = stCfg.showSeconds;
    doc["clock_pos"]    = stCfg.clockPos;
    doc["show_date"]    = stCfg.showDate;
    doc["date_format"]  = stCfg.dateFormat;
    doc["show_weather"] = stCfg.showWeather;
    doc["temp"]         = stCfg.temp;
    doc["humidity"]     = stCfg.humidity;
    doc["custom_text"]  = stCfg.customText;
    doc["bg_mode"]      = stCfg.bgMode;
    doc["bg_image"]     = stCfg.bgImage;
    doc["dim_overlay"]  = stCfg.dimOverlay;

    File wf = LittleFS.open(FILE_STANDBY_CONFIG, "w");
    if (wf) {
      serializeJson(doc, wf);
      wf.close();
    }
    if (stCfg.bgImage.length() > 0) {
      ImageManager::selectActiveImage(stCfg.bgImage);
    }
  }

  // Áp dụng Phong cách Giao diện (Theme S40 0..5) cho toàn hệ thống + đồng bộ màu Đồng hồ Màn Hình Chờ
  static void applyS40UiTheme(int newThemeIdx) {
    s40UiThemeIdx = ((newThemeIdx % S40_THEME_COUNT) + S40_THEME_COUNT) % S40_THEME_COUNT;
    const S40ThemeSpec& th = curTheme();
    stCfg.theme      = th.id;
    stCfg.clockColor = th.neonCyan;
    stCfg.bgColor    = th.darkBg;
    if (stCfg.bgMode.startsWith("gradient-")) {
      if (s40UiThemeIdx == 0 || s40UiThemeIdx == 4) stCfg.bgMode = "gradient-cyber";
      else if (s40UiThemeIdx == 1) stCfg.bgMode = "gradient-nebula";
      else if (s40UiThemeIdx == 5) stCfg.bgMode = "gradient-cyber";
      else stCfg.bgMode = "gradient-sunset";
    }
    saveConfigToFile();
  }

  // Lấy danh sách tất cả file ảnh trên Thẻ nhớ SD (/sd_images) & LittleFS cho App Thư Viện & App Hình Nền
  static int getStoredJpgList(String outNames[], size_t outSizes[], int maxItems) {
    int count = 0;

    // 1. Quét thư mục /sd_images trên Thẻ nhớ SD trước (chứa ảnh PNG/JPG + RGB565 0ms)
    if (TestSDCard::isMounted()) {
      ImageManager::ensureSdFolders();
      File sdDir = SD.open("/sd_images");
      if (sdDir && sdDir.isDirectory()) {
        File f = sdDir.openNextFile();
        while (f && count < maxItems) {
          if (!f.isDirectory()) {
            String fname = String(f.name());
            int sl = fname.lastIndexOf('/');
            if (sl >= 0) fname = fname.substring(sl + 1);
            String lower = fname;
            lower.toLowerCase();
            if (lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".bmp")) {
              outNames[count] = "/sd_images/" + fname;
              if (outSizes) outSizes[count] = f.size();
              count++;
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
    if (!root) return count;
    File file = root.openNextFile();
    while (file && count < maxItems) {
      String fname = String(file.name());
      if (!fname.startsWith("/")) fname = "/" + fname;
      String lower = fname;
      lower.toLowerCase();
      if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
        outNames[count] = fname;
        if (outSizes) outSizes[count] = file.size();
        count++;
      }
      file = root.openNextFile();
    }
    return count;
  }

  // Điều chỉnh độ sáng đèn nền màn hình bằng xung PWM trên chân GPIO 7 (PIN_TFT_BL)
  void setBacklightBrightness(int pct) {
    screenBrightnessPct = constrain(pct, 10, 100);
    if (!isScreenSleeping) {
      uint32_t duty = (uint32_t)((screenBrightnessPct * 255) / 100);
      ledcWrite(0, duty);
    }
  }

  int getBacklightBrightness() {
    return screenBrightnessPct;
  }

  // Lấy thời gian hiện tại (từ NTP nếu đã đồng bộ, hoặc đếm từ mốc mặc định)
  static void getCurrentDateTime(int& hour, int& minute, int& second, int& wday, int& day, int& month, int& year) {
    struct tm timeinfo;
    if (ntpConfigured && getLocalTime(&timeinfo, 5)) {
      hour   = timeinfo.tm_hour;
      minute = timeinfo.tm_min;
      second = timeinfo.tm_sec;
      wday   = timeinfo.tm_wday; // 0 = CN, 1 = T2 ... 6 = T7
      day    = timeinfo.tm_mday;
      month  = timeinfo.tm_mon + 1;
      year   = timeinfo.tm_year + 1900;
      return;
    }
    unsigned long totalSec = (millis() / 1000) + (0 * 3600 + 35 * 60);
    second = totalSec % 60;
    minute = (totalSec / 60) % 60;
    hour   = (totalSec / 3600) % 24;
    wday   = 5; // Thứ Sáu
    day    = 25;
    month  = 9;
    year   = 2026;
  }

  // Tính tọa độ Y của dải Đồng Hồ (theo đúng vị trí trên Web UI: Top / Center / Bottom)
  static int getStandbyClockStripY() {
    if (stCfg.clockPos == "top") return 50;
    if (stCfg.clockPos == "bottom") return 182;
    return 112; // "center" mặc định
  }

  // ============================================================================
  // BỘ ĐỆM KHUNG HÌNH RAM (4 DẢI 240x80 = 240x320 FULL-SCREEN CACHE)
  // Chia thành 4 dải 38.4KB giúp cấp phát 100% thành công trên SRAM nội của ESP32-S3
  // ============================================================================
  static uint16_t* standbyStripCache[4] = { nullptr, nullptr, nullptr, nullptr };

  static bool ensureStandbyStripCache() {
    for (int s = 0; s < 4; s++) {
      if (!standbyStripCache[s]) {
        if (psramFound()) {
          standbyStripCache[s] = (uint16_t*)ps_malloc(240 * 80 * sizeof(uint16_t));
        }
        if (!standbyStripCache[s]) {
          standbyStripCache[s] = (uint16_t*)malloc(240 * 80 * sizeof(uint16_t));
        }
        if (!standbyStripCache[s]) return false;
      }
    }
    return true;
  }

  static inline void setCachePixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= 240 || y < 0 || y >= 320) return;
    int s = y / 80;
    int sy = y % 80;
    if (standbyStripCache[s]) {
      standbyStripCache[s][sy * 240 + x] = color;
    }
  }

  static inline uint16_t getCachePixel(int x, int y) {
    if (x < 0 || x >= 240 || y < 0 || y >= 320) return 0;
    int s = y / 80;
    int sy = y % 80;
    return standbyStripCache[s] ? standbyStripCache[s][sy * 240 + x] : 0;
  }

  static void clearCacheRect(int x, int y, int w, int h, uint16_t color) {
    for (int py = y; py < y + h; py++) {
      if (py < 0 || py >= 320) continue;
      int s = py / 80;
      int sy = py % 80;
      if (!standbyStripCache[s]) continue;
      uint16_t* rowPtr = &standbyStripCache[s][sy * 240];
      for (int px = max(0, x); px < min(240, x + w); px++) {
        rowPtr[px] = color;
      }
    }
  }

  static void flushCacheToTft() {
    if (!tft) return;
    for (int s = 0; s < 4; s++) {
      if (standbyStripCache[s]) {
        tft->drawRGBBitmap(0, s * 80, standbyStripCache[s], 240, 80);
      }
    }
  }

  // Đọc toàn bộ file JPEG từ LittleFS vào RAM trước khi giải mã để loại bỏ độ trễ đọc Flash từng khối
  static bool decodeFsJpgFromRam(int16_t x, int16_t y, const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    size_t sz = f.size();
    if (sz == 0 || sz > 65536) {
      f.close();
      return (TJpgDec.drawFsJpg(x, y, path, LittleFS) == JDR_OK);
    }
    uint8_t* jpgBuf = (uint8_t*)malloc(sz);
    if (!jpgBuf) {
      f.close();
      return (TJpgDec.drawFsJpg(x, y, path, LittleFS) == JDR_OK);
    }
    size_t nRead = f.read(jpgBuf, sz);
    f.close();
    bool ok = false;
    if (nRead == sz) {
      ok = (TJpgDec.drawJpg(x, y, jpgBuf, sz) == JDR_OK);
    }
    free(jpgBuf);
    return ok;
  }

  // Callback giải mã từng khối MCU của ảnh JPEG thẳng vào Bộ Đệm RAM (Không vẽ từng ô lẻ lên màn hình!)
  static bool jpgRenderCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!tft || y >= 320 || x >= 240) return false;

    uint16_t baseKeep = (uint16_t)((100 - constrain(stCfg.dimOverlay, 0, 85)) * 255 / 100);
    uint16_t pillKeep = (baseKeep * 95) >> 8;
    bool hasCache = (standbyStripCache[0] != nullptr);

    for (uint16_t row = 0; row < h; row++) {
      int py = y + row;
      if (py < 0 || py >= 320) continue;
      bool inClockStrip = (clockBgBuf != nullptr && py >= activeClockStripY && py < activeClockStripY + CLOCK_STRIP_H);

      for (uint16_t col = 0; col < w; col++) {
        int px = x + col;
        if (px < 0 || px >= 240) continue;
        uint32_t idx = row * w + col;
        uint16_t keep = isInsideGlassPill(px, py) ? pillKeep : baseKeep;
        uint16_t dimmed = dimRgb565(bitmap[idx], keep);
        bitmap[idx] = dimmed;

        if (hasCache) {
          setCachePixel(px, py, dimmed);
        }
        if (inClockStrip) {
          clockBgBuf[(py - activeClockStripY) * CLOCK_STRIP_W + px] = dimmed;
        }
      }
    }

    if (!hasCache) {
      tft->drawRGBBitmap(x, y, bitmap, w, h);
    }
    return true;
  }

  // Vẽ nền Màn Hình Chờ (Giải mã trong RAM + Lưu Cache 240x320 -> Đẩy 1 lần ra màn hình không bị quét dòng!)
  static void renderStandbyBackground() {
    if (!tft) return;
    activeClockStripY = getStandbyClockStripY();

    if (!clockBgBuf) {
      if (psramFound()) {
        clockBgBuf = (uint16_t*)ps_malloc(CLOCK_STRIP_W * CLOCK_STRIP_H * sizeof(uint16_t));
      }
      if (!clockBgBuf) {
        clockBgBuf = (uint16_t*)malloc(CLOCK_STRIP_W * CLOCK_STRIP_H * sizeof(uint16_t));
      }
    }
    if (!clockCanvas) {
      clockCanvas = new VnCanvas16(CLOCK_STRIP_W, CLOCK_STRIP_H);
    }

    bool hasCache = ensureStandbyStripCache();
    String imgPath = resolveWallpaperPath();
    String curSig = stCfg.bgMode + "|" + imgPath + "|" + String(stCfg.dimOverlay) + "|" +
                    String(stCfg.showWeather ? 1 : 0) + "|" + String(activeClockStripY);

    // NẾU ĐÃ CÓ SẴN TRONG BỘ ĐỆM RAM (Khi từ Menu bấm EXIT quay về Màn Hình Chờ):
    // Đẩy ngay lập tức 240x320 từ RAM ra màn hình (0ms đọc Flash, 0ms giải mã JPEG, không bị quét từng dòng!)
    if (hasCache && standbyCacheValid && curSig == lastStandbyCacheSig) {
      if (clockBgBuf) {
        for (int r = 0; r < CLOCK_STRIP_H; r++) {
          int py = activeClockStripY + r;
          for (int c = 0; c < CLOCK_STRIP_W; c++) {
            clockBgBuf[r * CLOCK_STRIP_W + c] = getCachePixel(c, py);
          }
        }
      }
      flushCacheToTft();
      return;
    }

    if (hasCache) {
      clearCacheRect(0, 0, 240, 320, C_BLACK);
    }

    bool renderedJpg = false;
    if (stCfg.bgMode == "image" && imgPath.length() > 0) {
      // 1. ƯU TIÊN TUYỆT ĐỐI: Đọc trực tiếp file .RGB565 (240x320x2 = 153.6KB) từ Thẻ nhớ SD (0ms giải mã!)
      String rgb565Path = ImageManager::getRgb565CompanionPath(imgPath);
      if (hasCache && rgb565Path.length() > 0 && TestSDCard::isMounted()) {
        File rf = SD.open(rgb565Path, FILE_READ);
        if (rf && rf.size() >= 240 * 320 * 2) {
          uint16_t baseKeep = (uint16_t)((100 - constrain(stCfg.dimOverlay, 0, 85)) * 255 / 100);
          uint16_t pillKeep = (baseKeep * 95) >> 8;
          bool okRead = true;
          for (int s = 0; s < 4; s++) {
            size_t want = 240 * 80 * sizeof(uint16_t);
            if (rf.read((uint8_t*)standbyStripCache[s], want) != want) {
              okRead = false;
              break;
            }
            for (int sy = 0; sy < 80; sy++) {
              int py = s * 80 + sy;
              bool inClockStrip = (clockBgBuf != nullptr && py >= activeClockStripY && py < activeClockStripY + CLOCK_STRIP_H);
              uint16_t* rowPtr = &standbyStripCache[s][sy * 240];
              for (int px = 0; px < 240; px++) {
                uint16_t keep = isInsideGlassPill(px, py) ? pillKeep : baseKeep;
                uint16_t dimmed = dimRgb565(rowPtr[px], keep);
                rowPtr[px] = dimmed;
                if (inClockStrip) {
                  clockBgBuf[(py - activeClockStripY) * CLOCK_STRIP_W + px] = dimmed;
                }
              }
            }
          }
          rf.close();
          if (okRead) {
            renderedJpg = true;
          }
        } else if (rf) {
          rf.close();
        }
      }

      // 2. Nếu chưa có bản .rgb565, giải mã file .jpg từ SD hoặc LittleFS trong RAM
      if (!renderedJpg) {
        String fsPath = imgPath.startsWith("sd:") ? imgPath.substring(3) : imgPath;
        bool isSdFile = imgPath.startsWith("sd:");
        uint16_t jw = 0, jh = 0;
        TJpgDec.setCallback(jpgRenderCallback);
        TJpgDec.setSwapBytes(false);

        if (isSdFile && TestSDCard::isMounted() && SD.exists(fsPath)) {
          File sf = SD.open(fsPath, FILE_READ);
          size_t sz = sf ? sf.size() : 0;
          if (sz > 0 && sz <= 90000) {
            uint8_t* buf = (uint8_t*)malloc(sz);
            if (buf) {
              if (sf.read(buf, sz) == sz && TJpgDec.getJpgSize(&jw, &jh, buf, sz) == JDR_OK && jw > 0 && jh > 0) {
                uint8_t scale = 1;
                if (jw >= 960 || jh >= 1280) scale = 4;
                else if (jw >= 480 || jh >= 640) scale = 2;
                TJpgDec.setJpgScale(scale);
                int offsetX = max(0, (240 - (int)(jw / scale)) / 2);
                int offsetY = max(0, (320 - (int)(jh / scale)) / 2);
                if (TJpgDec.drawJpg(offsetX, offsetY, buf, sz) == JDR_OK) renderedJpg = true;
              }
              free(buf);
            }
          }
          if (sf) sf.close();
        } else if (!isSdFile && TJpgDec.getFsJpgSize(&jw, &jh, fsPath.c_str(), LittleFS) == JDR_OK && jw > 0 && jh > 0) {
          uint8_t scale = 1;
          if (jw >= 960 || jh >= 1280) scale = 4;
          else if (jw >= 480 || jh >= 640) scale = 2;
          TJpgDec.setJpgScale(scale);

          int drawW = jw / scale;
          int drawH = jh / scale;
          int offsetX = (240 - drawW) / 2;
          int offsetY = (320 - drawH) / 2;

          if (decodeFsJpgFromRam(max(0, offsetX), max(0, offsetY), fsPath.c_str())) {
            renderedJpg = true;
          }
        }
      }
    }

    if (!renderedJpg) {
      uint16_t cTop = hexToRgb565("#0b132b"), cMid = hexToRgb565("#1c2541"), cBot = hexToRgb565("#3a0ca3");
      if (stCfg.bgMode == "gradient-nebula") {
        cTop = hexToRgb565("#2b0938"); cMid = hexToRgb565("#0f172a"); cBot = hexToRgb565("#1e1b4b");
      } else if (stCfg.bgMode == "gradient-sunset") {
        cTop = hexToRgb565("#3d1308"); cMid = hexToRgb565("#1e1b4b"); cBot = hexToRgb565("#0f172a");
      } else if (stCfg.bgMode == "solid-black") {
        cTop = C_BLACK; cMid = C_BLACK; cBot = C_BLACK;
      }

      uint16_t baseKeep = (uint16_t)((100 - constrain(stCfg.dimOverlay, 0, 85)) * 255 / 100);
      uint16_t pillKeep = (baseKeep * 95) >> 8;
      uint16_t lineBuf[240];

      for (int y = 0; y < 320; y++) {
        uint16_t baseCol = (y < 160)
          ? lerpRgb565(cTop, cMid, (uint8_t)((y * 255) / 160))
          : lerpRgb565(cMid, cBot, (uint8_t)(((y - 160) * 255) / 160));

        uint16_t normalCol = dimRgb565(baseCol, baseKeep);
        uint16_t glassCol  = dimRgb565(baseCol, pillKeep);
        bool inClockStrip  = (clockBgBuf != nullptr && y >= activeClockStripY && y < activeClockStripY + CLOCK_STRIP_H);

        for (int x = 0; x < 240; x++) {
          uint16_t pxCol = isInsideGlassPill(x, y) ? glassCol : normalCol;
          lineBuf[x] = pxCol;
          if (hasCache) {
            setCachePixel(x, y, pxCol);
          }
          if (inClockStrip) {
            clockBgBuf[(y - activeClockStripY) * CLOCK_STRIP_W + x] = pxCol;
          }
        }
        if (!hasCache) {
          tft->drawRGBBitmap(0, y, lineBuf, 240, 1);
        }
      }
    }

    if (hasCache) {
      standbyCacheValid = true;
      lastStandbyCacheSig = curSig;
      flushCacheToTft();
    }
  }

  // Vẽ chữ có bóng đổ đen (Drop Shadow) trong suốt trên nền ảnh Wallpaper
  static void drawShadowText(int x, int y, const String& text, uint16_t color, uint8_t size = 1) {
    if (!tft) return;
    tft->setTextSize(size);
    tft->setTextColor(C_BLACK);
    tft->setCursor(x + 1, y + 1);
    tft->print(text);
    tft->setCursor(x + 1, y);
    tft->print(text);
    tft->setTextColor(color);
    tft->setCursor(x, y);
    tft->print(text);
  }

  // Vẽ thanh trạng thái đặc cho các màn hình Emoji / PC HUD / Diagnostic
  static void drawTopStatusBar() {
    if (!tft) return;
    int w = tft->width();
    tft->fillRect(0, 0, w, 24, C_CARD_BG);
    tft->drawFastHLine(0, 24, w, C_CARD_BORDER);

    bool connected = (WiFi.status() == WL_CONNECTED);
    uint16_t dotColor = connected ? C_NEON_GREEN : C_NEON_AMBER;
    tft->fillCircle(10, 12, 4, dotColor);

    tft->setTextSize(1);
    tft->setTextColor(C_WHITE, C_CARD_BG);
    tft->setCursor(18, 8);
    if (connected) {
      String ssid = toCleanAscii(WifiManager::getCurrentSsid());
      if (ssid.length() > 14) ssid = ssid.substring(0, 14);
      tft->printf("WiFi: %-14s", ssid.c_str());
    } else {
      tft->print("AP: TramVuTru-Config");
    }

    tft->drawRect(w - 38, 6, 26, 12, C_NEON_GREEN);
    tft->fillRect(w - 12, 9, 3, 6, C_NEON_GREEN);
    tft->fillRect(w - 36, 8, 22, 8, C_NEON_GREEN);
    tft->setTextColor(C_BLACK, C_NEON_GREEN);
    tft->setCursor(w - 34, 8);
    tft->print("5V");
  }

  // Vẽ đôi mắt robot XiaoZhi đồng bộ 100% với 12 trạng thái mắt tròn phát sáng trên Web UI
  static void drawXiaoZhiEyesBox(int boxX, int boxY, int boxW, int boxH, uint8_t state, uint8_t scaleMode) {
    if (!tft) return;
    int cx = boxX + boxW / 2;
    int cy = boxY + boxH / 2;

    if (scaleMode == 2) {
      // Màn hình Emoji chính: nền đen tuyền giống hệt khung màn hình ảo trên Web UI
      tft->fillRect(boxX, boxY, boxW, boxH, C_BLACK);
    } else {
      tft->fillRoundRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, 6, C_BLACK);
      tft->drawRoundRect(boxX, boxY, boxW, boxH, 6, C_NEON_CYAN);
    }

    int radius = (scaleMode == 2) ? 29 : 18;
    int gap    = (scaleMode == 2) ? 32 : 18;
    int leftCx  = cx - gap / 2 - radius;
    int rightCx = cx + gap / 2 + radius;
    int eyeCy   = cy;
    int pupR    = (scaleMode == 2) ? 6 : 4;

    uint16_t mainCyan = 0x5FFF; // #58ffff sáng rực như Web UI
    uint16_t haloCyan = C_NEON_CYAN;

    auto drawGlowingCircleEye = [&](int ex, int ey, int r, uint16_t col, int pupDx, int pupDy) {
      tft->drawCircle(ex, ey, r + 2, haloCyan);
      tft->drawCircle(ex, ey, r + 1, col);
      tft->fillCircle(ex, ey, r, col);
      tft->fillCircle(ex + pupDx, ey + pupDy, pupR, C_WHITE);
    };

    switch (state) {
      case 0: // IDLE (Bình thường - 2 mắt tròn phát sáng)
        drawGlowingCircleEye(leftCx,  eyeCy, radius, mainCyan,  6, -6);
        drawGlowingCircleEye(rightCx, eyeCy, radius, mainCyan,  6, -6);
        break;

      case 1: // BLINK (Nháy mắt - -)
        tft->fillRoundRect(leftCx - radius,  eyeCy - 4, radius * 2, 8, 4, mainCyan);
        tft->fillRoundRect(rightCx - radius, eyeCy - 4, radius * 2, 8, 4, mainCyan);
        break;

      case 2: // HAPPY (Vui vẻ ^ ^)
        drawGlowingCircleEye(leftCx,  eyeCy + 4, radius, C_NEON_GREEN, 0, -8);
        drawGlowingCircleEye(rightCx, eyeCy + 4, radius, C_NEON_GREEN, 0, -8);
        tft->fillCircle(leftCx,  eyeCy + radius / 2 + 6, radius, C_BLACK);
        tft->fillCircle(rightCx, eyeCy + radius / 2 + 6, radius, C_BLACK);
        break;

      case 3: // LOOK LEFT (Liếc Trái)
        drawGlowingCircleEye(leftCx - 12,  eyeCy, radius, mainCyan, -10, -4);
        drawGlowingCircleEye(rightCx - 12, eyeCy, radius, mainCyan, -10, -4);
        break;

      case 4: // LOOK RIGHT (Liếc Phải)
        drawGlowingCircleEye(leftCx + 12,  eyeCy, radius, mainCyan, 10, -4);
        drawGlowingCircleEye(rightCx + 12, eyeCy, radius, mainCyan, 10, -4);
        break;

      case 5: // LOOK UP (Nhìn Lên)
        drawGlowingCircleEye(leftCx,  eyeCy - 12, radius, mainCyan, 0, -11);
        drawGlowingCircleEye(rightCx, eyeCy - 12, radius, mainCyan, 0, -11);
        break;

      case 6: // LOOK DOWN (Nhìn Xuống)
        drawGlowingCircleEye(leftCx,  eyeCy + 12, radius, mainCyan, 0, 11);
        drawGlowingCircleEye(rightCx, eyeCy + 12, radius, mainCyan, 0, 11);
        break;

      case 7: // ANGRY (Giận >_<)
        tft->fillCircle(leftCx,  eyeCy, radius, C_NEON_PINK);
        tft->fillCircle(rightCx, eyeCy, radius, C_NEON_PINK);
        for (int i = -radius; i <= radius; i++) {
          int cutLeftY  = eyeCy - radius / 2 + (i + radius) / 3;
          int cutRightY = eyeCy - radius / 2 + (radius - i) / 3;
          tft->drawFastVLine(leftCx + i,  eyeCy - radius - 2, max(0, cutLeftY - (eyeCy - radius - 2)), C_BLACK);
          tft->drawFastVLine(rightCx + i, eyeCy - radius - 2, max(0, cutRightY - (eyeCy - radius - 2)), C_BLACK);
        }
        break;

      case 8: // CONFUSED (Bối rối @@)
        for (int r = radius; r >= 6; r -= 7) {
          tft->drawCircle(leftCx,  eyeCy, r, (r % 2 == 0) ? C_NEON_AMBER : C_WHITE);
          tft->drawCircle(rightCx, eyeCy, r, (r % 2 == 0) ? C_NEON_AMBER : C_WHITE);
        }
        tft->fillCircle(leftCx,  eyeCy, 4, C_NEON_AMBER);
        tft->fillCircle(rightCx, eyeCy, 4, C_NEON_AMBER);
        break;

      case 9: // WINK LEFT (Nháy mắt trái)
        tft->fillRoundRect(leftCx - radius, eyeCy - 4, radius * 2, 8, 4, mainCyan);
        drawGlowingCircleEye(rightCx, eyeCy, radius, mainCyan, 6, -6);
        break;

      case 10: // WINK RIGHT (Nháy mắt phải)
        drawGlowingCircleEye(leftCx, eyeCy, radius, mainCyan, 6, -6);
        tft->fillRoundRect(rightCx - radius, eyeCy - 4, radius * 2, 8, 4, mainCyan);
        break;

      case 11: // SLEEPY (Buồn ngủ)
      default:
        drawGlowingCircleEye(leftCx,  eyeCy + 4, radius, C_NEON_PURPLE, 0, 4);
        drawGlowingCircleEye(rightCx, eyeCy + 4, radius, C_NEON_PURPLE, 0, 4);
        tft->fillRect(leftCx - radius - 2,  eyeCy - radius - 2, radius * 2 + 4, radius + 2, C_BLACK);
        tft->fillRect(rightCx - radius - 2, eyeCy - radius - 2, radius * 2 + 4, radius + 2, C_BLACK);
        tft->drawFastHLine(leftCx - radius,  eyeCy, radius * 2, C_NEON_CYAN);
        tft->drawFastHLine(rightCx - radius, eyeCy, radius * 2, C_NEON_CYAN);
        break;
    }
  }

  // Cập nhật Đồng hồ số nổi trong suốt trên nền ảnh JPEG / Gradient (Không vẽ hộp đen che ảnh & không nháy hình)
  static void updateStandbyClockDigits(bool forceFullClock) {
    if (!tft || isScreenSleeping || currentMode != 0 || !clockCanvas) return;

    int hour, minute, second, wday, day, month, year;
    getCurrentDateTime(hour, minute, second, wday, day, month, year);

    bool is12h = (stCfg.clockFormat == "12h");
    bool isPM = (hour >= 12);
    int dispHour = hour;
    if (is12h) {
      dispHour = hour % 12;
      if (dispHour == 0) dispHour = 12;
    }

    char hmBuf[8];
    if (second % 2 == 0) {
      snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", dispHour, minute);
    } else {
      snprintf(hmBuf, sizeof(hmBuf), "%02d %02d", dispHour, minute);
    }

    // Khôi phục lát cắt ảnh nền 240x44 phía sau đồng hồ vào bộ đệm canvas
    if (clockBgBuf) {
      memcpy(clockCanvas->getBuffer(), clockBgBuf, CLOCK_STRIP_W * CLOCK_STRIP_H * sizeof(uint16_t));
    } else {
      clockCanvas->fillScreen(stCfg.bgColor);
    }

    int hmWidth = 5 * 24; // 120px (size 4)
    int secWidth = stCfg.showSeconds ? (3 * 12) : (is12h ? 28 : 0);
    int totalW = hmWidth + secWidth;
    int startX = (CLOCK_STRIP_W - totalW) / 2;
    int textY = 6;

    // 1. Vẽ bóng đổ đen đậm (Drop Shadow 2px) cho chữ HH:MM
    clockCanvas->setTextSize(4);
    clockCanvas->setTextColor(C_BLACK);
    clockCanvas->setCursor(startX + 2, textY + 2);
    clockCanvas->print(hmBuf);
    clockCanvas->setCursor(startX + 1, textY + 1);
    clockCanvas->print(hmBuf);

    // 2. Vẽ chữ HH:MM chính với màu clockColor rực rỡ
    clockCanvas->setTextColor(stCfg.clockColor);
    clockCanvas->setCursor(startX, textY);
    clockCanvas->print(hmBuf);

    // 3. Vẽ phần Giây (:SS) và AM/PM
    int secX = startX + hmWidth + 3;
    if (is12h) {
      clockCanvas->setTextSize(1);
      clockCanvas->setTextColor(C_BLACK);
      clockCanvas->setCursor(secX + 1, textY + 3);
      clockCanvas->print(isPM ? "PM" : "AM");
      clockCanvas->setTextColor(C_NEON_AMBER);
      clockCanvas->setCursor(secX, textY + 2);
      clockCanvas->print(isPM ? "PM" : "AM");
    }
    if (stCfg.showSeconds) {
      char secBuf[6];
      snprintf(secBuf, sizeof(secBuf), ":%02d", second);
      clockCanvas->setTextSize(2);
      clockCanvas->setTextColor(C_BLACK);
      clockCanvas->setCursor(secX + 1, textY + 15);
      clockCanvas->print(secBuf);
      clockCanvas->setTextColor(C_WHITE);
      clockCanvas->setCursor(secX, textY + 14);
      clockCanvas->print(secBuf);
    }

    // Đẩy duy nhất dải 240x44 ra màn hình SPI -> Mượt 100%, giữ nguyên ảnh nền phía dưới!
    tft->drawRGBBitmap(0, activeClockStripY, clockCanvas->getBuffer(), CLOCK_STRIP_W, CLOCK_STRIP_H);
  }

  // Vẽ toàn bộ bố cục Màn Hình Chờ đồng bộ 100% với màn hình ảo trên Web UI (240x320 Portrait)
  static void drawStandbyScreenFull() {
    if (!tft || isScreenSleeping) return;
    int w = tft->width();

    // Bước 1: Vẽ toàn bộ Ảnh nền JPEG từ LittleFS (hoặc Gradient) + làm mờ sẵn các ô kính mờ
    renderStandbyBackground();

    // Bước 2: Vẽ Top Bar (2 viên thuốc kính mờ: Trái = Wi-Fi/STA, Phải = ESP32-S3)
    bool connected = (WiFi.status() == WL_CONNECTED);
    tft->drawRoundRect(8, 8, 84, 18, 8, 0x39E7);
    tft->fillCircle(17, 17, 3, connected ? C_NEON_GREEN : C_NEON_AMBER);
    String wifiTag = "AP MODE";
    if (connected) {
      String s = toCleanAscii(WifiManager::getCurrentSsid());
      if (s.length() > 8) s = s.substring(0, 8);
      wifiTag = s.length() > 0 ? s : "STA";
    }
    drawShadowText(25, 13, wifiTag, C_WHITE, 1);

    tft->drawRoundRect(164, 8, 68, 18, 8, 0x39E7);
    drawShadowText(174, 13, "ESP32-S3", C_NEON_CYAN, 1);

    // Bước 3: Vẽ Đồng hồ số trung tâm (nổi trong suốt trên ảnh nền) + Ngày tháng bên dưới
    lastDrawnMinute = -1;
    updateStandbyClockDigits(true);

    if (stCfg.showDate) {
      int hour, minute, second, wday, day, month, year;
      getCurrentDateTime(hour, minute, second, wday, day, month, year);
      char dateBuf[48];
      const char* viDays[] = { "Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy" };
      const char* enDays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
      if (stCfg.dateFormat == "en") {
        snprintf(dateBuf, sizeof(dateBuf), "%s, %02d/%02d/%04d", enDays[wday % 7], day, month, year);
      } else if (stCfg.dateFormat == "num") {
        snprintf(dateBuf, sizeof(dateBuf), "%02d / %02d / %04d", day, month, year);
      } else {
        snprintf(dateBuf, sizeof(dateBuf), "%s, %02d/%02d/%04d", viDays[wday % 7], day, month, year);
      }
      int dateX = (w - vnStrLen(dateBuf) * 6) / 2;
      drawShadowText(max(8, dateX), activeClockStripY + CLOCK_STRIP_H + 4, String(dateBuf), C_WHITE, 1);
    }

    // Bước 4: Vẽ Footer (Viên thuốc kính mờ Thời tiết + Lời chào Slogan ở đáy màn hình giống hệt Web UI)
    if (stCfg.showWeather) {
      tft->drawRoundRect(42, 262, 156, 22, 10, 0x39E7);
      char wBuf[32];
      snprintf(wBuf, sizeof(wBuf), "%.1f°C  |  %d%%", stCfg.temp, stCfg.humidity);
      int wx = (w - vnStrLen(wBuf) * 6) / 2;
      drawShadowText(max(46, wx), 269, String(wBuf), C_WHITE, 1);
    }

    String msg = toCleanAscii(stCfg.customText);
    if (vnStrLen(msg) > 34) msg = vnSubstr(msg, 0, 34);
    int msgX = (w - vnStrLen(msg) * 6) / 2;
    drawShadowText(max(8, msgX), 292, msg, stCfg.textColor, 1);
  }

  // Vẽ hộp Phụ đề XiaoZhi ở phần dưới màn hình Emoji
  static void drawEmojiSubtitleBox() {
    if (!tft) return;
    int w = tft->width();
    int h = tft->height();
    int subY = 220;
    int subH = h - subY - 8;

    tft->fillRoundRect(10, subY, w - 20, subH, 8, C_CARD_BG);
    tft->drawRoundRect(10, subY, w - 20, subH, 8, 0x2188);

    // Badge "• XIAOZHI AI" giống hệt trên Web UI
    tft->fillCircle(20, subY + 13, 3, C_NEON_CYAN);
    tft->setTextSize(1);
    tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
    tft->setCursor(28, subY + 9);
    tft->print("TRỢ LÝ XIAOZHI AI");

    String cleanSub = toCleanAscii(customSubtitle);
    if (cleanSub.length() == 0) {
      cleanSub = "Xin chào! Mình là XiaoZhi * Hãy chạm biểu cảm hoặc trò chuyện cùng mình nhé!";
    }
    tft->setTextColor(C_WHITE, C_CARD_BG);
    int totalChars = vnStrLen(cleanSub);
    for (int line = 0; line < 3; line++) {
      int startIdx = line * 33;
      if (startIdx >= totalChars) break;
      String part = vnSubstr(cleanSub, startIdx, 33);
      tft->setCursor(16, subY + 26 + line * 14);
      tft->print(part);
    }
  }

  // Vẽ Chế độ 1: Giao diện Biểu Cảm XiaoZhi Toàn Màn Hình (70% Mắt + 30% Phụ đề)
  static void drawFullEmojiScreen() {
    if (!tft) return;
    int w = tft->width();

    tft->fillScreen(C_BLACK);
    drawTopStatusBar();

    // Phần trên (70%): Đôi mắt tròn phát sáng đồng bộ 100% với Web UI
    drawXiaoZhiEyesBox(8, 28, w - 16, 186, eyeState, 2);

    // Phần dưới (30%): Hộp phụ đề 3 dòng
    drawEmojiSubtitleBox();
  }

  // Vẽ Chế độ 2: Giao diện PC Status HUD (4 Phong cách: Bars, Gauges, Graph, Matrix + Báo Offline)
  static void drawPcHudScreen(bool fullRedraw) {
    if (!tft) return;
    int w = tft->width();
    int h = tft->height();

    const PcMetrics& m = PcStatsManager::getMetrics();
    bool live = PcStatsManager::isLive();

    // Nếu trạng thái kết nối PC vừa thay đổi (Online <-> Offline) -> Vẽ lại toàn màn hình
    if (live != lastPcLiveState) {
      lastPcLiveState = live;
      fullRedraw = true;
    }

    if (fullRedraw) {
      tft->fillScreen(C_DARK_BG);
      drawTopStatusBar();
    }

    // Header HUD
    tft->fillRect(8, 28, w - 16, 20, C_CARD_BG);
    tft->drawRect(8, 28, w - 16, 20, live ? C_NEON_GREEN : C_NEON_PINK);
    tft->setTextSize(1);
    tft->setTextColor(live ? C_NEON_GREEN : C_NEON_PINK, C_CARD_BG);
    tft->setCursor(14, 34);
    const char* styleNames[] = { "BARS", "GAUGES", "GRAPH", "MATRIX" };
    tft->printf("THONG SO PC // %-6s [%s]", styleNames[hudStyle % 4], live ? "ONLINE" : "OFFLINE");

    // Nếu máy tính chưa kết nối hoặc mất kết nối Server (> 6.5 giây không có gói tin)
    if (!live) {
      if (fullRedraw) {
        // Khung cảnh báo chính giữa màn hình
        tft->fillRoundRect(10, 58, w - 20, 224, 8, C_CARD_BG);
        tft->drawRoundRect(10, 58, w - 20, 224, 8, C_NEON_PINK);
        tft->drawRoundRect(12, 60, w - 24, 220, 6, C_CARD_BORDER);

        // Vẽ biểu tượng Máy Tính / Server Mất Kết Nối
        int cx = w / 2;
        tft->drawRoundRect(cx - 28, 74, 56, 38, 4, C_NEON_AMBER);
        tft->fillRect(cx - 24, 78, 48, 30, C_BLACK);
        tft->fillRect(cx - 10, 112, 20, 5, C_NEON_AMBER);
        tft->fillRect(cx - 18, 117, 36, 3, C_NEON_AMBER);
        // Dấu X đỏ ngắt kết nối trên màn hình PC
        tft->drawLine(cx - 10, 84, cx + 10, 102, C_NEON_PINK);
        tft->drawLine(cx - 9,  84, cx + 11, 102, C_NEON_PINK);
        tft->drawLine(cx + 10, 84, cx - 10, 102, C_NEON_PINK);
        tft->drawLine(cx + 11, 84, cx - 9,  102, C_NEON_PINK);

        // Dòng thông báo chính theo đúng yêu cầu
        tft->setTextSize(1);
        tft->setTextColor(C_NEON_PINK, C_CARD_BG);
        tft->setCursor(26, 132);
        tft->print("! CANH BAO KET NOI SERVER !");

        tft->fillRoundRect(18, 148, w - 36, 44, 5, C_BLACK);
        tft->drawRoundRect(18, 148, w - 36, 44, 5, C_NEON_AMBER);
        tft->setTextColor(C_YELLOW, C_BLACK);
        tft->setCursor(30, 157);
        tft->print("THIET BI DANG OFFLINE");
        tft->setTextColor(C_WHITE, C_BLACK);
        tft->setCursor(24, 173);
        tft->print("HOAC MAT KET NOI SERVER!");

        // Hướng dẫn kết nối giao thức Server
        tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
        tft->setCursor(20, 204);
        tft->print("Giao thuc: HTTP /api/pc_stats");
        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(20, 220);
        String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Chua ket noi WiFi";
        tft->printf("IP Tram: %s", ipStr.c_str());
        tft->setCursor(20, 236);
        tft->print("Chay script PC Telemetry de");
        tft->setCursor(20, 250);
        tft->print("dong bo CPU/GPU/RAM tu dong.");

        // Thanh Softkey Symbian S40 ở đáy
        tft->fillRect(0, h - 22, w, 22, 0x10A4);
        tft->drawFastHLine(0, h - 22, w, C_NEON_CYAN);
        tft->setTextColor(C_NEON_CYAN, 0x10A4);
        tft->setCursor(8, h - 15);
        tft->print("[MENU: S40]");
        tft->setTextColor(C_WHITE, 0x10A4);
        tft->setCursor(92, h - 15);
        tft->print("UP/DN:Kieu");
        tft->setTextColor(C_NEON_PINK, 0x10A4);
        tft->setCursor(w - 64, h - 15);
        tft->print("[EXIT:Ve]");
      }
      return;
    }

    int cpu = (int)round(m.cpu.usage);
    int gpu = (int)round(m.gpu.usage);
    int ram = (int)round(m.ram.usage);
    float cpuTemp = m.cpu.temp;
    float gpuTemp = m.gpu.temp;
    String cpuName = toCleanAscii(m.cpu.shortName);
    String gpuName = toCleanAscii(m.gpu.shortName);

    if (hudStyle == 0) {
      // STYLE 0: CYBERPUNK BARS
      auto drawBarCard = [&](int y, const char* label, const String& model, int pct, float temp, uint16_t col) {
        tft->fillRoundRect(8, y, w - 16, 56, 5, C_CARD_BG);
        tft->drawRoundRect(8, y, w - 16, 56, 5, C_CARD_BORDER);
        tft->setTextSize(1);
        tft->setTextColor(col, C_CARD_BG);
        tft->setCursor(14, y + 6);
        tft->printf("%s: %-10s", label, model.c_str());
        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(w - 52, y + 6);
        tft->printf("%3d%%", pct);

        int barW = w - 28;
        tft->drawRect(14, y + 20, barW, 14, col);
        int fillW = (barW - 4) * constrain(pct, 0, 100) / 100;
        tft->fillRect(16, y + 22, fillW, 10, col);
        tft->fillRect(16 + fillW, y + 22, (barW - 4) - fillW, 10, C_BLACK);

        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(14, y + 39);
        tft->printf("TEMP: %.1f oC", temp);
      };

      drawBarCard(54,  "CPU", cpuName, cpu, cpuTemp, C_NEON_CYAN);
      drawBarCard(116, "GPU", gpuName, gpu, gpuTemp, C_NEON_PINK);
      drawBarCard(178, "RAM", "DDR4 12GB", ram, 42.0f, C_NEON_AMBER);

      // Footer Net Speed
      tft->fillRoundRect(8, 240, w - 16, 70, 5, C_CARD_BG);
      tft->drawRoundRect(8, 240, w - 16, 70, 5, C_NEON_GREEN);
      tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
      tft->setCursor(14, 248);
      tft->printf("NET DL: %.1f KB/s | UL: %.1f KB/s", live ? m.net.dlSpeedKb : 142.5f, live ? m.net.ulSpeedKb : 24.1f);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 266);
      tft->printf("DISK USAGE: %.1f%% | FAN: %d RPM", live ? m.disk.usage : 82.0f, live ? m.fanRpm : 1450);
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(14, 286);
      tft->print("Bam phim 's' tren Serial de doi Style HUD");

    } else if (hudStyle == 1) {
      // STYLE 1: SPEEDOMETER GAUGES (Đồng hồ đo vòng tròn)
      auto drawGauge = [&](int cx, int cy, const char* title, const String& sub, int pct, float temp, uint16_t col) {
        tft->fillCircle(cx, cy, 46, C_CARD_BG);
        tft->drawCircle(cx, cy, 46, C_CARD_BORDER);
        tft->drawCircle(cx, cy, 45, col);
        tft->drawCircle(cx, cy, 38, col);

        tft->setTextSize(2);
        tft->setTextColor(col, C_CARD_BG);
        tft->setCursor(cx - 20, cy - 12);
        tft->printf("%2d%%", pct);

        tft->setTextSize(1);
        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(cx - 18, cy + 10);
        tft->printf("%.0foC", temp);

        tft->setTextColor(col, C_DARK_BG);
        tft->setCursor(cx - 32, cy + 52);
        tft->printf("%s %s", title, sub.c_str());
      };

      drawGauge(64,  112, "CPU", cpuName, cpu, cpuTemp, C_NEON_CYAN);
      drawGauge(176, 112, "GPU", gpuName, gpu, gpuTemp, C_NEON_PINK);
      drawGauge(64,  238, "RAM", "12GB",  ram, 41.0f,   C_NEON_AMBER);
      drawGauge(176, 238, "DSK", "SSD",   (int)(live ? m.disk.usage : 76), 39.0f, C_NEON_GREEN);

    } else if (hudStyle == 2) {
      // STYLE 2: REAL-TIME WAVEFORM GRAPH
      for (int i = 0; i < 19; i++) {
        graphHistoryCpu[i] = graphHistoryCpu[i + 1];
        graphHistoryGpu[i] = graphHistoryGpu[i + 1];
      }
      graphHistoryCpu[19] = constrain(cpu, 0, 100);
      graphHistoryGpu[19] = constrain(gpu, 0, 100);

      int gx = 12, gy = 58, gw = w - 24, gh = 160;
      tft->fillRect(gx, gy, gw, gh, C_BLACK);
      tft->drawRect(gx, gy, gw, gh, C_NEON_CYAN);

      // Grid lines
      for (int r = 1; r < 4; r++) {
        tft->drawFastHLine(gx + 1, gy + (gh * r) / 4, gw - 2, C_CARD_BORDER);
      }

      int stepX = (gw - 4) / 19;
      for (int i = 0; i < 19; i++) {
        int x1 = gx + 2 + i * stepX;
        int x2 = gx + 2 + (i + 1) * stepX;
        int yCpu1 = gy + gh - 4 - (graphHistoryCpu[i] * (gh - 8) / 100);
        int yCpu2 = gy + gh - 4 - (graphHistoryCpu[i + 1] * (gh - 8) / 100);
        int yGpu1 = gy + gh - 4 - (graphHistoryGpu[i] * (gh - 8) / 100);
        int yGpu2 = gy + gh - 4 - (graphHistoryGpu[i + 1] * (gh - 8) / 100);
        tft->drawLine(x1, yCpu1, x2, yCpu2, C_NEON_CYAN);
        tft->drawLine(x1, yGpu1, x2, yGpu2, C_NEON_PINK);
      }

      tft->fillRoundRect(8, 228, w - 16, 80, 6, C_CARD_BG);
      tft->setTextSize(1);
      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(16, 238);
      tft->printf("● CPU (%s): %d%%  [%.1f oC]   ", cpuName.c_str(), cpu, cpuTemp);
      tft->setTextColor(C_NEON_PINK, C_CARD_BG);
      tft->setCursor(16, 258);
      tft->printf("● GPU (%s): %d%%  [%.1f oC]   ", gpuName.c_str(), gpu, gpuTemp);
      tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
      tft->setCursor(16, 278);
      tft->printf("● RAM USED    : %d%%  [%.1f GB]   ", ram, live ? m.ram.usedGb : 8.2f);

    } else {
      // STYLE 3: TERMINAL MATRIX GRID (2x2)
      auto drawMatrixCell = [&](int x, int y, const char* tag, const String& model, int pct, float subVal, uint16_t col) {
        tft->fillRoundRect(x, y, 108, 122, 6, C_CARD_BG);
        tft->drawRoundRect(x, y, 108, 122, 6, col);
        tft->setTextSize(1);
        tft->setTextColor(col, C_CARD_BG);
        tft->setCursor(x + 8, y + 8);
        tft->printf("[%s] %s", tag, model.c_str());

        tft->setTextSize(3);
        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(x + 16, y + 34);
        tft->printf("%2d%%", pct);

        tft->setTextSize(1);
        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(x + 12, y + 74);
        tft->printf("TEMP: %.1f oC", subVal);

        // 5 segment blocks
        int activeSegs = (pct + 10) / 20;
        for (int s = 0; s < 5; s++) {
          tft->fillRect(x + 10 + s * 18, y + 96, 14, 12, (s < activeSegs) ? col : C_BLACK);
        }
      };

      drawMatrixCell(8,   56,  "CPU", cpuName, cpu, cpuTemp, C_NEON_CYAN);
      drawMatrixCell(124, 56,  "GPU", gpuName, gpu, gpuTemp, C_NEON_PINK);
      drawMatrixCell(8,   188, "RAM", "DDR4",  ram, 42.0f,   C_NEON_AMBER);
      drawMatrixCell(124, 188, "DSK", "NVMe",  (int)(live ? m.disk.usage : 84), 38.0f, C_NEON_GREEN);
    }
  }

  // Vẽ Chế độ 3: Biểu Đồ Sóng Âm & Cảm Biến Môi Trường (Chính thức)
  static void drawHardwareDiagScreen(bool fullRedraw = true) {
    if (!tft) return;
    // Luôn bật thu mẫu Micro trực tiếp riêng biệt cho màn hình Biểu đồ Sóng âm & Cảm biến (Mode 3),
    // không bị ảnh hưởng bởi trạng thái Tạm dừng/Dừng của Trình phát nhạc (Mode 12)
    if (!TestAudio::isMicMonitorActive() && !TestAudio::isVoiceRecording()) {
      TestAudio::enableMicMonitor(true);
    }
    int w = tft->width();
    int h = tft->height();

    if (fullRedraw) {
      tft->fillScreen(C_DARK_BG);
      tft->drawRect(0, 0, w, h, C_CYAN);

      // Header
      tft->fillRect(4, 4, w - 8, 24, C_HEADER_BG);
      tft->setTextColor(C_WHITE);
      tft->setTextSize(1);
      tft->setCursor(10, 8);
      tft->print("BIỂU ĐỒ SÓNG ÂM & CẢM BIẾN");
      tft->setTextColor(C_YELLOW);
      tft->setCursor(10, 18);
      tft->print("Nhiệt ẩm phòng + Sóng âm + Phím bấm");

      // Khung Card 1: Nhiệt độ & Độ ẩm (y=31..85)
      tft->fillRoundRect(6, 31, w - 12, 54, 5, C_CARD_BG);

      // Khung Card 2: Biểu đồ Sóng âm thanh (y=89..235)
      tft->fillRoundRect(6, 89, w - 12, 146, 5, C_CARD_BG);
      tft->drawRoundRect(6, 89, w - 12, 146, 5, C_NEON_CYAN);
      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(12, 94);
      tft->print("PHỔ SÓNG ÂM THANH TRỰC TIẾP");

      // Khung Card 3: Bàn phím & Cảm biến chạm (y=239..314)
      tft->fillRoundRect(6, 239, w - 12, 75, 5, C_CARD_BG);
      tft->drawRoundRect(6, 239, w - 12, 75, 5, C_NEON_GREEN);
    }

    // --- 1. CẬP NHẬT CARD 1: NHIỆT ĐỘ & ĐỘ ẨM PHÒNG ---
    bool shtOk = TestSensors::isSht31Connected();
    tft->drawRoundRect(6, 31, w - 12, 54, 5, shtOk ? C_NEON_GREEN : C_NEON_PINK);
    tft->setTextSize(1);
    tft->setTextColor(shtOk ? C_NEON_GREEN : C_NEON_PINK, C_CARD_BG);
    tft->setCursor(12, 36);
    tft->printf("Cảm biến môi trường: %s", shtOk ? "Đang hoạt động" : "Chưa kết nối");

    if (shtOk) {
      tft->setTextSize(2);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(12, 50);
      tft->printf("%4.1f°C", TestSensors::getTemperatureC());
      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(102, 50);
      tft->printf("%4.1f%%RH", TestSensors::getHumidityPct());
      tft->setTextSize(1);
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(12, 71);
      tft->print("Tự động cập nhật nhiệt độ & độ ẩm phòng");
    } else {
      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(12, 50);
      tft->print("Đang dùng dữ liệu thời tiết mặc định");
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(12, 64);
      tft->print("Hệ thống sẽ tự nhận khi cắm cảm biến");
    }

    // --- 2. CẬP NHẬT CARD 2: BIỂU ĐỒ SÓNG ÂM TRỰC TIẾP ---
    tft->setTextSize(1);
    int micPct      = TestAudio::getLastMicLevelPct();
    int32_t micPeak = TestAudio::getLastMicPeak();
    tft->setTextColor(micPeak > 0 ? C_NEON_GREEN : C_NEON_AMBER, C_CARD_BG);
    tft->setCursor(12, 106);
    tft->printf("Trạng thái thu: %s", micPeak > 0 ? "Đang thu âm thanh" : "Chờ tín hiệu...");

    // Khung vẽ sóng âm (x=12, y=118, sw=216, sh=78)
    const int sx = 12, sy = 118, sw = 216, sh = 78;
    const int midY = sy + (sh / 2);
    tft->fillRect(sx, sy, sw, sh, C_BLACK);
    tft->drawRect(sx, sy, sw, sh, C_CARD_BORDER);

    for (int gx = sx + 4; gx < sx + sw - 4; gx += 6) {
      tft->drawPixel(gx, midY, C_GRAY);
    }

    const int8_t* wave = TestAudio::getWaveformBuffer();
    int numPts = 48;
    int prevX = sx + 3;
    int prevY = midY;

    for (int i = 0; i < numPts; i++) {
      int cx = sx + 3 + (i * (sw - 6)) / (numPts - 1);
      int offset = ((int)wave[i] * 35) / 28;
      int cy = midY - offset;
      if (cy < sy + 2) cy = sy + 2;
      if (cy > sy + sh - 3) cy = sy + sh - 3;

      uint16_t waveCol = (abs(wave[i]) > 18) ? C_NEON_PINK :
                         (abs(wave[i]) > 6)  ? C_NEON_GREEN : C_NEON_CYAN;

      if (abs(offset) > 1) {
        int topY = (cy < midY) ? cy : midY;
        int barH = abs(cy - midY);
        tft->drawFastVLine(cx, topY, barH, C_BLUE);
      }
      if (i > 0) {
        tft->drawLine(prevX, prevY, cx, cy, waveCol);
      }
      tft->fillRect(cx - 1, cy - 1, 2, 2, C_WHITE);
      prevX = cx;
      prevY = cy;
    }

    // Thanh cường độ âm thanh (y=200..228)
    tft->setTextColor(C_WHITE, C_CARD_BG);
    tft->setCursor(12, 200);
    tft->printf("Cường độ âm thanh: %3d%%", micPct);

    int vuX = 12, vuY = 213, vuW = 216, vuH = 14;
    tft->drawRect(vuX, vuY, vuW, vuH, C_CARD_BORDER);
    int fillW = ((vuW - 4) * micPct) / 100;
    uint16_t vuCol = (micPct > 70) ? C_NEON_PINK : (micPct > 30) ? C_NEON_AMBER : C_NEON_GREEN;
    if (fillW > 0) {
      tft->fillRect(vuX + 2, vuY + 2, fillW, vuH - 4, vuCol);
    }
    if (fillW < vuW - 4) {
      tft->fillRect(vuX + 2 + fillW, vuY + 2, (vuW - 4) - fillW, vuH - 4, C_BLACK);
    }

    // --- 3. CẬP NHẬT CARD 3: PHÍM BẤM ĐIỀU KHIỂN ---
    tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
    tft->setCursor(12, 245);
    tft->print("BÀN PHÍM ĐIỀU KHIỂN HỆ THỐNG:");

    tft->setTextColor(C_YELLOW, C_CARD_BG);
    tft->setCursor(12, 259);
    tft->printf("Phím vừa nhấn: %-20.20s", TestButtons::getLastButtonName().c_str());

    const char* btnShort[7] = { "OK", "LÊN", "XNG", "TRÁI", "PHẢI", "MENU", "THOÁT" };
    int activeIdx = TestButtons::getActiveButtonIndex();
    for (int b = 0; b < 7; b++) {
      bool pressed = (activeIdx == b);
      int bx = 10 + b * 31;
      int by = 273;
      tft->fillRoundRect(bx, by, 29, 18, 3, pressed ? C_NEON_PINK : C_BLACK);
      tft->drawRoundRect(bx, by, 29, 18, 3, pressed ? C_WHITE : C_CARD_BORDER);
      tft->setTextColor(pressed ? C_WHITE : C_SLATE);
      int bwTxt = vnStrLen(btnShort[b]) * 6;
      tft->setCursor(bx + max(1, (29 - bwTxt) / 2), by + 5);
      tft->print(btnShort[b]);
    }

    tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
    tft->setCursor(12, 297);
    tft->printf("L298N (GPIO 1): %-20.20s", TestWindmill::getModeName());
  }

  // ============================================================================
  // BỘ ĐIỀU KHIỂN GIAO DIỆN SYMBIAN S40 (MENU BIỂU TƯỢNG & CÁC ỨNG DỤNG CON)
  // ============================================================================

  // Vẽ khung thẻ / ô Menu theo đúng phong cách hình học của Chủ đề (Theme) đang chọn
  static void drawThemedBox(int x, int y, int w, int h, bool sel) {
    uint16_t bg     = sel ? C_CARD_SEL : C_CARD_BG;
    uint16_t border = sel ? C_NEON_CYAN : C_CARD_BORDER;
    uint8_t style   = curTheme().boxStyle;

    if (style == 1) {
      // STYLE 1: CHILL THẢO MỘC (Bo tròn mềm mại kiểu viên sỏi r=9 + chấm lá thanh thoát)
      tft->fillRoundRect(x, y, w, h, 9, bg);
      tft->drawRoundRect(x, y, w, h, 9, border);
      if (sel) {
        tft->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 8, C_NEON_GREEN);
        tft->fillCircle(x + 8, y + 4, 1, C_NEON_AMBER);
        tft->fillCircle(x + w - 9, y + 4, 1, C_NEON_AMBER);
      }
    } else if (style == 2) {
      // STYLE 2: MECHA MẠNH MẼ (Giáp cơ khí góc vuông vát cạnh + 4 đinh tán góc khỏe khoắn)
      tft->fillRect(x, y, w, h, bg);
      tft->drawRect(x, y, w, h, border);
      if (sel) {
        tft->drawRect(x + 1, y + 1, w - 2, h - 2, C_NEON_PINK);
      }
      // Vát chéo 4 góc kiểu cơ khí Mecha + Đinh tán
      uint16_t rivetCol = sel ? C_YELLOW : C_NEON_CYAN;
      tft->drawLine(x, y + 4, x + 4, y, rivetCol);
      tft->drawLine(x + w - 5, y, x + w - 1, y + 4, rivetCol);
      tft->drawLine(x, y + h - 5, x + 4, y + h - 1, rivetCol);
      tft->drawLine(x + w - 5, y + h - 1, x + w - 1, y + h - 5, rivetCol);
      tft->fillRect(x + 2, y + 2, 2, 2, rivetCol);
      tft->fillRect(x + w - 4, y + 2, 2, 2, rivetCol);
      tft->fillRect(x + 2, y + h - 4, 2, 2, rivetCol);
      tft->fillRect(x + w - 4, y + h - 4, 2, 2, rivetCol);
    } else if (style == 3) {
      // STYLE 3: HOÀNG HÔN LOFI (Bo êm r=8 + Vạch chân trời ấm áp dưới đáy)
      tft->fillRoundRect(x, y, w, h, 8, bg);
      tft->drawRoundRect(x, y, w, h, 8, border);
      if (sel) {
        tft->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, C_NEON_AMBER);
      }
      tft->fillRect(x + 6, y + h - 3, max(4, w - 12), 2, sel ? C_NEON_AMBER : C_NEON_PINK);
    } else if (style == 4) {
      // STYLE 4: NOKIA S40 CỔ ĐIỂN (Khung chữ nhật vuông vức chuẩn Nokia 6300 + Ngoặc góc cam/trắng)
      tft->fillRect(x, y, w, h, bg);
      tft->drawRect(x, y, w, h, sel ? C_NEON_PINK : C_CARD_BORDER);
      if (sel) {
        tft->drawRect(x + 1, y + 1, w - 2, h - 2, C_WHITE);
        tft->fillRect(x, y, 5, 2, C_YELLOW);
        tft->fillRect(x, y, 2, 5, C_YELLOW);
        tft->fillRect(x + w - 5, y, 5, 2, C_YELLOW);
        tft->fillRect(x + w - 2, y, 2, 5, C_YELLOW);
      }
    } else if (style == 5) {
      // STYLE 5: MÈO GOME CHIBI (Bo tròn dễ thương r=8 + Viền kép Hồng/Cyan + Tai mèo Chibi)
      tft->fillRoundRect(x, y, w, h, 8, bg);
      tft->drawRoundRect(x, y, w, h, 8, border);
      if (sel) {
        tft->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, C_NEON_PINK);
        // Tai mèo nhỏ ở 2 góc trên bên trong thẻ
        tft->fillTriangle(x + 4, y + 5, x + 8, y + 1, x + 12, y + 5, C_NEON_PINK);
        tft->fillTriangle(x + w - 13, y + 5, x + w - 9, y + 1, x + w - 5, y + 5, C_NEON_PINK);
        tft->drawTriangle(x + 4, y + 5, x + 8, y + 1, x + 12, y + 5, C_WHITE);
        tft->drawTriangle(x + w - 13, y + 5, x + w - 9, y + 1, x + w - 5, y + 5, C_WHITE);
      }
    } else {
      // STYLE 0: TRẠM VŨ TRỤ DECOR (Bo góc hiện đại r=6 + Viền kép phát sáng Neon)
      tft->fillRoundRect(x, y, w, h, 6, bg);
      tft->drawRoundRect(x, y, w, h, 6, border);
      if (sel) {
        tft->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 5, C_NEON_AMBER);
        tft->fillRect(x + w / 2 - 8, y + 1, 16, 2, C_NEON_CYAN);
      }
    }
  }

  // Vẽ biểu tượng Mèo Gome Chibi (36x36 RGB565 trong suốt, bỏ qua pixel 0x0000)
  static void drawGomeIconTransparent(int idx, int cx, int cy) {
    if (!tft) return;
    int iconIdx = ((idx % GOME_ICON_COUNT) + GOME_ICON_COUNT) % GOME_ICON_COUNT;
    const uint16_t* icon = (const uint16_t*)pgm_read_ptr(&(GOME_ICONS[iconIdx]));
    if (!icon) return;
    int startX = cx - (GOME_ICON_W / 2);
    int startY = cy - (GOME_ICON_H / 2);
    for (int y = 0; y < GOME_ICON_H; y++) {
      for (int x = 0; x < GOME_ICON_W; x++) {
        uint16_t col = pgm_read_word(&icon[y * GOME_ICON_W + x]);
        if (col != 0x0000) {
          tft->drawPixel(startX + x, startY + y, col);
        }
      }
    }
  }

  // Vẽ Thanh Tiêu Đề Symbian S40 + Thanh Softkey Đáy + Thông Báo Toast (Theo màu Chủ Đề đang chọn)
  static void drawSymbianChrome(const char* title, const char* leftSoft, const char* midSoft, const char* rightSoft) {
    int w = tft->width();
    int h = tft->height();

    // 1. Thanh trạng thái trên cùng (y=0..24)
    tft->fillRect(0, 0, w, 24, C_HEADER_BG);
    tft->drawFastHLine(0, 24, w, C_NEON_CYAN);
    if (curTheme().boxStyle == 2) {
      // Vạch cảnh báo cơ khí cho chế độ Mecha Mạnh Mẽ
      tft->drawFastHLine(0, 23, w, C_NEON_PINK);
    }

    // Cột sóng Wi-Fi bên trái
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    for (int b = 0; b < 4; b++) {
      int bh = 4 + b * 3;
      tft->fillRect(6 + b * 4, 18 - bh, 3, bh, wifiOk ? C_NEON_GREEN : C_GRAY);
    }

    // Tiêu đề App ở giữa
    tft->setTextSize(1);
    tft->setTextColor(C_NEON_CYAN, C_HEADER_BG);
    tft->setCursor(28, 8);
    tft->print(title);

    // Đồng hồ nhỏ + Biểu tượng đèn nền góc phải
    int hr, mn, sc, wd, dy, mo, yr;
    getCurrentDateTime(hr, mn, sc, wd, dy, mo, yr);
    tft->setTextColor(C_WHITE, C_HEADER_BG);
    tft->setCursor(w - 64, 8);
    tft->printf("%02d:%02d", hr, mn);

    // Vạch pin / Độ sáng GPIO7
    tft->drawRect(w - 26, 7, 18, 10, C_NEON_GREEN);
    tft->fillRect(w - 8, 9, 2, 6, C_NEON_GREEN);
    int batFill = (screenBrightnessPct * 14) / 100;
    tft->fillRect(w - 24, 9, batFill, 6, C_NEON_GREEN);

    // 2. Thanh Softkey Symbian S40 ở đáy (y=298..320)
    tft->fillRect(0, h - 22, w, 22, C_HEADER_BG);
    tft->drawFastHLine(0, h - 22, w, C_NEON_CYAN);
    tft->setTextSize(1);
    tft->setTextColor(C_NEON_GREEN, C_HEADER_BG);
    tft->setCursor(6, h - 15);
    tft->print(leftSoft);

    if (midSoft && vnStrLen(midSoft) > 0) {
      int mx = (w - vnStrLen(midSoft) * 6) / 2;
      tft->setTextColor(C_YELLOW, C_HEADER_BG);
      tft->setCursor(mx, h - 15);
      tft->print(midSoft);
    }

    int rx = w - vnStrLen(rightSoft) * 6 - 6;
    tft->setTextColor(C_NEON_PINK, C_HEADER_BG);
    tft->setCursor(max(136, rx), h - 15);
    tft->print(rightSoft);

    // 3. Nếu đang có Toast thông báo ngắn (ví dụ: "ĐÃ ĐỔI GIAO DIỆN!")
    if (s40ToastMsg.length() > 0 && millis() < s40ToastExpireMs) {
      tft->fillRoundRect(16, h - 50, w - 32, 22, 5, C_NEON_GREEN);
      tft->drawRoundRect(16, h - 50, w - 32, 22, 5, C_WHITE);
      tft->setTextColor(C_BLACK, C_NEON_GREEN);
      int tx = (w - vnStrLen(s40ToastMsg) * 6) / 2;
      tft->setCursor(max(20, tx), h - 43);
      tft->print(s40ToastMsg);
    }
  }

  // Cập nhật riêng ô Đồng hồ nhỏ (cạnh biểu tượng Pin trên thanh trạng thái) mỗi giây mà không làm nháy màn hình
  static void updateSymbianTopStatusBarClock() {
    if (!tft || currentMode < 4) return;
    int w = tft->width();
    int hr, mn, sc, wd, dy, mo, yr;
    getCurrentDateTime(hr, mn, sc, wd, dy, mo, yr);

    // Chỉ xóa và vẽ đè đúng vùng 36x12px của đồng hồ góc phải (w - 65 .. w - 29, y = 6..18) trên nền C_HEADER_BG
    tft->fillRect(w - 65, 6, 36, 12, C_HEADER_BG);
    tft->setTextSize(1);
    tft->setTextColor(C_WHITE, C_HEADER_BG);
    tft->setCursor(w - 64, 8);
    // Nhấp nháy dấu hai chấm ':' mỗi giây để báo hiệu đồng hồ đang chạy thời gian thực
    if (sc % 2 == 0) {
      tft->printf("%02d:%02d", hr, mn);
    } else {
      tft->printf("%02d %02d", hr, mn);
    }
  }

  static void showSymbianToast(const String& msg, unsigned long durationMs = 2000) {
    s40ToastMsg = msg;
    s40ToastExpireMs = millis() + durationMs;
  }

  // Vẽ biểu tượng đồ họa cho từng mục trong Menu Symbian S40 (Lưới 4x3 = 12 Biểu Tượng, tự động đổi tông màu theo Chủ Đề!)
  static void drawSymbianIconArt(int idx, int cx, int cy) {
    if (curTheme().boxStyle == 5) {
      drawGomeIconTransparent(idx, cx, cy);
      return;
    }
    if (idx == 0) {
      // 0: HÌNH NỀN (Khung tranh + Mặt trời + Núi)
      tft->drawRoundRect(cx - 16, cy - 12, 32, 22, 3, C_NEON_CYAN);
      tft->fillCircle(cx - 7, cy - 5, 3, C_YELLOW);
      tft->fillTriangle(cx - 12, cy + 8, cx - 2, cy - 2, cx + 7, cy + 8, C_NEON_GREEN);
      tft->fillTriangle(cx - 2, cy + 8, cx + 7, cy - 4, cx + 13, cy + 8, C_NEON_BLUE);
    } else if (idx == 1) {
      // 1: CÀI ĐẶT & GIAO DIỆN (3 thanh trượt Sliders + Bảng màu Theme)
      for (int r = 0; r < 3; r++) {
        int ry = cy - 8 + r * 8;
        tft->drawFastHLine(cx - 14, ry, 28, C_SLATE);
        int knobX = (r == 0) ? (cx + 5) : ((r == 1) ? (cx - 5) : (cx + 2));
        uint16_t kCol = (r == 0) ? C_NEON_AMBER : ((r == 1) ? C_NEON_CYAN : C_NEON_GREEN);
        tft->fillCircle(knobX, ry, 3, kCol);
      }
    } else if (idx == 2) {
      // 2: THÔNG SỐ PC (Màn hình máy tính + Đồ thị xung nhịp)
      tft->drawRoundRect(cx - 16, cy - 12, 32, 20, 3, C_NEON_GREEN);
      tft->fillRect(cx - 5, cy + 8, 10, 2, C_NEON_GREEN);
      tft->drawFastHLine(cx - 10, cy + 10, 20, C_NEON_GREEN);
      tft->drawLine(cx - 11, cy + 2, cx - 5, cy - 5, C_NEON_CYAN);
      tft->drawLine(cx - 5, cy - 5, cx + 1, cy + 3, C_NEON_PINK);
      tft->drawLine(cx + 1, cy + 3, cx + 10, cy - 6, C_YELLOW);
    } else if (idx == 3) {
      // 3: THƯ VIỆN ẢNH (2 tấm ảnh xếp chồng Album)
      tft->drawRoundRect(cx - 12, cy - 12, 26, 18, 3, C_SLATE);
      tft->fillRoundRect(cx - 16, cy - 8, 26, 18, 3, C_CARD_BG);
      tft->drawRoundRect(cx - 16, cy - 8, 26, 18, 3, C_NEON_PINK);
      tft->fillCircle(cx - 9, cy - 2, 2, C_NEON_AMBER);
      tft->fillTriangle(cx - 12, cy + 8, cx - 3, cy, cx + 6, cy + 8, C_NEON_CYAN);
    } else if (idx == 4) {
      // 4: BỘ NHỚ / THẺ MICRO SD (Biểu tượng Thẻ nhớ MicroSD mạ vàng + Vạch dung lượng)
      tft->fillRoundRect(cx - 13, cy - 13, 26, 24, 3, C_DARK_BG);
      tft->drawRoundRect(cx - 13, cy - 13, 26, 24, 3, C_YELLOW);
      tft->fillTriangle(cx + 7, cy - 13, cx + 13, cy - 13, cx + 13, cy - 7, C_CARD_BG);
      tft->drawLine(cx + 7, cy - 13, cx + 13, cy - 7, C_YELLOW);
      for (int p = 0; p < 4; p++) {
        tft->fillRect(cx - 9 + p * 4, cy - 10, 3, 6, C_NEON_AMBER);
      }
      tft->drawRect(cx - 9, cy + 2, 18, 5, C_NEON_CYAN);
      tft->fillRect(cx - 8, cy + 3, 12, 3, C_NEON_GREEN);
    } else if (idx == 5) {
      // 5: GỌI TRỢ LÝ XIAOZHI AI (Biểu tượng Khuôn mặt Robot AI Đôi mắt phát sáng + Anten)
      tft->drawLine(cx, cy - 15, cx, cy - 11, C_NEON_PINK);
      tft->fillCircle(cx, cy - 15, 2, C_YELLOW);
      tft->fillRoundRect(cx - 15, cy - 11, 30, 22, 5, C_DARK_BG);
      tft->drawRoundRect(cx - 15, cy - 11, 30, 22, 5, C_NEON_CYAN);
      tft->fillRoundRect(cx - 10, cy - 6, 7, 8, 2, C_NEON_CYAN);
      tft->fillRoundRect(cx + 3,  cy - 6, 7, 8, 2, C_NEON_CYAN);
      tft->drawFastHLine(cx - 6, cy + 6, 12, C_NEON_GREEN);
    } else if (idx == 6) {
      // 6: ĐA PHƯƠNG TIỆN / ÂM NHẠC (Đĩa Vinyl xoay viền Neon + Nốt nhạc đôi phát sáng)
      tft->fillCircle(cx - 3, cy, 12, C_DARK_BG);
      tft->drawCircle(cx - 3, cy, 12, C_NEON_PINK);
      tft->drawCircle(cx - 3, cy, 8, C_SLATE);
      tft->fillCircle(cx - 3, cy, 4, C_NEON_CYAN);
      tft->fillCircle(cx - 3, cy, 1, C_BLACK);
      tft->fillCircle(cx + 8, cy + 6, 3, C_YELLOW);
      tft->drawFastVLine(cx + 10, cy - 8, 14, C_YELLOW);
      tft->fillRect(cx + 10, cy - 8, 5, 3, C_NEON_AMBER);
    } else if (idx == 7) {
      // 7: ĐỒNG HỒ / BÁO THỨC & BẤM GIỜ (Đồng hồ tròn 2 quả chuông + Kim Neon)
      tft->fillCircle(cx - 9, cy - 10, 3, C_NEON_AMBER);
      tft->fillCircle(cx + 9, cy - 10, 3, C_NEON_AMBER);
      tft->fillCircle(cx, cy + 1, 11, C_DARK_BG);
      tft->drawCircle(cx, cy + 1, 11, C_NEON_CYAN);
      tft->drawCircle(cx, cy + 1, 10, C_WHITE);
      tft->drawFastVLine(cx, cy - 6, 7, C_YELLOW);
      tft->drawFastHLine(cx, cy + 1, 6, C_NEON_PINK);
      tft->drawLine(cx - 8, cy + 11, cx - 11, cy + 14, C_NEON_AMBER);
      tft->drawLine(cx + 8, cy + 11, cx + 11, cy + 14, C_NEON_AMBER);
    } else if (idx == 8) {
      // 8: LỊCH VẠN NIÊN (Tờ lịch đóng gáy đỏ/hồng + Lưới ngày chấm sáng)
      tft->fillRoundRect(cx - 13, cy - 11, 26, 23, 3, C_DARK_BG);
      tft->fillRoundRect(cx - 13, cy - 11, 26, 7, 2, C_NEON_PINK);
      tft->drawRoundRect(cx - 13, cy - 11, 26, 23, 3, C_WHITE);
      tft->fillRect(cx - 8, cy - 13, 3, 4, C_YELLOW);
      tft->fillRect(cx + 5, cy - 13, 3, 4, C_YELLOW);
      for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 4; c++) {
          uint16_t dotCol = (r == 1 && c == 2) ? C_YELLOW : C_NEON_CYAN;
          tft->fillRect(cx - 9 + c * 5, cy - 1 + r * 5, 3, 3, dotCol);
        }
      }
    } else if (idx == 9) {
      // 9: ABOUT / THÔNG TIN (Huy hiệu chữ 'i' tròn phát sáng)
      tft->drawCircle(cx, cy - 1, 12, C_NEON_PURPLE);
      tft->drawCircle(cx, cy - 1, 11, C_NEON_CYAN);
      tft->fillCircle(cx, cy - 7, 2, C_YELLOW);
      tft->fillRect(cx - 2, cy - 3, 4, 8, C_WHITE);
    } else if (idx == 10) {
      // 10: SÓNG ÂM & CẢM BIẾN (Biểu tượng sóng âm + phím bấm)
      tft->drawRoundRect(cx - 16, cy - 11, 32, 20, 3, C_NEON_AMBER);
      for (int i = -10; i <= 10; i += 4) {
        int bh = (abs(i) == 2) ? 12 : ((i == 0) ? 15 : 6);
        tft->fillRect(cx + i - 1, cy - 1 - bh / 2, 2, bh, C_NEON_CYAN);
      }
    } else if (idx == 11) {
      // 11: THẺ NHỚ SD (Biểu tượng thông tin Thẻ nhớ MicroSD)
      tft->drawRoundRect(cx - 16, cy - 11, 32, 20, 3, C_NEON_GREEN);
      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW);
      tft->setCursor(cx - 11, cy - 8);
      tft->print("SD");
      tft->setTextColor(C_NEON_CYAN);
      tft->setCursor(cx - 11, cy + 1);
      tft->print("INFO");
    } else if (idx == 12) {
      // 12: TRÒ CHƠI GIẢI TRÍ (Tay cầm chơi Game cổ điển D-Pad + Nút A/B Neon)
      tft->fillRoundRect(cx - 16, cy - 10, 32, 18, 6, C_DARK_BG);
      tft->drawRoundRect(cx - 16, cy - 10, 32, 18, 6, C_NEON_PINK);
      // Hai báng tay cầm dưới
      tft->fillCircle(cx - 11, cy + 7, 5, C_DARK_BG);
      tft->drawCircle(cx - 11, cy + 7, 5, C_NEON_PINK);
      tft->fillCircle(cx + 11, cy + 7, 5, C_DARK_BG);
      tft->drawCircle(cx + 11, cy + 7, 5, C_NEON_PINK);
      // Phím điều hướng chữ thập D-Pad bên trái
      tft->fillRect(cx - 12, cy - 2, 7, 3, C_YELLOW);
      tft->fillRect(cx - 10, cy - 4, 3, 7, C_YELLOW);
      // 2 nút bấm A/B bên phải
      tft->fillCircle(cx + 6,  cy,     2, C_NEON_CYAN);
      tft->fillCircle(cx + 11, cy - 3, 2, C_NEON_GREEN);
    } else if (idx == 13) {
      // 13: MÃ QR CÀI ĐẶT WI-FI (Icon Mã QR với 3 mắt vuông định vị Neon Cyan)
      tft->fillRoundRect(cx - 15, cy - 13, 30, 26, 3, C_CARD_BG);
      tft->drawRoundRect(cx - 15, cy - 13, 30, 26, 3, C_NEON_CYAN);
      // 3 mắt vuông định vị
      tft->drawRect(cx - 12, cy - 10, 7, 7, C_WHITE);
      tft->fillRect(cx - 10, cy - 8, 3, 3, C_NEON_CYAN);
      tft->drawRect(cx + 5, cy - 10, 7, 7, C_WHITE);
      tft->fillRect(cx + 7, cy - 8, 3, 3, C_NEON_CYAN);
      tft->drawRect(cx - 12, cy + 3, 7, 7, C_WHITE);
      tft->fillRect(cx - 10, cy + 5, 3, 3, C_NEON_CYAN);
      // Vài module pixel ma trận
      tft->fillRect(cx - 1, cy - 8, 2, 2, C_YELLOW);
      tft->fillRect(cx + 2, cy - 2, 2, 2, C_NEON_GREEN);
      tft->fillRect(cx - 2, cy + 4, 3, 3, C_WHITE);
      tft->fillRect(cx + 6, cy + 5, 3, 3, C_YELLOW);
    }
  }

  static const int S40_MENU_COUNT = 14;
  static int s40MenuScrollRow     = 0; // Hàng bắt đầu hiển thị trên khung nhìn 4x3 (0 = hiện hàng 0..2, 1 = cuộn xuống hiện hàng 1..3)
  static int lastDrawnMenuCursor  = -1;
  static int lastDrawnMenuScroll  = -1;

  static const char* S40_MENU_TITLES[S40_MENU_COUNT] = {
    "Hình nền", "Cài đặt",  "Máy tính", "Thư viện",
    "Bộ nhớ",   "Trợ lý AI","Âm nhạc",  "Đồng hồ",
    "Lịch",     "Thiết bị", "Sóng âm",  "Thẻ nhớ",
    "Trò chơi", "Mã QR"
  };
  static const char* S40_MENU_DESCS[S40_MENU_COUNT] = {
    "1. Tùy chỉnh Hình nền & Đồng hồ chờ",
    "2. Giao diện (6 Chủ đề), Độ sáng & Loa",
    "3. Giám sát Hiệu năng Máy tính (PC)",
    "4. Bộ sưu tập Hình ảnh & Ảnh nền",
    "5. Quản lý Tệp tin & Bộ nhớ lưu trữ",
    "6. Trò chuyện Giọng nói XiaoZhi AI",
    "7. Trình phát Âm nhạc & Sóng nhạc",
    "8. Đồng hồ: Báo thức, Bấm giờ & Hẹn giờ",
    "9. Lịch Vạn Niên & Tra cứu Ngày tháng",
    "10. Thông tin Thiết bị & Kết nối Mạng",
    "11. Biểu đồ Sóng âm & Cảm biến phòng",
    "12. Thông tin Chi tiết Thẻ nhớ MicroSD",
    "13. Trò chơi: Rắn săn mồi, Flappy & Xếp gạch",
    "14. Quét Mã QR Cài Đặt Wi-Fi & Quản Lý"
  };

  static void drawSymbianMenuBanner() {
    if (!tft) return;
    int w = tft->width();
    int cur = ((s40MenuCursor % S40_MENU_COUNT) + S40_MENU_COUNT) % S40_MENU_COUNT;
    tft->fillRect(4, 26, w - 8, 21, C_DARK_BG);
    drawThemedBox(4, 26, w - 8, 21, false);
    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, C_CARD_BG);
    tft->setCursor(10, 33);
    tft->print(S40_MENU_DESCS[cur]);
  }

  // Vẽ thanh cuộn dọc bên phải lưới 4x3 để báo hiệu trang/hàng đang hiển thị (Cuộn giữa Hàng 1..3 và Hàng 2..4)
  static void drawSymbianMenuScrollbar() {
    if (!tft) return;
    const int sbX = 235;
    const int sbY = 51;
    const int sbH = 238;
    tft->fillRect(sbX, sbY, 4, sbH, 0x1082);
    tft->drawRect(sbX, sbY, 4, sbH, C_CARD_BORDER);
    int thumbH = (sbH * 3) / 4; // Khung nhìn hiển thị 3 / 4 hàng
    int thumbY = sbY + (s40MenuScrollRow * (sbH - thumbH));
    tft->fillRect(sbX + 1, thumbY + 1, 2, thumbH - 2, C_NEON_CYAN);
  }

  // Vẽ 1 ô tại vị trí hàng/cột trên khung nhìn 4x3 (visRow = 0..2, col = 0..3)
  static void drawSingleSymbianViewportCell(int actualIdx, int visRow, int col, bool sel) {
    if (!tft || visRow < 0 || visRow >= 3 || col < 0 || col >= 4) return;
    const int cellW = 54;
    const int cellH = 76;
    const int startX = 4;
    const int startY = 51;
    const int gapX = 4;
    const int gapY = 5;

    int x = startX + col * (cellW + gapX);
    int y = startY + visRow * (cellH + gapY);

    tft->fillRect(x, y, cellW, cellH, C_DARK_BG);
    if (actualIdx < 0 || actualIdx >= S40_MENU_COUNT) {
      // Ô trống trên hàng cuối khi cuộn xuống ngoài 13 biểu tượng: vẽ khung mờ trang trí gọn gàng
      tft->drawRoundRect(x + 2, y + 2, cellW - 4, cellH - 4, 5, 0x10A3);
      tft->fillCircle(x + cellW / 2, y + cellH / 2, 2, 0x2124);
      return;
    }

    drawThemedBox(x, y, cellW, cellH, sel);
    drawSymbianIconArt(actualIdx, x + cellW / 2, y + 29);

    tft->setTextSize(1);
    uint16_t txtCol = sel ? C_YELLOW : ((actualIdx == 12) ? C_NEON_PINK : ((actualIdx >= 4 && actualIdx <= 8) ? C_NEON_GREEN : C_WHITE));
    tft->setTextColor(txtCol, sel ? C_CARD_SEL : C_CARD_BG);
    int tw = vnStrLen(S40_MENU_TITLES[actualIdx]) * 6;
    tft->setCursor(x + max(1, (cellW - tw) / 2), y + 60);
    tft->print(S40_MENU_TITLES[actualIdx]);
  }

  // CHẾ ĐỘ 4: MENU CHÍNH (KHUNG NHÌN 4x3 CÓ CUỘN DỌC HIỂN THỊ BIỂU TƯỢNG THỨ 13+ BỊ CHE KHUẤT)
  static void drawSymbianMenuScreen(bool fullRedraw = true) {
    if (!tft) return;
    int w = tft->width();
    int cur = ((s40MenuCursor % S40_MENU_COUNT) + S40_MENU_COUNT) % S40_MENU_COUNT;
    s40MenuCursor = cur;

    // Tự động tính toán cuộn hàng (s40MenuScrollRow) khi con trỏ di chuyển xuống hàng 4 (index 12) hoặc lên hàng 1 (index 0..3)
    int curRow = cur / 4;
    if (curRow < s40MenuScrollRow) {
      s40MenuScrollRow = curRow;
    } else if (curRow > s40MenuScrollRow + 2) {
      s40MenuScrollRow = curRow - 2;
    }

    bool scrollChanged = (s40MenuScrollRow != lastDrawnMenuScroll);

    // Nếu không đổi tầng cuộn và chỉ di chuyển con trỏ trong cùng khung nhìn 4x3 -> Vẽ lại đúng 2 ô (< 2ms)
    if (!fullRedraw && !scrollChanged && lastDrawnMenuCursor >= 0 && lastDrawnMenuCursor < S40_MENU_COUNT && lastDrawnMenuCursor != cur) {
      int oldRow = (lastDrawnMenuCursor / 4) - s40MenuScrollRow;
      int oldCol = lastDrawnMenuCursor % 4;
      int newRow = (cur / 4) - s40MenuScrollRow;
      int newCol = cur % 4;
      if (oldRow >= 0 && oldRow < 3) drawSingleSymbianViewportCell(lastDrawnMenuCursor, oldRow, oldCol, false);
      if (newRow >= 0 && newRow < 3) drawSingleSymbianViewportCell(cur, newRow, newCol, true);
      drawSymbianMenuBanner();
      lastDrawnMenuCursor = cur;
      return;
    }

    if (fullRedraw) {
      tft->fillRect(0, 22, w, tft->height() - 44, C_DARK_BG);
    }
    char hdrTitle[48];
    snprintf(hdrTitle, sizeof(hdrTitle), "MENU CHÍNH [%d/%d]", cur + 1, S40_MENU_COUNT);
    drawSymbianChrome(hdrTitle, "Mở (OK)", s40MenuScrollRow > 0 ? "Cuộn Lên ^" : "Cuộn Xuống v", "Màn chờ");
    drawSymbianMenuBanner();

    for (int visRow = 0; visRow < 3; visRow++) {
      int actualRow = s40MenuScrollRow + visRow;
      for (int col = 0; col < 4; col++) {
        int actualIdx = actualRow * 4 + col;
        drawSingleSymbianViewportCell(actualIdx, visRow, col, actualIdx == cur);
      }
    }
    drawSymbianMenuScrollbar();
    lastDrawnMenuCursor = cur;
    lastDrawnMenuScroll = s40MenuScrollRow;
  }

  // CHẾ ĐỘ 5: APP HÌNH NỀN (TÙY CHỈNH MÀN HÌNH CHỜ)
  static int lastDrawnWallpaperCursor = -1;
  static int lastDrawnWallpaperFirstRow = -1;

  static void drawSingleWallpaperRow(int idx, int vis, bool sel) {
    if (!tft || idx < 0 || idx >= 10 || vis < 0 || vis >= 7) return;
    int w = tft->width();
    int ry = 30 + vis * 37;

    String imgLabel = stCfg.bgImage;
    if (imgLabel.startsWith("/")) imgLabel = imgLabel.substring(1);
    if (imgLabel.length() == 0) imgLabel = "(Chưa chọn)";
    if (vnStrLen(imgLabel) > 14) imgLabel = vnSubstr(imgLabel, 0, 14);

    const char* colorName = "Xanh Lam";
    if (stCfg.clockColor == C_NEON_GREEN) colorName = "Xanh Lá";
    else if (stCfg.clockColor == C_NEON_PINK) colorName = "Hồng Neon";
    else if (stCfg.clockColor == C_YELLOW || stCfg.clockColor == C_NEON_AMBER) colorName = "Vàng Ấm";
    else if (stCfg.clockColor == C_NEON_PURPLE) colorName = "Tím";
    else if (stCfg.clockColor == C_WHITE) colorName = "Trắng";

    const char* labels[10] = {
      "1. Chế độ hình nền",
      "2. Ảnh nền đã chọn",
      "3. Độ tối lớp phủ nền",
      "4. Kiểu chữ đồng hồ",
      "5. Vị trí đồng hồ",
      "6. Định dạng 12h/24h",
      "7. Hiển thị số giây",
      "8. Hiển thị thời tiết",
      "9. Màu sắc đồng hồ",
      "10. Xem màn hình chờ"
    };

    String values[10] = {
      stCfg.bgMode,
      imgLabel,
      String(stCfg.dimOverlay) + "%",
      stCfg.clockStyle,
      stCfg.clockPos,
      stCfg.clockFormat,
      stCfg.showSeconds ? "Đang bật" : "Đang tắt",
      stCfg.showWeather ? "Đang bật" : "Đang tắt",
      String(colorName),
      "Bấm OK để xem ->"
    };

    tft->fillRect(6, ry, w - 12, 33, C_DARK_BG);
    drawThemedBox(6, ry, w - 12, 33, sel);

    tft->setTextSize(1);
    tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? C_CARD_SEL : C_CARD_BG);
    tft->setCursor(12, ry + 6);
    tft->print(labels[idx]);

    tft->setTextColor(sel ? C_NEON_GREEN : C_NEON_CYAN, sel ? C_CARD_SEL : C_CARD_BG);
    tft->setCursor(18, ry + 19);
    tft->printf("< %s >", values[idx].c_str());
  }

  static void drawWallpaperAppScreen(bool fullRedraw = true) {
    if (!tft) return;
    int w = tft->width();
    if (fullRedraw) {
      tft->fillRect(0, 22, w, tft->height() - 44, C_DARK_BG);
      drawSymbianChrome("TÙY CHỈNH HÌNH NỀN", "Lưu (OK)", "< Đổi giá trị >", "Quay lại");
      lastDrawnWallpaperCursor = -1;
      lastDrawnWallpaperFirstRow = -1;
    }

    int firstRow = 0;
    if (s40WallpaperCursor >= 7) firstRow = s40WallpaperCursor - 6;

    // Tối ưu chống quét màn hình: Nếu không đổi khung cuộn trang -> Chỉ vẽ lại 2 ô thay đổi (< 2ms)
    if (!fullRedraw && firstRow == lastDrawnWallpaperFirstRow && lastDrawnWallpaperCursor >= 0 && lastDrawnWallpaperCursor != s40WallpaperCursor) {
      int oldVis = lastDrawnWallpaperCursor - firstRow;
      int newVis = s40WallpaperCursor - firstRow;
      if (oldVis >= 0 && oldVis < 7) drawSingleWallpaperRow(lastDrawnWallpaperCursor, oldVis, false);
      if (newVis >= 0 && newVis < 7) drawSingleWallpaperRow(s40WallpaperCursor, newVis, true);
      lastDrawnWallpaperCursor = s40WallpaperCursor;
      return;
    }

    for (int vis = 0; vis < 7; vis++) {
      int idx = firstRow + vis;
      if (idx >= 10) break;
      drawSingleWallpaperRow(idx, vis, idx == s40WallpaperCursor);
    }
    lastDrawnWallpaperCursor = s40WallpaperCursor;
    lastDrawnWallpaperFirstRow = firstRow;
  }

  // ============================================================================
  // MÀN HÌNH ALWAYS ON DISPLAY (AOD) / BẢO VỆ MÀN HÌNH: 3 PHONG CÁCH + NHIỆT ĐỘ ĐỘ ẨM
  // ============================================================================
  static void drawAnalogClockHand(int cx, int cy, float deg, int length, int width, uint16_t color) {
    float rad = deg * (3.14159265f / 180.0f);
    int xEnd = cx + (int)roundf(sinf(rad) * length);
    int yEnd = cy - (int)roundf(cosf(rad) * length);
    if (width <= 1) {
      tft->drawLine(cx, cy, xEnd, yEnd, color);
    } else {
      float nx = -cosf(rad) * 0.5f;
      float ny = -sinf(rad) * 0.5f;
      for (int w = -width / 2; w <= width / 2; w++) {
        tft->drawLine(cx + (int)roundf(nx * w), cy + (int)roundf(ny * w),
                      xEnd + (int)roundf(nx * w), yEnd + (int)roundf(ny * w), color);
      }
    }
  }

  static void drawAlwaysOnDisplayScreen(bool fullClear = true) {
    if (!tft) return;
    int W = tft->width();
    int H = tft->height();

    int hh = 12, mm = 0, ss = 0, dd = 28, mo = 9, yy = 2026, wday = 0;
    struct tm ti;
    if (getLocalTime(&ti, 5)) {
      hh = ti.tm_hour;
      mm = ti.tm_min;
      ss = ti.tm_sec;
      dd = ti.tm_mday;
      mo = ti.tm_mon + 1;
      yy = ti.tm_year + 1900;
      wday = ti.tm_wday;
    } else {
      unsigned long upSec = millis() / 1000UL;
      hh = (12 + (upSec / 3600)) % 24;
      mm = (upSec / 60) % 60;
      ss = upSec % 60;
    }

    // Dịch chuyển nhẹ vị trí mỗi phút (Pixel-Shift) để chống lưu ảnh (Burn-in)
    int shiftX = ((mm % 5) - 2) * 2;
    int shiftY = (((mm / 5) % 5) - 2) * 3;

    // Chuỗi nhiệt độ & độ ẩm thời gian thực từ cảm biến SHT31
    char envBuf[32];
    float curT = stCfg.temp;
    int curH = stCfg.humidity;
    if (curT > -40.0f && curT < 85.0f && curH > 0) {
      snprintf(envBuf, sizeof(envBuf), "%.1f'C  -  %d%% RH", curT, curH);
    } else {
      snprintf(envBuf, sizeof(envBuf), "--.-'C  -  --%% RH");
    }

    const char* viDays[7] = { "Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy" };
    char dateBuf[48];
    snprintf(dateBuf, sizeof(dateBuf), "%s, %02d/%02d", viDays[wday % 7], dd, mo);

    bool minChanged = (lastAodDrawnMinute != mm);
    if (fullClear || minChanged) {
      // TẮT HẲN HÌNH NỀN: Xoá sạch toàn bộ màn hình đen tuyền 100%
      tft->fillScreen(C_BLACK);
      lastAodDrawnMinute = mm;
    }

    // =========================================================================
    // MÀU SẮC AOD TỐI GIẢN (MUTED DIMMED PALETTE - SIÊU TIẾT KIỆM ĐIỆN)
    // =========================================================================
    const uint16_t C_AOD_TIME  = 0x8C71; // Xám bạc mờ dịu mắt (Muted Silver)
    const uint16_t C_AOD_COLON = 0x52AA; // Xám mờ (Dim Grey)
    const uint16_t C_AOD_DATE  = 0x52AA; // Xám mờ cho ngày tháng
    const uint16_t C_AOD_ENV   = 0x632C; // Xanh ngọc xám mờ cho thông số SHT31
    const uint16_t C_AOD_DIM   = 0x3186; // Xám đậm siêu mờ cho vạch cọc số/viền

    if (aodClockStyle == 2) {
      // =========================================================================
      // KIỂU 2: RETRO MINIMAL FLIP (LẬT SỐ CƠ KHÍ TỐI GIẢN NỀN ĐEN TUYỀN)
      // =========================================================================
      int cardW = 86, cardH = 56;
      int cardY = 96 + shiftY;
      int hrX = (W - (cardW * 2 + 16)) / 2 + shiftX;
      int minX = hrX + cardW + 16;

      if (fullClear || minChanged) {
        // Thẻ Giờ: Chỉ viền mỏng 1px màu xám mờ trên nền đen, không vẽ khối card đặc
        tft->drawRoundRect(hrX, cardY, cardW, cardH, 4, C_AOD_DIM);
        tft->drawFastHLine(hrX + 1, cardY + cardH / 2, cardW - 2, C_BLACK);
        tft->drawFastHLine(hrX + 1, cardY + cardH / 2 + 1, cardW - 2, 0x2124);
        tft->fillRect(hrX, cardY + cardH / 2 - 2, 2, 4, 0x4208);
        tft->fillRect(hrX + cardW - 2, cardY + cardH / 2 - 2, 2, 4, 0x4208);

        char hBuf[4]; snprintf(hBuf, sizeof(hBuf), "%02d", hh);
        tft->setTextSize(5);
        tft->setTextColor(C_AOD_TIME, C_BLACK);
        tft->setCursor(hrX + 15, cardY + 11);
        tft->print(hBuf);

        // Thẻ Phút
        tft->drawRoundRect(minX, cardY, cardW, cardH, 4, C_AOD_DIM);
        tft->drawFastHLine(minX + 1, cardY + cardH / 2, cardW - 2, C_BLACK);
        tft->drawFastHLine(minX + 1, cardY + cardH / 2 + 1, cardW - 2, 0x2124);
        tft->fillRect(minX, cardY + cardH / 2 - 2, 2, 4, 0x4208);
        tft->fillRect(minX + cardW - 2, cardY + cardH / 2 - 2, 2, 4, 0x4208);

        char mBuf[4]; snprintf(mBuf, sizeof(mBuf), "%02d", mm);
        tft->setCursor(minX + 15, cardY + 11);
        tft->print(mBuf);

        // Ngày tháng mờ bên dưới
        tft->setTextSize(1);
        tft->setTextColor(C_AOD_DATE, C_BLACK);
        int dw = vnStrLen(dateBuf) * 6;
        tft->setCursor((W - dw) / 2 + shiftX, cardY + cardH + 20);
        tft->print(dateBuf);

        // Nhiệt độ & Độ ẩm SHT31 mờ bên dưới, hoàn toàn không vẽ khung
        tft->setTextColor(C_AOD_ENV, C_BLACK);
        int ew = vnStrLen(envBuf) * 6;
        tft->setCursor((W - ew) / 2 + shiftX, cardY + cardH + 36);
        tft->print(envBuf);
      }

      // Hai chấm nhấp nháy mờ giữa 2 thẻ mỗi giây
      int colonX = hrX + cardW + 8;
      uint16_t dotCol = (ss % 2 == 0) ? C_AOD_COLON : C_BLACK;
      tft->fillCircle(colonX, cardY + 18, 2, dotCol);
      tft->fillCircle(colonX, cardY + 38, 2, dotCol);

    } else if (aodClockStyle == 3) {
      // =========================================================================
      // KIỂU 3: MINIMAL ANALOG DIAL (ĐỒNG HỒ KIM TỐI GIẢN NỀN ĐEN TUYỀN)
      // =========================================================================
      int cx = W / 2 + shiftX;
      int cy = 118 + shiftY;
      int R = 48;

      if (fullClear || minChanged) {
        // 12 Vạch số giờ mảnh mờ
        for (int i = 0; i < 12; i++) {
          float ang = i * 30.0f * (3.14159265f / 180.0f);
          int len = (i % 3 == 0) ? 6 : 3;
          int x1 = cx + (int)roundf(sinf(ang) * (R - len));
          int y1 = cy - (int)roundf(cosf(ang) * (R - len));
          int x2 = cx + (int)roundf(sinf(ang) * (R - 1));
          int y2 = cy - (int)roundf(cosf(ang) * (R - 1));
          uint16_t col = (i % 3 == 0) ? C_AOD_COLON : C_AOD_DIM;
          tft->drawLine(x1, y1, x2, y2, col);
        }

        // Kim Giờ (Xám trắng mờ, ngắn 24px, dày 2px)
        float hrAng = (hh % 12) * 30.0f + mm * 0.5f;
        drawAnalogClockHand(cx, cy, hrAng, 24, 2, C_AOD_TIME);

        // Kim Phút (Xanh ngọc xám mờ, dài 38px, dày 1px)
        float minAng = mm * 6.0f + ss * 0.1f;
        drawAnalogClockHand(cx, cy, minAng, 38, 1, C_AOD_ENV);

        // Trục tâm nhỏ 2px
        tft->fillCircle(cx, cy, 2, C_AOD_TIME);

        // Dòng giờ số nhỏ dưới mặt kim
        char digiBuf[16]; snprintf(digiBuf, sizeof(digiBuf), "%02d:%02d", hh, mm);
        tft->setTextSize(1);
        tft->setTextColor(C_AOD_DATE, C_BLACK);
        int tgw = strlen(digiBuf) * 6;
        tft->setCursor((W - tgw) / 2 + shiftX, cy + R + 14);
        tft->print(digiBuf);

        // Ngày tháng
        int dw = vnStrLen(dateBuf) * 6;
        tft->setCursor((W - dw) / 2 + shiftX, cy + R + 28);
        tft->print(dateBuf);

        // Nhiệt độ & Độ ẩm SHT31 mờ
        tft->setTextColor(C_AOD_ENV, C_BLACK);
        int ew = vnStrLen(envBuf) * 6;
        tft->setCursor((W - ew) / 2 + shiftX, cy + R + 44);
        tft->print(envBuf);
      }

    } else {
      // =========================================================================
      // KIỂU 1: SMARTPHONE MODERN DIGITAL AOD (CHUẨN ĐIỆN THOẠI SAMSUNG / IPHONE)
      // =========================================================================
      int timeY = 96 + shiftY;
      char hBuf[4], mBuf[4];
      snprintf(hBuf, sizeof(hBuf), "%02d", hh);
      snprintf(mBuf, sizeof(mBuf), "%02d", mm);

      if (fullClear || minChanged) {
        // Biểu tượng pin tối giản mờ ở góc trên
        int batX = W - 32 + shiftX;
        tft->drawRect(batX, 14 + shiftY, 18, 9, C_AOD_DIM);
        tft->fillRect(batX + 18, 16 + shiftY, 2, 5, C_AOD_DIM);
        tft->fillRect(batX + 2, 16 + shiftY, 12, 5, C_AOD_COLON);

        // Số Giờ cỡ 5 màu xám bạc mờ
        tft->setTextSize(5);
        tft->setTextColor(C_AOD_TIME, C_BLACK);
        tft->setCursor(44 + shiftX, timeY);
        tft->print(hBuf);

        // Số Phút cỡ 5 màu xám bạc mờ
        tft->setCursor(136 + shiftX, timeY);
        tft->print(mBuf);

        // Dòng Ngày Tháng mờ bên dưới
        tft->setTextSize(1);
        tft->setTextColor(C_AOD_DATE, C_BLACK);
        int dw = vnStrLen(dateBuf) * 6;
        tft->setCursor((W - dw) / 2 + shiftX, timeY + 48);
        tft->print(dateBuf);

        // Thông số Nhiệt độ & Độ ẩm SHT31 dạng phẳng không khung
        tft->setTextColor(C_AOD_ENV, C_BLACK);
        int ew = vnStrLen(envBuf) * 6;
        tft->setCursor((W - ew) / 2 + shiftX, timeY + 66);
        tft->print(envBuf);
      }

      // Hai chấm nhấp nháy mờ dịu mỗi giây
      uint16_t dotCol = (ss % 2 == 0) ? C_AOD_COLON : C_BLACK;
      tft->fillCircle(120 + shiftX, timeY + 14, 3, dotCol);
      tft->fillCircle(120 + shiftX, timeY + 30, 3, dotCol);
    }
  }

  // CHẾ ĐỘ 6: APP CÀI ĐẶT HỆ THỐNG & CHỦ ĐỀ GIAO DIỆN (5 PHONG CÁCH CHÍNH THỨC)
  static int lastDrawnSettingsCursor = -1;
  static int lastDrawnSettingsFirstRow = -1;

  static void drawSingleSettingsRow(int idx, int vis, bool sel) {
    if (!tft || idx < 0 || idx >= 11 || vis < 0 || vis >= 7) return;
    int w = tft->width();
    int ry = 30 + vis * 37;

    int spkVol = TestAudio::getSpeakerVolumePct();
    const char* micSensNames[3] = { "Thấp", "Tiêu chuẩn", "Cao" };
    const char* aodStyleLabels[4] = {
      "Tắt (Tắt màn hình)",
      "1. Thường (Kỹ thuật số)",
      "2. Lật số (Retro Flip)",
      "3. Đồng hồ kim (Analog)"
    };

    const char* labels[11] = {
      "1. Phong cách Giao diện",
      "2. Độ sáng màn hình",
      "3. Thời gian tắt màn hình",
      "4. Màn hình khóa (AOD)",
      "5. Âm lượng hệ thống",
      "6. Âm báo phím bấm",
      "7. Nhạc chuông báo thức",
      "8. Độ nhạy Micro thu âm",
      "9. Biểu cảm Trợ lý AI",
      "10. Biểu đồ Sóng âm & Cảm biến",
      "11. Động cơ & Vành đèn (L298N)"
    };

    String values[11] = {
      String(curTheme().name),
      String(screenBrightnessPct) + "%",
      String(TIMEOUT_LABELS[screenTimeoutIdx % 6]),
      String(aodStyleLabels[aodClockStyle % 4]),
      String(spkVol) + "%",
      keyBeepEnabled ? "Đang bật" : "Đang tắt",
      String(TestAudio::getAlarmTuneName(s40AlarmTuneIdx)),
      String(micSensNames[micSensitivityMode % 3]),
      getEmojiStateName(),
      "Bấm OK để mở ->",
      String(TestWindmill::getModeName())
    };

    tft->fillRect(6, ry, w - 12, 34, C_DARK_BG);
    drawThemedBox(6, ry, w - 12, 34, sel);

    tft->setTextSize(1);
    tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? C_CARD_SEL : C_CARD_BG);
    tft->setCursor(12, ry + 5);
    tft->print(labels[idx]);

    if (idx == 0) {
      // Hiển thị tên Chủ đề + 4 ô màu mẫu (Swatch) bên góc phải
      tft->setTextColor(C_NEON_CYAN, sel ? C_CARD_SEL : C_CARD_BG);
      tft->setCursor(12, ry + 19);
      tft->printf("< %s >", values[0].c_str());
      int sx = w - 48;
      tft->fillRect(sx,      ry + 6, 7, 7, C_DARK_BG);
      tft->drawRect(sx,      ry + 6, 7, 7, C_WHITE);
      tft->fillRect(sx + 9,  ry + 6, 7, 7, C_NEON_CYAN);
      tft->fillRect(sx + 18, ry + 6, 7, 7, C_NEON_GREEN);
      tft->fillRect(sx + 27, ry + 6, 7, 7, C_NEON_PINK);
    } else if (idx == 1 || idx == 4) {
      int pct = (idx == 1) ? screenBrightnessPct : spkVol;
      uint16_t barCol = (idx == 1) ? C_NEON_AMBER : C_NEON_GREEN;
      int bx = 12, by = ry + 19, bw = 136, bh = 10;
      tft->drawRect(bx, by, bw, bh, barCol);
      int fillW = ((bw - 4) * constrain(pct, 0, 100)) / 100;
      if (fillW > 0) tft->fillRect(bx + 2, by + 2, fillW, bh - 4, barCol);
      if (fillW < bw - 4) tft->fillRect(bx + 2 + fillW, by + 2, (bw - 4) - fillW, bh - 4, C_BLACK);

      tft->setTextColor(barCol, sel ? C_CARD_SEL : C_CARD_BG);
      tft->setCursor(156, ry + 19);
      tft->print(values[idx]);
    } else {
      uint16_t valCol = (idx == 3) ? (aodClockStyle > 0 ? C_NEON_GREEN : C_NEON_PINK) : (sel ? C_NEON_GREEN : C_NEON_CYAN);
      tft->setTextColor(valCol, sel ? C_CARD_SEL : C_CARD_BG);
      tft->setCursor(16, ry + 19);
      tft->printf("< %s >", values[idx].c_str());
    }
  }

  static void drawSettingsAppScreen(bool fullRedraw = true) {
    if (!tft) return;
    int w = tft->width();
    if (fullRedraw) {
      tft->fillRect(0, 22, w, tft->height() - 44, C_DARK_BG);
      drawSymbianChrome("CÀI ĐẶT HỆ THỐNG", "Đổi (OK)", "< Trái / Phải >", "Quay lại");
      lastDrawnSettingsCursor = -1;
      lastDrawnSettingsFirstRow = -1;
    }

    int firstRow = 0;
    if (s40SettingsCursor >= 7) firstRow = s40SettingsCursor - 6;

    // Tối ưu chống quét màn hình: Nếu không đổi tầng cuộn trang -> Chỉ vẽ lại 2 ô thay đổi (< 2ms)
    if (!fullRedraw && firstRow == lastDrawnSettingsFirstRow && lastDrawnSettingsCursor >= 0 && lastDrawnSettingsCursor != s40SettingsCursor) {
      int oldVis = lastDrawnSettingsCursor - firstRow;
      int newVis = s40SettingsCursor - firstRow;
      if (oldVis >= 0 && oldVis < 7) drawSingleSettingsRow(lastDrawnSettingsCursor, oldVis, false);
      if (newVis >= 0 && newVis < 7) drawSingleSettingsRow(s40SettingsCursor, newVis, true);
      lastDrawnSettingsCursor = s40SettingsCursor;
      return;
    }

    // Nếu con trỏ không đổi (ví dụ bấm Trái/Phải đổi giá trị): Chỉ vẽ lại đúng 1 hàng đang chọn!
    if (!fullRedraw && firstRow == lastDrawnSettingsFirstRow && lastDrawnSettingsCursor == s40SettingsCursor) {
      int curVis = s40SettingsCursor - firstRow;
      if (curVis >= 0 && curVis < 7) drawSingleSettingsRow(s40SettingsCursor, curVis, true);
      return;
    }

    for (int vis = 0; vis < 7; vis++) {
      int idx = firstRow + vis;
      if (idx >= 10) break;
      drawSingleSettingsRow(idx, vis, idx == s40SettingsCursor);
    }
    lastDrawnSettingsCursor = s40SettingsCursor;
    lastDrawnSettingsFirstRow = firstRow;
  }

  // Callback giải mã JPEG vào Bộ Đệm Khung Hình RAM cho App Thư Viện
  static bool jpgGalleryCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!tft || y >= 320 || x >= 240) return false;
    if (standbyStripCache[0] != nullptr) {
      for (uint16_t row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= 320) continue;
        for (uint16_t col = 0; col < w; col++) {
          int px = x + col;
          if (px < 0 || px >= 240) continue;
          setCachePixel(px, py, bitmap[row * w + col]);
        }
      }
    } else {
      tft->drawRGBBitmap(x, y, bitmap, w, h);
    }
    return true;
  }

  // CHẾ ĐỘ 7: APP THƯ VIỆN ẢNH (BỘ SƯU TẬP HÌNH ẢNH)
  static void drawGalleryAppScreen(bool fullRedraw = true) {
    if (!tft) return;
    int w = tft->width();
    if (fullRedraw) {
      tft->fillRect(0, 22, w, tft->height() - 44, C_DARK_BG);
      drawSymbianChrome("BỘ SƯU TẬP HÌNH ẢNH", "Đặt nền", "< Trước/Sau >", "Quay lại");
    }

    String files[20];
    size_t sizes[20];
    int count = getStoredJpgList(files, sizes, 20);

    if (count <= 0) {
      tft->fillRoundRect(12, 56, w - 24, 180, 6, C_CARD_BG);
      tft->drawRoundRect(12, 56, w - 24, 180, 6, C_NEON_PINK);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(28, 96);
      tft->print("THƯ VIỆN ĐANG TRỐNG!");
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(20, 120);
      tft->print("Hãy truy cập trang Web của Trạm");
      tft->setCursor(20, 136);
      tft->print("để tải thêm hình ảnh yêu thích");
      tft->setCursor(20, 152);
      tft->print("vào Thẻ nhớ SD hoặc Bộ nhớ nhé!");
      return;
    }

    if (s40GalleryIndex < 0) s40GalleryIndex = count - 1;
    if (s40GalleryIndex >= count) s40GalleryIndex = 0;

    String curFile = files[s40GalleryIndex];
    size_t curSizeKb = (sizes[s40GalleryIndex] + 512) / 1024;
    bool isCurrentBg = (stCfg.bgImage == curFile || ("/" + stCfg.bgImage) == curFile);
    String rgb565Path = ImageManager::getRgb565CompanionPath(curFile);

    // 1. Thanh thông tin file ảnh (y=28..50)
    tft->fillRoundRect(6, 28, w - 12, 22, 4, C_CARD_BG);
    tft->drawRoundRect(6, 28, w - 12, 22, 4, isCurrentBg ? C_NEON_GREEN : C_NEON_CYAN);
    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, C_CARD_BG);
    tft->setCursor(10, 35);
    String shortName = curFile;
    if (shortName.startsWith("sd:/sd_images/")) shortName = "SD:" + shortName.substring(14);
    else if (shortName.startsWith("/")) shortName = shortName.substring(1);
    if (shortName.length() > 16) shortName = shortName.substring(0, 16);
    if (rgb565Path.length() > 0) {
      tft->printf("[%d/%d] %s [0ms]", s40GalleryIndex + 1, count, shortName.c_str());
    } else {
      tft->printf("[%d/%d] %s (%uKB)", s40GalleryIndex + 1, count, shortName.c_str(), (unsigned int)curSizeKb);
    }

    // 2. Khung hiển thị Preview ảnh (y=54..266): Giải mã hoàn toàn trong RAM rồi đẩy 1 lần ra màn hình!
    tft->drawRect(18, 54, 204, 212, C_CARD_BORDER);
    bool hasCache = ensureStandbyStripCache();
    if (hasCache) {
      standbyCacheValid = false; // Đánh dấu bộ đệm tạm mượn cho Gallery để khi về Standby tự nạp lại
      clearCacheRect(19, 55, 202, 210, C_BLACK);
    }

    bool previewRendered = false;
    // ƯU TIÊN 1: Đọc trực tiếp file .RGB565 (240x320) trên Thẻ nhớ SD vào khung Preview 156x208 (0ms giải mã!)
    if (hasCache && rgb565Path.length() > 0 && TestSDCard::isMounted()) {
      File rf = SD.open(rgb565Path, FILE_READ);
      if (rf && rf.size() >= 240 * 320 * 2) {
        uint16_t srcRow[240];
        const int dstW = 156;
        const int dstH = 208;
        const int dstX0 = (w - dstW) / 2; // 42
        const int dstY0 = 56;
        int lastSrcY = -1;
        for (int dy = 0; dy < dstH; dy++) {
          int srcY = (dy * 320) / dstH;
          while (lastSrcY < srcY) {
            rf.read((uint8_t*)srcRow, 240 * sizeof(uint16_t));
            lastSrcY++;
          }
          for (int dx = 0; dx < dstW; dx++) {
            int srcX = (dx * 240) / dstW;
            setCachePixel(dstX0 + dx, dstY0 + dy, srcRow[srcX]);
          }
        }
        rf.close();
        previewRendered = true;
      } else if (rf) {
        rf.close();
      }
    }

    // ƯU TIÊN 2: Nếu là ảnh .jpg trên SD hoặc LittleFS, giải mã trong RAM
    if (!previewRendered) {
      String fsPath = curFile.startsWith("sd:") ? curFile.substring(3) : curFile;
      bool isSdFile = curFile.startsWith("sd:");
      uint16_t jw = 0, jh = 0;
      TJpgDec.setCallback(jpgGalleryCallback);
      TJpgDec.setSwapBytes(false);
      if (isSdFile && TestSDCard::isMounted() && SD.exists(fsPath)) {
        File sf = SD.open(fsPath, FILE_READ);
        size_t sz = sf ? sf.size() : 0;
        if (sz > 0 && sz <= 90000) {
          uint8_t* buf = (uint8_t*)malloc(sz);
          if (buf) {
            if (sf.read(buf, sz) == sz && TJpgDec.getJpgSize(&jw, &jh, buf, sz) == JDR_OK && jw > 0 && jh > 0) {
              uint8_t scale = 2;
              if (jw >= 480 || jh >= 640) scale = 4;
              else if (jw <= 180 && jh <= 200) scale = 1;
              TJpgDec.setJpgScale(scale);
              int dw = jw / scale;
              int dh = jh / scale;
              int px = max(20, (w - dw) / 2);
              int py = max(56, 56 + (208 - dh) / 2);
              TJpgDec.drawJpg(px, py, buf, sz);
            }
            free(buf);
          }
        }
        if (sf) sf.close();
      } else if (!isSdFile && TJpgDec.getFsJpgSize(&jw, &jh, fsPath.c_str(), LittleFS) == JDR_OK && jw > 0 && jh > 0) {
        uint8_t scale = 2;
        if (jw >= 480 || jh >= 640) scale = 4;
        else if (jw <= 180 && jh <= 200) scale = 1;
        TJpgDec.setJpgScale(scale);
        int dw = jw / scale;
        int dh = jh / scale;
        int px = max(20, (w - dw) / 2);
        int py = max(56, 56 + (208 - dh) / 2);
        decodeFsJpgFromRam(px, py, fsPath.c_str());
      }
    }

    // Đẩy toàn bộ khung Preview 202x210 từ RAM ra màn hình ST7789 trong 1 lượt (Không còn cảnh quét từng ô!)
    if (hasCache) {
      for (int ry = 55; ry < 265; ry++) {
        int s = ry / 80;
        int sy = ry % 80;
        tft->drawRGBBitmap(19, ry, &standbyStripCache[s][sy * 240 + 19], 202, 1);
      }
    }

    // 3. Trạng thái dưới đáy khung ảnh (y=270..292)
    tft->fillRoundRect(6, 270, w - 12, 22, 4, C_CARD_BG);
    tft->setTextColor(isCurrentBg ? C_NEON_GREEN : C_WHITE, C_CARD_BG);
    tft->setCursor(14, 277);
    if (isCurrentBg) {
      tft->print("* Đang dùng làm Hình nền chờ *");
    } else {
      tft->print("Bấm phím OK để đặt làm Hình nền");
    }
  }

  // ============================================================================
  // HÀM TẠO VÀ KẾT XUẤT MÃ QR CODE CHUẨN ISO LÊN MÀN HÌNH ST7789
  // Hỗ trợ cả chuỗi Wi-Fi (WIFI:S:...) và Web URL (http://...)
  // Tự động căn giữa, nền trắng tuyết, module đen tương phản cao, camera bắt nét tức thì
  // ============================================================================
  static void drawQrCodeToScreen(int centerX, int centerY, const String& text, int targetBoxSize = 145) {
    if (!tft) return;
    
    // Tự động chọn QR version phù hợp: 3 (tối đa 53 ký tự) hoặc 4 (tối đa 78 ký tự)
    uint8_t version = (text.length() <= 45) ? 3 : 4;
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(4)]; // Buffer an toàn cho version 4 (~137 bytes)
    
    int8_t err = qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, text.c_str());
    if (err != 0) {
      version = 4;
      err = qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, text.c_str());
      if (err != 0) {
        tft->setTextColor(C_RED, C_CARD_BG);
        tft->setCursor(centerX - 40, centerY);
        tft->print("LỖI SINH QR!");
        return;
      }
    }

    int modSize = qrcode.size; // 29 hoặc 33
    int scale = targetBoxSize / modSize;
    if (scale < 3) scale = 3;
    if (scale > 5) scale = 5;

    int qrPixelSize = modSize * scale;
    int startX = centerX - (qrPixelSize / 2);
    int startY = centerY - (qrPixelSize / 2);

    // Vẽ nền trắng tĩnh lặng (quiet zone)
    int quietPad = 7;
    tft->fillRoundRect(startX - quietPad, startY - quietPad, qrPixelSize + quietPad * 2, qrPixelSize + quietPad * 2, 5, C_WHITE);
    tft->drawRoundRect(startX - quietPad - 1, startY - quietPad - 1, qrPixelSize + quietPad * 2 + 2, qrPixelSize + quietPad * 2 + 2, 6, C_NEON_CYAN);

    // Vẽ từng module đen
    for (uint8_t y = 0; y < modSize; y++) {
      for (uint8_t x = 0; x < modSize; x++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          tft->fillRect(startX + x * scale, startY + y * scale, scale, scale, C_BLACK);
        }
      }
    }
  }

  static void drawQrContentCard(int w, bool isStandaloneMode) {
    // 1. Tab chuyển đổi ở trên
    int tabW = (w - 18) / 2;
    uint16_t t0Bg = (s40QrModeTab == 0) ? C_NEON_CYAN : C_CARD_BG;
    uint16_t t0Fg = (s40QrModeTab == 0) ? C_BLACK : C_WHITE;
    tft->fillRoundRect(7, 28, tabW, 20, 4, t0Bg);
    tft->drawRoundRect(7, 28, tabW, 20, 4, C_NEON_CYAN);
    tft->setTextColor(t0Fg, t0Bg);
    tft->setCursor(14, 34);
    tft->print("1. NỐI WI-FI");

    uint16_t t1Bg = (s40QrModeTab == 1) ? C_NEON_PINK : C_CARD_BG;
    uint16_t t1Fg = (s40QrModeTab == 1) ? C_BLACK : C_WHITE;
    tft->fillRoundRect(11 + tabW, 28, tabW, 20, 4, t1Bg);
    tft->drawRoundRect(11 + tabW, 28, tabW, 20, 4, C_NEON_PINK);
    tft->setTextColor(t1Fg, t1Bg);
    tft->setCursor(18 + tabW, 34);
    tft->print("2. MỞ WEB CÀI");

    // Khung card chứa mã QR
    tft->fillRoundRect(6, 52, w - 12, 240, 6, C_CARD_BG);
    tft->drawRoundRect(6, 52, w - 12, 240, 6, (s40QrModeTab == 0) ? C_NEON_CYAN : C_NEON_PINK);

    String qrPayload;
    String infoTitle;
    String infoSub;
    String tipText;

    if (s40QrModeTab == 0) {
      qrPayload = String("WIFI:S:") + DEFAULT_AP_SSID + ";T:WPA;P:" + DEFAULT_AP_PASS + ";;";
      infoTitle = String("SSID: ") + DEFAULT_AP_SSID;
      infoSub   = String("Mật khẩu: ") + DEFAULT_AP_PASS;
      tipText   = "Quét bằng Camera để tự động kết nối Wi-Fi";
    } else {
      bool wifiOk = (WiFi.status() == WL_CONNECTED);
      String hostIp = wifiOk ? WiFi.localIP().toString() : "192.168.4.1";
      qrPayload = String("http://") + hostIp + "/#wifi";
      infoTitle = String("Địa chỉ Web: ") + hostIp;
      infoSub   = wifiOk ? "Mạng gia đình" : "SoftAP Trạm Vũ Trụ";
      tipText   = "Quét để mở trình duyệt & tự quét Wi-Fi";
    }

    // Vẽ mã QR ở giữa khung
    drawQrCodeToScreen(w / 2, 134, qrPayload, 145);

    // Thông tin bên dưới mã QR
    tft->setTextColor((s40QrModeTab == 0) ? C_NEON_CYAN : C_NEON_PINK, C_CARD_BG);
    tft->setCursor(14, 218);
    tft->print(infoTitle);

    tft->setTextColor(C_WHITE, C_CARD_BG);
    tft->setCursor(14, 234);
    tft->print(infoSub);

    tft->drawFastHLine(14, 250, w - 28, C_CARD_BORDER);

    tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
    tft->setCursor(14, 256);
    tft->print(tipText);

    tft->setTextColor(C_SLATE, C_CARD_BG);
    tft->setCursor(14, 274);
    if (isStandaloneMode) {
      tft->print("Bấm phím [OK] hoặc Trái/Phải để đổi mã");
    } else {
      tft->print("Bấm [OK]: Đổi mã | Trái/Phải: Trang");
    }
  }

  // ============================================================================
  // HÀM VẼ POPUP MENU TÙY CHỌN HỆ THỐNG (CHUẨN SYMBIAN S40 OPTIONS MENU)
  // ============================================================================
  static void drawSingleAboutMenuItem(int idx, bool sel) {
    if (!tft || idx < 0 || idx >= 4) return;
    int w = tft->width();
    int mw = 216;
    int mx = (w - mw) / 2;
    int my = 85;
    int iy = my + 34 + idx * 26;

    const char* items[5] = {
      "1. Kiem tra cap nhat OTA",
      "2. Kiem tra ngoai vi (POST)",
      "3. Ma QR Cai Dat Wi-Fi",
      "4. Doi sang Trang ke tiep",
      "5. Dong menu tuy chon"
    };

    if (sel) {
      tft->fillRoundRect(mx + 6, iy, mw - 12, 22, 4, C_NEON_PURPLE);
      tft->drawRoundRect(mx + 6, iy, mw - 12, 22, 4, C_YELLOW);
      tft->setTextColor(C_WHITE, C_NEON_PURPLE);
    } else {
      tft->fillRoundRect(mx + 6, iy, mw - 12, 22, 4, 0x10A2);
      tft->setTextColor(0xBDD7, 0x10A2);
    }
    tft->setTextSize(1);
    tft->setCursor(mx + 14, iy + 6);
    tft->print(items[idx]);
  }

  static void drawAboutPopupMenu(int w, int h) {
    int mw = 216;
    int mh = 172;
    int mx = (w - mw) / 2;
    int my = 85;

    // Bóng đổ mờ
    tft->fillRoundRect(mx + 4, my + 4, mw, mh, 8, 0x0000);
    // Khung Card Popup
    tft->fillRoundRect(mx, my, mw, mh, 8, 0x10A2); // Deep Navy Blue
    tft->drawRoundRect(mx, my, mw, mh, 8, C_NEON_CYAN);
    tft->drawRoundRect(mx + 1, my + 1, mw - 2, mh - 2, 7, C_WHITE);

    // Tiêu đề Popup
    tft->fillRoundRect(mx + 4, my + 4, mw - 8, 24, 5, 0x0210);
    tft->setTextColor(C_YELLOW, 0x0210);
    tft->setTextSize(1);
    tft->setCursor(mx + 14, my + 11);
    tft->print("TÙY CHỌN HỆ THỐNG");

    for (int i = 0; i < 5; i++) {
      drawSingleAboutMenuItem(i, i == s40AboutMenuCursor);
    }
  }

  // ============================================================================
  // HÀM VẼ HỘP THOẠI TRẠNG THÁI KIỂM TRA CẬP NHẬT CLOUD OTA (GITHUB)
  // ============================================================================
  static void drawAboutOtaModal(int w, int h) {
    int mw = 224;
    int mh = (s40OtaCheckState == 2) ? 220 : 160;
    int mx = (w - mw) / 2;
    int my = 50;

    // Bóng đổ mờ
    tft->fillRoundRect(mx + 4, my + 4, mw, mh, 8, 0x0000);

    uint16_t borderCol = (s40OtaCheckState == 1) ? C_NEON_CYAN :
                         ((s40OtaCheckState == 2) ? C_NEON_GREEN :
                         ((s40OtaCheckState == 3) ? C_NEON_CYAN : C_RED));

    tft->fillRoundRect(mx, my, mw, mh, 8, C_CARD_BG);
    tft->drawRoundRect(mx, my, mw, mh, 8, borderCol);
    tft->drawRoundRect(mx + 1, my + 1, mw - 2, mh - 2, 7, C_WHITE);

    if (s40OtaCheckState == 1) {
      // 1. Đang kết nối kiểm tra
      tft->fillRoundRect(mx + 4, my + 4, mw - 8, 24, 5, 0x0210);
      tft->setTextColor(C_NEON_CYAN, 0x0210);
      tft->setCursor(mx + 16, my + 11);
      tft->print("KIỂM TRA CẬP NHẬT...");

      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(mx + 14, my + 48);
      tft->print("Đang kết nối GitHub...");
      tft->setCursor(mx + 14, my + 68);
      tft->print("Vui lòng đợi giây lát...");

      tft->drawRoundRect(mx + 20, my + 105, mw - 40, 12, 3, C_CARD_BORDER);
      tft->fillRect(mx + 22, my + 107, (mw - 44) * 3 / 4, 8, C_NEON_CYAN);
    } else if (s40OtaCheckState == 2) {
      // 2. Phát hiện bản cập nhật mới!
      tft->fillRoundRect(mx + 4, my + 4, mw - 8, 24, 5, 0x0320);
      tft->setTextColor(C_NEON_GREEN, 0x0320);
      tft->setCursor(mx + 16, my + 11);
      tft->print("PHÁT HIỆN BẢN MỚI!");

      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(mx + 14, my + 38);
      tft->printf("Bản mới : %s", s40OtaUpdateInfo.latestVersion.c_str());

      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(mx + 14, my + 54);
      tft->printf("Hiện tại: %s", FIRMWARE_VERSION);

      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(mx + 14, my + 70);
      float mb = (float)s40OtaUpdateInfo.binSize / (1024.0f * 1024.0f);
      tft->printf("Kích thước: %.2f MB", mb > 0.1f ? mb : 1.45f);

      tft->drawFastHLine(mx + 10, my + 88, mw - 20, C_CARD_BORDER);

      tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
      tft->setCursor(mx + 14, my + 96);
      tft->print("Bấm OK để nâng cấp OTA:");

      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(mx + 14, my + 112);
      tft->print("* Tải trực tiếp từ GitHub");
      tft->setCursor(mx + 14, my + 128);
      tft->print("* Tự reboot sang bản mới");

      int btnW = (mw - 28) / 2;
      tft->fillRoundRect(mx + 10, my + 172, btnW, 28, 5, C_NEON_GREEN);
      tft->setTextColor(C_BLACK, C_NEON_GREEN);
      tft->setCursor(mx + 16, my + 180);
      tft->print("[OK] NÂNG CẤP");

      tft->fillRoundRect(mx + 18 + btnW, my + 172, btnW, 28, 5, C_CARD_BORDER);
      tft->setTextColor(C_WHITE, C_CARD_BORDER);
      tft->setCursor(mx + 26 + btnW, my + 180);
      tft->print("[EXIT] ĐÓNG");
    } else if (s40OtaCheckState == 3) {
      // 3. Đã là bản mới nhất!
      tft->fillRoundRect(mx + 4, my + 4, mw - 8, 24, 5, 0x0210);
      tft->setTextColor(C_NEON_CYAN, 0x0210);
      tft->setCursor(mx + 16, my + 11);
      tft->print("PHIÊN BẢN MỚI NHẤT");

      tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
      tft->setCursor(mx + 14, my + 44);
      tft->print("Space OS v3.1.0");

      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(mx + 14, my + 64);
      tft->print("Thiết bị đang chạy bản");
      tft->setCursor(mx + 14, my + 80);
      tft->print("mới nhất trên GitHub!");

      tft->fillRoundRect(mx + (mw - 100) / 2, my + 115, 100, 26, 5, C_NEON_CYAN);
      tft->setTextColor(C_BLACK, C_NEON_CYAN);
      tft->setCursor(mx + (mw - 100) / 2 + 18, my + 123);
      tft->print("[OK] ĐỒNG Ý");
    } else if (s40OtaCheckState == 4) {
      // 4. Lỗi kết nối mạng
      tft->fillRoundRect(mx + 4, my + 4, mw - 8, 24, 5, 0x4000);
      tft->setTextColor(C_RED, 0x4000);
      tft->setCursor(mx + 16, my + 11);
      tft->print("LỖI KẾT NỐI MẠNG");

      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(mx + 14, my + 44);
      tft->print("Không thể kết nối GitHub!");
      tft->setCursor(mx + 14, my + 64);
      tft->print("Vui lòng kiểm tra lại");
      tft->setCursor(mx + 14, my + 80);
      tft->print("sóng Wi-Fi & Internet.");

      tft->fillRoundRect(mx + (mw - 100) / 2, my + 115, 100, 26, 5, C_RED);
      tft->setTextColor(C_WHITE, C_RED);
      tft->setCursor(mx + (mw - 100) / 2 + 20, my + 123);
      tft->print("[OK] ĐÓNG");
    }
  }

  // CHẾ ĐỘ 8: THÔNG TIN THIẾT BỊ & KẾT NỐI MẠNG (CHÍNH THỨC - 3 TRANG)
  static void drawAboutAppScreen(bool fullRedraw = true) {
    if (!tft) return;
    int w = tft->width();
    if (fullRedraw) {
      tft->fillRect(0, 22, w, tft->height() - 44, C_DARK_BG);
    }

    const char* leftSoft = "Tùy chọn";
    const char* midTitle = (s40AboutPage == 0) ? "< Trang 1/3 >" : ((s40AboutPage == 1) ? "< Trang 2/3 >" : "< Trang 3/3 >");
    const char* rightSoft = "Quay lại";

    if (s40OtaCheckState != 0) {
      if (s40OtaCheckState == 2) {
        leftSoft = "Nâng cấp";
        rightSoft = "Đóng";
      } else {
        leftSoft = "Đóng";
        rightSoft = "";
      }
      midTitle = "";
    } else if (s40AboutMenuOpen) {
      leftSoft = "Chọn";
      midTitle = "< Chọn mục >";
      rightSoft = "Đóng";
    }

    drawSymbianChrome("THÔNG TIN THIẾT BỊ", leftSoft, midTitle, rightSoft);

    if (s40AboutPage == 0) {
      tft->fillRoundRect(6, 28, w - 12, 264, 6, C_CARD_BG);
      tft->drawRoundRect(6, 28, w - 12, 264, 6, C_NEON_PURPLE);

      // TRANG 1: THÔNG TIN PHẦN MỀM & KẾT NỐI MẠNG
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(14, 38);
      tft->print("[TRANG 1/3] HỆ THỐNG & KẾT NỐI");
      tft->drawFastHLine(12, 50, w - 24, C_CARD_BORDER);

      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(14, 58);
      tft->printf("Tên thiết bị: %s", OtaManager::getDeviceName().c_str());
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 74);
      tft->printf("Hệ điều hành: %s", OtaManager::getOsName().c_str());
      tft->setCursor(14, 90);
      tft->printf("Phiên bản   : %s (%s)", OtaManager::getFirmwareVersion().c_str(), OtaManager::getRunningPartitionName().c_str());

      tft->drawFastHLine(12, 106, w - 24, C_CARD_BORDER);
      tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
      tft->setCursor(14, 114);
      tft->print("TRẠNG THÁI MẠNG & ĐỒNG BỘ:");

      bool wifiOk = (WiFi.status() == WL_CONNECTED);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 130);
      tft->printf("Wi-Fi : %s", wifiOk ? toCleanAscii(WiFi.SSID()).c_str() : "Phát Wi-Fi (AP Mode)");
      tft->setCursor(14, 146);
      tft->printf("IP    : %s", wifiOk ? WiFi.localIP().toString().c_str() : "192.168.4.1");
      tft->setCursor(14, 162);
      tft->printf("MAC   : %s", WiFi.macAddress().c_str());
      tft->setCursor(14, 178);
      tft->printf("Sóng  : %d dBm", wifiOk ? WiFi.RSSI() : 0);

      bool pcLive = PcStatsManager::isLive();
      tft->setTextColor(pcLive ? C_NEON_GREEN : C_NEON_PINK, C_CARD_BG);
      tft->setCursor(14, 196);
      tft->printf("Máy tính PC: %s", pcLive ? "Đang kết nối" : "Chưa kết nối");

      tft->drawFastHLine(12, 214, w - 24, C_CARD_BORDER);
      tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
      tft->setCursor(14, 222);
      tft->printf("Hoạt động  : %lu phút %lu giây", (millis() / 60000UL), (millis() / 1000UL) % 60UL);
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(14, 242);
      tft->print("Bấm MENU: Tùy chọn & Cập nhật OTA");
      tft->setCursor(14, 256);
      tft->print("Trang 2: Bộ nhớ | Trang 3: QR Wi-Fi");
    } else if (s40AboutPage == 1) {
      tft->fillRoundRect(6, 28, w - 12, 264, 6, C_CARD_BG);
      tft->drawRoundRect(6, 28, w - 12, 264, 6, C_NEON_PURPLE);

      // TRANG 2: THÔNG SỐ HỆ THỐNG & DUNG LƯỢNG BỘ NHỚ
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(14, 38);
      tft->print("[TRANG 2/3] CẤU HÌNH HỆ THỐNG");
      tft->drawFastHLine(12, 50, w - 24, C_CARD_BORDER);

      uint32_t freeRamKb    = ESP.getFreeHeap() / 1024;
      uint32_t totalRamKb   = ESP.getHeapSize() / 1024;
      uint32_t freePsramKb  = ESP.getFreePsram() / 1024;
      uint32_t totalPsramKb = ESP.getPsramSize() / 1024;
      uint32_t fsUsedKb     = LittleFS.usedBytes() / 1024;
      uint32_t fsTotalKb    = LittleFS.totalBytes() / 1024;

      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(14, 58);
      tft->printf("Vi xử lý : %s @ %dMHz", ESP.getChipModel(), ESP.getCpuFreqMHz());
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 74);
      tft->printf("Bộ nhớ RAM   : %u / %u KB trống", (unsigned)freeRamKb, (unsigned)totalRamKb);
      tft->setCursor(14, 90);
      tft->printf("Bộ nhớ PSRAM : %u / %u KB trống", (unsigned)freePsramKb, (unsigned)totalPsramKb);
      tft->setCursor(14, 106);
      tft->printf("Bộ nhớ trong : %u / %u KB đã dùng", (unsigned)fsUsedKb, (unsigned)fsTotalKb);

      tft->drawFastHLine(12, 122, w - 24, C_CARD_BORDER);
      tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
      tft->setCursor(14, 130);
      tft->print("TRẠNG THÁI CÁC BỘ PHẬN:");

      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 146);
      tft->printf("* Màn hình : Sáng %d%% (%s)", screenBrightnessPct, curTheme().headerTag);
      tft->setCursor(14, 162);
      tft->printf("* Âm thanh : Loa %d%% | Phím %s", TestAudio::getSpeakerVolumePct(), keyBeepEnabled ? "Bật" : "Tắt");
      tft->setCursor(14, 178);
      tft->printf("* Micro    : Độ nhạy %s", (micSensitivityMode == 0) ? "Thấp" : ((micSensitivityMode == 1) ? "Tiêu chuẩn" : "Cao"));
      tft->setCursor(14, 194);
      tft->printf("* Thẻ nhớ  : %s", TestSDCard::isMounted() ? "Đã nhận Thẻ nhớ SD" : "Chưa lắp Thẻ nhớ");
      tft->setCursor(14, 210);
      if (TestSensors::isSht31Connected()) {
        tft->printf("* Cảm biến : %.1f°C / %.1f%%RH", TestSensors::getTemperatureC(), TestSensors::getHumidityPct());
      } else {
        tft->print("* Cảm biến : Chế độ tiêu chuẩn");
      }
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(14, 238);
      tft->print("Bấm MENU: Tùy chọn | OK: Trang 3");
      tft->setCursor(14, 252);
      tft->print("Trang 3: Mã QR Cài Đặt Wi-Fi");
    } else {
      // TRANG 3: MÃ QR CÀI ĐẶT WI-FI & WEB PORTAL
      drawQrContentCard(w, false);
    }

    // Hiển thị Menu Tùy Chọn Popup nếu đang mở
    if (s40AboutMenuOpen) {
      drawAboutPopupMenu(w, tft->height());
    }
    // Hiển thị Hộp thoại Kiểm tra OTA nếu đang kích hoạt
    if (s40OtaCheckState != 0) {
      drawAboutOtaModal(w, tft->height());
    }
  }

  // CHẾ ĐỘ 16: MÀN HÌNH MÃ QR CÀI ĐẶT & KẾT NỐI WI-FI TOÀN DIỆN
  static void drawQrCodeWifiScreen(bool fullRedraw = true) {
    if (!tft) return;
    int w = tft->width();
    if (fullRedraw) {
      tft->fillRect(0, 22, w, tft->height() - 44, C_DARK_BG);
    }
    drawSymbianChrome("MÃ QR CÀI ĐẶT WI-FI", "Đổi mã", s40QrModeTab == 0 ? "Wi-Fi" : "Web", "Quay lại");
    drawQrContentCard(w, true);
  }

  static String s40AiEmotionTag = "HAPPY";
  static bool s40AiAutoScroll = true;
  static int& s40MemPopupCursor = s40MemOptionCursor;
  static unsigned long& s40AiStateChangeMs = s40AiStateStartMs;
  static bool& s40AiSpeechDetected = s40AiHeardSpeech;
  static String s40AiSttPhaseMsg = "";

  static String s40LastHeaderTitle = "TRẠM VŨ TRỤ";
  static void drawSymbianHeader(const char* title) {
    s40LastHeaderTitle = title ? title : "TRẠM VŨ TRỤ";
    drawSymbianChrome(s40LastHeaderTitle.c_str(), "Chọn", "OK", "Quay lại");
  }
  static void drawSymbianSoftkeys(const char* leftSoft, const char* midSoft, const char* rightSoft) {
    drawSymbianChrome(s40LastHeaderTitle.c_str(), leftSoft, midSoft, rightSoft);
  }

  // ============================================================================
  // HÀM TÁCH DÒNG TRÒN CHỮ TIẾNG VIỆT UTF-8 (WORD-BOUNDARY WRAPPING)
  // ============================================================================
  static int wrapTextWordBoundary(const String& text, int maxCharsPerLine, String outLines[], int maxLines) {
    int lineCount = 0;
    String currentLine = "";
    int len = text.length();
    int i = 0;

    while (i < len && lineCount < maxLines) {
      while (i < len && text[i] == ' ') i++;
      if (i >= len) break;

      if (text[i] == '\n') {
        outLines[lineCount++] = currentLine;
        currentLine = "";
        i++;
        continue;
      }

      int wordStart = i;
      while (i < len && text[i] != ' ' && text[i] != '\n') {
        i++;
      }
      String word = text.substring(wordStart, i);

      while (vnStrLen(word) > maxCharsPerLine && lineCount < maxLines) {
        if (currentLine.length() > 0) {
          outLines[lineCount++] = currentLine;
          currentLine = "";
          if (lineCount >= maxLines) break;
        }
        outLines[lineCount++] = vnSubstr(word, 0, maxCharsPerLine);
        word = vnSubstr(word, maxCharsPerLine, vnStrLen(word) - maxCharsPerLine);
      }

      if (lineCount >= maxLines) break;

      if (currentLine.length() == 0) {
        currentLine = word;
      } else if (vnStrLen(currentLine) + 1 + vnStrLen(word) <= maxCharsPerLine) {
        currentLine += " " + word;
      } else {
        outLines[lineCount++] = currentLine;
        currentLine = word;
      }
    }

    if (currentLine.length() > 0 && lineCount < maxLines) {
      outLines[lineCount++] = currentLine;
    }
    if (lineCount == 0 && maxLines > 0) {
      outLines[0] = "";
      lineCount = 1;
    }
    return lineCount;
  }

  // ============================================================================
  // CHẾ ĐỘ 11: TRỢ LÝ XIAOZHI AI (TRÒ CHUYỆN GIỌNG NÓI THÔNG MINH)
  // ============================================================================
  static void drawXiaoZhiConversationBoxOnly() {
    if (!tft) return;
    int W = tft->width();

    uint16_t borderCol = C_NEON_CYAN;
    if (s40AiState == AI_STATE_LISTENING) borderCol = C_NEON_GREEN;
    else if (s40AiState == AI_STATE_THINKING) borderCol = C_NEON_PINK;
    else if (s40AiState == AI_STATE_REPLYING) borderCol = C_NEON_CYAN;

    tft->fillRoundRect(6, 196, W - 12, 98, 8, 0x0862);
    tft->drawRoundRect(6, 196, W - 12, 98, 8, borderCol);

    tft->setTextSize(1);
    if (s40AiState == AI_STATE_IDLE) {
      tft->setTextColor(C_NEON_CYAN, 0x0862);
      tft->setCursor(14, 203);
      tft->print("SẴN SÀNG TRÒ CHUYỆN CÙNG BẠN");

      tft->setTextColor(C_YELLOW, 0x0862);
      tft->setCursor(14, 220);
      tft->print("Bấm phím [OK] và nói Tiếng Việt");
      tft->setCursor(14, 234);
      tft->print("vào Micro (cách khoảng 10 - 25cm).");

      tft->setTextColor(C_WHITE, 0x0862);
      tft->setCursor(14, 252);
      tft->print("* Nhận diện giọng nói thông minh");
      tft->setCursor(14, 266);
      tft->print("* Tự động trả lời & biểu cảm mắt");
      tft->setCursor(14, 280);
      tft->print("* Phản hồi trực tiếp bằng Tiếng Việt");
    } else if (s40AiState == AI_STATE_LISTENING) {
      int peak = TestAudio::getLastMicLevelPct();
      float recSec = TestAudio::getRecordedDurationSec();
      int maxVu = TestAudio::getRecordedMaxVuPct();

      tft->setTextColor(C_NEON_GREEN, 0x0862);
      tft->setCursor(14, 203);
      tft->printf("ĐANG LẮNG NGHE... (Âm lượng: %d%%)", peak);

      tft->setTextColor(C_WHITE, 0x0862);
      tft->setCursor(14, 220);
      tft->printf("Thời gian thu: %.1f / 8.0 giây", recSec);
      tft->setCursor(14, 235);
      tft->printf("Độ lớn giọng nói: %d%%", maxVu);

      tft->setCursor(14, 252);
      if (s40AiSpeechDetected) {
        tft->setTextColor(C_NEON_GREEN, 0x0862);
        tft->print(">> Đã nhận diện được tiếng nói!");
      } else {
        tft->setTextColor(C_YELLOW, 0x0862);
        tft->print(">> Hãy nói rõ ràng vào Micro...");
      }

      tft->setTextColor(C_SLATE, 0x0862);
      tft->setCursor(14, 272);
      tft->print("Ngừng nói 1.6s hoặc bấm [OK] gửi ngay");
    } else if (s40AiState == AI_STATE_THINKING) {
      tft->setTextColor(C_YELLOW, 0x0862);
      tft->setCursor(14, 203);
      tft->print("Nội dung bạn vừa nói:");

      String userLines[3];
      int nUser = wrapTextWordBoundary("\"" + s40AiUserSpeechText + "\"", 34, userLines, 3);
      tft->setTextColor(C_WHITE, 0x0862);
      for (int i = 0; i < nUser && i < 3; i++) {
        tft->setCursor(14, 219 + i * 14);
        tft->print(userLines[i]);
      }

      tft->fillRoundRect(12, 266, W - 24, 22, 5, 0x2008);
      tft->drawRoundRect(12, 266, W - 24, 22, 5, C_NEON_PINK);
      tft->setTextColor(C_NEON_PINK, 0x2008);
      tft->setCursor(18, 273);
      tft->print(s40AiSttPhaseMsg.length() > 0 ? s40AiSttPhaseMsg : ">> Trợ lý XiaoZhi đang suy nghĩ...");
    } else if (s40AiState == AI_STATE_REPLYING) {
      tft->setTextColor(C_NEON_CYAN, 0x0862);
      tft->setCursor(12, 202);
      tft->printf("XiaoZhi [%s]:", s40AiEmotionTag.c_str());

      int visibleRows = 5;
      int maxStart = (s40AiTotalLines > visibleRows) ? (s40AiTotalLines - visibleRows) : 0;
      if (s40AiScrollLine > maxStart) s40AiScrollLine = maxStart;
      if (s40AiScrollLine < 0) s40AiScrollLine = 0;

      tft->setTextColor(C_SLATE, 0x0862);
      tft->setCursor(W - 64, 202);
      tft->printf("D%d/%d", s40AiScrollLine + 1, s40AiTotalLines);

      tft->setTextColor(C_WHITE, 0x0862);
      for (int r = 0; r < visibleRows; r++) {
        int lineIdx = s40AiScrollLine + r;
        if (lineIdx < s40AiTotalLines) {
          tft->setCursor(12, 216 + r * 14);
          tft->print(s40AiWrappedLines[lineIdx]);
        }
      }

      int sbX = W - 13;
      int sbY = 215;
      int sbH = 72;
      tft->drawFastVLine(sbX, sbY, sbH, 0x2124);
      if (s40AiTotalLines > visibleRows) {
        int thumbH = max(12, (sbH * visibleRows) / s40AiTotalLines);
        int thumbY = sbY + ((sbH - thumbH) * s40AiScrollLine) / maxStart;
        tft->fillRoundRect(sbX - 1, thumbY, 3, thumbH, 1, C_NEON_CYAN);
      } else {
        tft->fillRoundRect(sbX - 1, sbY, 3, sbH, 1, 0x3186);
      }
    }
  }

  static void drawXiaoZhiAssistantStatusStrip() {
    if (!tft) return;
    int W = tft->width();
    int peak = TestAudio::getLastMicLevelPct();

    tft->fillRoundRect(6, 166, W - 12, 27, 6, C_CARD_BG);
    tft->drawRoundRect(6, 166, W - 12, 27, 6, 0x2124);

    tft->setTextSize(1);
    tft->setCursor(12, 171);
    if (s40AiState == AI_STATE_IDLE) {
      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->print("MICRO: SẴN SÀNG");
    } else if (s40AiState == AI_STATE_LISTENING) {
      tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
      tft->print(s40AiSpeechDetected ? "MICRO: ĐÃ BẮT GIỌNG!" : "MICRO: ĐANG LẮNG NGHE...");
    } else if (s40AiState == AI_STATE_THINKING) {
      tft->setTextColor(C_NEON_PINK, C_CARD_BG);
      tft->print("XIAOZHI ĐANG SUY NGHĨ");
    } else {
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->printf("CUỘN TỰ ĐỘNG (%s)", s40AiAutoScroll ? "BẬT" : "DỪNG");
    }

    int barX = 128, barY = 171, barW = 96, barH = 7;
    tft->fillRect(barX, barY, barW, barH, 0x0841);
    int fillW = constrain((peak * barW) / 100, 2, barW);
    uint16_t vuCol = (peak > 60) ? C_NEON_PINK : ((peak > 14) ? C_NEON_GREEN : C_NEON_CYAN);
    tft->fillRect(barX, barY, fillW, barH, vuCol);

    tft->setTextColor(C_SLATE, C_CARD_BG);
    tft->setCursor(12, 182);
    if (WiFi.status() == WL_CONNECTED) {
      tft->print("STT: Google vi-VN + XiaoZhi AI");
    } else {
      tft->print("CANH BAO: Can WiFi de nhan dien STT");
    }
  }

  static void drawXiaoZhiBindingScreen() {
    if (!tft) return;
    int W = tft->width();

    tft->fillRect(0, 0, W, 296, C_BLACK);
    drawSymbianHeader("XAC THUC XIAOZHI HUB");

    // 1. Vẽ đôi mắt XiaoZhi biểu cảm chờ đợi
    drawXiaoZhiEyesBox(6, 24, W - 12, 105, 1, 2);

    // 2. Thẻ hiển thị mã OTP trung tâm
    int cardX = 6, cardY = 135, cardW = W - 12, cardH = 132;
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 6, C_CARD_BG);
    tft->drawRoundRect(cardX, cardY, cardW, cardH, 6, C_NEON_CYAN);

    // Tiêu đề thẻ
    tft->setTextSize(1);
    tft->setTextColor(C_SLATE, C_CARD_BG);
    tft->setCursor(cardX + 10, cardY + 8);
    tft->print("MA XAC THUC AGENT (OTP):");

    // Hộp số OTP to nổi bật
    int boxX = cardX + 10, boxY = cardY + 22, boxW = cardW - 20, boxH = 34;
    tft->fillRoundRect(boxX, boxY, boxW, boxH, 4, 0x0821);
    uint16_t borderCol = (s40AiOtpCode != "NO WIFI" && s40AiOtpCode != "..." && s40AiOtpCode != "CHO OTP") ? C_YELLOW : 0x4208;
    tft->drawRoundRect(boxX, boxY, boxW, boxH, 4, borderCol);

    tft->setTextSize(3);
    tft->setTextColor(C_YELLOW, 0x0821);
    int textW = s40AiOtpCode.length() * 18;
    int otpX = boxX + (boxW - textW) / 2;
    tft->setCursor(max(boxX + 6, otpX), boxY + 6);
    tft->print(s40AiOtpCode);

    // Chi tiết thiết bị
    tft->setTextSize(1);
    tft->setTextColor(C_WHITE, C_CARD_BG);
    tft->setCursor(cardX + 10, cardY + 62);
    tft->printf("MAC: %s", s40AiMacStr.c_str());

    tft->setCursor(cardX + 10, cardY + 74);
    tft->print("Trang Hub: xiaozhi.me / tenclass.net");

    tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
    tft->setCursor(cardX + 10, cardY + 86);
    tft->print("Huong dan: Mo Hub -> Them thiet bi");

    tft->setCursor(cardX + 10, cardY + 98);
    tft->print("           Nhap MAC & Ma OTP de ket noi");

    tft->setTextColor(0x8410, C_CARD_BG);
    tft->setCursor(cardX + 10, cardY + 114);
    tft->printf("Trang thai: %s", s40AiBindStatus.c_str());

    // Thanh softkeys đáy màn hình
    drawSymbianSoftkeys("Kiem tra(OK)", "xiaozhi.me", "Thoat(Exit)");
  }

  static void enterXiaoZhiAssistantMode(bool forceBinding = false) {
    currentMode = 11;
    s40AiMacStr = WiFi.macAddress();
    if (forceBinding) {
      XiaoZhiClient::unbindDevice();
    }
    if (!forceBinding && XiaoZhiClient::isDeviceBound()) {
      s40AiState = AI_STATE_IDLE;
    } else {
      s40AiState = AI_STATE_BINDING;
      if (WiFi.status() != WL_CONNECTED) {
        s40AiBindStatus = "Chua co Wi-Fi! Hay ket noi Wi-Fi truoc.";
        s40AiOtpCode = "NO WIFI";
      } else {
        s40AiBindStatus = "Dang lay ma OTP tu XiaoZhi Cloud...";
        s40AiOtpCode = "...";
        drawXiaoZhiBindingScreen();
        String otaPayload = XiaoZhiClient::queryOTA(true);
        if (!forceBinding && XiaoZhiClient::isDeviceBound()) {
          s40AiState = AI_STATE_IDLE;
          s40AiReplyText = "Xin chao! Thiet bi da ket noi thanh cong voi XiaoZhi Hub! Nhan [OK] de hoi thoai.";
        } else {
          String authCode = XiaoZhiClient::getLastAuthCode();
          if (authCode.length() > 0) {
            s40AiOtpCode = authCode;
            s40AiBindStatus = "Da co ma OTP! Vui long nhap tren Hub.";
          } else {
            s40AiOtpCode = "CHO OTP";
            s40AiBindStatus = "Dang cho phan hoi tu may chu XiaoZhi...";
          }
        }
      }
    }
  }

  static void drawXiaoZhiAssistantScreen(bool fullRedraw) {
    if (!tft) return;
    if (s40AiState == AI_STATE_BINDING || s40AiState == AI_STATE_BINDING_CHECKING) {
      drawXiaoZhiBindingScreen();
      return;
    }
    int W = tft->width();

    if (fullRedraw) {
      tft->fillRect(0, 0, W, 296, C_BLACK);
      drawSymbianHeader("TRO LY XIAOZHI AI");
      drawXiaoZhiEyesBox(6, 24, W - 12, 140, eyeState, 2);
      drawXiaoZhiAssistantStatusStrip();
      drawXiaoZhiConversationBoxOnly();

      if (s40AiState == AI_STATE_IDLE) {
        drawSymbianSoftkeys("Thu am(OK)", "Lien ket(Menu)", "Thoat");
      } else if (s40AiState == AI_STATE_LISTENING) {
        drawSymbianSoftkeys("Gui(OK)", "Dang thu...", "Huy(Exit)");
      } else if (s40AiState == AI_STATE_THINKING) {
        drawSymbianSoftkeys("Dang xu ly", "...", "Thoat");
      } else {
        drawSymbianSoftkeys("Noi tiep(OK)", "Phat lai(Trai)", "Thoat");
      }
    } else {
      drawXiaoZhiAssistantStatusStrip();
    }
  }

  // Bắt đầu thu âm thực tế từ Mic INMP441 (8,000 Hz 16-bit Mono PCM)
  static void startXiaoZhiListening() {
    TestAudio::startVoiceRecording();
    s40AiState = AI_STATE_LISTENING;
    s40AiStateChangeMs = millis();
    s40AiSpeechDetected = false;
    s40AiLastVoiceMs = millis();
    eyeState = 2; // Mắt vui vẻ chăm chú lắng nghe
    lastExternalEmojiSync = millis();
    Serial.println("🎤 [XIAOZHI AI] Bắt đầu thu âm giọng nói thực tế từ Mic INMP441 (Không dùng câu mẫu)...");
    drawXiaoZhiAssistantScreen(true);
  }

  // Dừng thu âm -> Gửi PCM 16-bit lên Google Speech-to-Text (vi-VN) -> Hiện câu nói nhận diện được -> Gửi XiaoZhi AI
  static void triggerXiaoZhiThinkingAndReply() {
    TestAudio::stopVoiceRecording();

    size_t sampleCount = TestAudio::getRecordedSampleCount();
    float recSec       = TestAudio::getRecordedDurationSec();
    int maxVu          = TestAudio::getRecordedMaxVuPct();
    const int16_t* pcm = TestAudio::getRecordedPcmBuffer();

    // Luôn tự động lưu bản thu âm gần nhất vào thẻ nhớ SD (/recording/rec_latest.wav) nếu có dữ liệu
    if (sampleCount > 0) {
      TestAudio::saveRecordedVoiceToSd("/recording/rec_latest.wav");
      Serial.println("💡 [MẸO] Bấm phím TRÁI trên kit để mở TRÌNH PHÁT NHẠC (Mode 12) nghe lại bản thu qua Loa PWM GPIO 15!");
    }

    // 1. Kiểm tra nếu cường độ âm thanh quá nhỏ (chưa nói gì vào Mic)
    if (maxVu < 4 || sampleCount < 2000) {
      s40AiEmotionTag = "CONFUSED";
      eyeState = 8;
      s40AiReplyText = "Chua phat hien tieng noi tren Mic INMP441 (Da thu " + String(recSec, 1) +
                       "s, Max VU: " + String(maxVu) + "%).\nBan hay bam [OK] noi to cach Mic 10-20cm, hoac bam [TRAI] de nghe loa!";
      s40AiTotalLines = wrapTextWordBoundary(s40AiReplyText, 34, s40AiWrappedLines, 28);
      s40AiScrollLine = 0;
      s40AiAutoScroll = true;
      s40AiLastScrollMs = millis();
      s40AiState = AI_STATE_REPLYING;
      drawXiaoZhiAssistantScreen(true);
      return;
    }

    // 2. Hiển thị trạng thái ĐANG NHẬN DIỆN GIỌNG NÓI (STT) lên khung 1/3 màn hình
    s40AiState = AI_STATE_THINKING;
    s40AiStateChangeMs = millis();
    eyeState = 8;
    lastExternalEmojiSync = millis();
    s40AiUserSpeechText = "Dang phan tich " + String(recSec, 1) + "s am thanh (" +
                          String((unsigned)((sampleCount * 2) / 1024)) + "KB PCM, MaxVU " + String(maxVu) + "%)...";
    s40AiSttPhaseMsg = ">> Dang nhan dien giong noi (STT)...";

    drawXiaoZhiEyesBox(6, 24, tft->width() - 12, 140, eyeState, 2);
    drawXiaoZhiAssistantStatusStrip();
    drawXiaoZhiConversationBoxOnly();
    drawSymbianSoftkeys("Nhan dien", "Google STT", "Thoat");

    // 3. Gửi dữ liệu PCM 16-bit 16,000Hz thực tế từ INMP441 lên Google Speech API (vi-VN)
    String rawUtf8 = "";
    String recognizedAscii = XiaoZhiClient::transcribeMicAudioPcm16(pcm, sampleCount, TestAudio::getRecordSampleRate(), rawUtf8);

    // Nếu Google STT không nghe rõ từ nào -> Báo rõ kết quả thu âm (Tuyệt đối KHÔNG dùng câu mẫu giả!)
    if (recognizedAscii.length() == 0) {
      s40AiEmotionTag = "CONFUSED";
      eyeState = 8;
      s40AiReplyText = "[KET QUA THU AM MIC INMP441]\nDa thu: " + String(recSec, 1) +
                       " giay (" + String((unsigned)((sampleCount * 2) / 1024)) + " KB PCM) | Max VU: " + String(maxVu) +
                       "%.\nGoogle STT chua nghe ro. Bam [TRAI] de nghe lai qua Trinh phat nhac, hoac bam [OK] thu lai!";
      s40AiTotalLines = wrapTextWordBoundary(s40AiReplyText, 34, s40AiWrappedLines, 28);
      s40AiScrollLine = 0;
      s40AiAutoScroll = true;
      s40AiLastScrollMs = millis();
      s40AiState = AI_STATE_REPLYING;
      drawXiaoZhiAssistantScreen(true);
      return;
    }

    // 4. ĐÃ NHẬN DIỆN ĐƯỢC GIỌNG NÓI THẬT -> HIỂN THỊ NGAY LÊN 1/3 MÀN HÌNH TRONG LÚC XIAOZHI ĐANG NGHĨ!
    s40AiUserSpeechText = recognizedAscii;
    s40AiSttPhaseMsg = ">> XiaoZhi AI dang suy nghi...";
    drawXiaoZhiConversationBoxOnly();
    drawSymbianSoftkeys("Dang nghi", "Cho AI...", "Thoat");

    Serial.printf("💭 [XIAOZHI AI] Câu nói thực tế từ Mic: \"%s\" -> Đang gửi lên XiaoZhi AI...\n", recognizedAscii.c_str());

    String emotionTag = "HAPPY";
    float t = TestSensors::isSht31Connected() ? TestSensors::getTemperatureC() : stCfg.temp;
    float h = TestSensors::isSht31Connected() ? TestSensors::getHumidityPct() : (float)stCfg.humidity;
    String queryForLlm = (rawUtf8.length() > 0) ? rawUtf8 : recognizedAscii;
    String aiAnswer = XiaoZhiClient::askXiaoZhiAI(queryForLlm, emotionTag, t, h);

    s40AiEmotionTag = emotionTag;
    s40AiReplyText = "Ban: \"" + recognizedAscii + "\"\nXiaoZhi: " + aiAnswer;

    if (emotionTag == "HAPPY") eyeState = 2;
    else if (emotionTag == "WINK") eyeState = 9;
    else if (emotionTag == "CONFUSED") eyeState = 8;
    else if (emotionTag == "COOL") eyeState = 0;
    else if (emotionTag == "SLEEPY") eyeState = 11;
    else eyeState = 2;
    lastExternalEmojiSync = millis();

    s40AiTotalLines = wrapTextWordBoundary(s40AiReplyText, 34, s40AiWrappedLines, 28);
    s40AiScrollLine = 0;
    s40AiAutoScroll = true;
    s40AiLastScrollMs = millis();
    s40AiState = AI_STATE_REPLYING;

    drawXiaoZhiAssistantScreen(true);
  }

  // ============================================================================
  // CHẾ ĐỘ 9: MÀN HÌNH TEST MODULE THẺ NHỚ SD (GIỐNG MÀN DIO OSCILLOSCOPE)
  // ============================================================================
  static void drawSdCardTestScreen(bool fullRedraw) {
    if (!tft) return;
    int W = tft->width();

    if (fullRedraw) {
      tft->fillRect(0, 0, W, 296, C_BLACK);
      drawSymbianHeader("TRẠNG THÁI THẺ NHỚ SD");
    }

    // Khung 1: Sơ đồ đấu nối chân Thẻ nhớ SD & Mức điện áp thực tế (y = 26 .. 140)
    tft->fillRoundRect(6, 26, W - 12, 114, 6, C_CARD_BG);
    tft->drawRoundRect(6, 26, W - 12, 114, 6, TestSDCard::isMounted() ? C_NEON_GREEN : C_NEON_PINK);

    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, C_CARD_BG);
    tft->setCursor(12, 33);
    tft->print("ĐIỆN ÁP & KẾT NỐI KHAY THẺ NHỚ SD:");

    const TestSDCard::SdPinDiag* diags = TestSDCard::getPinDiags();
    for (int i = 0; i < 4; i++) {
      int ry = 48 + i * 15;
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(12, ry);
      tft->printf("%s(%s->%d):", diags[i].shieldPin, diags[i].spiRole, diags[i].gpio);
      uint16_t vCol = (diags[i].estVoltage >= 3.0f) ? C_NEON_GREEN : C_NEON_PINK;
      tft->setTextColor(vCol, C_CARD_BG);
      tft->setCursor(102, ry);
      tft->printf("%.2fV %s", diags[i].estVoltage, diags[i].stateDesc);
    }

    tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
    tft->setCursor(12, 112);
    tft->printf("Mã phản hồi: 0x%02X | Lần quét #%u",
                TestSDCard::getCmd0R1Byte(),
                (unsigned)TestSDCard::getScanCount());
    tft->setCursor(12, 125);
    tft->print(TestSDCard::getCmd0RawHexStr().substring(0, 35));

    // Khung 2: Trạng thái Nhận Thẻ nhớ & Dung lượng (y = 145 .. 290)
    tft->fillRoundRect(6, 145, W - 12, 145, 6, C_CARD_BG);
    tft->drawRoundRect(6, 145, W - 12, 145, 6, 0x2965);

    tft->setCursor(12, 153);
    if (TestSDCard::isMounted()) {
      tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
      tft->printf("TRẠNG THÁI : ĐÃ NHẬN THẺ (%s)", TestSDCard::getCardTypeName().c_str());

      uint32_t totalMB = TestSDCard::getTotalMB();
      uint32_t usedMB  = TestSDCard::getUsedMB();
      uint32_t freeMB  = TestSDCard::getFreeMB();
      int usedPct      = (totalMB > 0) ? (int)((usedMB * 100) / totalMB) : 0;

      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(12, 171);
      tft->printf("Tổng dung lượng : %u MB", (unsigned)totalMB);
      tft->setCursor(12, 186);
      tft->printf("Đã sử dụng      : %u MB (%d%%)", (unsigned)usedMB, usedPct);
      tft->setCursor(12, 201);
      tft->printf("Còn trống       : %u MB", (unsigned)freeMB);
      tft->setCursor(12, 216);
      tft->printf("Số lượng tệp    : %d tệp tin", TestSDCard::getFileCount());

      tft->fillRoundRect(12, 232, W - 24, 12, 4, 0x0841);
      int fillW = constrain((usedPct * (W - 24)) / 100, 2, W - 24);
      tft->fillRoundRect(12, 232, fillW, 12, 4, C_NEON_GREEN);
    } else {
      tft->setTextColor(C_NEON_PINK, C_CARD_BG);
      tft->print("TRẠNG THÁI : CHƯA NHẬN THẺ SD!");
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(12, 171);
      tft->print("Hướng dẫn kiểm tra nhanh:");
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(12, 187);
      tft->print("1. Kiểm tra thẻ nhớ đã cắm chặt chưa");
      tft->setCursor(12, 201);
      tft->print("2. Định dạng thẻ chuẩn FAT32 (<=32GB)");
      tft->setCursor(12, 215);
      tft->print("3. Kiểm tra nguồn cấp 3.3V cho khay");
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(12, 232);
      tft->print(TestSDCard::getStatusSummary().substring(0, 35));
    }

    tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
    tft->setCursor(12, 254);
    tft->print("[OK]: Quét lại & Làm mới Thẻ nhớ SD");
    tft->setCursor(12, 268);
    tft->print("[PHẢI]: Mở Trình Quản lý Tệp tin");

    drawSymbianSoftkeys("Quét(OK)", "Bộ nhớ", "Thoát");
  }

  // ============================================================================
  // CHẾ ĐỘ 10: QUẢN LÝ BỘ NHỚ (BỘ NHỚ SD & BỘ NHỚ TRONG + DUYỆT THƯ MỤC ĐA CẤP + THAO TÁC TỆP)
  // ============================================================================
  static String s40MemCurrentDir = "/";

  static int countFilesInSdDir(const String& dirPath) {
    if (!TestSDCard::isMounted()) return 0;
    File d = SD.open(dirPath);
    if (!d || !d.isDirectory()) {
      if (d) d.close();
      return 0;
    }
    int c = 0;
    File f = d.openNextFile();
    while (f && c < 99) {
      c++;
      f.close();
      f = d.openNextFile();
    }
    d.close();
    return c;
  }

  static int listMemoryEntriesInDir(const String& dirPath, String outNames[], size_t outSizes[], bool outIsDir[], int maxItems) {
    int count = 0;
    if (dirPath != "/" && dirPath.length() > 0) {
      if (count < maxItems) {
        outNames[count] = "..";
        outSizes[count] = 0;
        outIsDir[count] = true;
        count++;
      }
      if (TestSDCard::isMounted()) {
        File subDir = SD.open(dirPath);
        if (subDir && subDir.isDirectory()) {
          File f = subDir.openNextFile();
          while (f && count < maxItems) {
            if (f.isDirectory()) {
              String fn = String(f.name());
              int sl = fn.lastIndexOf('/');
              if (sl >= 0) fn = fn.substring(sl + 1);
              String fullSub = (dirPath == "/") ? ("/" + fn) : (dirPath + "/" + fn);
              outNames[count] = fullSub;
              outSizes[count] = countFilesInSdDir(fullSub);
              outIsDir[count] = true;
              count++;
            }
            f.close();
            f = subDir.openNextFile();
          }
          subDir.close();

          subDir = SD.open(dirPath);
          if (subDir && subDir.isDirectory()) {
            File f2 = subDir.openNextFile();
            while (f2 && count < maxItems) {
              if (!f2.isDirectory()) {
                String fn = String(f2.name());
                int sl = fn.lastIndexOf('/');
                if (sl >= 0) fn = fn.substring(sl + 1);
                String fullFile = (dirPath == "/") ? ("/" + fn) : (dirPath + "/" + fn);
                outNames[count] = fullFile;
                outSizes[count] = f2.size();
                outIsDir[count] = false;
                count++;
              }
              f2.close();
              f2 = subDir.openNextFile();
            }
            subDir.close();
          }
        } else if (subDir) {
          subDir.close();
        }
      }
      return count;
    }

    if (TestSDCard::isMounted()) {
      if (!SD.exists("/Musics")) {
        SD.mkdir("/Musics");
      }
      File root = SD.open("/");
      if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f && count < maxItems) {
          if (f.isDirectory()) {
            String fn = String(f.name());
            if (!fn.startsWith("/")) fn = "/" + fn;
            if (fn != "/System Volume Information" && fn != "/sd_rgb565") {
              outNames[count] = fn;
              outSizes[count] = countFilesInSdDir(fn);
              outIsDir[count] = true;
              count++;
            }
          }
          f.close();
          f = root.openNextFile();
        }
        root.close();
      }

      root = SD.open("/");
      if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f && count < maxItems) {
          if (!f.isDirectory()) {
            String fn = String(f.name());
            if (!fn.startsWith("/")) fn = "/" + fn;
            outNames[count] = fn;
            outSizes[count] = f.size();
            outIsDir[count] = false;
            count++;
          }
          f.close();
          f = root.openNextFile();
        }
        root.close();
      }
    }

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

  // Biến ghi nhớ trạng thái hiển thị của Trình Quản Lý Tệp (Loại bỏ 100% hiện tượng nháy đen khi cuộn/chọn mục)
  static int    lastDrawnMemSubState    = -1;
  static int    lastDrawnMemFileCursor  = -1;
  static int    lastDrawnMemScroll      = -1;
  static int    lastDrawnMemPopupCursor = -1;
  static String lastDrawnMemDir         = "";

  // Vẽ riêng 1 dòng tệp tin trong danh sách (Chỉ tốn ~0.8ms, hoàn toàn không vẽ lại tiêu đề hay nền màn hình)
  static void drawSingleMemFileRow(int idx, int visRow, bool sel, const String& name, size_t sizeBytes, bool isDir) {
    if (!tft || visRow < 0 || visRow >= 7) return;
    int W = tft->width();
    int ry = 52 + visRow * 31;
    uint16_t cardBg = sel ? 0x1148 : 0x0841;

    tft->fillRect(12, ry, W - 24, 27, cardBg);
    if (sel) tft->drawRoundRect(12, ry, W - 24, 27, 4, C_NEON_CYAN);
    else     tft->drawRoundRect(12, ry, W - 24, 27, 4, 0x2124);

    tft->setTextSize(1);
    tft->setTextColor(sel ? C_YELLOW : C_WHITE, cardBg);
    tft->setCursor(18, ry + 5);
    if (name == "..") {
      tft->print("[..] Quay lại thư mục trước");
      tft->setTextColor(C_NEON_CYAN, cardBg);
      tft->setCursor(18, ry + 16);
      tft->print("Bấm [OK] hoặc [EXIT] để lùi ra");
    } else if (isDir) {
      String shortDir = "[THƯ MỤC] " + name;
      if (vnStrLen(shortDir) > 28) shortDir = vnSubstr(shortDir, 0, 28);
      tft->print(shortDir);
      tft->setTextColor(C_NEON_GREEN, cardBg);
      tft->setCursor(18, ry + 16);
      tft->printf("#%d - Chứa %u tệp (Bấm OK để mở)", idx + 1, (unsigned)sizeBytes);
    } else {
      String dispFile = name;
      if (s40MemCurrentDir != "/" && dispFile.startsWith(s40MemCurrentDir + "/")) {
        dispFile = dispFile.substring(s40MemCurrentDir.length() + 1);
      }
      if (vnStrLen(dispFile) > 28) dispFile = vnSubstr(dispFile, 0, 28);
      tft->print(dispFile);

      tft->setTextColor(C_NEON_GREEN, cardBg);
      tft->setCursor(18, ry + 16);
      if (sizeBytes >= 1048576) {
        tft->printf("#%d - Dung lượng: %.2f MB", idx + 1, sizeBytes / 1048576.0f);
      } else {
        tft->printf("#%d - Dung lượng: %.1f KB", idx + 1, sizeBytes / 1024.0f);
      }
    }
  }

  // Kết xuất ảnh tràn viền 240x320 toàn màn hình (Full-Screen 0ms với .rgb565 hoặc giải mã JPEG)
  static bool renderFullScreenImageFromFile(const String& rawPath) {
    if (!tft) return false;
    int W = tft->width();
    int H = tft->height();

    bool isFs = rawPath.startsWith("[FS]");
    String cleanPath = isFs ? rawPath.substring(4) : rawPath;
    if (cleanPath.startsWith("sd:")) cleanPath = cleanPath.substring(3);
    if (!cleanPath.startsWith("/")) cleanPath = "/" + cleanPath;

    String rgb565Path = ImageManager::getRgb565CompanionPath(cleanPath);

    // 1. Ưu tiên tập tin .rgb565 tăng tốc phần cứng (240x320x2 = 153.6KB)
    if (!isFs && TestSDCard::isMounted()) {
      String tryRgb = (rgb565Path.length() > 0) ? rgb565Path : (cleanPath.endsWith(".rgb565") ? cleanPath : "");
      if (tryRgb.length() > 0 && SD.exists(tryRgb)) {
        File rf = SD.open(tryRgb, FILE_READ);
        if (rf && rf.size() >= 240 * 320 * 2) {
          uint16_t rowBuf[240];
          for (int y = 0; y < 320; y++) {
            if (rf.read((uint8_t*)rowBuf, 240 * sizeof(uint16_t)) == 240 * sizeof(uint16_t)) {
              tft->drawRGBBitmap(0, y, rowBuf, 240, 1);
            }
          }
          rf.close();
          return true;
        }
        if (rf) rf.close();
      }
    }

    // 2. Giải mã trực tiếp hình ảnh JPEG / JPG
    String low = cleanPath;
    low.toLowerCase();
    if (low.endsWith(".jpg") || low.endsWith(".jpeg")) {
      TJpgDec.setJpgScale(1);
      TJpgDec.setSwapBytes(false);
      TJpgDec.setCallback(jpgGalleryCallback);

      if (!isFs && TestSDCard::isMounted() && SD.exists(cleanPath)) {
        File f = SD.open(cleanPath, FILE_READ);
        if (f) {
          size_t sz = f.size();
          if (sz > 0 && sz <= 180000) {
            uint8_t* buf = (uint8_t*)malloc(sz);
            if (buf) {
              if (f.read(buf, sz) == sz) {
                uint16_t jw = 0, jh = 0;
                if (TJpgDec.getJpgSize(&jw, &jh, buf, sz) == JDR_OK && jw > 0 && jh > 0) {
                  uint8_t scale = 1;
                  if (jw >= 480 || jh >= 640) scale = 2;
                  TJpgDec.setJpgScale(scale);
                  int ox = max(0, (W - (int)(jw / scale)) / 2);
                  int oy = max(0, (H - (int)(jh / scale)) / 2);
                  tft->fillScreen(C_BLACK);
                  TJpgDec.drawJpg(ox, oy, buf, sz);
                }
              }
              free(buf);
            }
          }
          f.close();
          return true;
        }
      } else if (isFs && LittleFS.exists(cleanPath)) {
        uint16_t jw = 0, jh = 0;
        if (TJpgDec.getFsJpgSize(&jw, &jh, cleanPath.c_str(), LittleFS) == JDR_OK && jw > 0 && jh > 0) {
          uint8_t scale = 1;
          if (jw >= 480 || jh >= 640) scale = 2;
          TJpgDec.setJpgScale(scale);
          int ox = max(0, (W - (int)(jw / scale)) / 2);
          int oy = max(0, (H - (int)(jh / scale)) / 2);
          tft->fillScreen(C_BLACK);
          decodeFsJpgFromRam(ox, oy, cleanPath.c_str());
          return true;
        }
      }
    }

    tft->fillScreen(0x0841);
    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, 0x0841);
    tft->setCursor(20, 140);
    tft->print("KHÔNG THỂ MỞ TOÀN MÀN HÌNH!");
    tft->setTextColor(C_WHITE, 0x0841);
    tft->setCursor(20, 160);
    tft->printf("File: %s", cleanPath.c_str());
    return false;
  }

  // Vẽ riêng 1 nút trong menu popup thao tác tệp (Chỉ tốn ~0.8ms, chống nháy hoàn toàn khi di chuyển Lên/Xuống)
  static void drawSingleMemPopupButton(int idx, bool sel, const char* text) {
    if (!tft || idx < 0 || idx >= 5) return;
    int W = tft->width();
    int ry = 66 + idx * 38;
    uint16_t bgCol = sel ? 0x218A : 0x0841;
    tft->fillRoundRect(16, ry, W - 32, 32, 6, bgCol);
    tft->drawRoundRect(16, ry, W - 32, 32, 6, sel ? C_NEON_CYAN : 0x2124);
    tft->setTextSize(1);
    tft->setTextColor(sel ? C_YELLOW : C_WHITE, bgCol);
    tft->setCursor(24, ry + 12);
    tft->print(text);
  }

  static void drawMemoryManagerAppScreen(bool forceFullRedraw = false) {
    if (!tft) return;
    int W = tft->width();
    int H = tft->height();

    // NẾU ĐANG Ở CHẾ ĐỘ 5: XEM ẢNH TRÀN VIỀN TOÀN MÀN HÌNH (FULL-SCREEN SLIDESHOW)
    if (s40MemSubState == 5) {
      String names[28];
      size_t sizesBytes[28];
      bool isDirs[28];
      int totalFiles = listMemoryEntriesInDir(s40MemCurrentDir, names, sizesBytes, isDirs, 28);
      if (totalFiles <= 0) {
        s40MemSubState = 1;
        drawMemoryManagerAppScreen(true);
        return;
      }
      s40MemFileCursor = ((s40MemFileCursor % totalFiles) + totalFiles) % totalFiles;
      String rawName = names[s40MemFileCursor];
      renderFullScreenImageFromFile(rawName);

      // Thanh Overlay mờ nổi bật ở góc dưới
      tft->fillRoundRect(8, H - 32, W - 16, 26, 6, 0x0841);
      tft->drawRoundRect(8, H - 32, W - 16, 26, 6, C_NEON_CYAN);
      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, 0x0841);
      String dispTitle = rawName.startsWith("[FS]") ? rawName.substring(4) : rawName;
      int sl = dispTitle.lastIndexOf('/');
      if (sl >= 0) dispTitle = dispTitle.substring(sl + 1);
      if (vnStrLen(dispTitle) > 18) dispTitle = vnSubstr(dispTitle, 0, 18);
      tft->setCursor(14, H - 24);
      tft->print(dispTitle);

      tft->setTextColor(C_NEON_GREEN, 0x0841);
      const char* fsAction = "[OK:ĐặtNền] [Trái/Phải:Đổi] [EXIT]";
      tft->setCursor(W - vnStrLen(fsAction) * 6 - 12, H - 24);
      tft->print(fsAction);
      lastDrawnMemSubState = 5;
      return;
    }

    // NẾU ĐANG Ở CHẾ ĐỘ 1: DUYỆT THƯ MỤC & TỆP TIN TRÊN THẺ NHỚ SD / BỘ NHỚ TRONG
    if (s40MemSubState == 1) {
      String names[28];
      size_t sizesBytes[28];
      bool isDirs[28];
      int totalFiles = listMemoryEntriesInDir(s40MemCurrentDir, names, sizesBytes, isDirs, 28);
      if (totalFiles > 0) {
        s40MemFileCursor = ((s40MemFileCursor % totalFiles) + totalFiles) % totalFiles;
      } else {
        s40MemFileCursor = 0;
      }
      bool curSelectedIsDir = (totalFiles > 0) ? isDirs[s40MemFileCursor] : false;
      int startIdx = 0;
      if (s40MemFileCursor >= 7) startIdx = s40MemFileCursor - 6;

      // 1. CẬP NHẬT CỰC NHANH KHÔNG QUÉT MÀN HÌNH KHI DUYỆT TỪNG MỤC (<1.5ms)
      if (!forceFullRedraw && lastDrawnMemSubState == 1 && s40MemCurrentDir == lastDrawnMemDir) {
        if (startIdx == lastDrawnMemScroll) {
          // Con trỏ di chuyển trong cùng 1 trang: Chỉ vẽ lại 2 dòng thay đổi!
          if (lastDrawnMemFileCursor != s40MemFileCursor) {
            int oldRow = lastDrawnMemFileCursor - startIdx;
            int newRow = s40MemFileCursor - startIdx;
            if (oldRow >= 0 && oldRow < 7 && lastDrawnMemFileCursor < totalFiles) {
              drawSingleMemFileRow(lastDrawnMemFileCursor, oldRow, false, names[lastDrawnMemFileCursor], sizesBytes[lastDrawnMemFileCursor], isDirs[lastDrawnMemFileCursor]);
            }
            if (newRow >= 0 && newRow < 7 && s40MemFileCursor < totalFiles) {
              drawSingleMemFileRow(s40MemFileCursor, newRow, true, names[s40MemFileCursor], sizesBytes[s40MemFileCursor], isDirs[s40MemFileCursor]);
            }
            bool prevIsDir = (lastDrawnMemFileCursor >= 0 && lastDrawnMemFileCursor < totalFiles) ? isDirs[lastDrawnMemFileCursor] : false;
            if (prevIsDir != curSelectedIsDir) {
              drawSymbianSoftkeys("[MENU:TùyChọn]", curSelectedIsDir ? "[OK:MởThưMục]" : "[OK:Mở/Xem]", "[EXIT:QuayLại]");
            }
            lastDrawnMemFileCursor = s40MemFileCursor;
          }
          return;
        } else {
          // Danh sách cuộn trang: Chỉ vẽ lại 7 dòng trong khung, không xoá hay quét lại màn hình
          for (int row = 0; row < 7; row++) {
            int idx = startIdx + row;
            if (idx < totalFiles) {
              drawSingleMemFileRow(idx, row, (idx == s40MemFileCursor), names[idx], sizesBytes[idx], isDirs[idx]);
            } else {
              int ry = 52 + row * 31;
              tft->fillRect(12, ry, W - 24, 27, C_CARD_BG);
            }
          }
          drawSymbianSoftkeys("[MENU:TùyChọn]", curSelectedIsDir ? "[OK:MởThưMục]" : "[OK:Mở/Xem]", "[EXIT:QuayLại]");
          lastDrawnMemScroll = startIdx;
          lastDrawnMemFileCursor = s40MemFileCursor;
          return;
        }
      }

      // 2. KHI ĐỔI THƯ MỤC HOẶC LẦN ĐẦU VÀO DANH SÁCH:
      bool sameState = (lastDrawnMemSubState == 1);
      if (!sameState) {
        // Chuyển từ substate khác sang: Vẽ tiêu đề Symbian và khung viền Card
        tft->fillRect(0, 0, W, 296, C_BLACK);
        drawSymbianHeader("QUẢN LÝ BỘ NHỚ & TỆP TIN");
        tft->fillRoundRect(6, 26, W - 12, 264, 6, C_CARD_BG);
        tft->drawRoundRect(6, 26, W - 12, 264, 6, C_NEON_CYAN);
      } else {
        // Đang ở sẵn trong danh sách tệp (VD: mở thư mục con hoặc bấm '..' lùi thư mục):
        // TUYỆT ĐỐI KHÔNG quét đen toàn màn hình! Chỉ làm sạch phần lòng bên trong Card!
        tft->fillRect(8, 28, W - 16, 260, C_CARD_BG);
      }

      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(12, 34);
      if (s40MemCurrentDir == "/") {
        tft->printf("THƯ MỤC GỐC [/] (%d mục):", totalFiles);
      } else {
        String hdrDir = s40MemCurrentDir;
        if (vnStrLen(hdrDir) > 18) hdrDir = vnSubstr(hdrDir, 0, 18);
        tft->printf("THƯ MỤC: %s (%d tệp)", hdrDir.c_str(), max(0, totalFiles - 1));
      }

      if (totalFiles <= 0) {
        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(18, 120);
        tft->print("(Thư mục trống - Chưa có tệp tin)");
      } else {
        for (int row = 0; row < 7; row++) {
          int idx = startIdx + row;
          if (idx < totalFiles) {
            drawSingleMemFileRow(idx, row, (idx == s40MemFileCursor), names[idx], sizesBytes[idx], isDirs[idx]);
          } else {
            int ry = 52 + row * 31;
            tft->fillRect(12, ry, W - 24, 27, C_CARD_BG);
          }
        }
      }
      drawSymbianSoftkeys("[MENU:TùyChọn]", curSelectedIsDir ? "[OK:MởThưMục]" : "[OK:Mở/Xem]", "[EXIT:QuayLại]");

      lastDrawnMemSubState = 1;
      lastDrawnMemScroll = startIdx;
      lastDrawnMemFileCursor = s40MemFileCursor;
      lastDrawnMemDir = s40MemCurrentDir;
      return;
    }

    // NẾU ĐANG Ở CHẾ ĐỘ 2: MENU POPUP TÙY CHỌN THAO TÁC THÔNG MINH
    if (s40MemSubState == 2) {
      String names[28];
      size_t sizesBytes[28];
      bool isDirs[28];
      int count = listMemoryEntriesInDir(s40MemCurrentDir, names, sizesBytes, isDirs, 28);
      int selIdx = (count > 0) ? (((s40MemFileCursor % count) + count) % count) : 0;
      String selName = (count > 0) ? names[selIdx] : "";
      bool isDir = (count > 0) ? isDirs[selIdx] : false;
      String low = selName;
      low.toLowerCase();
      bool isImg = !isDir && (low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp") || low.endsWith(".rgb565"));
      bool isAudio = !isDir && (low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac") || low.endsWith(".ogg"));

      const char* actions[5];
      if (isImg) {
        actions[0] = "1. Xem ảnh tràn viền (Toàn màn hình)";
        actions[1] = "2. Đặt làm Hình nền màn chờ ngay";
        actions[2] = "3. Xem thông tin chi tiết tệp ảnh";
        actions[3] = "4. Xóa tệp ảnh khỏi bộ nhớ";
        actions[4] = "5. Quay lại danh sách tệp tin";
      } else if (isAudio) {
        actions[0] = "1. Phát ngay trên Trình phát nhạc";
        actions[1] = "2. Xem thông tin chi tiết bài hát";
        actions[2] = "3. Xóa bài hát khỏi thẻ nhớ SD";
        actions[3] = "4. Quay lại danh sách tệp tin";
        actions[4] = "5. Xem tổng quan dung lượng bộ nhớ";
      } else if (isDir) {
        actions[0] = "1. Mở thư mục bên trong";
        actions[1] = "2. Xem thông tin chi tiết thư mục";
        actions[2] = "3. Quay lại thư mục trước (..)";
        actions[3] = "4. Quay về thư mục gốc [/]";
        actions[4] = "5. Xem tổng quan dung lượng bộ nhớ";
      } else {
        actions[0] = "1. Xem trước nội dung tệp tin";
        actions[1] = "2. Xem thông tin chi tiết tệp";
        actions[2] = "3. Xóa tệp đang chọn khỏi bộ nhớ";
        actions[3] = "4. Quay lại danh sách tệp tin";
        actions[4] = "5. Xem tổng quan dung lượng bộ nhớ";
      }

      if (!forceFullRedraw && lastDrawnMemSubState == 2) {
        if (lastDrawnMemPopupCursor != s40MemPopupCursor) {
          if (lastDrawnMemPopupCursor >= 0 && lastDrawnMemPopupCursor < 5) {
            drawSingleMemPopupButton(lastDrawnMemPopupCursor, false, actions[lastDrawnMemPopupCursor]);
          }
          drawSingleMemPopupButton(s40MemPopupCursor, true, actions[s40MemPopupCursor]);
          lastDrawnMemPopupCursor = s40MemPopupCursor;
        }
        return;
      }

      tft->fillRoundRect(8, 34, W - 16, 248, 8, 0x0862);
      tft->drawRoundRect(8, 34, W - 16, 248, 8, C_YELLOW);

      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, 0x0862);
      tft->setCursor(16, 44);
      if (count > 0) {
        String dispShort = selName;
        if (dispShort.startsWith("[FS]")) dispShort = dispShort.substring(4);
        int sl = dispShort.lastIndexOf('/');
        if (sl >= 0 && sl + 1 < (int)dispShort.length()) dispShort = dispShort.substring(sl + 1);
        if (vnStrLen(dispShort) > 26) dispShort = vnSubstr(dispShort, 0, 26);
        tft->printf("MỤC CHỌN: %s", dispShort.c_str());
      } else {
        tft->print("TÙY CHỌN QUẢN LÝ TỆP TIN:");
      }

      for (int i = 0; i < 5; i++) {
        drawSingleMemPopupButton(i, i == s40MemPopupCursor, actions[i]);
      }
      drawSymbianSoftkeys("[OK:Chọn]", "Lên/Xuống", "[EXIT:Đóng]");
      lastDrawnMemSubState = 2;
      lastDrawnMemPopupCursor = s40MemPopupCursor;
      return;
    }

    tft->fillRect(0, 0, W, 296, C_BLACK);
    drawSymbianHeader("QUẢN LÝ BỘ NHỚ & TỆP TIN");

    if (s40MemSubState == 0) {
      // TRANG TỔNG QUAN DUNG LƯỢNG HIỆN TẠI / CÒN LẠI
      tft->fillRoundRect(6, 28, W - 12, 122, 7, C_CARD_BG);
      tft->drawRoundRect(6, 28, W - 12, 122, 7, TestSDCard::isMounted() ? C_NEON_GREEN : C_NEON_PINK);

      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(14, 36);
      tft->printf("1. THẺ NHỚ MICRO-SD (%s)", TestSDCard::getCardTypeName().c_str());

      if (TestSDCard::isMounted()) {
        uint32_t usedMB = TestSDCard::getUsedMB();
        uint32_t freeMB = TestSDCard::getFreeMB();
        uint32_t totMB  = TestSDCard::getTotalMB();
        int usedPct     = (totMB > 0) ? (int)((usedMB * 100) / totMB) : 0;

        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(14, 54);
        tft->printf("Dung lượng đã dùng  : %u / %u MB", (unsigned)usedMB, (unsigned)totMB);
        tft->setCursor(14, 70);
        tft->printf("Dung lượng còn trống: %u MB", (unsigned)freeMB);

        tft->fillRoundRect(14, 88, W - 28, 12, 4, 0x0841);
        int fillW = constrain((usedPct * (W - 28)) / 100, 3, W - 28);
        tft->fillRoundRect(14, 88, fillW, 12, 4, C_NEON_GREEN);

        tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
        tft->setCursor(14, 108);
        tft->printf("Số tệp: %d | Bấm [OK] để Mở danh sách", TestSDCard::getFileCount());
        tft->setCursor(14, 124);
        tft->print("Bấm [MENU] để mở Tùy chọn Thẻ nhớ");
      } else {
        tft->setTextColor(C_NEON_PINK, C_CARD_BG);
        tft->setCursor(14, 56);
        tft->print("Trạng thái: CHƯA LẮP THẺ NHỚ SD");
        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(14, 74);
        tft->print("Bấm [OK] để Mở tệp Bộ nhớ trong");
        tft->setCursor(14, 92);
        tft->print("Hoặc bấm [MENU] -> Quét lại thẻ nhớ");
        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(14, 112);
        tft->print("Hỗ trợ thẻ nhớ MicroSD chuẩn FAT32");
      }

      // 2. Bộ nhớ trong hệ thống
      tft->fillRoundRect(6, 158, W - 12, 118, 7, C_CARD_BG);
      tft->drawRoundRect(6, 158, W - 12, 118, 7, C_NEON_CYAN);

      size_t lfsTotal = LittleFS.totalBytes();
      size_t lfsUsed  = LittleFS.usedBytes();
      size_t lfsFree  = (lfsTotal > lfsUsed) ? (lfsTotal - lfsUsed) : 0;
      int lfsPct      = (lfsTotal > 0) ? (int)((lfsUsed * 100) / lfsTotal) : 0;

      tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(14, 166);
      tft->print("2. BỘ NHỚ TRONG HỆ THỐNG:");

      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 184);
      tft->printf("Dung lượng đã dùng  : %u / %u KB", (unsigned)(lfsUsed / 1024), (unsigned)(lfsTotal / 1024));
      tft->setCursor(14, 200);
      tft->printf("Dung lượng còn trống: %u KB (%d%%)", (unsigned)(lfsFree / 1024), 100 - lfsPct);

      tft->fillRoundRect(14, 218, W - 28, 12, 4, 0x0841);
      int lfsW = constrain((lfsPct * (W - 28)) / 100, 3, W - 28);
      tft->fillRoundRect(14, 218, lfsW, 12, 4, C_NEON_CYAN);

      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(14, 240);
      tft->print("[OK]: Mở Danh sách Thư mục & Tệp tin");
      tft->setCursor(14, 255);
      tft->print("[MENU]: Tùy chọn Quản lý Bộ nhớ");

      drawSymbianSoftkeys("Mở tệp", "Tùy chọn", "Về Menu");
      lastDrawnMemSubState = 0;
    } else if (s40MemSubState == 3) {
      // TRẠNG THÁI 3: XEM TRƯỚC TRỰC TIẾP ẢNH / NGHE THỬ NHẠC / ĐỌC VĂN BẢN (KHI BẤM OK TRÊN TỆP)
      String names[28];
      size_t sizesBytes[28];
      bool isDirs[28];
      int totalFiles = listMemoryEntriesInDir(s40MemCurrentDir, names, sizesBytes, isDirs, 28);
      if (totalFiles <= 0) {
        s40MemSubState = 1;
        drawMemoryManagerAppScreen(true);
        return;
      }
      s40MemFileCursor = ((s40MemFileCursor % totalFiles) + totalFiles) % totalFiles;
      String rawName = names[s40MemFileCursor];
      size_t szBytes = sizesBytes[s40MemFileCursor];
      size_t szKb = (szBytes + 512) / 1024;
      String low = rawName;
      low.toLowerCase();
      bool isImg = low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp") || low.endsWith(".rgb565");
      bool isAudio = low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac") || low.endsWith(".ogg");

      String cleanFsPath = rawName.startsWith("[FS]") ? rawName.substring(4) : rawName;
      bool isFromFs = rawName.startsWith("[FS]");
      String rgb565Path = ImageManager::getRgb565CompanionPath(cleanFsPath);

      tft->fillRoundRect(6, 26, W - 12, 22, 4, C_CARD_BG);
      tft->drawRoundRect(6, 26, W - 12, 22, 4, isImg ? C_NEON_GREEN : C_NEON_CYAN);
      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(10, 33);
      String dispName = cleanFsPath;
      int sl = dispName.lastIndexOf('/');
      if (sl >= 0) dispName = dispName.substring(sl + 1);
      if (vnStrLen(dispName) > 18) dispName = vnSubstr(dispName, 0, 18);
      tft->printf("[%d/%d] %s (%uKB)", s40MemFileCursor + 1, totalFiles, dispName.c_str(), (unsigned)szKb);

      if (isImg) {
        tft->drawRect(18, 52, 204, 212, C_CARD_BORDER);
        bool hasCache = ensureStandbyStripCache();
        if (hasCache) {
          standbyCacheValid = false;
          clearCacheRect(19, 53, 202, 210, C_BLACK);
        }

        bool rendered = false;
        if (hasCache && rgb565Path.length() > 0 && TestSDCard::isMounted()) {
          File rf = SD.open(rgb565Path, FILE_READ);
          if (rf && rf.size() >= 240 * 320 * 2) {
            uint16_t srcRow[240];
            const int dstW = 156, dstH = 208;
            const int dstX0 = (W - dstW) / 2, dstY0 = 54;
            int lastSrcY = -1;
            for (int dy = 0; dy < dstH; dy++) {
              int srcY = (dy * 320) / dstH;
              while (lastSrcY < srcY) {
                rf.read((uint8_t*)srcRow, 240 * sizeof(uint16_t));
                lastSrcY++;
              }
              for (int dx = 0; dx < dstW; dx++) {
                setCachePixel(dstX0 + dx, dstY0 + dy, srcRow[(dx * 240) / dstW]);
              }
            }
            rf.close();
            rendered = true;
          } else if (rf) {
            rf.close();
          }
        }

        if (!rendered) {
          uint16_t jw = 0, jh = 0;
          TJpgDec.setCallback(jpgGalleryCallback);
          TJpgDec.setSwapBytes(false);
          if (!isFromFs && TestSDCard::isMounted() && SD.exists(cleanFsPath)) {
            File sf = SD.open(cleanFsPath, FILE_READ);
            size_t sz = sf ? sf.size() : 0;
            if (sz > 0 && sz <= 90000) {
              uint8_t* buf = (uint8_t*)malloc(sz);
              if (buf) {
                if (sf.read(buf, sz) == sz && TJpgDec.getJpgSize(&jw, &jh, buf, sz) == JDR_OK && jw > 0 && jh > 0) {
                  uint8_t scale = (jw >= 480 || jh >= 640) ? 4 : 2;
                  TJpgDec.setJpgScale(scale);
                  int px = max(20, (W - (int)(jw / scale)) / 2);
                  int py = max(54, 54 + (208 - (int)(jh / scale)) / 2);
                  TJpgDec.drawJpg(px, py, buf, sz);
                }
                free(buf);
              }
            }
            if (sf) sf.close();
          } else if (isFromFs && TJpgDec.getFsJpgSize(&jw, &jh, cleanFsPath.c_str(), LittleFS) == JDR_OK && jw > 0 && jh > 0) {
            uint8_t scale = (jw >= 480 || jh >= 640) ? 4 : 2;
            TJpgDec.setJpgScale(scale);
            int px = max(20, (W - (int)(jw / scale)) / 2);
            int py = max(54, 54 + (208 - (int)(jh / scale)) / 2);
            decodeFsJpgFromRam(px, py, cleanFsPath.c_str());
          }
        }

        if (hasCache) {
          for (int ry = 53; ry < 263; ry++) {
            int s = ry / 80, sy = ry % 80;
            tft->drawRGBBitmap(19, ry, &standbyStripCache[s][sy * 240 + 19], 202, 1);
          }
        }

        tft->fillRoundRect(6, 268, W - 12, 22, 4, C_CARD_BG);
        tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
        tft->setCursor(10, 275);
        tft->print("[OK]: Toàn Màn Hình | [MENU]: Đặt Nền");
        drawSymbianSoftkeys("[MENU:ĐặtNền]", "[OK:ToànMànHình]", "[EXIT:DS Tệp]");
      } else if (isAudio) {
        tft->fillRoundRect(6, 54, W - 12, 234, 6, C_CARD_BG);
        tft->drawRoundRect(6, 54, W - 12, 234, 6, C_NEON_GREEN);

        String songTitle = cleanFsPath;
        int lastSl = songTitle.lastIndexOf('/');
        if (lastSl >= 0) songTitle = songTitle.substring(lastSl + 1);
        if (vnStrLen(songTitle) > 30) songTitle = vnSubstr(songTitle, 0, 30);

        uint16_t estDur = low.endsWith(".wav") ? max((size_t)5, szBytes / 32000) : max((size_t)5, szBytes / 16000);
        if (estDur > 1800) estDur = 240;

        tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
        tft->setCursor(16, 68);
        tft->print("♫ TỆP ÂM NHẠC TRÊN THẺ NHỚ SD:");

        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(16, 90);
        tft->print(songTitle);

        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(16, 114);
        tft->printf("Đường dẫn: %s", cleanFsPath.c_str());

        tft->setTextColor(C_YELLOW, C_CARD_BG);
        tft->setCursor(16, 136);
        if (szBytes >= 1048576) {
          tft->printf("Dung lượng: %.2f MB (%u Bytes)", szBytes / 1048576.0f, (unsigned)szBytes);
        } else {
          tft->printf("Dung lượng: %.1f KB (%u Bytes)", szBytes / 1024.0f, (unsigned)szBytes);
        }
        tft->setCursor(16, 156);
        tft->printf("Thời lượng ước tính: %02u phút %02u giây", estDur / 60, estDur % 60);

        tft->fillRoundRect(16, 188, W - 32, 82, 6, 0x0841);
        tft->drawRoundRect(16, 188, W - 32, 82, 6, C_NEON_CYAN);
        tft->setTextColor(C_NEON_GREEN, 0x0841);
        tft->setCursor(24, 200);
        tft->print("► Bấm [OK]: Phát Ngay Trên Trình Nhạc");
        tft->setTextColor(C_YELLOW, 0x0841);
        tft->setCursor(24, 220);
        tft->print("• Bấm [MENU]: Tùy chọn / Xóa tệp");
        tft->setTextColor(C_SLATE, 0x0841);
        tft->setCursor(24, 240);
        tft->print("• Bấm [Trái/Phải]: Chuyển bài khác");
        tft->setCursor(24, 256);
        tft->print("• Bấm [EXIT]: Quay lại thư mục");

        drawSymbianSoftkeys("[MENU:TùyChọn]", "[OK:PhátNhạc]", "[EXIT:DS Tệp]");
      } else {
        tft->fillRoundRect(6, 54, W - 12, 234, 6, C_CARD_BG);
        tft->drawRoundRect(6, 54, W - 12, 234, 6, C_NEON_CYAN);
        String previewTxt = TestSDCard::readSdFilePreview(rawName);
        String lines[12];
        int nLines = wrapTextWordBoundary(previewTxt, 34, lines, 12);
        tft->setTextColor(C_WHITE, C_CARD_BG);
        for (int l = 0; l < nLines; l++) {
          tft->setCursor(12, 64 + l * 16);
          tft->print(lines[l]);
        }
        drawSymbianSoftkeys("[MENU:TùyChọn]", "[OK:Đóng]", "[EXIT:DS Tệp]");
      }
      lastDrawnMemSubState = 3;
    } else if (s40MemSubState == 4) {
      tft->fillRoundRect(8, 32, W - 16, 250, 8, C_CARD_BG);
      tft->drawRoundRect(8, 32, W - 16, 250, 8, C_NEON_CYAN);
      tft->fillRoundRect(10, 34, W - 20, 24, 6, 0x1128);
      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, 0x1128);
      tft->setCursor(18, 42);
      tft->print("THÔNG TIN CHI TIẾT TỆP TIN");

      String names[28];
      size_t sizesBytes[28];
      bool isDirs[28];
      int count = listMemoryEntriesInDir(s40MemCurrentDir, names, sizesBytes, isDirs, 28);
      if (count > 0) {
        int selIdx = ((s40MemFileCursor % count) + count) % count;
        String rawName = names[selIdx];
        bool isFlash = rawName.startsWith("[FS]");
        String cleanPath = isFlash ? rawName.substring(4) : rawName;
        if (!cleanPath.startsWith("/") && cleanPath != "..") cleanPath = "/" + cleanPath;
        size_t szBytes = sizesBytes[selIdx];
        bool isDir = isDirs[selIdx];

        String low = cleanPath;
        low.toLowerCase();
        const char* fileKind = "Tệp dữ liệu hệ thống";
        uint16_t kindColor = C_WHITE;
        bool isImg = !isDir && (low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp") || low.endsWith(".rgb565"));
        bool isAudio = !isDir && (low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac") || low.endsWith(".ogg"));

        if (isDir) {
          fileKind = "Thư mục lưu trữ (Folder)";
          kindColor = C_YELLOW;
        } else if (isAudio) {
          fileKind = "Tệp Âm thanh / Nhạc số";
          kindColor = C_NEON_GREEN;
        } else if (isImg) {
          fileKind = "Tệp Hình ảnh / Hình nền 240x320";
          kindColor = C_NEON_CYAN;
        } else if (low.endsWith(".json") || low.endsWith(".txt") || low.endsWith(".csv") || low.endsWith(".log")) {
          fileKind = "Văn bản / Cấu hình (Text)";
          kindColor = C_ORANGE;
        }

        int lastSlash = cleanPath.lastIndexOf('/');
        String baseName = (lastSlash >= 0 && lastSlash + 1 < (int)cleanPath.length()) ? cleanPath.substring(lastSlash + 1) : cleanPath;
        if (vnStrLen(baseName) > 30) baseName = vnSubstr(baseName, 0, 30);
        String dispPath = cleanPath;
        if (vnStrLen(dispPath) > 30) dispPath = vnSubstr(dispPath, 0, 30);

        int y0 = 66;
        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(16, y0);      tft->print("• Tên mục:");
        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(16, y0 + 14); tft->print(baseName);

        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(16, y0 + 32); tft->print("• Phân loại:");
        tft->setTextColor(kindColor, C_CARD_BG);
        tft->setCursor(16, y0 + 46); tft->print(fileKind);

        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(16, y0 + 64); tft->print("• Đường dẫn lưu trữ:");
        tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
        tft->setCursor(16, y0 + 78);
        tft->printf("[%s] %s", isFlash ? "FLASH" : "SD", dispPath.c_str());

        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(16, y0 + 96); tft->print("• Dung lượng thực tế:");
        tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
        tft->setCursor(16, y0 + 110);
        if (isDir) {
          tft->printf("Thư mục (Chứa %u tệp bên trong)", (unsigned)szBytes);
        } else if (szBytes >= 1048576) {
          tft->printf("%.2f MB (%u Bytes)", szBytes / 1048576.0f, (unsigned)szBytes);
        } else {
          tft->printf("%.2f KB (%u Bytes)", szBytes / 1024.0f, (unsigned)szBytes);
        }

        // Dòng phụ trợ riêng cho ảnh hoặc nhạc
        tft->setTextColor(C_YELLOW, C_CARD_BG);
        tft->setCursor(16, y0 + 128);
        if (isImg) {
          String rgbComp = ImageManager::getRgb565CompanionPath(cleanPath);
          if (rgbComp.length() > 0 && SD.exists(rgbComp)) {
            tft->print("• Tăng tốc: File RGB565 sẵn sàng (0ms)");
          } else {
            tft->print("• Độ phân giải chuẩn: 240 x 320 px");
          }
        } else if (isAudio) {
          tft->printf("• Định dạng: Chuẩn âm thanh I2S Stereo");
        }

        tft->fillRoundRect(16, 212, W - 32, 58, 6, 0x0841);
        tft->drawRoundRect(16, 212, W - 32, 58, 6, C_CARD_BORDER);
        tft->setTextColor(C_YELLOW, 0x0841);
        tft->setCursor(24, 222);
        tft->print("PHÍM TẮT THAO TÁC:");
        tft->setTextColor(C_NEON_GREEN, 0x0841);
        tft->setCursor(24, 238);
        if (isImg) {
          tft->print("[OK]: Xem Toàn Màn Hình | [MENU]: Đặt Nền");
        } else if (isAudio) {
          tft->print("[OK]: Phát bài hát | [MENU]: Xóa tệp");
        } else {
          tft->print("[OK]: Mở / Xem   | [MENU]: Xóa tệp");
        }
        tft->setTextColor(C_SLATE, 0x0841);
        tft->setCursor(24, 253);
        tft->print("[Trái/Phải]: Đổi tệp | [EXIT]: Quay lại");
      }
      drawSymbianSoftkeys("[MENU:XóaTệp]", "[OK:MởTệp]", "[EXIT:DS Tệp]");
      lastDrawnMemSubState = 4;
    }
  }

  static bool deleteSelectedMemoryFile() {
    String names[28];
    size_t sizes[28];
    bool dirs[28];
    int cnt = listMemoryEntriesInDir(s40MemCurrentDir, names, sizes, dirs, 28);
    if (cnt <= 0) return false;
    int idx = ((s40MemFileCursor % cnt) + cnt) % cnt;
    String rawName = names[idx];
    if (dirs[idx] || rawName == "..") {
      showSymbianToast("KHÔNG XÓA ĐƯỢC THƯ MỤC!");
      return false;
    }
    bool isFlash = rawName.startsWith("[FS]");
    String cleanPath = isFlash ? rawName.substring(4) : rawName;
    if (!cleanPath.startsWith("/")) cleanPath = "/" + cleanPath;

    bool ok = false;
    if (isFlash) {
      ok = LittleFS.remove(cleanPath);
      if (cleanPath.endsWith(".jpg") || cleanPath.endsWith(".png")) {
        int dot = cleanPath.lastIndexOf('.');
        String rgbPath = cleanPath.substring(0, dot) + ".rgb565";
        if (LittleFS.exists(rgbPath)) LittleFS.remove(rgbPath);
      }
    } else if (TestSDCard::isMounted()) {
      ok = SD.remove(cleanPath);
      if (cleanPath.endsWith(".jpg") || cleanPath.endsWith(".png")) {
        int dot = cleanPath.lastIndexOf('.');
        String rgbPath = cleanPath.substring(0, dot) + ".rgb565";
        if (SD.exists(rgbPath)) SD.remove(rgbPath);
      }
    }

    if (ok) {
      if (s40MemFileCursor > 0 && s40MemFileCursor >= cnt - 1) {
        s40MemFileCursor--;
      }
      lastDrawnMemSubState = -1; // Reset để vẽ lại toàn bộ danh sách tệp
      TestAudio::playDeleteChime();
      showSymbianToast("ĐÃ XÓA TỆP THÀNH CÔNG!");
      return true;
    } else {
      TestAudio::playDeleteChime();
      showSymbianToast("LỖI: KHÔNG THỂ XÓA TỆP!");
      return false;
    }
  }

  // ============================================================================
  // CHẾ ĐỘ 12: ĐA PHƯƠNG TIỆN - TRÌNH PHÁT NHẠC (1/3 VISUALIZER + 2/3 CONTROLS)
  // ============================================================================
  static bool s40MediaPlaying = true;
  static uint8_t s40MediaVisMode = 0;   // 0 = Đĩa quay Vinyl, 1 = Sóng Hình Sin, 2 = VU Meter Dạng Thanh
  static uint8_t s40MediaRepeatMode = 0; // 0 = Lặp tất cả, 1 = Lặp 1 bài, 2 = Ngẫu nhiên
  static uint8_t s40MediaSubState = 0;   // 0 = Trình phát chính, 1 = Popup Menu, 2 = Danh sách bài hát SD
  static int s40MediaPopupCursor = 0;
  static int s40MediaTrackIdx = 0;
  static int s40MediaTrackCount = 0;
  static String s40MediaTracks[24];
  static uint16_t s40MediaDurations[24];
  static bool s40MediaFromSd[24];
  static size_t s40MediaFileSizes[24];
  static uint16_t s40MediaCurSec = 0;
  static float s40MediaDiscAngle = 0.0f;
  static float s40MediaWavePhase = 0.0f;
  static uint8_t s40MediaVuPeaks[16] = {0};
  static unsigned long s40MediaLastSecMs = 0;
  static VnCanvas16* mediaVisCanvas = nullptr;

  static void refreshMediaPlaylist() {
    s40MediaTrackCount = 0;
    // 0. Quét thư mục /recording trên Thẻ nhớ SD (Chứa các bản ghi âm từ Mic INMP441)
    if (TestSDCard::isMounted()) {
      if (!SD.exists("/recording")) {
        SD.mkdir("/recording");
      }
      File rDir = SD.open("/recording");
      if (rDir && rDir.isDirectory()) {
        File f = rDir.openNextFile();
        while (f && s40MediaTrackCount < 18) {
          if (!f.isDirectory()) {
            String fn = String(f.name());
            int sl = fn.lastIndexOf('/');
            if (sl >= 0) fn = fn.substring(sl + 1);
            String low = fn;
            low.toLowerCase();
            if (low.endsWith(".wav") || low.endsWith(".mp3")) {
              size_t fsz = f.size();
              s40MediaTracks[s40MediaTrackCount] = fn;
              s40MediaFileSizes[s40MediaTrackCount] = fsz;
              uint16_t estSec = (fsz > 44) ? max((size_t)3, (fsz - 44) / 32000) : 10;
              s40MediaDurations[s40MediaTrackCount] = estSec;
              s40MediaFromSd[s40MediaTrackCount] = true;
              s40MediaTrackCount++;
            }
          }
          f.close();
          f = rDir.openNextFile();
        }
        rDir.close();
      }

      // 1. Quét tiếp thư mục /Musics tồn tại trên Thẻ nhớ MicroSD
      if (!SD.exists("/Musics")) {
        SD.mkdir("/Musics");
      }
      File mDir = SD.open("/Musics");
      if (mDir && mDir.isDirectory()) {
        File f = mDir.openNextFile();
        while (f && s40MediaTrackCount < 18) {
          if (!f.isDirectory()) {
            String fn = String(f.name());
            int sl = fn.lastIndexOf('/');
            if (sl >= 0) fn = fn.substring(sl + 1);
            String low = fn;
            low.toLowerCase();
            if (low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac") || low.endsWith(".ogg")) {
              size_t fsz = f.size();
              s40MediaTracks[s40MediaTrackCount] = fn;
              s40MediaFileSizes[s40MediaTrackCount] = fsz;
              // Ước tính thời lượng thực từ dung lượng file (.mp3 ~ 16KB/s @128kbps, .wav ~ 88KB/s hoặc 32KB/s)
              uint16_t estSec = 180;
              if (low.endsWith(".wav")) {
                estSec = (fsz > 44) ? max((size_t)5, (fsz - 44) / 32000) : 120;
              } else {
                estSec = (fsz > 1024) ? max((size_t)5, fsz / 16000) : 180;
              }
              if (estSec > 1800) estSec = 240;
              s40MediaDurations[s40MediaTrackCount] = estSec;
              s40MediaFromSd[s40MediaTrackCount] = true;
              s40MediaTrackCount++;
            }
          }
          f.close();
          f = mDir.openNextFile();
        }
        mDir.close();
      }

      // 2. Quét thêm thư mục gốc / của Thẻ nhớ SD (nếu người dùng chép ở ngoài /Musics)
      String names[24];
      size_t sizes[24];
      bool dirs[24];
      int cnt = TestSDCard::listSdFiles(names, sizes, dirs, 24);
      for (int i = 0; i < cnt && s40MediaTrackCount < 18; i++) {
        if (dirs[i]) continue;
        String n = names[i];
        if (n.startsWith("[FS]")) continue;
        if (n.startsWith("/")) n = n.substring(1);
        String low = n;
        low.toLowerCase();
        if (low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac") || low.endsWith(".ogg")) {
          // Kiểm tra trùng tên với /Musics
          bool dup = false;
          for (int k = 0; k < s40MediaTrackCount; k++) {
            if (s40MediaTracks[k].equalsIgnoreCase(n)) { dup = true; break; }
          }
          if (!dup) {
            s40MediaTracks[s40MediaTrackCount] = n;
            s40MediaFileSizes[s40MediaTrackCount] = sizes[i];
            s40MediaDurations[s40MediaTrackCount] = (sizes[i] > 16000) ? (sizes[i] / 16000) : 180;
            s40MediaFromSd[s40MediaTrackCount] = true;
            s40MediaTrackCount++;
          }
        }
      }
    }

    // 3. Bổ sung danh sách nhạc Demo để luôn có sẵn bài hát hiển thị & chuyển bài mượt mà
    const char* demoTitles[6] = {
      "Cyberpunk 2077 - Night City.mp3",
      "Midnight Synthwave - Retro.mp3",
      "Đà Lạt Chill - Mưa Đêm.wav",
      "Tokyo Drift - Phonk Bass.mp3",
      "Blade Runner Blues - Synth.flac",
      "Esp32 S40 - Chiptune Beats.mp3"
    };
    const uint16_t demoDurs[6] = { 215, 198, 164, 152, 240, 135 };
    for (int i = 0; i < 6 && s40MediaTrackCount < 24; i++) {
      s40MediaTracks[s40MediaTrackCount] = demoTitles[i];
      s40MediaDurations[s40MediaTrackCount] = demoDurs[i];
      s40MediaFileSizes[s40MediaTrackCount] = (size_t)demoDurs[i] * 16384;
      s40MediaFromSd[s40MediaTrackCount] = false;
      s40MediaTrackCount++;
    }
    if (s40MediaTrackIdx >= s40MediaTrackCount) s40MediaTrackIdx = 0;
  }

  static bool deleteSelectedMediaTrack() {
    if (s40MediaTrackCount <= 0) return false;
    if (!s40MediaFromSd[s40MediaTrackIdx]) {
      showSymbianToast("NHẠC MẪU ROM (KHÔNG THỂ XÓA)!");
      return false;
    }
    if (!TestSDCard::isMounted()) {
      showSymbianToast("CHƯA GẮN THẺ NHỚ MICROSD!");
      return false;
    }
    String trackName = s40MediaTracks[s40MediaTrackIdx];
    String p0 = "/recording/" + trackName;
    String p1 = "/Musics/" + trackName;
    String p2 = "/" + trackName;
    bool removed = false;
    if (SD.exists(p0)) removed = SD.remove(p0);
    else if (SD.exists(p1)) removed = SD.remove(p1);
    if (!removed && SD.exists(p2)) removed = SD.remove(p2);
    if (removed) {
      refreshMediaPlaylist();
      if (s40MediaTrackIdx >= s40MediaTrackCount) {
        s40MediaTrackIdx = max(0, s40MediaTrackCount - 1);
      }
      TestAudio::playDeleteChime();
      showSymbianToast("ĐÃ XÓA BÀI HÁT KHỎI THẺ SD!");
      return true;
    } else {
      TestAudio::playDeleteChime();
      showSymbianToast("LỖI: KHÔNG THỂ XÓA BÀI HÁT!");
      return false;
    }
  }

  // Đọc trực tiếp mẫu PCM từ file .wav trong /Musics trên Thẻ SD (nếu bài đang phát là .wav) để tạo phổ VU Meter thật!
  static void sampleWavAudioFromSdIfApplicable() {
    if (!s40MediaPlaying || s40MediaTrackCount <= 0) return;

    // Nếu đang phát bất đồng bộ từ RAM hoặc SD trên Core 0, không truy cập SD từ Core 1 để tránh xung đột SPI bus!
    if (TestAudio::isVoicePlaybackAsyncRunning()) {
      if (TestAudio::getRecordedSampleCount() > 0) {
        uint32_t cur = TestAudio::getVoicePlaybackCurrentSample();
        const int16_t* pcmBuf = TestAudio::getRecordedPcmBuffer();
        size_t total = TestAudio::getRecordedSampleCount();
        if (pcmBuf && total > 32) {
          uint8_t wavBands[16] = {0};
          int maxLv = 0;
          for (int b = 0; b < 16; b++) {
            size_t idx = (cur + b * 4) % total;
            int s = abs((int)pcmBuf[idx]);
            int val = (s / 280);
            wavBands[b] = (uint8_t)constrain(val, 4, 98);
            if (wavBands[b] > maxLv) maxLv = wavBands[b];
          }
          TestAudio::setExternalAudioSpectrum(wavBands, maxLv);
        }
      }
      return;
    }

    if (!TestSDCard::isMounted()) return;
    if (!s40MediaFromSd[s40MediaTrackIdx]) return;
    String fn = s40MediaTracks[s40MediaTrackIdx];
    String low = fn;
    low.toLowerCase();
    if (!low.endsWith(".wav")) return;

    String fullPath = "/recording/" + fn;
    if (!SD.exists(fullPath)) fullPath = "/Musics/" + fn;
    if (!SD.exists(fullPath)) fullPath = "/" + fn;
    if (!SD.exists(fullPath)) return;

    File wf = SD.open(fullPath, FILE_READ);
    if (!wf) return;
    size_t fsz = wf.size();
    uint16_t totSec = max((uint16_t)1, s40MediaDurations[s40MediaTrackIdx]);
    if (fsz > 128) {
      size_t offset = 44 + (size_t)(((uint64_t)(fsz - 108) * (s40MediaCurSec % totSec)) / totSec);
      offset = (offset / 2) * 2; // Căn chẵn 16-bit PCM
      if (wf.seek(offset)) {
        int16_t pcm[32];
        size_t nRead = wf.read((uint8_t*)pcm, sizeof(pcm));
        if (nRead == sizeof(pcm)) {
          uint8_t wavBands[16];
          int maxLv = 0;
          for (int b = 0; b < 16; b++) {
            int s0 = abs((int)pcm[b * 2]);
            int s1 = abs((int)pcm[b * 2 + 1]);
            int diff = abs((int)pcm[b * 2 + 1] - (int)pcm[b * 2]);
            int val = ((s0 + s1) / 360) + (diff / 280);
            wavBands[b] = (uint8_t)constrain(val, 4, 98);
            if (wavBands[b] > maxLv) maxLv = wavBands[b];
          }
          TestAudio::setExternalAudioSpectrum(wavBands, maxLv);
        }
      }
    }
    wf.close();
  }

  // Bắt đầu / Dừng phát âm thanh thật ra Loa PWM GPIO 15 trong Trình Phát Nhạc
  static void startMediaAudioPlayback() {
    if (s40MediaTrackCount <= 0) return;
    String fn = s40MediaTracks[s40MediaTrackIdx];
    Serial.printf("▶️ [MEDIA PLAYER] Bat dau phat: %s\n", fn.c_str());

    // Nếu bài đang chọn là bản thu âm gần nhất từ Mic INMP441
    if (fn.indexOf("rec_latest") >= 0) {
      if (TestAudio::getRecordedSampleCount() > 0) {
        TestAudio::startVoicePlaybackAsync(PIN_I2S_SPK_DIN);
        return;
      } else if (TestSDCard::isMounted() && SD.exists("/recording/rec_latest.wav")) {
        TestAudio::playSdFileAsync("/recording/rec_latest.wav", PIN_I2S_SPK_DIN);
        return;
      }
    }

    // Nếu là file trên thẻ nhớ SD (trong /recording/ hoặc /Musics/ hoặc /)
    if (TestSDCard::isMounted() && s40MediaFromSd[s40MediaTrackIdx]) {
      String path = "/recording/" + fn;
      if (!SD.exists(path)) path = "/Musics/" + fn;
      if (!SD.exists(path)) path = "/" + fn;
      if (SD.exists(path)) {
        TestAudio::playSdFileAsync(path, PIN_I2S_SPK_DIN);
        return;
      }
    }

    // Nếu là bài nhạc mẫu điện tử (Demo ROM Synthwave / Chiptune tracks)
    TestAudio::playSynthMusicAsync(fn, PIN_I2S_SPK_DIN);
  }

  static void stopMediaAudioPlayback() {
    TestAudio::stopVoicePlaybackAsync();
  }

  // Vẽ vùng 1/3 Màn hình trên cùng (Top Visualizer Box: 224x82) bằng Canvas chống nháy 100%
  static void drawMediaTopVisualizerOnly() {
    if (!tft || s40MediaSubState != 0) return;
    if (s40MediaPlaying) {
      if (!TestAudio::isMicMonitorActive() && !TestAudio::isVoiceRecording()) {
        TestAudio::enableMicMonitor(true);
      }
      sampleWavAudioFromSdIfApplicable();
    }
    // Lưu ý: Khi s40MediaPlaying == false (Pause/Stop trong Trình phát nhạc), chỉ dừng vẽ hiệu ứng
    // Sóng Sin / VU Meter / Đĩa quay của riêng Trình phát nhạc (bên dưới dùng biến s40MediaPlaying),
    // KHÔNG tắt TestAudio::enableMicMonitor(false) để tránh làm khóa cứng màn hình Biểu đồ Sóng âm & Mic (Mode 3)!

    const int vx = 8, vy = 26, vw = 224, vh = 82;
    if (!mediaVisCanvas) {
      mediaVisCanvas = new VnCanvas16(vw, vh);
    }
    if (!mediaVisCanvas) return;

    mediaVisCanvas->fillScreen(0x0842);
    mediaVisCanvas->drawRoundRect(0, 0, vw, vh, 6, C_NEON_CYAN);

    int vol = TestAudio::getSpeakerVolumePct();
    int micPeak = s40MediaPlaying ? TestAudio::getLastMicLevelPct() : 0;
    const uint8_t* realSpec16 = TestAudio::getMicSpectrum16();
    const int8_t* realWave48  = TestAudio::getWaveformBuffer();
    bool hasLiveAudioSignal   = s40MediaPlaying && (micPeak >= 4);

    float ampScale = s40MediaPlaying ? (0.45f + (vol / 160.0f) + (micPeak / 140.0f)) : 0.0f;
    if (ampScale > 1.25f) ampScale = 1.25f;

    if (s40MediaVisMode == 0) {
      // --- CHẾ ĐỘ 0: ĐĨA QUAY VINYL / CD (SPINNING TURNTABLE) ---
      int dcx = 48, dcy = 41, dr = 33;
      // Thân đĩa than đen + các vòng rãnh đồng tâm
      mediaVisCanvas->fillCircle(dcx, dcy, dr, C_BLACK);
      mediaVisCanvas->drawCircle(dcx, dcy, dr, C_NEON_PINK);
      mediaVisCanvas->drawCircle(dcx, dcy, dr - 5, 0x2124);
      mediaVisCanvas->drawCircle(dcx, dcy, dr - 11, 0x3186);
      mediaVisCanvas->drawCircle(dcx, dcy, dr - 17, 0x2124);

      // Nan quạt phản chiếu ánh sáng xoay theo góc s40MediaDiscAngle
      for (int k = 0; k < 2; k++) {
        float a1 = s40MediaDiscAngle + k * 3.14159f;
        float a2 = a1 + 0.35f;
        int x1 = dcx + (int)(cosf(a1) * (dr - 3));
        int y1 = dcy + (int)(sinf(a1) * (dr - 3));
        int x2 = dcx + (int)(cosf(a2) * (dr - 3));
        int y2 = dcy + (int)(sinf(a2) * (dr - 3));
        mediaVisCanvas->fillTriangle(dcx, dcy, x1, y1, x2, y2, 0x1949);
        mediaVisCanvas->drawLine(dcx, dcy, x1, y1, C_NEON_CYAN);
      }

      // Nhãn đĩa trung tâm (Record Label) + dấu chấm xoay
      mediaVisCanvas->fillCircle(dcx, dcy, 11, C_NEON_PINK);
      mediaVisCanvas->drawCircle(dcx, dcy, 11, C_YELLOW);
      int dotX = dcx + (int)(cosf(s40MediaDiscAngle) * 6);
      int dotY = dcy + (int)(sinf(s40MediaDiscAngle) * 6);
      mediaVisCanvas->fillCircle(dotX, dotY, 2, C_YELLOW);
      mediaVisCanvas->fillCircle(dcx, dcy, 3, C_BLACK);

      // Cần kim đọc đĩa (Tonearm)
      mediaVisCanvas->fillCircle(88, 12, 4, C_SLATE);
      if (s40MediaPlaying) {
        mediaVisCanvas->drawLine(88, 12, 74, 48, C_WHITE);
        mediaVisCanvas->fillRect(71, 46, 5, 4, C_YELLOW);
      } else {
        mediaVisCanvas->drawLine(88, 12, 88, 54, C_SLATE);
        mediaVisCanvas->fillRect(86, 52, 5, 4, C_NEON_AMBER);
      }

      // Phần thông tin & Equalizer mini bên phải đĩa (Dừng hẳn về 3px khi Pause/Stop)
      mediaVisCanvas->setTextSize(1);
      mediaVisCanvas->setTextColor(C_YELLOW);
      mediaVisCanvas->setCursor(100, 10);
      mediaVisCanvas->print("ĐĨA QUAY VINYL");
      mediaVisCanvas->setTextColor(s40MediaPlaying ? C_NEON_CYAN : C_SLATE);
      mediaVisCanvas->setCursor(100, 22);
      mediaVisCanvas->printf("TRẠNG THÁI: %s", s40MediaPlaying ? "ĐANG PHÁT" : "TẠM DỪNG ");

      for (int b = 0; b < 8; b++) {
        int bh = 3;
        if (s40MediaPlaying) {
          int realBar = (realSpec16[b * 2] * 34) / 100;
          int simBar  = 6 + (int)(fabsf(sinf(s40MediaWavePhase + b * 0.7f)) * 24.0f * ampScale);
          bh = hasLiveAudioSignal ? max(realBar, simBar / 2) : simBar;
          bh = constrain(bh, 3, 34);
        }
        int bx = 100 + b * 14;
        int by = 72 - bh;
        uint16_t bCol = !s40MediaPlaying ? C_SLATE : ((b % 2 == 0) ? C_NEON_GREEN : C_NEON_PINK);
        mediaVisCanvas->fillRect(bx, by, 9, bh, bCol);
      }
    } else if (s40MediaVisMode == 1) {
      // --- CHẾ ĐỘ 1: HIỂN THỊ SÓNG ÂM DẠNG HÌNH SIN (PHẲNG TĨNH KHI PAUSE/STOP) ---
      for (int gx = 16; gx < vw - 8; gx += 24) {
        mediaVisCanvas->drawFastVLine(gx, 6, vh - 12, 0x10A3);
      }
      mediaVisCanvas->drawFastHLine(6, vh / 2, vw - 12, 0x18E5);

      mediaVisCanvas->setTextSize(1);
      mediaVisCanvas->setTextColor(C_YELLOW);
      mediaVisCanvas->setCursor(10, 7);
      mediaVisCanvas->printf("SÓNG HÌNH SIN [%s]", s40MediaPlaying ? "ĐANG PHÁT" : "TẠM DỪNG");

      int midY = vh / 2 + 5;
      int prevY1 = midY, prevY2 = midY;
      for (int x = 8; x < vw - 8; x += 2) {
        int y1 = midY, y2 = midY;
        if (s40MediaPlaying) {
          int wIdx = ((x - 8) * 48) / (vw - 16);
          wIdx = constrain(wIdx, 0, 47);
          int micSample = realWave48 ? realWave48[wIdx] : 0;

          float t = (float)x * 0.065f;
          float s1 = sinf(t + s40MediaWavePhase) * cosf(t * 0.35f - s40MediaWavePhase * 0.5f);
          float s2 = sinf(t * 1.35f - s40MediaWavePhase * 1.2f);
          y1 = midY + (int)(s1 * 22.0f * ampScale) + (micSample * 3) / 4;
          y2 = midY + (int)(s2 * 16.0f * ampScale) - (micSample / 2);
          y1 = constrain(y1, 18, vh - 6);
          y2 = constrain(y2, 18, vh - 6);
        }
        if (x > 8) {
          mediaVisCanvas->drawLine(x - 2, prevY2, x, y2, s40MediaPlaying ? C_NEON_PINK : C_SLATE);
          mediaVisCanvas->drawLine(x - 2, prevY1, x, y1, C_NEON_CYAN);
          mediaVisCanvas->drawLine(x - 2, prevY1 + 1, x, y1 + 1, C_NEON_CYAN);
        }
        prevY1 = y1;
        prevY2 = y2;
      }
    } else {
      // --- CHẾ ĐỘ 2: HIỂN THỊ VU METER DẠNG THANH 16-BAR (HẠ VỀ 0 KHI PAUSE/STOP) ---
      mediaVisCanvas->setTextSize(1);
      mediaVisCanvas->setTextColor(C_YELLOW);
      mediaVisCanvas->setCursor(10, 7);
      mediaVisCanvas->printf("PHỔ ÂM VU METER [%s]", s40MediaPlaying ? "ĐANG PHÁT" : "TẠM DỪNG");

      int barW = 10, gap = 3, startX = 10, bottomY = vh - 7, maxH = 52;
      for (int i = 0; i < 16; i++) {
        int targetH = 2;
        if (s40MediaPlaying) {
          int realH = (realSpec16[i] * maxH) / 100;
          float wave = fabsf(sinf(s40MediaWavePhase * 1.1f + i * 0.45f) * cosf(s40MediaWavePhase * 0.6f - i * 0.2f));
          int simH = (int)(wave * (hasLiveAudioSignal ? 14 : 38) * ampScale);
          targetH = constrain(max(realH, simH), 2, maxH);
        }

        if (targetH >= s40MediaVuPeaks[i]) {
          s40MediaVuPeaks[i] = targetH;
        } else if (s40MediaVuPeaks[i] > 2) {
          s40MediaVuPeaks[i] = max(2, s40MediaVuPeaks[i] - 3);
        }

        int bx = startX + i * (barW + gap);
        // Vẽ từng đốt LED cho cột VU Meter
        for (int h = 0; h < targetH; h += 4) {
          uint16_t segCol = !s40MediaPlaying ? C_SLATE : ((h < 26) ? C_NEON_GREEN : ((h < 40) ? C_YELLOW : C_NEON_PINK));
          mediaVisCanvas->fillRect(bx, bottomY - h - 3, barW, 3, segCol);
        }
        // Vạch giữ đỉnh (Peak Hold)
        int py = bottomY - s40MediaVuPeaks[i] - 2;
        mediaVisCanvas->drawFastHLine(bx, py, barW, s40MediaPlaying ? C_WHITE : C_SLATE);
      }
    }

    tft->drawRGBBitmap(vx, vy, mediaVisCanvas->getBuffer(), vw, vh);
  }

  // Vẽ riêng thanh Slidebar tiến trình & Thời gian (cập nhật mỗi giây không nháy toàn màn hình)
  static void drawMediaProgressAndTimeOnly() {
    if (!tft || s40MediaSubState != 0) return;
    int W = tft->width();
    uint16_t totalSec = (s40MediaTrackCount > 0) ? s40MediaDurations[s40MediaTrackIdx] : 200;
    if (totalSec == 0) totalSec = 180;
    if (s40MediaCurSec > totalSec) s40MediaCurSec = totalSec;

    // 1. Dòng thời gian hiện tại (Trái) & Tổng thời gian (Phải)
    char curBuf[12], totBuf[12];
    snprintf(curBuf, sizeof(curBuf), "%02u:%02u", s40MediaCurSec / 60, s40MediaCurSec % 60);
    snprintf(totBuf, sizeof(totBuf), "%02u:%02u", totalSec / 60, totalSec % 60);

    tft->setTextSize(1);
    tft->setTextColor(C_NEON_CYAN, C_DARK_BG);
    tft->setCursor(12, 174);
    tft->print(curBuf);

    tft->setTextColor(C_SLATE, C_DARK_BG);
    tft->setCursor(W - 42, 174);
    tft->print(totBuf);

    // 2. Thanh trượt Slidebar (y = 188..202)
    int sbX = 14, sbY = 192, sbW = W - 28;
    tft->fillRect(sbX - 4, sbY - 6, sbW + 8, 14, C_DARK_BG);
    tft->fillRoundRect(sbX, sbY, sbW, 4, 2, 0x2124);
    int fillW = (int)((long)s40MediaCurSec * sbW / totalSec);
    fillW = constrain(fillW, 0, sbW);
    if (fillW > 0) {
      tft->fillRoundRect(sbX, sbY, fillW, 4, 2, C_NEON_CYAN);
    }
    // Nút tròn kéo (Thumb Knob) trên Slidebar
    int knobX = sbX + fillW;
    tft->fillCircle(knobX, sbY + 2, 5, C_YELLOW);
    tft->drawCircle(knobX, sbY + 2, 5, C_WHITE);
  }

  static void drawMediaPlayerAppScreen(bool fullRedraw = true) {
    if (!tft) return;
    int W = tft->width();
    if (s40MediaTrackCount == 0) refreshMediaPlaylist();

    if (fullRedraw) {
      tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
      drawSymbianChrome("TRÌNH PHÁT ÂM NHẠC", "[MENU:TùyChọn]", "[OK:Phát]", "[EXIT:Menu]");
    }

    // Nếu đang mở Popup Menu (s40MediaSubState == 1)
    if (s40MediaSubState == 1) {
      tft->fillRoundRect(12, 38, W - 24, 236, 8, 0x0862);
      tft->drawRoundRect(12, 38, W - 24, 236, 8, C_YELLOW);
      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, 0x0862);
      tft->setCursor(22, 48);
      tft->print("TÙY CHỌN TRÌNH PHÁT NHẠC:");

      const char* visNames[3] = { "Đĩa quay Vinyl", "Sóng Hình Sin", "Cột sóng VU Meter" };
      const char* repNames[3] = { "Lặp tất cả", "Lặp 1 bài", "Phát ngẫu nhiên" };
      String opts[4] = {
        "1. Danh sách bài hát (" + String(s40MediaTrackCount) + " bài)",
        "2. Hiệu ứng: " + String(visNames[s40MediaVisMode % 3]),
        "3. Chế độ: " + String(repNames[s40MediaRepeatMode % 3]),
        "4. Thoát về Menu Chính"
      };
      for (int i = 0; i < 4; i++) {
        int ry = 72 + i * 44;
        bool sel = (i == s40MediaPopupCursor);
        tft->fillRoundRect(20, ry, W - 40, 36, 6, sel ? 0x218A : 0x0841);
        tft->drawRoundRect(20, ry, W - 40, 36, 6, sel ? C_NEON_CYAN : 0x2124);
        tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? 0x218A : 0x0841);
        tft->setCursor(28, ry + 14);
        tft->print(opts[i]);
      }
      drawSymbianSoftkeys("[OK:Chọn]", "Lên/Xuống", "[EXIT:Đóng]");
      return;
    }

    // Nếu đang mở Danh sách Bài hát quét từ Thẻ SD (s40MediaSubState == 2)
    if (s40MediaSubState == 2) {
      tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
      drawSymbianChrome("DANH SÁCH BÀI HÁT (SD)", "[MENU:TùyChọn]", "[OK:Phát]", "[EXIT:QuayLại]");
      int startIdx = max(0, min(s40MediaTrackIdx - 2, max(0, s40MediaTrackCount - 6)));
      for (int row = 0; row < 6 && (startIdx + row) < s40MediaTrackCount; row++) {
        int idx = startIdx + row;
        bool sel = (idx == s40MediaTrackIdx);
        int ry = 32 + row * 41;
        tft->fillRoundRect(8, ry, W - 16, 37, 5, sel ? 0x1949 : C_CARD_BG);
        tft->drawRoundRect(8, ry, W - 16, 37, 5, sel ? C_NEON_CYAN : C_CARD_BORDER);
        tft->setTextSize(1);
        tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? 0x1949 : C_CARD_BG);
        String name = s40MediaTracks[idx];
        if (vnStrLen(name) > 28) name = vnSubstr(name, 0, 28);
        tft->setCursor(16, ry + 8);
        tft->printf("%02d. %s", idx + 1, name.c_str());

        tft->setTextColor(s40MediaFromSd[idx] ? C_NEON_GREEN : C_NEON_PINK, sel ? 0x1949 : C_CARD_BG);
        tft->setCursor(16, ry + 22);
        if (s40MediaFromSd[idx] && s40MediaFileSizes[idx] > 0) {
          tft->printf("[THẺ SD: %.1f KB]   %02u:%02u", s40MediaFileSizes[idx] / 1024.0f,
                      s40MediaDurations[idx] / 60, s40MediaDurations[idx] % 60);
        } else {
          tft->printf("[%s]  Thời lượng: %02u:%02u", s40MediaFromSd[idx] ? "THẺ SD" : "NHẠC MẪU",
                      s40MediaDurations[idx] / 60, s40MediaDurations[idx] % 60);
        }
      }
      return;
    }

    // Nếu đang mở Popup Thao tác trên Bài hát đang chọn trong Danh sách (s40MediaSubState == 3)
    if (s40MediaSubState == 3) {
      tft->fillRoundRect(10, 42, W - 20, 224, 8, 0x0862);
      tft->drawRoundRect(10, 42, W - 20, 224, 8, C_YELLOW);
      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, 0x0862);
      tft->setCursor(18, 52);
      String curSong = (s40MediaTrackCount > 0) ? s40MediaTracks[s40MediaTrackIdx] : "Chưa chọn bài";
      if (vnStrLen(curSong) > 24) curSong = vnSubstr(curSong, 0, 24);
      tft->printf("BÀI HÁT: %s", curSong.c_str());

      const char* songOpts[4] = {
        "1. Phát ngay bài hát đang chọn",
        "2. Xem thông tin chi tiết tệp nhạc",
        "3. Xóa bài hát này khỏi Thẻ nhớ SD",
        "4. Quét & Làm mới danh sách nhạc"
      };
      for (int i = 0; i < 4; i++) {
        int ry = 72 + i * 44;
        bool sel = (i == s40MediaPopupCursor);
        tft->fillRoundRect(18, ry, W - 36, 36, 6, sel ? 0x218A : 0x0841);
        tft->drawRoundRect(18, ry, W - 36, 36, 6, sel ? C_NEON_CYAN : 0x2124);
        tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? 0x218A : 0x0841);
        tft->setCursor(26, ry + 14);
        tft->print(songOpts[i]);
      }
      drawSymbianSoftkeys("[OK:Chọn]", "Lên/Xuống", "[EXIT:DS Bài]");
      return;
    }

    // Nếu đang mở Bảng Thông tin Chi tiết Tệp nhạc (s40MediaSubState == 4)
    if (s40MediaSubState == 4) {
      tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
      drawSymbianChrome("CHI TIẾT TỆP BÀI HÁT", "[MENU:XóaBài]", "[OK:Phát]", "[EXIT:DS Bài]");

      tft->fillRoundRect(8, 32, W - 16, 250, 8, C_CARD_BG);
      tft->drawRoundRect(8, 32, W - 16, 250, 8, C_NEON_CYAN);
      tft->fillRoundRect(10, 34, W - 20, 24, 6, 0x1128);
      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, 0x1128);
      tft->setCursor(18, 42);
      tft->printf("THÔNG TIN BÀI HÁT #%02d/%02d", s40MediaTrackIdx + 1, s40MediaTrackCount);

      if (s40MediaTrackCount > 0) {
        String sName = s40MediaTracks[s40MediaTrackIdx];
        bool fromSd = s40MediaFromSd[s40MediaTrackIdx];
        size_t fSize = s40MediaFileSizes[s40MediaTrackIdx];
        uint16_t dur = s40MediaDurations[s40MediaTrackIdx];

        String low = sName;
        low.toLowerCase();
        const char* fmtStr = "MP3 Audio (MPEG-1 Layer 3)";
        if (low.endsWith(".wav")) fmtStr = "WAV Audio (PCM Không nén)";
        else if (low.endsWith(".flac")) fmtStr = "FLAC Audio (Lossless)";
        else if (!fromSd) fmtStr = "Bản nhạc mẫu tích hợp sẵn";

        String dispName = sName;
        if (vnStrLen(dispName) > 30) dispName = vnSubstr(dispName, 0, 30);
        String dispPath = "Bộ nhớ ROM (Nhạc mẫu)";
        if (fromSd) {
          if (SD.exists("/recording/" + sName)) dispPath = "/recording/" + sName;
          else if (SD.exists("/Musics/" + sName)) dispPath = "/Musics/" + sName;
          else dispPath = "/" + sName;
        }
        if (vnStrLen(dispPath) > 30) dispPath = vnSubstr(dispPath, 0, 30);

        int y0 = 68;
        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(16, y0);      tft->print("• Tên bài hát:");
        tft->setTextColor(C_WHITE, C_CARD_BG);
        tft->setCursor(16, y0 + 14); tft->print(dispName);

        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(16, y0 + 34); tft->print("• Định dạng & Nguồn phát:");
        tft->setTextColor(fromSd ? C_NEON_GREEN : C_NEON_PINK, C_CARD_BG);
        tft->setCursor(16, y0 + 48); tft->print(fmtStr);

        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(16, y0 + 68); tft->print("• Đường dẫn trên Thẻ nhớ:");
        tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
        tft->setCursor(16, y0 + 82); tft->print(dispPath);

        tft->setTextColor(C_SLATE, C_CARD_BG);
        tft->setCursor(16, y0 + 102); tft->print("• Dung lượng & Thời lượng:");
        tft->setTextColor(C_YELLOW, C_CARD_BG);
        tft->setCursor(16, y0 + 116);
        if (fromSd && fSize > 0) {
          if (fSize >= 1048576) {
            tft->printf("%.2f MB (%u B) | %02u:%02u", fSize / 1048576.0f, (unsigned)fSize, dur / 60, dur % 60);
          } else {
            tft->printf("%.1f KB (%u B) | %02u:%02u", fSize / 1024.0f, (unsigned)fSize, dur / 60, dur % 60);
          }
        } else {
          tft->printf("Nhạc mẫu Demo | Thời lượng: %02u:%02u", dur / 60, dur % 60);
        }

        tft->fillRoundRect(16, 212, W - 32, 58, 6, 0x0841);
        tft->drawRoundRect(16, 212, W - 32, 58, 6, C_CARD_BORDER);
        tft->setTextColor(C_YELLOW, 0x0841);
        tft->setCursor(24, 222);
        tft->print("THAO TÁC VỚI BÀI HÁT NÀY:");
        tft->setTextColor(C_NEON_GREEN, 0x0841);
        tft->setCursor(24, 238);
        tft->print("[OK]: Phát ngay | [MENU]: Xóa bài hát");
        tft->setTextColor(C_SLATE, 0x0841);
        tft->setCursor(24, 253);
        tft->print("[Trái/Phải]: Đổi bài | [EXIT]: Quay lại");
      }
      return;
    }

    // --- MÀN HÌNH CHÍNH TRÌNH PHÁT NHẠC ---
    // 1. Vẽ vùng 1/3 trên cùng (Visualizer)
    drawMediaTopVisualizerOnly();

    // 2. Vùng thông tin bài hát & Âm lượng (y = 113..166)
    tft->fillRoundRect(8, 113, W - 16, 54, 6, C_CARD_BG);
    tft->drawRoundRect(8, 113, W - 16, 54, 6, C_CARD_BORDER);

    String title = (s40MediaTrackCount > 0) ? s40MediaTracks[s40MediaTrackIdx] : "Chưa có bài hát";
    if (vnStrLen(title) > 33) title = vnSubstr(title, 0, 33);

    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, C_CARD_BG);
    tft->setCursor(14, 120);
    tft->printf("BÀI %02d/%02d [%s]", s40MediaTrackIdx + 1, s40MediaTrackCount,
                (s40MediaTrackCount > 0 && s40MediaFromSd[s40MediaTrackIdx]) ? "THẺ SD" : "MẪU");

    int vol = TestAudio::getSpeakerVolumePct();
    tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
    tft->setCursor(W - 68, 120);
    tft->printf("ÂM:%3d%%", vol);

    tft->setTextColor(C_WHITE, C_CARD_BG);
    tft->setCursor(14, 136);
    tft->print(title);

    const char* visLabels[3] = { "Hiển thị: Đĩa Quay", "Hiển thị: Sóng Sin", "Hiển thị: Cột Sóng" };
    tft->setTextColor(C_NEON_PINK, C_CARD_BG);
    tft->setCursor(14, 151);
    tft->print(visLabels[s40MediaVisMode % 3]);

    tft->setTextColor(s40MediaPlaying ? C_NEON_CYAN : C_NEON_AMBER, C_CARD_BG);
    tft->setCursor(W - 68, 151);
    tft->print(s40MediaPlaying ? "[ĐANG PHÁT]" : "[TẠM DỪNG] ");

    // 3. Thời gian & Thanh trượt Slidebar
    drawMediaProgressAndTimeOnly();

    // 4. Thanh các biểu tượng chức năng điều khiển (Bottom Transport Dock: y = 212..290)
    tft->fillRoundRect(8, 212, W - 16, 78, 7, C_CARD_BG);
    tft->drawRoundRect(8, 212, W - 16, 78, 7, C_CARD_BORDER);

    // Vẽ 5 nút chức năng trực quan: [PREV] [PLAY/PAUSE] [NEXT] [VOL+-] [MENU/VIS]
    const int btnY = 244;
    // Nút 1: PREV (LEFT)
    int b1x = 34;
    tft->fillRoundRect(b1x - 18, btnY - 16, 36, 32, 5, 0x1084);
    tft->drawRoundRect(b1x - 18, btnY - 16, 36, 32, 5, C_NEON_CYAN);
    tft->fillRect(b1x - 9, btnY - 7, 3, 14, C_WHITE);
    tft->fillTriangle(b1x - 5, btnY, b1x + 7, btnY - 7, b1x + 7, btnY + 7, C_NEON_CYAN);

    // Nút 2: PLAY / PAUSE (OK - Nổi bật ở giữa)
    int b2x = 80;
    tft->fillCircle(b2x, btnY, 19, s40MediaPlaying ? 0x1949 : 0x2104);
    tft->drawCircle(b2x, btnY, 19, C_YELLOW);
    tft->drawCircle(b2x, btnY, 18, C_NEON_PINK);
    if (s40MediaPlaying) {
      tft->fillRect(b2x - 6, btnY - 8, 4, 16, C_YELLOW);
      tft->fillRect(b2x + 2, btnY - 8, 4, 16, C_YELLOW);
    } else {
      tft->fillTriangle(b2x - 5, btnY - 9, b2x - 5, btnY + 9, b2x + 9, btnY, C_NEON_GREEN);
    }

    // Nút 3: NEXT (RIGHT)
    int b3x = 126;
    tft->fillRoundRect(b3x - 18, btnY - 16, 36, 32, 5, 0x1084);
    tft->drawRoundRect(b3x - 18, btnY - 16, 36, 32, 5, C_NEON_CYAN);
    tft->fillTriangle(b3x - 7, btnY - 7, b3x - 7, btnY + 7, b3x + 5, btnY, C_NEON_CYAN);
    tft->fillRect(b3x + 6, btnY - 7, 3, 14, C_WHITE);

    // Nút 4: VOLUME (UP/DOWN)
    int b4x = 168;
    tft->fillRoundRect(b4x - 17, btnY - 16, 34, 32, 5, 0x1084);
    tft->drawRoundRect(b4x - 17, btnY - 16, 34, 32, 5, C_NEON_GREEN);
    tft->fillRect(b4x - 11, btnY - 3, 5, 6, C_NEON_GREEN);
    tft->fillTriangle(b4x - 6, btnY - 3, b4x, btnY - 8, b4x, btnY + 8, C_NEON_GREEN);
    int vBars = (vol + 24) / 25;
    for (int v = 0; v < 4; v++) {
      uint16_t vc = (v < vBars) ? C_YELLOW : 0x2124;
      tft->fillRect(b4x + 3 + v * 3, btnY + 5 - (v + 1) * 3, 2, (v + 1) * 3, vc);
    }

    // Nút 5: MODE / LIST (MENU)
    int b5x = 208;
    tft->fillRoundRect(b5x - 17, btnY - 16, 34, 32, 5, 0x1084);
    tft->drawRoundRect(b5x - 17, btnY - 16, 34, 32, 5, C_NEON_PINK);
    for (int r = 0; r < 3; r++) {
      tft->fillRect(b5x - 10, btnY - 7 + r * 6, 4, 3, C_YELLOW);
      tft->fillRect(b5x - 3, btnY - 7 + r * 6, 13, 3, C_NEON_CYAN);
    }

    // Nhãn hướng dẫn phím dưới chân các biểu tượng
    tft->setTextSize(1);
    tft->setTextColor(C_SLATE, C_CARD_BG);
    tft->setCursor(14, 274);
    tft->print("<Lùi   OK:Phát   Tiếp>  Lên/Xuống:Âm");
  }

  // ============================================================================
  // CHẾ ĐỘ 13: ĐỒNG HỒ ĐA NĂNG (BÁO THỨC, BẤM GIỜ, HẸN GIỜ, CÀI ĐẶT GIỜ)
  // ============================================================================
  static uint8_t s40ClockTab = 0; // 0 = Báo thức, 1 = Bấm giờ, 2 = Hẹn giờ, 3 = Cài đặt giờ
  // Trạng thái Báo thức (3 mốc) + Lưu NVS Vĩnh viễn + Hoãn 5 phút (Snooze)
  static uint8_t s40AlarmHour[3]   = { 6, 7, 12 };
  static uint8_t s40AlarmMin[3]    = { 30, 15, 0 };
  static bool    s40AlarmEnable[3] = { true, false, false };
  static int     s40AlarmCursor    = 0;
  static uint8_t s40AlarmEditField = 0; // 0 = Chọn dòng, 1 = Sửa Giờ, 2 = Sửa Phút
  static int     s40LastAlarmTrigMin = -1;
  static bool    s40ClockPrefsLoaded = false;

  // Trạng thái Hoãn báo thức 5 phút (Snooze)
  static bool    s40SnoozeActive   = false;
  static uint8_t s40SnoozeHour     = 0;
  static uint8_t s40SnoozeMin      = 0;
  static uint8_t s40SnoozeAlarmIdx = 0;

  // Trạng thái THÔNG BÁO ĐẨY TOÀN MÀN HÌNH (FULL-SCREEN PUSH ALERT MODAL) cho Báo Thức & Hẹn Giờ
  static bool          s40PushAlertActive      = false;
  static uint8_t       s40PushAlertType        = 0; // 1 = Báo thức (Alarm), 2 = Hẹn giờ (Countdown Timer)
  static uint8_t       s40PushAlertAlarmIdx    = 0; // 0..2
  static unsigned long s40PushAlertStartMs     = 0;
  static unsigned long s40PushAlertLastAnimMs  = 0;
  static unsigned long s40PushAlertLastBeepMs  = 0;
  static uint8_t       s40PushAlertAnimPhase   = 0;
  static String        s40PushAlertTitle       = "";
  static String        s40PushAlertSubTitle    = "";
  static String        s40PushAlertBigDigits   = "00:00";

  // Trạng thái Bấm giờ (Stopwatch)
  static bool          s40SwRunning = false;
  static unsigned long s40SwStartMs = 0;
  static unsigned long s40SwElapsedMs = 0;
  static unsigned long s40SwLaps[3] = { 0, 0, 0 };
  static int           s40SwLapCount = 0;

  // Trạng thái Hẹn giờ đếm ngược (Countdown Timer)
  static bool          s40TimerRunning = false;
  static uint32_t      s40TimerPresetSec = 180; // Mặc định 03:00
  static uint32_t      s40TimerRemainSec = 180;
  static unsigned long s40TimerLastTickMs = 0;

  static void loadClockSuitePrefsIfNeeded() {
    if (s40ClockPrefsLoaded) return;
    s40ClockPrefsLoaded = true;
    Preferences p;
    if (p.begin("s40_clock", true)) {
      for (int i = 0; i < 3; i++) {
        char kh[8], km[8], ke[8];
        snprintf(kh, sizeof(kh), "ah%d", i);
        snprintf(km, sizeof(km), "am%d", i);
        snprintf(ke, sizeof(ke), "ae%d", i);
        s40AlarmHour[i]   = p.getUChar(kh, s40AlarmHour[i]) % 24;
        s40AlarmMin[i]    = p.getUChar(km, s40AlarmMin[i]) % 60;
        s40AlarmEnable[i] = p.getBool(ke, s40AlarmEnable[i]);
      }
      s40AlarmTuneIdx = p.getInt("al_tune", 1) % 4;
      uint32_t savedPreset = p.getUInt("tm_pre", s40TimerPresetSec);
      if (savedPreset >= 10 && savedPreset <= 3600) {
        s40TimerPresetSec = savedPreset;
        if (!s40TimerRunning) s40TimerRemainSec = savedPreset;
      }
      p.end();
    }
  }

  static void saveClockSuitePrefs() {
    Preferences p;
    if (p.begin("s40_clock", false)) {
      for (int i = 0; i < 3; i++) {
        char kh[8], km[8], ke[8];
        snprintf(kh, sizeof(kh), "ah%d", i);
        snprintf(km, sizeof(km), "am%d", i);
        snprintf(ke, sizeof(ke), "ae%d", i);
        p.putUChar(kh, s40AlarmHour[i]);
        p.putUChar(km, s40AlarmMin[i]);
        p.putBool(ke, s40AlarmEnable[i]);
      }
      p.putInt("al_tune", s40AlarmTuneIdx);
      p.putUInt("tm_pre", s40TimerPresetSec);
      p.end();
    }
  }

  // Vẽ Hộp thoại Thông báo Đẩy Toàn Màn Hình (Full-Screen Push Alert) cho Báo thức & Hẹn giờ
  static void drawPushAlertModal(bool fullRedraw = true) {
    if (!tft || !s40PushAlertActive) return;
    int W = tft->width();
    int H = tft->height();

    uint16_t accentCol = (s40PushAlertAnimPhase % 2 == 0)
                           ? ((s40PushAlertType == 1) ? C_NEON_PINK : C_NEON_CYAN)
                           : C_YELLOW;
    uint16_t badgeBg   = (s40PushAlertType == 1) ? 0x6008 : 0x0210;

    if (fullRedraw) {
      // Làm tối nền và vẽ khung Modal nổi bật chính giữa màn hình (240x320)
      tft->fillScreen(0x0841);
      tft->fillRoundRect(8, 12, W - 16, H - 24, 10, C_CARD_BG);
    }

    // Viền Neon đôi nhấp nháy cảnh báo theo nhịp 350ms
    tft->drawRoundRect(8, 12, W - 16, H - 24, 10, accentCol);
    tft->drawRoundRect(10, 14, W - 20, H - 28, 9, (s40PushAlertAnimPhase % 2 == 0) ? C_WHITE : accentCol);

    // Thanh Header của Push Alert
    tft->fillRoundRect(14, 18, W - 28, 30, 6, badgeBg);
    tft->drawRoundRect(14, 18, W - 28, 30, 6, accentCol);
    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, badgeBg);
    int tw = vnStrLen(s40PushAlertTitle) * 6;
    tft->setCursor(max(18, (W - tw) / 2), 29);
    tft->print(s40PushAlertTitle);

    // Vẽ biểu tượng Chuông rung / Đồng hồ cát động ở giữa trên (cx = W/2, cy = 86)
    int cx = W / 2;
    int cy = 84;
    tft->fillRect(cx - 56, cy - 28, 112, 56, C_CARD_BG);
    int shake = (s40PushAlertAnimPhase % 2 == 0) ? -3 : 3;

    // Sóng âm tỏa ra 2 bên
    uint16_t waveCol1 = (s40PushAlertAnimPhase % 2 == 0) ? C_NEON_CYAN : C_SLATE;
    uint16_t waveCol2 = (s40PushAlertAnimPhase % 2 == 1) ? C_NEON_PINK : C_SLATE;
    tft->drawCircleHelper(cx - 18, cy, 14, 0x09, waveCol1);
    tft->drawCircleHelper(cx + 18, cy, 14, 0x06, waveCol1);
    tft->drawCircleHelper(cx - 22, cy, 22, 0x09, waveCol2);
    tft->drawCircleHelper(cx + 22, cy, 22, 0x06, waveCol2);

    if (s40PushAlertType == 1) {
      // Vẽ quả chuông báo thức lắc lư
      tft->fillCircle(cx + shake, cy - 6, 12, C_YELLOW);
      tft->fillRoundRect(cx - 15 + shake, cy - 4, 30, 14, 4, C_YELLOW);
      tft->fillCircle(cx + shake, cy + 14, 4, C_NEON_PINK);
      tft->drawCircle(cx + shake, cy - 18, 3, C_WHITE);
    } else {
      // Vẽ đồng hồ hẹn giờ phát sáng
      tft->fillCircle(cx, cy, 18, 0x1949);
      tft->drawCircle(cx, cy, 18, accentCol);
      tft->drawCircle(cx, cy, 16, C_WHITE);
      tft->drawLine(cx, cy, cx, cy - 10, C_YELLOW);
      tft->drawLine(cx, cy, cx + shake * 3, cy + 4, C_NEON_PINK);
    }

    // Khung số lớn hiển thị Giờ Báo Thức hoặc 00:00
    tft->fillRoundRect(22, 118, W - 44, 58, 8, 0x0841);
    tft->drawRoundRect(22, 118, W - 44, 58, 8, accentCol);
    tft->setTextSize(4);
    tft->setTextColor((s40PushAlertAnimPhase % 2 == 0) ? C_WHITE : C_YELLOW, 0x0841);
    int dW = s40PushAlertBigDigits.length() * 24;
    tft->setCursor(max(26, (W - dW) / 2), 132);
    tft->print(s40PushAlertBigDigits);

    // Dòng mô tả chi tiết & Thời gian thực hiện tại
    int hr, mn, sc, wd, dy, mo, yr;
    getCurrentDateTime(hr, mn, sc, wd, dy, mo, yr);
    tft->setTextSize(1);
    tft->fillRect(16, 184, W - 32, 36, C_CARD_BG);
    tft->setTextColor(C_NEON_GREEN, C_CARD_BG);
    int stW = vnStrLen(s40PushAlertSubTitle) * 6;
    tft->setCursor(max(18, (W - stW) / 2), 188);
    tft->print(s40PushAlertSubTitle);

    char nowBuf[48];
    snprintf(nowBuf, sizeof(nowBuf), "Giờ hệ thống: %02d:%02d:%02d - %02d/%02d/%04d", hr, mn, sc, dy, mo, yr);
    tft->setTextColor(C_NEON_CYAN, C_CARD_BG);
    int nbW = vnStrLen(String(nowBuf)) * 6;
    tft->setCursor(max(18, (W - nbW) / 2), 204);
    tft->print(nowBuf);

    // 2 Nút thao tác lớn ở đáy Modal (TẮT CHUÔNG / HOÃN 5 PHÚT)
    tft->fillRoundRect(16, 228, W - 32, 34, 6, C_NEON_GREEN);
    tft->drawRoundRect(16, 228, W - 32, 34, 6, C_WHITE);
    tft->setTextColor(C_BLACK, C_NEON_GREEN);
    const char* btnOkTxt = "[OK] : TẮT CHUÔNG BÁO NGAY";
    tft->setCursor((W - vnStrLen(btnOkTxt) * 6) / 2, 241);
    tft->print(btnOkTxt);

    tft->fillRoundRect(16, 268, W - 32, 28, 6, 0x1949);
    tft->drawRoundRect(16, 268, W - 32, 28, 6, C_NEON_CYAN);
    tft->setTextColor(C_YELLOW, 0x1949);
    const char* btnSnoozeTxt = (s40PushAlertType == 1)
                                 ? "[TRÁI/PHẢI/EXIT] : HOÃN BÁO THỨC (+5 PHÚT)"
                                 : "[TRÁI/PHẢI/EXIT] : ĐẾM THÊM +1 PHÚT";
    tft->setCursor(max(18, (W - vnStrLen(btnSnoozeTxt) * 6) / 2), 278);
    tft->print(btnSnoozeTxt);
  }

  static void triggerPushAlertModal(uint8_t alertType, uint8_t alarmIdx, const String& title, const String& subTitle, const String& bigDigits) {
    // Đánh thức màn hình ngay lập tức kể cả khi đang ở AOD hoặc Sleep!
    isScreenSleeping = false;
    lastUserActivityMs = millis();
    setBacklightBrightness(100);

    s40PushAlertActive     = true;
    s40PushAlertType       = alertType;
    s40PushAlertAlarmIdx   = alarmIdx;
    s40PushAlertStartMs    = millis();
    s40PushAlertLastAnimMs = millis();
    s40PushAlertLastBeepMs = 0;
    s40PushAlertAnimPhase  = 0;
    s40PushAlertTitle      = title;
    s40PushAlertSubTitle   = subTitle;
    s40PushAlertBigDigits  = bigDigits;

    drawPushAlertModal(true);
    if (alertType == 1) {
      TestAudio::playAlarmTuneStep(s40AlarmTuneIdx, 0);
    } else {
      TestAudio::playSmsSpecialTone();
    }
  }

  // Trạng thái Cài đặt thời gian thủ công
  static int s40TimeEditField = 0; // 0=Giờ, 1=Phút, 2=Ngày, 3=Tháng, 4=Năm
  static int s40EditHour = 12, s40EditMin = 0, s40EditDay = 28, s40EditMonth = 9, s40EditYear = 2026;
  static bool s40EditInitialized = false;

  static void initManualTimeFieldsFromSystem() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5)) {
      s40EditHour  = timeinfo.tm_hour;
      s40EditMin   = timeinfo.tm_min;
      s40EditDay   = timeinfo.tm_mday;
      s40EditMonth = timeinfo.tm_mon + 1;
      s40EditYear  = timeinfo.tm_year + 1900;
    }
    s40EditInitialized = true;
  }

  static void applyManualTimeToSystem() {
    struct tm t = {0};
    t.tm_year = s40EditYear - 1900;
    t.tm_mon  = s40EditMonth - 1;
    t.tm_mday = s40EditDay;
    t.tm_hour = s40EditHour;
    t.tm_min  = s40EditMin;
    t.tm_sec  = 0;
    time_t epoch = mktime(&t);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
  }

  static void drawClockSuiteAppScreen(bool fullRedraw = true) {
    if (!tft) return;
    loadClockSuitePrefsIfNeeded();
    int W = tft->width();
    if (!s40EditInitialized) initManualTimeFieldsFromSystem();

    if (fullRedraw) {
      tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
      drawSymbianChrome("ĐỒNG HỒ & BÁO THỨC", "[MENU:ĐổiMục]", "[OK:Chọn]", "[EXIT:Menu]");
    }

    // 1. Thanh 4 Tab trên cùng (y = 26..50)
    const char* tabNames[4] = { "BÁO THỨC", "BẤM GIỜ", "HẸN GIỜ", "CÀI GIỜ" };
    for (int i = 0; i < 4; i++) {
      int tx = 4 + i * 58;
      bool active = (s40ClockTab == i);
      tft->fillRoundRect(tx, 26, 56, 22, 4, active ? 0x1949 : C_CARD_BG);
      tft->drawRoundRect(tx, 26, 56, 22, 4, active ? C_NEON_CYAN : C_CARD_BORDER);
      tft->setTextSize(1);
      tft->setTextColor(active ? C_YELLOW : C_SLATE, active ? 0x1949 : C_CARD_BG);
      int tw = vnStrLen(String(tabNames[i])) * 6;
      tft->setCursor(tx + (56 - tw) / 2, 33);
      tft->print(tabNames[i]);
    }

    if (fullRedraw) {
      tft->fillRect(4, 52, W - 8, 240, C_DARK_BG);
    }

    if (s40ClockTab == 0) {
      // --- TAB 0: BÁO THỨC (ALARM CLOCK) ---
      tft->setTextSize(1);
      tft->fillRect(6, 54, W - 12, 14, C_DARK_BG);
      tft->setTextColor(s40AlarmEditField > 0 ? C_YELLOW : C_NEON_CYAN, C_DARK_BG);
      tft->setCursor(8, 56);
      if (s40AlarmCursor == 3) {
        tft->print("CHỌN CHUÔNG: [TRÁI/PHẢI] Đổi Chuông | [OK] Nghe thử");
      } else if (s40AlarmEditField == 1) {
        tft->print("ĐANG CHỈNH GIỜ: [LÊN/XUỐNG] +-1h | [OK] Sang Phút");
      } else if (s40AlarmEditField == 2) {
        tft->print("CHỈNH PHÚT: [LÊN/XUỐNG] +-1p, [TRÁI/PHẢI] +-5p");
      } else if (s40SnoozeActive) {
        tft->printf("Đang hoãn chuông -> Sẽ kêu lại lúc %02u:%02u", s40SnoozeHour, s40SnoozeMin);
      } else {
        tft->print("[OK]: Chỉnh Giờ/Phút | [TRÁI/PHẢI]: Bật/Tắt nhanh");
      }

      for (int i = 0; i < 3; i++) {
        int ry = 70 + i * 58;
        bool sel = (i == s40AlarmCursor);
        uint16_t cardBg = sel ? 0x1949 : C_CARD_BG;
        tft->fillRoundRect(8, ry, W - 16, 52, 6, cardBg);
        tft->drawRoundRect(8, ry, W - 16, 52, 6, sel ? (s40AlarmEditField > 0 ? C_YELLOW : C_NEON_CYAN) : C_CARD_BORDER);

        // Vẽ khung nổi bật quanh phần Giờ hoặc Phút nếu đang chỉnh trực tiếp
        if (sel && s40AlarmEditField == 1) {
          tft->fillRoundRect(15, ry + 8, 28, 22, 4, C_NEON_PINK);
        } else if (sel && s40AlarmEditField == 2) {
          tft->fillRoundRect(51, ry + 8, 28, 22, 4, C_NEON_PINK);
        }

        char hStr[6], mStr[6];
        snprintf(hStr, sizeof(hStr), "%02u", s40AlarmHour[i]);
        snprintf(mStr, sizeof(mStr), "%02u", s40AlarmMin[i]);

        tft->setTextSize(2);
        uint16_t baseTimeCol = s40AlarmEnable[i] ? C_YELLOW : C_SLATE;
        tft->setTextColor((sel && s40AlarmEditField == 1) ? C_WHITE : baseTimeCol,
                          (sel && s40AlarmEditField == 1) ? C_NEON_PINK : cardBg);
        tft->setCursor(18, ry + 11);
        tft->print(hStr);

        tft->setTextColor(baseTimeCol, cardBg);
        tft->setCursor(42, ry + 11);
        tft->print(":");

        tft->setTextColor((sel && s40AlarmEditField == 2) ? C_WHITE : baseTimeCol,
                          (sel && s40AlarmEditField == 2) ? C_NEON_PINK : cardBg);
        tft->setCursor(54, ry + 11);
        tft->print(mStr);

        tft->setTextSize(1);
        tft->setTextColor(C_WHITE, cardBg);
        tft->setCursor(18, ry + 35);
        if (sel && s40AlarmEditField == 1) {
          tft->print(">> Đang sửa GIỜ (Bấm OK sang Phút)");
        } else if (sel && s40AlarmEditField == 2) {
          tft->print(">> Đang sửa PHÚT (Bấm OK để Lưu)");
        } else {
          tft->printf("Báo thức #%d (Chuông đẩy toàn màn hình)", i + 1);
        }

        // Nút gạt BẬT/TẮT
        uint16_t badgeCol = s40AlarmEnable[i] ? C_NEON_GREEN : 0x2124;
        tft->fillRoundRect(W - 72, ry + 14, 52, 24, 5, badgeCol);
        tft->drawRoundRect(W - 72, ry + 14, 52, 24, 5, C_WHITE);
        tft->setTextColor(s40AlarmEnable[i] ? C_BLACK : C_SLATE, badgeCol);
        tft->setCursor(W - 58, ry + 22);
        tft->print(s40AlarmEnable[i] ? "BẬT" : "TẮT");
      }

      // 4. Mục chọn Nhạc chuông Báo thức (Alarm Ringtone)
      int ryTune = 250;
      bool selTune = (s40AlarmCursor == 3);
      uint16_t tuneBg = selTune ? 0x1949 : C_CARD_BG;
      tft->fillRoundRect(8, ryTune, W - 16, 40, 5, tuneBg);
      tft->drawRoundRect(8, ryTune, W - 16, 40, 5, selTune ? C_NEON_CYAN : C_CARD_BORDER);

      tft->setTextSize(1);
      tft->setTextColor(selTune ? C_YELLOW : C_SLATE, tuneBg);
      tft->setCursor(14, ryTune + 6);
      tft->print("Nhạc chuông báo thức:");

      tft->setTextColor(selTune ? C_NEON_GREEN : C_NEON_CYAN, tuneBg);
      tft->setCursor(14, ryTune + 22);
      tft->printf("< %s >", TestAudio::getAlarmTuneName(s40AlarmTuneIdx));

      tft->fillRoundRect(W - 68, ryTune + 8, 54, 24, 4, selTune ? C_NEON_PINK : 0x2124);
      tft->setTextColor(C_WHITE, selTune ? C_NEON_PINK : 0x2124);
      tft->setCursor(W - 58, ryTune + 16);
      tft->print("Nghe");

      drawSymbianSoftkeys("[MENU:Tab]",
                          s40AlarmCursor == 3 ? "[OK:NgheThử]" :
                          (s40AlarmEditField == 0 ? "[OK:ChỉnhGiờ]" : (s40AlarmEditField == 1 ? "[OK:SangPhút]" : "[OK:Lưu&Bật]")),
                          s40AlarmEditField > 0 ? "[EXIT:Xong]" : "[EXIT:Menu]");
    } else if (s40ClockTab == 1) {
      // --- TAB 1: BẤM GIỜ THỂ THAO (STOPWATCH) ---
      unsigned long totalMs = s40SwElapsedMs + (s40SwRunning ? (millis() - s40SwStartMs) : 0);
      unsigned int mins = (totalMs / 60000UL) % 100;
      unsigned int secs = (totalMs / 1000UL) % 60;
      unsigned int csec = (totalMs / 10UL) % 100;

      tft->fillRoundRect(8, 56, W - 16, 86, 8, C_CARD_BG);
      tft->drawRoundRect(8, 56, W - 16, 86, 8, s40SwRunning ? C_NEON_GREEN : C_NEON_CYAN);

      tft->setTextSize(1);
      tft->setTextColor(s40SwRunning ? C_NEON_GREEN : C_YELLOW, C_CARD_BG);
      tft->setCursor(18, 64);
      tft->print(s40SwRunning ? "ĐANG BẤM GIỜ THỂ THAO..." : "ĐỒNG HỒ BẤM GIỜ (SẴN SÀNG)");

      char swBuf[16];
      snprintf(swBuf, sizeof(swBuf), "%02u:%02u.%02u", mins, secs, csec);
      tft->setTextSize(3);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(32, 86);
      tft->print(swBuf);

      tft->setTextSize(1);
      tft->setTextColor(C_NEON_PINK, C_CARD_BG);
      tft->setCursor(18, 124);
      tft->print("[OK]: Chạy/Dừng    [LÊN]: Lưu vòng / Đặt lại");

      // Bảng lưu 3 vòng chạy (Laps)
      tft->fillRoundRect(8, 150, W - 16, 134, 6, C_CARD_BG);
      tft->drawRoundRect(8, 150, W - 16, 134, 6, C_CARD_BORDER);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(16, 160);
      tft->print("LỊCH SỬ CÁC VÒNG ĐO:");

      for (int i = 0; i < 3; i++) {
        int ly = 180 + i * 32;
        tft->fillRoundRect(16, ly, W - 32, 26, 4, 0x0841);
        tft->setTextColor(C_NEON_CYAN, 0x0841);
        tft->setCursor(24, ly + 9);
        if (s40SwLaps[i] > 0) {
          unsigned long lm = s40SwLaps[i];
          tft->printf("VÒNG ĐO #%d :      %02lu:%02lu.%02lu", i + 1,
                      (lm / 60000UL) % 100, (lm / 1000UL) % 60, (lm / 10UL) % 100);
        } else {
          tft->printf("VÒNG ĐO #%d :      --:--.--", i + 1);
        }
      }
      drawSymbianSoftkeys("[MENU:Mục]", s40SwRunning ? "[OK:Dừng]" : "[OK:Chạy]", "[EXIT:Menu]");
    } else if (s40ClockTab == 2) {
      // --- TAB 2: HẸN GIỜ ĐẾM NGƯỢC (COUNTDOWN TIMER) ---
      tft->fillRoundRect(8, 56, W - 16, 136, 8, C_CARD_BG);
      tft->drawRoundRect(8, 56, W - 16, 136, 8, s40TimerRunning ? C_NEON_PINK : C_NEON_CYAN);

      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(16, 66);
      tft->print(s40TimerRunning ? "ĐANG ĐẾM NGƯỢC THỜI GIAN..." : "HẸN GIỜ ĐẾM NGƯỢC (LÊN/XUỐNG: Phút)");

      char tmBuf[16];
      snprintf(tmBuf, sizeof(tmBuf), "%02u:%02u", (unsigned)(s40TimerRemainSec / 60), (unsigned)(s40TimerRemainSec % 60));
      tft->setTextSize(4);
      tft->setTextColor((s40TimerRemainSec == 0) ? C_NEON_PINK : C_NEON_CYAN, C_CARD_BG);
      tft->setCursor(52, 92);
      tft->print(tmBuf);

      // Thanh tiến trình rút dần
      int barX = 22, barY = 148, barW = W - 44;
      tft->fillRoundRect(barX, barY, barW, 12, 4, 0x2124);
      uint32_t denom = (s40TimerPresetSec > 0) ? s40TimerPresetSec : 1;
      int fillW = (int)((uint64_t)s40TimerRemainSec * barW / denom);
      fillW = constrain(fillW, 0, barW);
      if (fillW > 0) {
        tft->fillRoundRect(barX, barY, fillW, 12, 4, C_NEON_GREEN);
      }
      tft->drawRoundRect(barX, barY, barW, 12, 4, C_WHITE);

      tft->setTextSize(1);
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(18, 170);
      tft->printf("Mức hẹn đặt: %02u phút %02u giây", (unsigned)(s40TimerPresetSec / 60), (unsigned)(s40TimerPresetSec % 60));

      // Hộp hướng dẫn phím bên dưới
      tft->fillRoundRect(8, 200, W - 16, 84, 6, C_CARD_BG);
      tft->drawRoundRect(8, 200, W - 16, 84, 6, C_CARD_BORDER);
      tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
      tft->setCursor(14, 212);
      tft->print("ĐIỀU KHIỂN HẸN GIỜ ĐẾM NGƯỢC:");
      tft->setTextColor(C_WHITE, C_CARD_BG);
      tft->setCursor(14, 230);
      tft->print("+ [OK]: Bắt đầu / Tạm dừng đếm ngược");
      tft->setCursor(14, 246);
      tft->print("+ [LÊN/XUỐNG]: +-1 Phút | [TRÁI/PHẢI]: +-10 Giây");
      tft->setCursor(14, 262);
      tft->print("+ Về 00:00: Bật Chuông Đẩy Toàn Màn Hình!");
      drawSymbianSoftkeys("[MENU:Tab]", s40TimerRunning ? "[OK:TạmDừng]" : "[OK:BắtĐầu]", "[EXIT:Menu]");
    } else {
      // --- TAB 3: CÀI ĐẶT THỜI GIAN & NGÀY THÁNG THỦ CÔNG ---
      tft->fillRoundRect(8, 56, W - 16, 228, 8, C_CARD_BG);
      tft->drawRoundRect(8, 56, W - 16, 228, 8, C_NEON_CYAN);

      tft->setTextSize(1);
      tft->setTextColor(C_YELLOW, C_CARD_BG);
      tft->setCursor(16, 66);
      tft->print("CÀI ĐẶT NGÀY GIỜ HỆ THỐNG:");

      const char* fLabels[5] = {
        "1. Giờ hiện tại (0..23)",
        "2. Phút hiện tại (0..59)",
        "3. Ngày trong tháng (1..31)",
        "4. Tháng trong năm (1..12)",
        "5. Năm dương lịch"
      };
      int fVals[5] = { s40EditHour, s40EditMin, s40EditDay, s40EditMonth, s40EditYear };

      for (int i = 0; i < 5; i++) {
        int ry = 86 + i * 36;
        bool sel = (i == s40TimeEditField);
        tft->fillRoundRect(16, ry, W - 32, 30, 5, sel ? 0x1949 : 0x0841);
        tft->drawRoundRect(16, ry, W - 32, 30, 5, sel ? C_NEON_CYAN : 0x2124);
        tft->setTextColor(sel ? C_YELLOW : C_WHITE, sel ? 0x1949 : 0x0841);
        tft->setCursor(24, ry + 11);
        tft->print(fLabels[i]);

        tft->setTextColor(C_NEON_GREEN, sel ? 0x1949 : 0x0841);
        tft->setCursor(W - 68, ry + 11);
        tft->printf("< %02d >", fVals[i]);
      }
      drawSymbianSoftkeys("[MENU:Mục]", "[OK:ĐổiÔ]", "[EXIT:Lưu&Ra]");
    }
  }

  // ============================================================================
  // CHẾ ĐỘ 14: LỊCH VẠN NIÊN - BẢNG LỊCH THÁNG (7 CỘT T2..CN x 6 HÀNG)
  // ============================================================================
  static int s40CalMonth = 9;
  static int s40CalYear  = 2026;
  static bool s40CalInitialized = false;

  static int getDaysInMonth(int m, int y) {
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    if (m == 2) {
      bool leap = ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
      return leap ? 29 : 28;
    }
    return 31;
  }

  // Trả về thứ của ngày mday/month/year: 0 = Thứ Hai (T2) ... 6 = Chủ Nhật (CN)
  static int getDayOfWeekMon0(int d, int m, int y) {
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    int dowSun0 = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7; // 0 = CN, 1 = T2..6 = T7
    return (dowSun0 + 6) % 7; // Chuyển sang 0 = T2 .. 6 = CN
  }

  static void drawCalendarAppScreen(bool fullRedraw = true) {
    if (!tft) return;
    int W = tft->width();

    int todayD = 28, todayM = 9, todayY = 2026;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5)) {
      todayD = timeinfo.tm_mday;
      todayM = timeinfo.tm_mon + 1;
      todayY = timeinfo.tm_year + 1900;
    }
    if (!s40CalInitialized) {
      s40CalMonth = todayM;
      s40CalYear  = todayY;
      s40CalInitialized = true;
    }

    if (fullRedraw) {
      tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
      drawSymbianChrome("LỊCH VẠN NIÊN", "[OK:HômNay]", "<Tháng/Năm>", "[EXIT:Menu]");
    }

    // 1. Thanh Tiêu đề Tháng / Năm (y = 26..54)
    tft->fillRoundRect(6, 26, W - 12, 28, 5, 0x1949);
    tft->drawRoundRect(6, 26, W - 12, 28, 5, C_NEON_CYAN);
    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, 0x1949);
    tft->setCursor(16, 32);
    tft->printf("<  THÁNG %02d / NĂM %04d  >", s40CalMonth, s40CalYear);
    tft->setTextColor(C_NEON_GREEN, 0x1949);
    tft->setCursor(16, 43);
    tft->printf("Hôm nay: Ngày %02d/%02d/%04d", todayD, todayM, todayY);

    // 2. Hàng tiêu đề 7 Thứ trong tuần (T2 .. CN)
    const char* wkNames[7] = { "T2", "T3", "T4", "T5", "T6", "T7", "CN" };
    const int colW = 32;
    const int startX = 8;
    tft->fillRoundRect(6, 58, W - 12, 20, 4, C_CARD_BG);
    for (int c = 0; c < 7; c++) {
      uint16_t hCol = (c == 5) ? C_NEON_CYAN : ((c == 6) ? C_NEON_PINK : C_YELLOW);
      tft->setTextColor(hCol, C_CARD_BG);
      tft->setCursor(startX + c * colW + 8, 64);
      tft->print(wkNames[c]);
    }

    // 3. Lưới các ngày trong tháng (6 hàng x 7 cột)
    tft->fillRoundRect(6, 80, W - 12, 212, 6, C_CARD_BG);
    tft->drawRoundRect(6, 80, W - 12, 212, 6, C_CARD_BORDER);

    int firstDow = getDayOfWeekMon0(1, s40CalMonth, s40CalYear);
    int totalDays = getDaysInMonth(s40CalMonth, s40CalYear);

    int dayNum = 1;
    for (int r = 0; r < 6; r++) {
      int ry = 86 + r * 33;
      for (int c = 0; c < 7; c++) {
        int cellIdx = r * 7 + c;
        if (cellIdx < firstDow || dayNum > totalDays) continue;

        int cx = startX + c * colW;
        bool isToday = (dayNum == todayD && s40CalMonth == todayM && s40CalYear == todayY);

        if (isToday) {
          tft->fillRoundRect(cx + 1, ry, colW - 2, 28, 5, C_NEON_PINK);
          tft->drawRoundRect(cx + 1, ry, colW - 2, 28, 5, C_YELLOW);
        } else {
          tft->drawRoundRect(cx + 2, ry + 1, colW - 4, 26, 4, 0x18E5);
        }

        uint16_t dCol = isToday ? C_WHITE : ((c == 5) ? C_NEON_CYAN : ((c == 6) ? C_NEON_PINK : C_WHITE));
        tft->setTextSize(1);
        tft->setTextColor(dCol, isToday ? C_NEON_PINK : C_CARD_BG);
        tft->setCursor(cx + (dayNum < 10 ? 11 : 8), ry + 10);
        tft->print(dayNum);

        dayNum++;
      }
    }
  }

  // ============================================================================
  // CHẾ ĐỘ 15: TRUNG TÂM TRÒ CHƠI GIẢI TRÍ (GAME CENTER: RẮN SĂN MỒI, FLAPPY BIRD, XẾP GẠCH TETRIS)
  // Sử dụng kỹ thuật kết xuất 3 dải (Triple-Band Offscreen Canvas) trên mediaVisCanvas (224x82) -> 0 tốn thêm RAM, 0% nháy hình!
  // ============================================================================
  static uint8_t s40GameActiveId   = 0; // 0 = Sảnh chọn Game, 1 = Rắn Săn Mồi, 2 = Flappy Bird, 3 = Xếp Gạch (Tetris)
  static int     s40GameMenuCursor = 0; // 0..2 trong Sảnh chọn Game
  static unsigned long s40GameLastTickMs = 0;

  // --- TRẠNG THÁI GAME 1: RẮN SĂN MỒI (SNAKE XENZIA) ---
  static const int SNAKE_COLS = 20;
  static const int SNAKE_ROWS = 17;
  static int8_t  snakeX[80], snakeY[80];
  static int     snakeLen = 4;
  static int8_t  snakeDirX = 1, snakeDirY = 0;
  static int8_t  snakeNextDirX = 1, snakeNextDirY = 0;
  static int8_t  snakeFoodX = 14, snakeFoodY = 8;
  static int     snakeScore = 0, snakeBestScore = 0;
  static bool    snakeGameOver = false, snakePaused = false;

  static void spawnSnakeFood() {
    for (int tries = 0; tries < 50; tries++) {
      int fx = random(0, SNAKE_COLS);
      int fy = random(0, SNAKE_ROWS);
      bool onBody = false;
      for (int i = 0; i < snakeLen; i++) {
        if (snakeX[i] == fx && snakeY[i] == fy) { onBody = true; break; }
      }
      if (!onBody) {
        snakeFoodX = fx;
        snakeFoodY = fy;
        return;
      }
    }
  }

  static void initSnakeGame() {
    snakeLen = 4;
    for (int i = 0; i < snakeLen; i++) {
      snakeX[i] = 8 - i;
      snakeY[i] = 8;
    }
    snakeDirX = 1; snakeDirY = 0;
    snakeNextDirX = 1; snakeNextDirY = 0;
    snakeScore = 0;
    snakeGameOver = false;
    snakePaused = false;
    spawnSnakeFood();
    s40GameLastTickMs = millis();
  }

  // --- TRẠNG THÁI GAME 2: CHIM VỖ CÁNH (FLAPPY CYBER BIRD) ---
  static float   flappyY = 100.0f, flappyVy = 0.0f;
  static float   flappyPipeX[2] = { 160.0f, 276.0f };
  static int16_t flappyPipeGapY[2] = { 96, 128 };
  static bool    flappyPipePassed[2] = { false, false };
  static int     flappyScore = 0, flappyBestScore = 0;
  static bool    flappyGameOver = false, flappyPaused = false;
  static uint8_t flappyGroundOffset = 0;

  static void initFlappyGame() {
    flappyY = 102.0f;
    flappyVy = -1.5f;
    flappyPipeX[0] = 168.0f;
    flappyPipeGapY[0] = random(68, 148);
    flappyPipePassed[0] = false;
    flappyPipeX[1] = 284.0f;
    flappyPipeGapY[1] = random(68, 148);
    flappyPipePassed[1] = false;
    flappyScore = 0;
    flappyGameOver = false;
    flappyPaused = false;
    s40GameLastTickMs = millis();
  }

  // --- TRẠNG THÁI GAME 3: XẾP GẠCH CỔ ĐIỂN (TETRIS 10x18) ---
  static const int TETRIS_COLS = 10;
  static const int TETRIS_ROWS = 18;
  static uint8_t tetrisBoard[TETRIS_ROWS][TETRIS_COLS];
  static int     tetrisCurType = 0, tetrisCurRot = 0, tetrisCurX = 3, tetrisCurY = 0;
  static int     tetrisNextType = 1;
  static int     tetrisScore = 0, tetrisBestScore = 0, tetrisLines = 0;
  static bool    tetrisGameOver = false, tetrisPaused = false;
  static unsigned long tetrisLastFallMs = 0;

  static bool s40GameScoresLoaded = false;
  static void loadGameScoresPrefsIfNeeded() {
    if (s40GameScoresLoaded) return;
    s40GameScoresLoaded = true;
    Preferences p;
    if (p.begin("s40_games", true)) {
      snakeBestScore  = p.getInt("snake", 0);
      flappyBestScore = p.getInt("flappy", 0);
      tetrisBestScore = p.getInt("tetris", 0);
      p.end();
    }
  }

  static void saveGameScoresPrefs() {
    Preferences p;
    if (p.begin("s40_games", false)) {
      p.putInt("snake", snakeBestScore);
      p.putInt("flappy", flappyBestScore);
      p.putInt("tetris", tetrisBestScore);
      p.end();
    }
  }

  // Bảng ma trận 4x4 (16-bit) cho 7 khối gạch chuẩn (I, O, T, S, Z, J, L) x 4 góc xoay
  static const uint16_t TETRIS_SHAPES[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 }, // I
    { 0x6600, 0x6600, 0x6600, 0x6600 }, // O
    { 0x0E40, 0x4C40, 0x4E00, 0x4640 }, // T
    { 0x06C0, 0x8C40, 0x6C00, 0x4620 }, // S
    { 0x0C60, 0x4C80, 0xC600, 0x2640 }, // Z
    { 0x0E20, 0x44C0, 0x8E00, 0x6440 }, // J
    { 0x0E80, 0xC440, 0x2E00, 0x4460 }  // L
  };
  static const uint16_t TETRIS_COLORS[8] = {
    0x0000, 0x07FF, 0xFFE0, 0xA2B7, 0x07E0, 0xF292, 0x3DF7, 0xFD20
  };

  static bool tetrisCollision(int type, int rot, int px, int py) {
    uint16_t mask = TETRIS_SHAPES[type % 7][rot % 4];
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if ((mask >> (15 - (r * 4 + c))) & 1) {
          int bx = px + c;
          int by = py + r;
          if (bx < 0 || bx >= TETRIS_COLS || by >= TETRIS_ROWS) return true;
          if (by >= 0 && tetrisBoard[by][bx] != 0) return true;
        }
      }
    }
    return false;
  }

  static void spawnNextTetrisPiece() {
    tetrisCurType = tetrisNextType;
    tetrisNextType = random(0, 7);
    tetrisCurRot = 0;
    tetrisCurX = 3;
    tetrisCurY = 0;
    if (tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX, tetrisCurY)) {
      tetrisGameOver = true;
      saveGameScoresPrefs();
    }
  }

  static void initTetrisGame() {
    memset(tetrisBoard, 0, sizeof(tetrisBoard));
    tetrisScore = 0;
    tetrisLines = 0;
    tetrisGameOver = false;
    tetrisPaused = false;
    tetrisNextType = random(0, 7);
    spawnNextTetrisPiece();
    tetrisLastFallMs = millis();
    s40GameLastTickMs = millis();
  }

  static void lockAndClearTetrisPiece() {
    uint16_t mask = TETRIS_SHAPES[tetrisCurType % 7][tetrisCurRot % 4];
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if ((mask >> (15 - (r * 4 + c))) & 1) {
          int bx = tetrisCurX + c;
          int by = tetrisCurY + r;
          if (by >= 0 && by < TETRIS_ROWS && bx >= 0 && bx < TETRIS_COLS) {
            tetrisBoard[by][bx] = (uint8_t)(tetrisCurType + 1);
          }
        }
      }
    }
    // Kiểm tra và xóa các hàng đã lấp đầy
    int cleared = 0;
    for (int r = TETRIS_ROWS - 1; r >= 0; r--) {
      bool full = true;
      for (int c = 0; c < TETRIS_COLS; c++) {
        if (tetrisBoard[r][c] == 0) { full = false; break; }
      }
      if (full) {
        cleared++;
        for (int kr = r; kr > 0; kr--) {
          for (int c = 0; c < TETRIS_COLS; c++) {
            tetrisBoard[kr][c] = tetrisBoard[kr - 1][c];
          }
        }
        for (int c = 0; c < TETRIS_COLS; c++) tetrisBoard[0][c] = 0;
        r++; // Kiểm tra lại hàng vừa dồn xuống
      }
    }
    if (cleared > 0) {
      const int bonus[5] = { 0, 100, 300, 500, 800 };
      tetrisLines += cleared;
      tetrisScore += bonus[min(4, cleared)];
      if (tetrisScore > tetrisBestScore) tetrisBestScore = tetrisScore;
    }
    spawnNextTetrisPiece();
  }

  // Vẽ nội dung khung chơi Game (224 x 216) với độ lệch dải yOff (0, 72, 144) lên mediaVisCanvas
  static void renderGameBandToCanvas(VnCanvas16* cv, int yOff) {
    cv->fillScreen(0x0842);
    cv->drawRoundRect(0, -yOff, 224, 216, 6, C_NEON_CYAN);

    if (s40GameActiveId == 1) {
      // --- VẼ GAME 1: RẮN SĂN MỒI (SNAKE XENZIA) ---
      cv->fillRect(2, 2 - yOff, 220, 20, 0x10A4);
      cv->drawFastHLine(2, 22 - yOff, 220, C_NEON_CYAN);
      cv->setTextSize(1);
      cv->setTextColor(C_YELLOW);
      cv->setCursor(8, 8 - yOff);
      cv->printf("RẮN SĂN MỒI | Điểm:%d", snakeScore);
      cv->setTextColor(C_NEON_GREEN);
      cv->setCursor(150, 8 - yOff);
      cv->printf("Kỷ lục:%d", snakeBestScore);

      const int gx0 = 12, gy0 = 28, cell = 10;
      cv->fillRect(gx0 - 1, gy0 - 1 - yOff, SNAKE_COLS * cell + 2, SNAKE_ROWS * cell + 2, C_BLACK);
      cv->drawRect(gx0 - 1, gy0 - 1 - yOff, SNAKE_COLS * cell + 2, SNAKE_ROWS * cell + 2, 0x2965);

      // Vẽ chấm lưới mờ
      for (int r = 0; r < SNAKE_ROWS; r += 2) {
        for (int c = 0; c < SNAKE_COLS; c += 2) {
          cv->drawPixel(gx0 + c * cell + 4, gy0 + r * cell + 4 - yOff, 0x1082);
        }
      }

      // Vẽ Mồi năng lượng (Quả táo đỏ + tâm vàng sáng)
      int fx = gx0 + snakeFoodX * cell;
      int fy = gy0 + snakeFoodY * cell - yOff;
      cv->fillCircle(fx + 4, fy + 4, 4, C_NEON_PINK);
      cv->drawCircle(fx + 4, fy + 4, 4, C_YELLOW);
      cv->fillRect(fx + 4, fy - 1, 2, 2, C_NEON_GREEN);

      // Vẽ Thân & Đầu Rắn
      for (int i = snakeLen - 1; i >= 0; i--) {
        int sx = gx0 + snakeX[i] * cell;
        int sy = gy0 + snakeY[i] * cell - yOff;
        if (i == 0) {
          cv->fillRoundRect(sx, sy, cell - 1, cell - 1, 3, C_YELLOW);
          cv->fillCircle(sx + 3, sy + 3, 1, C_BLACK);
          cv->fillCircle(sx + 6, sy + 3, 1, C_BLACK);
        } else {
          uint16_t bCol = (i % 2 == 0) ? C_NEON_GREEN : C_NEON_CYAN;
          cv->fillRoundRect(sx + 1, sy + 1, cell - 2, cell - 2, 2, bCol);
        }
      }

      if (snakeGameOver || snakePaused) {
        cv->fillRoundRect(26, 82 - yOff, 172, 54, 6, 0x1949);
        cv->drawRoundRect(26, 82 - yOff, 172, 54, 6, C_YELLOW);
        cv->setTextColor(snakeGameOver ? C_NEON_PINK : C_YELLOW);
        cv->setCursor(44, 94 - yOff);
        cv->print(snakeGameOver ? "THUA CUỘC! (GAME OVER)" : "ĐANG TẠM DỪNG GAME");
        cv->setTextColor(C_WHITE);
        cv->setCursor(38, 114 - yOff);
        cv->print(snakeGameOver ? "Bấm [OK] để Chơi lại ngay" : "Bấm [OK] để Tiếp tục chơi");
      }
    } else if (s40GameActiveId == 2) {
      // --- VẼ GAME 2: CHIM VỖ CÁNH (FLAPPY CYBER BIRD) ---
      // Nền bầu trời đêm + các vì sao
      for (int s = 0; s < 12; s++) {
        int sx = (s * 37 + 15) % 216;
        int sy = 30 + ((s * 23) % 140) - yOff;
        cv->drawPixel(sx, sy, (s % 3 == 0) ? C_YELLOW : C_WHITE);
      }

      // Vẽ 2 cặp Ống nước Neon
      const int pipeW = 26, gapH = 58;
      for (int p = 0; p < 2; p++) {
        int px = (int)flappyPipeX[p];
        int topH = flappyPipeGapY[p] - gapH / 2;
        int botY = flappyPipeGapY[p] + gapH / 2;
        int botH = 198 - botY;

        if (px + pipeW >= 2 && px < 222) {
          // Ống trên
          if (topH > 24) {
            cv->fillRect(px, 24 - yOff, pipeW, topH - 24, 0x05E8);
            cv->drawRect(px, 24 - yOff, pipeW, topH - 24, C_NEON_GREEN);
            cv->fillRect(px - 2, topH - 8 - yOff, pipeW + 4, 8, C_NEON_GREEN);
          }
          // Ống dưới
          if (botH > 0) {
            cv->fillRect(px, botY - yOff, pipeW, botH, 0x05E8);
            cv->drawRect(px, botY - yOff, pipeW, botH, C_NEON_GREEN);
            cv->fillRect(px - 2, botY - yOff, pipeW + 4, 8, C_NEON_GREEN);
          }
        }
      }

      // Mặt đất chuyển động ở đáy (y = 198..214)
      cv->fillRect(2, 198 - yOff, 220, 16, 0x2124);
      cv->drawFastHLine(2, 198 - yOff, 220, C_YELLOW);
      for (int gx = -flappyGroundOffset; gx < 220; gx += 14) {
        if (gx >= 2) cv->drawLine(gx, 200 - yOff, gx + 6, 212 - yOff, C_NEON_AMBER);
      }

      // Vẽ chú chim Flappy (x = 48, y = flappyY)
      int by = (int)flappyY - yOff;
      cv->fillRoundRect(40, by - 7, 16, 14, 5, C_YELLOW);
      cv->drawRoundRect(40, by - 7, 16, 14, 5, C_WHITE);
      // Cánh vỗ
      int wingOff = (flappyVy < 0) ? 3 : -2;
      cv->fillRoundRect(42, by + wingOff - 2, 8, 5, 2, C_WHITE);
      // Mắt & Mỏ đỏ
      cv->fillCircle(52, by - 3, 2, C_WHITE);
      cv->drawPixel(53, by - 3, C_BLACK);
      cv->fillRect(55, by - 1, 5, 4, C_NEON_PINK);

      // Thanh điểm số trên cùng
      cv->fillRect(2, 2 - yOff, 220, 20, 0x10A4);
      cv->drawFastHLine(2, 22 - yOff, 220, C_NEON_CYAN);
      cv->setTextSize(1);
      cv->setTextColor(C_YELLOW);
      cv->setCursor(8, 8 - yOff);
      cv->printf("CHIM VỖ CÁNH | Điểm:%d", flappyScore);
      cv->setTextColor(C_NEON_GREEN);
      cv->setCursor(150, 8 - yOff);
      cv->printf("Kỷ lục:%d", flappyBestScore);

      if (flappyGameOver || flappyPaused) {
        cv->fillRoundRect(26, 82 - yOff, 172, 54, 6, 0x1949);
        cv->drawRoundRect(26, 82 - yOff, 172, 54, 6, C_YELLOW);
        cv->setTextColor(flappyGameOver ? C_NEON_PINK : C_YELLOW);
        cv->setCursor(44, 94 - yOff);
        cv->print(flappyGameOver ? "VA CHẠM! (GAME OVER)" : "ĐANG TẠM DỪNG GAME");
        cv->setTextColor(C_WHITE);
        cv->setCursor(38, 114 - yOff);
        cv->print(flappyGameOver ? "Bấm [OK/LÊN] để Chơi lại" : "Bấm [OK] để Tiếp tục bay");
      }
    } else if (s40GameActiveId == 3) {
      // --- VẼ GAME 3: XẾP GẠCH CỔ ĐIỂN (TETRIS 10x18) ---
      const int bx0 = 10, by0 = 8, cell = 11;
      const int bw = TETRIS_COLS * cell; // 110
      const int bh = TETRIS_ROWS * cell; // 198

      // Khung bàn cờ bên trái
      cv->fillRect(bx0 - 2, by0 - 2 - yOff, bw + 4, bh + 4, C_BLACK);
      cv->drawRect(bx0 - 2, by0 - 2 - yOff, bw + 4, bh + 4, C_NEON_CYAN);

      // Lưới bàn cờ & các viên gạch đã khóa
      for (int r = 0; r < TETRIS_ROWS; r++) {
        int py = by0 + r * cell - yOff;
        if (py + cell < 0 || py >= 72) continue;
        for (int c = 0; c < TETRIS_COLS; c++) {
          int px = bx0 + c * cell;
          uint8_t val = tetrisBoard[r][c];
          if (val == 0) {
            cv->drawPixel(px + 5, py + 5, 0x1082);
          } else {
            uint16_t col = TETRIS_COLORS[val % 8];
            cv->fillRect(px + 1, py + 1, cell - 2, cell - 2, col);
            cv->drawRect(px, py, cell - 1, cell - 1, C_WHITE);
          }
        }
      }

      // Tính toán vị trí bóng mờ (Ghost Piece) ở đáy và vẽ khối gạch đang rơi
      if (!tetrisGameOver) {
        int ghostY = tetrisCurY;
        while (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX, ghostY + 1)) {
          ghostY++;
        }
        uint16_t mask = TETRIS_SHAPES[tetrisCurType % 7][tetrisCurRot % 4];
        uint16_t pCol = TETRIS_COLORS[(tetrisCurType % 7) + 1];
        for (int r = 0; r < 4; r++) {
          for (int c = 0; c < 4; c++) {
            if ((mask >> (15 - (r * 4 + c))) & 1) {
              // Vẽ bóng mờ tại ghostY
              if (ghostY != tetrisCurY && (ghostY + r) >= 0) {
                int gx = bx0 + (tetrisCurX + c) * cell;
                int gy = by0 + (ghostY + r) * cell - yOff;
                cv->drawRect(gx + 1, gy + 1, cell - 3, cell - 3, 0x4208);
              }
              // Vẽ khối gạch thực tại tetrisCurY
              if ((tetrisCurY + r) >= 0) {
                int px = bx0 + (tetrisCurX + c) * cell;
                int py = by0 + (tetrisCurY + r) * cell - yOff;
                cv->fillRect(px + 1, py + 1, cell - 2, cell - 2, pCol);
                cv->drawRect(px, py, cell - 1, cell - 1, C_WHITE);
              }
            }
          }
        }
      }

      // Khung thông tin & Xem trước khối tiếp theo (Next Piece) bên phải (x = 126..216)
      cv->fillRoundRect(126, 8 - yOff, 90, 200, 5, 0x10A4);
      cv->drawRoundRect(126, 8 - yOff, 90, 200, 5, C_CARD_BORDER);
      cv->setTextSize(1);
      cv->setTextColor(C_YELLOW);
      cv->setCursor(138, 15 - yOff);
      cv->print("XẾP GẠCH");

      cv->setTextColor(C_NEON_CYAN);
      cv->setCursor(134, 30 - yOff);
      cv->print("KHỐI TIẾP:");
      cv->fillRect(142, 42 - yOff, 54, 46, C_BLACK);
      cv->drawRect(142, 42 - yOff, 54, 46, 0x2965);

      uint16_t nMask = TETRIS_SHAPES[tetrisNextType % 7][0];
      uint16_t nCol  = TETRIS_COLORS[(tetrisNextType % 7) + 1];
      for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
          if ((nMask >> (15 - (r * 4 + c))) & 1) {
            cv->fillRect(149 + c * 10, 47 + r * 10 - yOff, 8, 8, nCol);
            cv->drawRect(149 + c * 10, 47 + r * 10 - yOff, 8, 8, C_WHITE);
          }
        }
      }

      cv->setTextColor(C_WHITE);
      cv->setCursor(132, 98 - yOff);
      cv->print("ĐIỂM SỐ:");
      cv->setTextColor(C_NEON_GREEN);
      cv->setCursor(132, 110 - yOff);
      cv->printf("%d", tetrisScore);

      cv->setTextColor(C_WHITE);
      cv->setCursor(132, 128 - yOff);
      cv->print("KỶ LỤC:");
      cv->setTextColor(C_YELLOW);
      cv->setCursor(132, 140 - yOff);
      cv->printf("%d", tetrisBestScore);

      cv->setTextColor(C_WHITE);
      cv->setCursor(132, 158 - yOff);
      cv->printf("HÀNG: %d", tetrisLines);
      cv->setTextColor(C_NEON_PINK);
      cv->setCursor(132, 174 - yOff);
      cv->printf("CẤP : %d", 1 + (tetrisLines / 5));

      cv->setTextColor(C_SLATE);
      cv->setCursor(130, 192 - yOff);
      cv->print("OK/LÊN: Xoay");

      if (tetrisGameOver || tetrisPaused) {
        cv->fillRoundRect(16, 82 - yOff, 192, 54, 6, 0x1949);
        cv->drawRoundRect(16, 82 - yOff, 192, 54, 6, C_YELLOW);
        cv->setTextColor(tetrisGameOver ? C_NEON_PINK : C_YELLOW);
        cv->setCursor(44, 94 - yOff);
        cv->print(tetrisGameOver ? "ĐẦY CỘT! (GAME OVER)" : "ĐANG TẠM DỪNG GAME");
        cv->setTextColor(C_WHITE);
        cv->setCursor(38, 114 - yOff);
        cv->print(tetrisGameOver ? "Bấm [OK] để Chơi ván mới" : "Bấm [MENU/OK] Tiếp tục");
      }
    }
  }

  static void drawActiveGamePlayfieldOnly() {
    if (!tft || s40GameActiveId == 0) return;
    if (!mediaVisCanvas) {
      mediaVisCanvas = new VnCanvas16(224, 82);
    }
    if (!mediaVisCanvas) return;

    // Kết xuất 3 dải ngang (3 x 72px = 224 x 216px) chống nháy 100% và không tốn thêm RAM
    for (int band = 0; band < 3; band++) {
      int yOff = band * 72;
      renderGameBandToCanvas(mediaVisCanvas, yOff);
      tft->drawRGBBitmap(8, 54 + yOff, mediaVisCanvas->getBuffer(), 224, 72);
    }
  }

  static int lastDrawnGameMenuCursor = -1;

  static void drawSingleGameMenuCard(int i, bool sel) {
    if (!tft || i < 0 || i >= 3) return;
    int W = tft->width();
    int ry = 56 + i * 78;
    drawThemedBox(8, ry, W - 16, 72, sel);

    // Vẽ biểu tượng đại diện từng Game ở góc trái thẻ
    int icx = 30, icy = ry + 36;
    if (i == 0) {
      // Icon Rắn Săn Mồi
      tft->fillRoundRect(icx - 14, icy - 14, 28, 28, 5, C_BLACK);
      tft->drawRoundRect(icx - 14, icy - 14, 28, 28, 5, C_NEON_GREEN);
      tft->fillRect(icx - 8, icy, 12, 5, C_NEON_GREEN);
      tft->fillRect(icx, icy - 8, 5, 13, C_NEON_CYAN);
      tft->fillRect(icx, icy - 8, 9, 5, C_YELLOW);
      tft->fillCircle(icx - 6, icy - 6, 3, C_NEON_PINK);
    } else if (i == 1) {
      // Icon Flappy Bird
      tft->fillRoundRect(icx - 14, icy - 14, 28, 28, 5, C_BLACK);
      tft->drawRoundRect(icx - 14, icy - 14, 28, 28, 5, C_YELLOW);
      tft->fillRoundRect(icx - 7, icy - 5, 14, 10, 4, C_YELLOW);
      tft->fillCircle(icx + 3, icy - 2, 2, C_WHITE);
      tft->fillRect(icx + 6, icy - 1, 4, 3, C_NEON_PINK);
    } else {
      // Icon Xếp Gạch Tetris
      tft->fillRoundRect(icx - 14, icy - 14, 28, 28, 5, C_BLACK);
      tft->drawRoundRect(icx - 14, icy - 14, 28, 28, 5, C_NEON_CYAN);
      tft->fillRect(icx - 9, icy + 1, 6, 6, C_NEON_PINK);
      tft->fillRect(icx - 3, icy + 1, 6, 6, C_NEON_PINK);
      tft->fillRect(icx + 3, icy + 1, 6, 6, C_NEON_PINK);
      tft->fillRect(icx - 3, icy - 5, 6, 6, C_YELLOW);
    }

    const char* gTitles[3] = {
      "1. Rắn Săn Mồi (Snake Xenzia)",
      "2. Chim Vỗ Cánh (Flappy Bird)",
      "3. Xếp Gạch Cổ Điển (Tetris)"
    };
    const char* gSub[3] = {
      "Điều khiển 4 hướng ăn mồi năng lượng",
      "Bấm [OK / LÊN] vỗ cánh vượt ống nước",
      "Xoay khối [OK/LÊN], dịch Trái/Phải/Xuống"
    };
    int bests[3] = { snakeBestScore, flappyBestScore, tetrisBestScore };

    uint16_t bg = sel ? C_CARD_SEL : C_CARD_BG;
    tft->setTextSize(1);
    tft->setTextColor(sel ? C_YELLOW : C_WHITE, bg);
    tft->setCursor(52, ry + 12);
    tft->print(gTitles[i]);

    tft->setTextColor(C_NEON_CYAN, bg);
    tft->setCursor(52, ry + 30);
    tft->print(gSub[i]);

    tft->setTextColor(C_NEON_GREEN, bg);
    tft->setCursor(52, ry + 48);
    tft->printf("Kỷ lục cao nhất: %d điểm", bests[i]);
  }

  static void drawGameCenterAppScreen(bool fullRedraw = true) {
    if (!tft) return;
    loadGameScoresPrefsIfNeeded();
    int W = tft->width();

    if (s40GameActiveId == 0) {
      // SẢNH CHỌN 3 TRÒ CHƠI GIẢI TRÍ (GAME HUB)
      if (fullRedraw) {
        tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
        drawSymbianChrome("TRUNG TÂM TRÒ CHƠI", "[OK:Chơi]", "Lên/Xuống", "[EXIT:Menu]");

        tft->fillRoundRect(6, 26, W - 12, 24, 5, 0x1949);
        tft->drawRoundRect(6, 26, W - 12, 24, 5, C_NEON_PINK);
        tft->setTextSize(1);
        tft->setTextColor(C_YELLOW, 0x1949);
        tft->setCursor(14, 34);
        tft->print("CHỌN TRÒ CHƠI GIẢI TRÍ (3 GAME):");

        for (int i = 0; i < 3; i++) {
          drawSingleGameMenuCard(i, i == s40GameMenuCursor);
        }
        lastDrawnGameMenuCursor = s40GameMenuCursor;
      } else {
        if (lastDrawnGameMenuCursor != s40GameMenuCursor) {
          if (lastDrawnGameMenuCursor >= 0 && lastDrawnGameMenuCursor < 3) {
            drawSingleGameMenuCard(lastDrawnGameMenuCursor, false);
          }
          drawSingleGameMenuCard(s40GameMenuCursor, true);
          lastDrawnGameMenuCursor = s40GameMenuCursor;
        }
      }
      return;
    }

    // ĐANG CHƠI 1 TRONG 3 GAME (s40GameActiveId == 1, 2, 3)
    if (fullRedraw) {
      tft->fillRect(0, 22, W, tft->height() - 44, C_DARK_BG);
      const char* gHdr[4] = { "", "GAME 1: RẮN SĂN MỒI", "GAME 2: CHIM VỖ CÁNH", "GAME 3: XẾP GẠCH" };
      drawSymbianChrome(gHdr[s40GameActiveId % 4], "[MENU:Dừng]", "[OK:ThaoTác]", "[EXIT:Sảnh]");

      tft->fillRoundRect(8, 26, W - 16, 24, 4, C_CARD_BG);
      tft->drawRoundRect(8, 26, W - 16, 24, 4, C_CARD_BORDER);
      tft->setTextSize(1);
      tft->setTextColor(C_NEON_AMBER, C_CARD_BG);
      tft->setCursor(14, 34);
      if (s40GameActiveId == 1)      tft->print("4 Hướng: Lái Rắn | OK: Dừng/Chơi lại");
      else if (s40GameActiveId == 2) tft->print("Bấm [OK] hoặc [LÊN]: Vỗ cánh bay lên!");
      else                           tft->print("Trái/Phải: Dịch | OK/Lên: Xoay | Xuống: Hạ");

      tft->fillRoundRect(8, 273, W - 16, 20, 4, C_CARD_BG);
      tft->setTextColor(C_SLATE, C_CARD_BG);
      tft->setCursor(16, 279);
      tft->print("Bấm [EXIT] để quay về Sảnh chọn Trò chơi");
    }
    drawActiveGamePlayfieldOnly();
  }

  static void updateActiveGameLoop(unsigned long now) {
    if (currentMode != 15 || s40GameActiveId == 0) return;

    if (s40GameActiveId == 1) {
      // Vòng lặp Rắn Săn Mồi (tốc độ 130ms -> 75ms tùy theo điểm)
      unsigned long stepMs = max(75UL, 130UL - (unsigned long)(snakeScore / 2));
      if (now - s40GameLastTickMs >= stepMs) {
        s40GameLastTickMs = now;
        if (!snakeGameOver && !snakePaused) {
          snakeDirX = snakeNextDirX;
          snakeDirY = snakeNextDirY;
          int nx = (snakeX[0] + snakeDirX + SNAKE_COLS) % SNAKE_COLS;
          int ny = (snakeY[0] + snakeDirY + SNAKE_ROWS) % SNAKE_ROWS;

          // Kiểm tra tự cắn đuôi
          for (int i = 0; i < snakeLen; i++) {
            if (snakeX[i] == nx && snakeY[i] == ny) {
              snakeGameOver = true;
              saveGameScoresPrefs();
              drawActiveGamePlayfieldOnly();
              return;
            }
          }

          // Dịch chuyển thân rắn
          if (nx == snakeFoodX && ny == snakeFoodY) {
            if (snakeLen < 78) snakeLen++;
            for (int i = snakeLen - 1; i > 0; i--) {
              snakeX[i] = snakeX[i - 1];
              snakeY[i] = snakeY[i - 1];
            }
            snakeX[0] = nx;
            snakeY[0] = ny;
            snakeScore += 10;
            if (snakeScore > snakeBestScore) snakeBestScore = snakeScore;
            spawnSnakeFood();
          } else {
            for (int i = snakeLen - 1; i > 0; i--) {
              snakeX[i] = snakeX[i - 1];
              snakeY[i] = snakeY[i - 1];
            }
            snakeX[0] = nx;
            snakeY[0] = ny;
          }
          drawActiveGamePlayfieldOnly();
        }
      }
    } else if (s40GameActiveId == 2) {
      // Vòng lặp Vật lý Chim Vỗ Cánh Flappy Bird (55ms = ~18 FPS mượt)
      if (now - s40GameLastTickMs >= 55) {
        s40GameLastTickMs = now;
        if (!flappyGameOver && !flappyPaused) {
          flappyGroundOffset = (flappyGroundOffset + 3) % 14;
          flappyVy += 0.46f;
          if (flappyVy > 5.2f) flappyVy = 5.2f;
          flappyY += flappyVy;

          // Kiểm tra chạm trần hoặc chạm đất
          if (flappyY < 30.0f) { flappyY = 30.0f; flappyVy = 0.0f; }
          if (flappyY >= 191.0f) {
            flappyY = 191.0f;
            flappyGameOver = true;
            saveGameScoresPrefs();
          }

          // Di chuyển 2 cặp ống nước & kiểm tra va chạm
          const int pipeW = 26, gapH = 58;
          for (int p = 0; p < 2; p++) {
            flappyPipeX[p] -= 2.5f;
            if (!flappyPipePassed[p] && (flappyPipeX[p] + pipeW) < 40.0f) {
              flappyPipePassed[p] = true;
              flappyScore++;
              if (flappyScore > flappyBestScore) flappyBestScore = flappyScore;
            }
            if (flappyPipeX[p] < -30.0f) {
              int other = 1 - p;
              flappyPipeX[p] = max(224.0f, flappyPipeX[other] + 118.0f);
              flappyPipeGapY[p] = random(66, 150);
              flappyPipePassed[p] = false;
            }

            // Kiểm tra khung va chạm với chú chim (x = 40..55, y = flappyY-6..flappyY+6)
            if (55.0f >= flappyPipeX[p] && 40.0f <= (flappyPipeX[p] + pipeW)) {
              float topEdge = (float)(flappyPipeGapY[p] - gapH / 2);
              float botEdge = (float)(flappyPipeGapY[p] + gapH / 2);
              if ((flappyY - 5.0f) < topEdge || (flappyY + 5.0f) > botEdge) {
                flappyGameOver = true;
                saveGameScoresPrefs();
              }
            }
          }
          drawActiveGamePlayfieldOnly();
        }
      }
    } else if (s40GameActiveId == 3) {
      // Vòng lặp Xếp Gạch Tetris
      unsigned long fallInterval = max(160UL, 520UL - (unsigned long)(tetrisLines / 5) * 45UL);
      if (!tetrisGameOver && !tetrisPaused && (now - tetrisLastFallMs >= fallInterval)) {
        tetrisLastFallMs = now;
        if (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX, tetrisCurY + 1)) {
          tetrisCurY++;
        } else {
          lockAndClearTetrisPiece();
        }
        drawActiveGamePlayfieldOnly();
      }
    }
  }

  static void refreshActiveScreen();

  static void handleGameCenterKey(int keyIndex) {
    if (s40GameActiveId == 0) {
      // Đang ở Sảnh chọn 3 Game
      if (keyIndex == 1 || keyIndex == 3) {
        s40GameMenuCursor = (s40GameMenuCursor + 2) % 3;
        drawGameCenterAppScreen(false);
      } else if (keyIndex == 2 || keyIndex == 4) {
        s40GameMenuCursor = (s40GameMenuCursor + 1) % 3;
        drawGameCenterAppScreen(false);
      } else if (keyIndex == 0) {
        s40GameActiveId = (uint8_t)(s40GameMenuCursor + 1);
        if (s40GameActiveId == 1)      initSnakeGame();
        else if (s40GameActiveId == 2) initFlappyGame();
        else if (s40GameActiveId == 3) initTetrisGame();
        drawGameCenterAppScreen(true);
      } else if (keyIndex == 6 || keyIndex == 5) {
        currentMode = 4;
        refreshActiveScreen();
      }
      return;
    }

    // Phím EXIT (keyIndex == 6) khi đang trong Game -> Quay lại Sảnh chọn 3 Game
    if (keyIndex == 6) {
      saveGameScoresPrefs();
      s40GameActiveId = 0;
      drawGameCenterAppScreen(true);
      return;
    }

    if (s40GameActiveId == 1) {
      // Điều khiển Rắn Săn Mồi
      if (keyIndex == 5) {
        snakePaused = !snakePaused;
        drawActiveGamePlayfieldOnly();
      } else if (keyIndex == 0) {
        if (snakeGameOver) initSnakeGame();
        else snakePaused = !snakePaused;
        drawActiveGamePlayfieldOnly();
      } else if (keyIndex == 1 && snakeDirY == 0) { // UP
        snakeNextDirX = 0; snakeNextDirY = -1;
      } else if (keyIndex == 2 && snakeDirY == 0) { // DOWN
        snakeNextDirX = 0; snakeNextDirY = 1;
      } else if (keyIndex == 3 && snakeDirX == 0) { // LEFT
        snakeNextDirX = -1; snakeNextDirY = 0;
      } else if (keyIndex == 4 && snakeDirX == 0) { // RIGHT
        snakeNextDirX = 1; snakeNextDirY = 0;
      }
    } else if (s40GameActiveId == 2) {
      // Điều khiển Chim Vỗ Cánh (Flappy Bird)
      if (keyIndex == 5) {
        flappyPaused = !flappyPaused;
        drawActiveGamePlayfieldOnly();
      } else if (keyIndex == 0 || keyIndex == 1) { // OK hoặc LÊN: Vỗ cánh!
        if (flappyGameOver) {
          initFlappyGame();
        } else if (flappyPaused) {
          flappyPaused = false;
        } else {
          flappyVy = -4.1f;
        }
        drawActiveGamePlayfieldOnly();
      }
    } else if (s40GameActiveId == 3) {
      // Điều khiển Xếp Gạch (Tetris)
      if (keyIndex == 5) {
        tetrisPaused = !tetrisPaused;
        drawActiveGamePlayfieldOnly();
      } else if (tetrisGameOver) {
        if (keyIndex == 0) {
          initTetrisGame();
          drawActiveGamePlayfieldOnly();
        }
      } else if (tetrisPaused) {
        if (keyIndex == 0) {
          tetrisPaused = false;
          drawActiveGamePlayfieldOnly();
        }
      } else {
        if (keyIndex == 3) { // LEFT
          if (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX - 1, tetrisCurY)) {
            tetrisCurX--;
            drawActiveGamePlayfieldOnly();
          }
        } else if (keyIndex == 4) { // RIGHT
          if (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX + 1, tetrisCurY)) {
            tetrisCurX++;
            drawActiveGamePlayfieldOnly();
          }
        } else if (keyIndex == 1 || keyIndex == 0) { // UP hoặc OK: Xoay khối gạch 90 độ
          int nextRot = (tetrisCurRot + 1) % 4;
          if (!tetrisCollision(tetrisCurType, nextRot, tetrisCurX, tetrisCurY)) {
            tetrisCurRot = nextRot;
          } else if (!tetrisCollision(tetrisCurType, nextRot, tetrisCurX - 1, tetrisCurY)) {
            tetrisCurX--;
            tetrisCurRot = nextRot;
          } else if (!tetrisCollision(tetrisCurType, nextRot, tetrisCurX + 1, tetrisCurY)) {
            tetrisCurX++;
            tetrisCurRot = nextRot;
          }
          drawActiveGamePlayfieldOnly();
        } else if (keyIndex == 2) { // DOWN: Hạ nhanh
          if (!tetrisCollision(tetrisCurType, tetrisCurRot, tetrisCurX, tetrisCurY + 1)) {
            tetrisCurY++;
            tetrisScore += 1;
            if (tetrisScore > tetrisBestScore) tetrisBestScore = tetrisScore;
          } else {
            lockAndClearTetrisPiece();
          }
          tetrisLastFallMs = millis();
          drawActiveGamePlayfieldOnly();
        }
      }
    }
  }

  static void refreshActiveScreen() {
    if (!tft) return;
    if (isScreenSleeping) {
      if (alwaysOnDisplayEnabled) {
        drawAlwaysOnDisplayScreen(true);
      } else {
        tft->fillScreen(C_BLACK);
        ledcWrite(0, 0);
      }
      return;
    }
    if (currentMode == 0) drawStandbyScreenFull();
    else if (currentMode == 1) drawFullEmojiScreen();
    else if (currentMode == 2) drawPcHudScreen(true);
    else if (currentMode == 3) drawHardwareDiagScreen(true);
    else if (currentMode == 4) drawSymbianMenuScreen(true);
    else if (currentMode == 5) drawWallpaperAppScreen();
    else if (currentMode == 6) drawSettingsAppScreen();
    else if (currentMode == 7) drawGalleryAppScreen();
    else if (currentMode == 8) drawAboutAppScreen();
    else if (currentMode == 9) drawSdCardTestScreen(true);
    else if (currentMode == 10) drawMemoryManagerAppScreen();
    else if (currentMode == 11) drawXiaoZhiAssistantScreen(true);
    else if (currentMode == 12) drawMediaPlayerAppScreen(true);
    else if (currentMode == 13) drawClockSuiteAppScreen(true);
    else if (currentMode == 14) drawCalendarAppScreen();
    else if (currentMode == 15) drawGameCenterAppScreen(true);
    else if (currentMode == 16) drawQrCodeWifiScreen(true);
    if (s40PushAlertActive) {
      drawPushAlertModal(true);
    }
  }

  // Xử lý thay đổi giá trị trong App Hình Nền (Mode 5) khi bấm LEFT (-1) hoặc RIGHT/OK (+1)
  static void adjustWallpaperAppSetting(int row, int dir) {
    if (row == 0) {
      const char* modes[5] = { "image", "gradient-cyber", "gradient-nebula", "gradient-sunset", "solid-black" };
      int cur = 0;
      for (int i = 0; i < 5; i++) {
        if (stCfg.bgMode == modes[i]) { cur = i; break; }
      }
      cur = (cur + dir + 5) % 5;
      stCfg.bgMode = modes[cur];
    } else if (row == 1) {
      String files[20];
      int count = getStoredJpgList(files, nullptr, 20);
      if (count > 0) {
        int cur = 0;
        for (int i = 0; i < count; i++) {
          if (stCfg.bgImage == files[i] || ("/" + stCfg.bgImage) == files[i]) { cur = i; break; }
        }
        cur = (cur + dir + count) % count;
        stCfg.bgImage = files[cur];
        stCfg.bgMode = "image";
      }
    } else if (row == 2) {
      stCfg.dimOverlay = constrain(stCfg.dimOverlay + dir * 10, 0, 80);
    } else if (row == 3) {
      const char* styles[4] = { "digital", "neon", "minimal", "retro" };
      int cur = 0;
      for (int i = 0; i < 4; i++) {
        if (stCfg.clockStyle == styles[i]) { cur = i; break; }
      }
      cur = (cur + dir + 4) % 4;
      stCfg.clockStyle = styles[cur];
    } else if (row == 4) {
      const char* pos[3] = { "center", "top", "bottom" };
      int cur = 0;
      for (int i = 0; i < 3; i++) {
        if (stCfg.clockPos == pos[i]) { cur = i; break; }
      }
      cur = (cur + dir + 3) % 3;
      stCfg.clockPos = pos[cur];
    } else if (row == 5) {
      stCfg.clockFormat = (stCfg.clockFormat == "24h") ? "12h" : "24h";
    } else if (row == 6) {
      stCfg.showSeconds = !stCfg.showSeconds;
    } else if (row == 7) {
      stCfg.showWeather = !stCfg.showWeather;
    } else if (row == 8) {
      const uint16_t cols[6] = { C_NEON_CYAN, C_NEON_GREEN, C_NEON_PINK, C_YELLOW, C_NEON_PURPLE, C_WHITE };
      int cur = 0;
      for (int i = 0; i < 6; i++) {
        if (stCfg.clockColor == cols[i]) { cur = i; break; }
      }
      cur = (cur + dir + 6) % 6;
      stCfg.clockColor = cols[cur];
    } else if (row == 9) {
      saveConfigToFile();
      currentMode = 0;
      refreshActiveScreen();
      return;
    }

    saveConfigToFile();
    showSymbianToast("ĐÃ LƯU CẤU HÌNH HÌNH NỀN!");
    drawWallpaperAppScreen(false);
  }

  static void adjustSettingsAppItem(int row, int dir) {
    if (row == 0) {
      applyS40UiTheme(s40UiThemeIdx + dir);
      showSymbianToast(String("GIAO DIỆN: ") + curTheme().headerTag);
      drawSettingsAppScreen(true);
      return;
    } else if (row == 1) {
      setBacklightBrightness(screenBrightnessPct + dir * 10);
      saveSystemSettingsPrefs();
    } else if (row == 2) {
      screenTimeoutIdx = (screenTimeoutIdx + dir + 6) % 6;
      saveSystemSettingsPrefs();
    } else if (row == 3) {
      aodClockStyle = (aodClockStyle + dir + 4) % 4;
      alwaysOnDisplayEnabled = (aodClockStyle > 0);
      saveSystemSettingsPrefs();
      const char* aodStNames[4] = { "Đã TẮT màn hình khóa", "AOD: Kỹ thuật số", "AOD: Lật số cổ điển", "AOD: Đồng hồ kim" };
      showSymbianToast(aodStNames[aodClockStyle]);
    } else if (row == 4) {
      TestAudio::setSpeakerVolumePct(TestAudio::getSpeakerVolumePct() + dir * 5);
      TestAudio::playKeyBeep();
    } else if (row == 5) {
      keyBeepEnabled = !keyBeepEnabled;
      saveSystemSettingsPrefs();
      if (keyBeepEnabled) TestAudio::playOkChime();
      else TestAudio::playDeleteChime();
      showSymbianToast(keyBeepEnabled ? "ĐÃ BẬT ÂM PHÍM BẤM" : "ĐÃ TẮT ÂM PHÍM BẤM");
    } else if (row == 6) {
      loadClockSuitePrefsIfNeeded();
      s40AlarmTuneIdx = (s40AlarmTuneIdx + dir + 4) % 4;
      saveClockSuitePrefs();
      TestAudio::playAlarmTuneStep(s40AlarmTuneIdx, 0);
      showSymbianToast(String("CHUÔNG: ") + TestAudio::getAlarmTuneName(s40AlarmTuneIdx));
    } else if (row == 7) {
      micSensitivityMode = (micSensitivityMode + dir + 3) % 3;
      TestAudio::setMicSensitivityMode(micSensitivityMode);
      saveSystemSettingsPrefs();
      const char* mNames[3] = { "Mic: THẤP (1.8x)", "Mic: TIÊU CHUẨN (3.5x)", "Mic: CAO (5.5x)" };
      showSymbianToast(mNames[micSensitivityMode]);
    } else if (row == 8) {
      eyeState = (eyeState + dir + 12) % 12;
      lastExternalEmojiSync = millis();
    } else if (row == 9) {
      currentMode = 3;
      refreshActiveScreen();
      return;
    } else if (row == 10) {
      int m = (TestWindmill::getMode() + dir + 5) % 5;
      TestWindmill::setMode(m);
      saveSystemSettingsPrefs();
      showSymbianToast(String("L298N: ") + TestWindmill::getModeName());
    }
    drawSettingsAppScreen(false);
  }

  // ============================================================================
  // HÀM NHẬN SỰ KIỆN TỪ BÀN PHÍM 7 NÚT ADC (GPIO 3)
  // keyIndex: 0=OK, 1=UP, 2=DOWN, 3=LEFT, 4=RIGHT, 5=MENU, 6=EXIT
  // ============================================================================
  void onKeypadEvent(int keyIndex) {
    lastUserActivityMs = millis();

    // 0. Nếu đang hiển thị THÔNG BÁO ĐẨY TOÀN MÀN HÌNH (Báo thức đến giờ / Hết giờ hẹn):
    if (s40PushAlertActive) {
      s40PushAlertActive = false;
      isScreenSleeping = false;
      setBacklightBrightness(screenBrightnessPct);
      if (keyIndex == 0) {
        // Phím OK: Tắt chuông hoàn toàn
        TestAudio::playOkChime();
        if (s40PushAlertType == 1) {
          s40SnoozeActive = false;
          showSymbianToast("ĐÃ TẮT CHUÔNG BÁO THỨC!");
        } else {
          s40TimerRemainSec = s40TimerPresetSec;
          showSymbianToast("ĐÃ TẮT CHUÔNG HẸN GIỜ!");
        }
      } else {
        // Các phím khác (TRÁI / PHẢI / EXIT / LÊN / XUỐNG / MENU): Hoãn +5 Phút (Snooze) hoặc Đếm thêm +1 Phút
        TestAudio::playDeleteChime();
        if (s40PushAlertType == 1) {
          int hr, mn, sc, wd, dy, mo, yr;
          getCurrentDateTime(hr, mn, sc, wd, dy, mo, yr);
          int snzTot = hr * 60 + mn + 5;
          s40SnoozeHour     = (uint8_t)((snzTot / 60) % 24);
          s40SnoozeMin      = (uint8_t)(snzTot % 60);
          s40SnoozeAlarmIdx = s40PushAlertAlarmIdx;
          s40SnoozeActive   = true;
          char msg[44];
          snprintf(msg, sizeof(msg), "HOÃN +5P -> BÁO LẠI LÚC %02u:%02u", s40SnoozeHour, s40SnoozeMin);
          showSymbianToast(String(msg), 3000);
        } else {
          s40TimerRemainSec  = 60;
          s40TimerRunning    = true;
          s40TimerLastTickMs = millis();
          showSymbianToast("ĐÃ ĐẾM THÊM +1 PHÚT (01:00)!", 2500);
        }
      }
      refreshActiveScreen();
      return;
    }

    // Nếu màn hình đang ở chế độ AOD hoặc Sleep -> Bấm phím bất kỳ sẽ đánh thức màn hình ngay!
    if (isScreenSleeping) {
      isScreenSleeping = false;
      setBacklightBrightness(screenBrightnessPct);
      Serial.println("💡 [WAKEUP] Đã đánh thức màn hình từ chế độ AOD / Sleep bằng phím bấm!");
      refreshActiveScreen();
      return;
    }

    // Phát âm bíp bàn phím mono siêu ngắn dứt khoát nếu tính năng Âm bàn phím đang bật (trừ khi đang chơi game để tránh trễ)
    if (keyBeepEnabled && (currentMode != 15 || s40GameActiveId == 0)) {
      TestAudio::playKeyBeep();
    }

    // Nếu đang ở Trung tâm Trò chơi (Mode 15): chuyển toàn bộ sự kiện phím (bao gồm cả MENU) cho Game xử lý!
    if (currentMode == 15) {
      handleGameCenterKey(keyIndex);
      return;
    }

    // Nếu đang ở App Quản lý Bộ nhớ (Mode 10) và bấm phím MENU (keyIndex == 5)
    if (currentMode == 10 && keyIndex == 5) {
      if (s40MemSubState == 4) {
        // Đang ở bảng Thông tin Chi tiết Tệp -> Bấm MENU để XÓA NGAY tệp đang xem!
        deleteSelectedMemoryFile();
        s40MemSubState = 1;
      } else if (s40MemSubState == 2) {
        s40MemSubState = 1;
      } else {
        s40MemPopupCursor = 0;
        s40MemSubState = 2;
      }
      drawMemoryManagerAppScreen();
      return;
    }

    // Nếu đang ở App Đa phương tiện / Trình phát nhạc (Mode 12) và bấm phím MENU (keyIndex == 5)
    if (currentMode == 12 && keyIndex == 5) {
      if (s40MediaSubState == 2) {
        // Đang ở Danh sách bài hát SD -> Mở Popup Thao tác với Bài hát đang chọn (Xem thông tin / Xóa / Phát)
        s40MediaPopupCursor = 0;
        s40MediaSubState = 3;
      } else if (s40MediaSubState == 3) {
        s40MediaSubState = 2;
      } else if (s40MediaSubState == 4) {
        // Đang ở bảng Chi tiết bài hát -> Bấm MENU để XÓA bài hát khỏi Thẻ nhớ SD!
        deleteSelectedMediaTrack();
        s40MediaSubState = 2;
      } else {
        s40MediaSubState = (s40MediaSubState == 1) ? 0 : 1;
        s40MediaPopupCursor = 0;
      }
      drawMediaPlayerAppScreen(true);
      return;
    }

    // Nếu đang ở App Đồng hồ Đa năng (Mode 13) và bấm phím MENU (keyIndex == 5) -> Chuyển qua lại 4 Tab!
    if (currentMode == 13 && keyIndex == 5) {
      s40ClockTab = (s40ClockTab + 1) % 4;
      drawClockSuiteAppScreen(true);
      return;
    }

    // Nếu đang ở App Thông tin Thiết bị (Mode 8) và bấm phím MENU (keyIndex == 5)
    if (currentMode == 8 && keyIndex == 5) {
      if (s40OtaCheckState != 0) {
        s40OtaCheckState = 0;
        drawAboutAppScreen(true);
      } else {
        s40AboutMenuOpen = !s40AboutMenuOpen;
        s40AboutMenuCursor = 0;
        if (s40AboutMenuOpen) {
          drawAboutPopupMenu(tft->width(), tft->height());
          drawSymbianChrome("THÔNG TIN THIẾT BỊ", "Chọn", "< Chọn mục >", "Đóng");
        } else {
          drawAboutAppScreen(true);
        }
      }
      return;
    }

    // Phím MENU (keyIndex == 5): Từ các màn hình khác mở ngay Giao diện Symbian S40 Menu 4x3
    if (keyIndex == 5) {
      currentMode = 4;
      refreshActiveScreen();
      return;
    }

    // Điều hướng tùy theo màn hình hiện tại
    if (currentMode == 0 || currentMode == 1 || currentMode == 3) {
      if (keyIndex == 0) {
        currentMode = 4;
        refreshActiveScreen();
      } else if (keyIndex == 3) {
        currentMode = (currentMode + 3) % 4;
        refreshActiveScreen();
      } else if (keyIndex == 4) {
        currentMode = (currentMode + 1) % 4;
        refreshActiveScreen();
      } else if (keyIndex == 1 && currentMode == 1) {
        eyeState = (eyeState + 1) % 12;
        drawFullEmojiScreen();
      } else if (keyIndex == 2 && currentMode == 1) {
        eyeState = (eyeState + 11) % 12;
        drawFullEmojiScreen();
      } else if (keyIndex == 6) {
        if (currentMode != 0) {
          currentMode = 0;
          refreshActiveScreen();
        } else {
          // Đang ở Màn hình chờ (Mode 0) bấm EXIT -> Khóa màn hình nhanh vào Always On Display (AOD) hoặc Sleep!
          isScreenSleeping = true;
          if (alwaysOnDisplayEnabled) {
            ledcWrite(0, 4);
            tft->fillScreen(C_BLACK);
            drawAlwaysOnDisplayScreen(true);
            Serial.println("🌙 [AOD] Khóa màn hình nhanh vào Always On Display (AOD) bằng phím EXIT!");
          } else {
            tft->fillScreen(C_BLACK);
            ledcWrite(0, 0);
            Serial.println("🌙 [SLEEP] Tắt màn hình nhanh bằng phím EXIT!");
          }
          return;
        }
      }
    } else if (currentMode == 2) {
      if (keyIndex == 1 || keyIndex == 4) {
        cycleHudStyle(1);
      } else if (keyIndex == 2 || keyIndex == 3) {
        cycleHudStyle(-1);
      } else if (keyIndex == 0) {
        drawPcHudScreen(true);
      } else if (keyIndex == 6) {
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 4) {
      // ĐANG Ở MENU CHÍNH SYMBIAN S40 (Khung nhìn 4x3 có cuộn dọc cho 14 Biểu tượng: 0..13)
      if (keyIndex == 3) { // LEFT
        s40MenuCursor = (s40MenuCursor + S40_MENU_COUNT - 1) % S40_MENU_COUNT;
        drawSymbianMenuScreen(false);
      } else if (keyIndex == 4) { // RIGHT
        s40MenuCursor = (s40MenuCursor + 1) % S40_MENU_COUNT;
        drawSymbianMenuScreen(false);
      } else if (keyIndex == 1) { // UP (Lên 1 hàng, tự động cuộn khung nhìn 4x3)
        if (s40MenuCursor >= 4) {
          s40MenuCursor -= 4;
        } else {
          s40MenuCursor = min(S40_MENU_COUNT - 1, s40MenuCursor + 12);
        }
        drawSymbianMenuScreen(false);
      } else if (keyIndex == 2) { // DOWN (Xuống 1 hàng, tự động cuộn khung nhìn 4x3)
        if (s40MenuCursor + 4 < S40_MENU_COUNT) {
          s40MenuCursor += 4;
        } else if (s40MenuCursor >= 8 && s40MenuCursor <= 11) {
          s40MenuCursor = (s40MenuCursor % 2 == 0) ? 12 : 13;
        } else {
          s40MenuCursor = s40MenuCursor % 4;
        }
        drawSymbianMenuScreen(false);
      } else if (keyIndex == 0) { // OK -> Mở ứng dụng tương ứng trong 14 biểu tượng
        if (s40MenuCursor == 0)      currentMode = 5;  // 1. Hình nền
        else if (s40MenuCursor == 1) currentMode = 6;  // 2. Cài đặt
        else if (s40MenuCursor == 2) currentMode = 2;  // 3. Máy tính (PC)
        else if (s40MenuCursor == 3) currentMode = 7;  // 4. Thư viện ảnh
        else if (s40MenuCursor == 4) { s40MemSubState = 0; currentMode = 10; } // 5. Bộ nhớ SD
        else if (s40MenuCursor == 5) { enterXiaoZhiAssistantMode(false); } // 6. Trợ lý AI (Tự động kiểm tra liên kết / OTP)
        else if (s40MenuCursor == 6) { s40MediaSubState = 0; refreshMediaPlaylist(); currentMode = 12; } // 7. Âm nhạc
        else if (s40MenuCursor == 7) { currentMode = 13; } // 8. Đồng hồ
        else if (s40MenuCursor == 8) { currentMode = 14; } // 9. Lịch vạn niên
        else if (s40MenuCursor == 9) currentMode = 8;  // 10. Thiết bị
        else if (s40MenuCursor == 10) currentMode = 3; // 11. Sóng âm
        else if (s40MenuCursor == 11) currentMode = 9; // 12. Thẻ nhớ
        else if (s40MenuCursor == 12) { s40GameActiveId = 0; currentMode = 15; } // 13. Trung tâm Trò chơi (3 Game)
        else if (s40MenuCursor == 13) { currentMode = 16; } // 14. Mã QR Cài Đặt Wi-Fi
        refreshActiveScreen();
      } else if (keyIndex == 5) { // MENU -> Nếu đang ở icon Trợ lý AI: Buộc lấy mã OTP mới!
        if (s40MenuCursor == 5) {
          TestAudio::playKeyBeep();
          showSymbianToast("DANG LAY MA OTP MOI...");
          enterXiaoZhiAssistantMode(true);
          refreshActiveScreen();
        }
      } else if (keyIndex == 6) { // EXIT -> Về Màn Hình Chờ (Mode 0)
        currentMode = 0;
        refreshActiveScreen();
      }
    } else if (currentMode == 5) {
      if (keyIndex == 1) {
        s40WallpaperCursor = (s40WallpaperCursor + 9) % 10;
        drawWallpaperAppScreen(false);
      } else if (keyIndex == 2) {
        s40WallpaperCursor = (s40WallpaperCursor + 1) % 10;
        drawWallpaperAppScreen(false);
      } else if (keyIndex == 3) {
        adjustWallpaperAppSetting(s40WallpaperCursor, -1);
      } else if (keyIndex == 4 || keyIndex == 0) {
        adjustWallpaperAppSetting(s40WallpaperCursor, 1);
      } else if (keyIndex == 6) {
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 6) {
      if (keyIndex == 1) {
        s40SettingsCursor = (s40SettingsCursor + 10) % 11;
        drawSettingsAppScreen(false);
      } else if (keyIndex == 2) {
        s40SettingsCursor = (s40SettingsCursor + 1) % 11;
        drawSettingsAppScreen(false);
      } else if (keyIndex == 3) {
        adjustSettingsAppItem(s40SettingsCursor, -1);
      } else if (keyIndex == 4 || keyIndex == 0) {
        adjustSettingsAppItem(s40SettingsCursor, 1);
      } else if (keyIndex == 6) {
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 7) {
      if (keyIndex == 1 || keyIndex == 3) {
        s40GalleryIndex--;
        drawGalleryAppScreen(false);
      } else if (keyIndex == 2 || keyIndex == 4) {
        s40GalleryIndex++;
        drawGalleryAppScreen(false);
      } else if (keyIndex == 0) {
        String files[20];
        int count = getStoredJpgList(files, nullptr, 20);
        if (count > 0) {
          int idx = ((s40GalleryIndex % count) + count) % count;
          stCfg.bgImage = files[idx];
          stCfg.bgMode = "image";
          saveConfigToFile();
          TestAudio::playOkChime();
          showSymbianToast("ĐÃ ĐẶT LÀM HÌNH NỀN!");
          drawGalleryAppScreen(false);
        }
      } else if (keyIndex == 6) {
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 8) {
      if (s40OtaCheckState != 0) {
        if (s40OtaCheckState == 2) {
          // Trạng thái 2: Đã phát hiện bản cập nhật mới
          if (keyIndex == 0) {
            // OK: Nâng cấp OTA ngay
            showSymbianToast("BẮT ĐẦU TẢI & NÂNG CẤP...");
            s40OtaCheckState = 0;
            OtaManager::startCloudUpdate(s40OtaUpdateInfo.downloadUrl);
            drawAboutAppScreen();
          } else if (keyIndex == 6 || keyIndex == 5) {
            // EXIT hoặc MENU: Đóng modal
            s40OtaCheckState = 0;
            drawAboutAppScreen();
          }
        } else {
          // Trạng thái 1, 3, hoặc 4: Bấm OK hoặc EXIT hoặc MENU để đóng modal
          if (keyIndex == 0 || keyIndex == 6 || keyIndex == 5) {
            s40OtaCheckState = 0;
            drawAboutAppScreen();
          }
        }
      } else if (s40AboutMenuOpen) {
        if (keyIndex == 1) { // UP
          int oldCur = s40AboutMenuCursor;
          s40AboutMenuCursor = (s40AboutMenuCursor + 4) % 5;
          drawSingleAboutMenuItem(oldCur, false);
          drawSingleAboutMenuItem(s40AboutMenuCursor, true);
        } else if (keyIndex == 2) { // DOWN
          int oldCur = s40AboutMenuCursor;
          s40AboutMenuCursor = (s40AboutMenuCursor + 1) % 5;
          drawSingleAboutMenuItem(oldCur, false);
          drawSingleAboutMenuItem(s40AboutMenuCursor, true);
        } else if (keyIndex == 0) { // OK
          if (s40AboutMenuCursor == 0) {
            // Mục 0: Kiểm tra cập nhật OTA từ Cloud GitHub
            s40AboutMenuOpen = false;
            s40OtaCheckState = 1; // Đang kết nối...
            drawAboutOtaModal(tft->width(), tft->height());
            drawSymbianChrome("THÔNG TIN THIẾT BỊ", "Đóng", "", "");

            // Kiểm tra manifest từ xa
            bool checkOk = OtaManager::checkCloudUpdate(s40OtaUpdateInfo);
            if (!checkOk) {
              s40OtaCheckState = 4; // Lỗi mạng / không truy cập được manifest
            } else if (s40OtaUpdateInfo.hasUpdate) {
              s40OtaCheckState = 2; // Phát hiện bản mới
            } else {
              s40OtaCheckState = 3; // Bản mới nhất
            }
            drawAboutOtaModal(tft->width(), tft->height());
            drawSymbianChrome("THÔNG TIN THIẾT BỊ", s40OtaCheckState == 2 ? "Nâng cấp" : "Đóng", "", s40OtaCheckState == 2 ? "Đóng" : "");
          } else if (s40AboutMenuCursor == 1) {
            // Mục 1: Chạy kiểm tra ngoại vi POST & Hoạt ảnh Boot
            s40AboutMenuOpen = false;
            runBootSequence(true);
            currentMode = 8;
            drawAboutAppScreen(true);
          } else if (s40AboutMenuCursor == 2) {
            // Mục 2: Mã QR Cài Đặt Wi-Fi
            s40AboutMenuOpen = false;
            s40AboutPage = 2;
            drawAboutAppScreen(true);
          } else if (s40AboutMenuCursor == 3) {
            // Mục 3: Sang trang kế tiếp
            s40AboutMenuOpen = false;
            s40AboutPage = (s40AboutPage + 1) % 3;
            drawAboutAppScreen(true);
          } else if (s40AboutMenuCursor == 4) {
            // Mục 4: Đóng menu
            s40AboutMenuOpen = false;
            drawAboutAppScreen(true);
          }
        } else if (keyIndex == 6) { // EXIT
          s40AboutMenuOpen = false;
          drawAboutAppScreen(true);
        }
      } else {
        // Duyệt trang bình thường
        if (keyIndex == 0) {
          if (s40AboutPage == 2) {
            // Ở trang QR: Đổi giữa QR Wi-Fi và QR Web
            s40QrModeTab = (s40QrModeTab + 1) % 2;
            drawAboutAppScreen(false);
          } else {
            // Ở trang 1 hoặc 2: Bấm OK mở Menu Tùy Chọn Popup
            s40AboutMenuOpen = true;
            s40AboutMenuCursor = 0;
            drawAboutPopupMenu(tft->width(), tft->height());
            drawSymbianChrome("THÔNG TIN THIẾT BỊ", "Chọn", "< Chọn mục >", "Đóng");
          }
        } else if (keyIndex == 3) { // LEFT
          s40AboutPage = (s40AboutPage + 2) % 3;
          drawAboutAppScreen(true);
        } else if (keyIndex == 4) { // RIGHT
          s40AboutPage = (s40AboutPage + 1) % 3;
          drawAboutAppScreen(true);
        } else if (keyIndex == 1 || keyIndex == 2) { // UP / DOWN
          if (s40AboutPage == 2) {
            s40QrModeTab = (s40QrModeTab + 1) % 2;
            drawAboutAppScreen(false);
          } else {
            s40AboutPage = (s40AboutPage + 1) % 3;
            drawAboutAppScreen(true);
          }
        } else if (keyIndex == 6) { // EXIT
          currentMode = 4;
          refreshActiveScreen();
        }
      }
    } else if (currentMode == 16) {
      if (keyIndex == 0 || keyIndex == 1 || keyIndex == 2 || keyIndex == 3 || keyIndex == 4) {
        s40QrModeTab = (s40QrModeTab + 1) % 2;
        drawQrCodeWifiScreen(false);
      } else if (keyIndex == 6) {
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 9) {
      if (keyIndex == 0) {
        showSymbianToast("ĐANG QUÉT LẠI THẺ NHỚ SD...");
        TestSDCard::runFullElectricalAndSpiProbe();
        drawSdCardTestScreen(true);
      } else if (keyIndex == 4) {
        s40MemSubState = 0;
        currentMode = 10;
        refreshActiveScreen();
      } else if (keyIndex == 6) {
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 10) {
      if (s40MemSubState == 0) {
        if (keyIndex == 0) {
          s40MemCurrentDir = "/";
          s40MemSubState = 1;
          s40MemFileCursor = 0;
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 6) {
          currentMode = 4;
          refreshActiveScreen();
        }
      } else if (s40MemSubState == 1) {
        if (keyIndex == 1 || keyIndex == 3) {
          s40MemFileCursor--;
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 2 || keyIndex == 4) {
          s40MemFileCursor++;
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 0) {
          String names[28];
          size_t sizes[28];
          bool dirs[28];
          int cnt = listMemoryEntriesInDir(s40MemCurrentDir, names, sizes, dirs, 28);
          if (cnt > 0) {
            int idx = ((s40MemFileCursor % cnt) + cnt) % cnt;
            if (names[idx] == "..") {
              s40MemCurrentDir = "/";
              s40MemFileCursor = 0;
              drawMemoryManagerAppScreen();
            } else if (dirs[idx]) {
              s40MemCurrentDir = names[idx];
              int subCnt = countFilesInSdDir(s40MemCurrentDir);
              s40MemFileCursor = (subCnt > 0) ? 1 : 0;
              drawMemoryManagerAppScreen();
            } else {
              String selPath = names[idx];
              String low = selPath;
              low.toLowerCase();
              if (low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac") || low.endsWith(".ogg")) {
                // Tệp âm thanh: Bấm OK phát ngay trên Trình phát nhạc
                refreshMediaPlaylist();
                String cleanAudio = selPath.startsWith("[FS]") ? selPath.substring(4) : selPath;
                String songBase = cleanAudio;
                int sl = songBase.lastIndexOf('/');
                if (sl >= 0) songBase = songBase.substring(sl + 1);

                int foundIdx = -1;
                for (int k = 0; k < s40MediaTrackCount; k++) {
                  if (s40MediaTracks[k].equalsIgnoreCase(songBase) || s40MediaTracks[k].equalsIgnoreCase(cleanAudio)) {
                    foundIdx = k;
                    break;
                  }
                }
                if (foundIdx >= 0) {
                  s40MediaTrackIdx = foundIdx;
                } else if (s40MediaTrackCount < 24) {
                  s40MediaTracks[s40MediaTrackCount] = songBase;
                  s40MediaFileSizes[s40MediaTrackCount] = (sizes[idx] > 0) ? sizes[idx] : 2048000;
                  s40MediaDurations[s40MediaTrackCount] = (s40MediaFileSizes[s40MediaTrackCount] > 16000) ? (s40MediaFileSizes[s40MediaTrackCount] / 16000) : 180;
                  s40MediaFromSd[s40MediaTrackCount] = !selPath.startsWith("[FS]");
                  s40MediaTrackIdx = s40MediaTrackCount;
                  s40MediaTrackCount++;
                } else {
                  s40MediaTracks[0] = songBase;
                  s40MediaTrackIdx = 0;
                }
                s40MediaCurSec = 0;
                s40MediaPlaying = true;
                s40MediaSubState = 0;
                currentMode = 12;
                refreshActiveScreen();
                return;
              } else {
                s40MemSubState = 3;
                drawMemoryManagerAppScreen();
              }
            }
          }
        } else if (keyIndex == 6) {
          if (s40MemCurrentDir != "/") {
            s40MemCurrentDir = "/";
            s40MemFileCursor = 0;
            drawMemoryManagerAppScreen();
          } else {
            s40MemSubState = 0;
            drawMemoryManagerAppScreen();
          }
        }
      } else if (s40MemSubState == 3) {
        if (keyIndex == 1 || keyIndex == 3) {
          s40MemFileCursor--;
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 2 || keyIndex == 4) {
          s40MemFileCursor++;
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 0) {
          String names[28];
          size_t sizes[28];
          bool dirs[28];
          int cnt = listMemoryEntriesInDir(s40MemCurrentDir, names, sizes, dirs, 28);
          if (cnt > 0) {
            int idx = ((s40MemFileCursor % cnt) + cnt) % cnt;
            String selPath = names[idx];
            String low = selPath;
            low.toLowerCase();
            if (low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp") || low.endsWith(".rgb565")) {
              // Bấm OK từ Preview ảnh -> Mở Xem Toàn Màn Hình Tràn Viền 240x320!
              s40MemSubState = 5;
            } else if (low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac") || low.endsWith(".ogg")) {
              refreshMediaPlaylist();
              String cleanAudio = selPath.startsWith("[FS]") ? selPath.substring(4) : selPath;
              String songBase = cleanAudio;
              int sl = songBase.lastIndexOf('/');
              if (sl >= 0) songBase = songBase.substring(sl + 1);

              int foundIdx = -1;
              for (int k = 0; k < s40MediaTrackCount; k++) {
                if (s40MediaTracks[k].equalsIgnoreCase(songBase) || s40MediaTracks[k].equalsIgnoreCase(cleanAudio)) {
                  foundIdx = k;
                  break;
                }
              }
              if (foundIdx >= 0) {
                s40MediaTrackIdx = foundIdx;
              } else if (s40MediaTrackCount < 24) {
                s40MediaTracks[s40MediaTrackCount] = songBase;
                s40MediaFileSizes[s40MediaTrackCount] = (sizes[idx] > 0) ? sizes[idx] : 2048000;
                s40MediaDurations[s40MediaTrackCount] = (s40MediaFileSizes[s40MediaTrackCount] > 16000) ? (s40MediaFileSizes[s40MediaTrackCount] / 16000) : 180;
                s40MediaFromSd[s40MediaTrackCount] = !selPath.startsWith("[FS]");
                s40MediaTrackIdx = s40MediaTrackCount;
                s40MediaTrackCount++;
              }
              s40MediaCurSec = 0;
              s40MediaPlaying = true;
              s40MediaSubState = 0;
              currentMode = 12;
              refreshActiveScreen();
              return;
            } else {
              s40MemSubState = 1;
            }
          }
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 6) {
          s40MemSubState = 1;
          drawMemoryManagerAppScreen();
        }
      } else if (s40MemSubState == 2) {
        if (keyIndex == 1) {
          s40MemPopupCursor = (s40MemPopupCursor + 4) % 5;
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 2) {
          s40MemPopupCursor = (s40MemPopupCursor + 1) % 5;
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 0) {
          String names[28];
          size_t sizes[28];
          bool dirs[28];
          int cnt = listMemoryEntriesInDir(s40MemCurrentDir, names, sizes, dirs, 28);
          if (cnt > 0) {
            int idx = ((s40MemFileCursor % cnt) + cnt) % cnt;
            String selPath = names[idx];
            bool isDir = dirs[idx];
            String low = selPath;
            low.toLowerCase();
            bool isImg = !isDir && (low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp") || low.endsWith(".rgb565"));
            bool isAudio = !isDir && (low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac") || low.endsWith(".ogg"));

            if (isImg) {
              if (s40MemPopupCursor == 0) {
                s40MemSubState = 5; // Xem toàn màn hình
              } else if (s40MemPopupCursor == 1) {
                String targetBg = selPath.startsWith("[FS]") ? selPath.substring(4) : ("sd:" + selPath);
                stCfg.bgImage = targetBg;
                stCfg.bgMode = "image";
                saveConfigToFile();
                showSymbianToast("ĐÃ ĐẶT LÀM HÌNH NỀN!");
                TestAudio::playOkChime();
                s40MemSubState = 1;
              } else if (s40MemPopupCursor == 2) {
                s40MemSubState = 4; // Chi tiết tệp
              } else if (s40MemPopupCursor == 3) {
                deleteSelectedMemoryFile();
                s40MemSubState = 1;
              } else {
                s40MemSubState = 1;
              }
            } else if (isAudio) {
              if (s40MemPopupCursor == 0) {
                // Phát bài hát ngay
                refreshMediaPlaylist();
                String cleanAudio = selPath.startsWith("[FS]") ? selPath.substring(4) : selPath;
                String songBase = cleanAudio;
                int sl = songBase.lastIndexOf('/');
                if (sl >= 0) songBase = songBase.substring(sl + 1);

                int foundIdx = -1;
                for (int k = 0; k < s40MediaTrackCount; k++) {
                  if (s40MediaTracks[k].equalsIgnoreCase(songBase) || s40MediaTracks[k].equalsIgnoreCase(cleanAudio)) {
                    foundIdx = k;
                    break;
                  }
                }
                if (foundIdx >= 0) {
                  s40MediaTrackIdx = foundIdx;
                } else if (s40MediaTrackCount < 24) {
                  s40MediaTracks[s40MediaTrackCount] = songBase;
                  s40MediaFileSizes[s40MediaTrackCount] = (sizes[idx] > 0) ? sizes[idx] : 2048000;
                  s40MediaDurations[s40MediaTrackCount] = (s40MediaFileSizes[s40MediaTrackCount] > 16000) ? (s40MediaFileSizes[s40MediaTrackCount] / 16000) : 180;
                  s40MediaFromSd[s40MediaTrackCount] = !selPath.startsWith("[FS]");
                  s40MediaTrackIdx = s40MediaTrackCount;
                  s40MediaTrackCount++;
                }
                s40MediaCurSec = 0;
                s40MediaPlaying = true;
                s40MediaSubState = 0;
                currentMode = 12;
                refreshActiveScreen();
                return;
              } else if (s40MemPopupCursor == 1) {
                s40MemSubState = 4; // Chi tiết
              } else if (s40MemPopupCursor == 2) {
                deleteSelectedMemoryFile();
                s40MemSubState = 1;
              } else if (s40MemPopupCursor == 3) {
                s40MemSubState = 1;
              } else {
                s40MemSubState = 0;
              }
            } else if (isDir) {
              if (s40MemPopupCursor == 0) {
                if (selPath == "..") {
                  s40MemCurrentDir = "/";
                  s40MemFileCursor = 0;
                } else {
                  s40MemCurrentDir = selPath;
                  s40MemFileCursor = (countFilesInSdDir(s40MemCurrentDir) > 0) ? 1 : 0;
                }
                s40MemSubState = 1;
              } else if (s40MemPopupCursor == 1) {
                s40MemSubState = 4;
              } else if (s40MemPopupCursor == 2) {
                s40MemCurrentDir = "/";
                s40MemFileCursor = 0;
                s40MemSubState = 1;
              } else if (s40MemPopupCursor == 3) {
                s40MemCurrentDir = "/";
                s40MemFileCursor = 0;
                s40MemSubState = 1;
              } else {
                s40MemSubState = 0;
              }
            } else {
              if (s40MemPopupCursor == 0) {
                s40MemSubState = 3;
              } else if (s40MemPopupCursor == 1) {
                s40MemSubState = 4;
              } else if (s40MemPopupCursor == 2) {
                deleteSelectedMemoryFile();
                s40MemSubState = 1;
              } else if (s40MemPopupCursor == 3) {
                s40MemSubState = 1;
              } else {
                s40MemSubState = 0;
              }
            }
          }
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 6) {
          s40MemSubState = 1;
          drawMemoryManagerAppScreen();
        }
      } else if (s40MemSubState == 4) {
        if (keyIndex == 1 || keyIndex == 3) {
          s40MemFileCursor--;
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 2 || keyIndex == 4) {
          s40MemFileCursor++;
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 0) {
          String names[28];
          size_t sizes[28];
          bool dirs[28];
          int cnt = listMemoryEntriesInDir(s40MemCurrentDir, names, sizes, dirs, 28);
          if (cnt > 0) {
            int idx = ((s40MemFileCursor % cnt) + cnt) % cnt;
            String selPath = names[idx];
            bool isDir = dirs[idx];
            String low = selPath;
            low.toLowerCase();
            if (!isDir && (low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp") || low.endsWith(".rgb565"))) {
              s40MemSubState = 5; // Mở ảnh toàn màn hình
              drawMemoryManagerAppScreen();
            } else if (!isDir && (low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac") || low.endsWith(".ogg"))) {
              refreshMediaPlaylist();
              String cleanAudio = selPath.startsWith("[FS]") ? selPath.substring(4) : selPath;
              String songBase = cleanAudio;
              int sl = songBase.lastIndexOf('/');
              if (sl >= 0) songBase = songBase.substring(sl + 1);

              int foundIdx = -1;
              for (int k = 0; k < s40MediaTrackCount; k++) {
                if (s40MediaTracks[k].equalsIgnoreCase(songBase) || s40MediaTracks[k].equalsIgnoreCase(cleanAudio)) {
                  foundIdx = k;
                  break;
                }
              }
              if (foundIdx >= 0) {
                s40MediaTrackIdx = foundIdx;
              } else if (s40MediaTrackCount < 24) {
                s40MediaTracks[s40MediaTrackCount] = songBase;
                s40MediaFileSizes[s40MediaTrackCount] = (sizes[idx] > 0) ? sizes[idx] : 2048000;
                s40MediaDurations[s40MediaTrackCount] = (s40MediaFileSizes[s40MediaTrackCount] > 16000) ? (s40MediaFileSizes[s40MediaTrackCount] / 16000) : 180;
                s40MediaFromSd[s40MediaTrackCount] = !selPath.startsWith("[FS]");
                s40MediaTrackIdx = s40MediaTrackCount;
                s40MediaTrackCount++;
              }
              s40MediaCurSec = 0;
              s40MediaPlaying = true;
              s40MediaSubState = 0;
              currentMode = 12;
              refreshActiveScreen();
              return;
            } else {
              s40MemSubState = 3;
              drawMemoryManagerAppScreen();
            }
          }
        } else if (keyIndex == 6) {
          s40MemSubState = 1;
          drawMemoryManagerAppScreen();
        }
      } else if (s40MemSubState == 5) {
        // TRÌNH XEM ẢNH TRÀN VIỀN TOÀN MÀN HÌNH (FULL-SCREEN SLIDESHOW)
        String names[28];
        size_t sizes[28];
        bool dirs[28];
        int cnt = listMemoryEntriesInDir(s40MemCurrentDir, names, sizes, dirs, 28);
        if (keyIndex == 3) { // TRÁI: Tìm ảnh trước đó trong thư mục
          if (cnt > 0) {
            for (int step = 1; step < cnt; step++) {
              int prevIdx = (s40MemFileCursor + cnt - step) % cnt;
              String low = names[prevIdx];
              low.toLowerCase();
              if (!dirs[prevIdx] && (low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp") || low.endsWith(".rgb565"))) {
                s40MemFileCursor = prevIdx;
                break;
              }
            }
          }
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 4) { // PHẢI: Tìm ảnh kế tiếp trong thư mục
          if (cnt > 0) {
            for (int step = 1; step < cnt; step++) {
              int nextIdx = (s40MemFileCursor + step) % cnt;
              String low = names[nextIdx];
              low.toLowerCase();
              if (!dirs[nextIdx] && (low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".bmp") || low.endsWith(".rgb565"))) {
                s40MemFileCursor = nextIdx;
                break;
              }
            }
          }
          drawMemoryManagerAppScreen();
        } else if (keyIndex == 0) { // OK: Đặt bức ảnh đang xem làm Hình nền màn chờ ngay lập tức!
          if (cnt > 0) {
            int idx = ((s40MemFileCursor % cnt) + cnt) % cnt;
            String selPath = names[idx];
            String targetBg = selPath.startsWith("[FS]") ? selPath.substring(4) : ("sd:" + selPath);
            stCfg.bgImage = targetBg;
            stCfg.bgMode = "image";
            saveConfigToFile();
            showSymbianToast("ĐÃ ĐẶT LÀM HÌNH NỀN!");
            TestAudio::playOkChime();
            drawMemoryManagerAppScreen();
          }
        } else if (keyIndex == 6) { // EXIT: Thoát khỏi chế độ tràn viền về danh sách tệp
          s40MemSubState = 1;
          drawMemoryManagerAppScreen();
        }
      }
    } else if (currentMode == 11) {
      if (s40AiState == AI_STATE_BINDING || s40AiState == AI_STATE_BINDING_CHECKING) {
        if (keyIndex == 0) { // OK: Kiểm tra lại liên kết với XiaoZhi Hub
          TestAudio::playKeyBeep();
          if (WiFi.status() != WL_CONNECTED) {
            s40AiBindStatus = "Chua co Wi-Fi! Hay ket noi Wi-Fi truoc.";
            s40AiOtpCode = "NO WIFI";
            drawXiaoZhiAssistantScreen(true);
            return;
          }
          s40AiBindStatus = "Dang kiem tra voi XiaoZhi Hub...";
          drawXiaoZhiAssistantScreen(true);
          XiaoZhiClient::queryOTA(true);
          if (XiaoZhiClient::isDeviceBound()) {
            TestAudio::playOkChime();
            showSymbianToast("KET NOI XIAOZHI THANH CONG!");
            s40AiState = AI_STATE_IDLE;
            s40AiReplyText = "Xin chao! Thiet bi da ket noi thanh cong voi XiaoZhi Hub! Nhan [OK] de hoi thoai.";
            drawXiaoZhiAssistantScreen(true);
          } else {
            String authCode = XiaoZhiClient::getLastAuthCode();
            if (authCode.length() > 0) {
              s40AiOtpCode = authCode;
              s40AiBindStatus = "Da co OTP! Nhap MAC & OTP tren Hub.";
            } else {
              s40AiBindStatus = "Khong lay duoc OTP. Vui long thu lai!";
            }
            drawXiaoZhiAssistantScreen(true);
          }
        } else if (keyIndex == 6) { // EXIT: Thoát về menu
          currentMode = 4;
          refreshActiveScreen();
        }
        return;
      }

      if (keyIndex == 0) {
        if (s40AiState == AI_STATE_IDLE || s40AiState == AI_STATE_REPLYING) {
          startXiaoZhiListening();
        } else if (s40AiState == AI_STATE_LISTENING) {
          triggerXiaoZhiThinkingAndReply();
        }
      } else if (keyIndex == 1) {
        if (s40AiState == AI_STATE_REPLYING && s40AiScrollLine > 0) {
          s40AiScrollLine--;
          s40AiLastScrollMs = millis();
          drawXiaoZhiConversationBoxOnly();
        }
      } else if (keyIndex == 2) {
        if (s40AiState == AI_STATE_REPLYING && s40AiScrollLine + 5 < s40AiTotalLines) {
          s40AiScrollLine++;
          s40AiLastScrollMs = millis();
          drawXiaoZhiConversationBoxOnly();
        }
      } else if (keyIndex == 3) { // TRÁI (LEFT): Chuyển sang TRÌNH PHÁT NHẠC (Mode 12) & Phát lại bản thu!
        if (s40AiState == AI_STATE_LISTENING) {
          TestAudio::stopVoiceRecording();
          s40AiState = AI_STATE_IDLE;
        }

        if (TestAudio::getRecordedSampleCount() > 0) {
          TestAudio::saveRecordedVoiceToSd("/recording/rec_latest.wav");
        }

        bool hasVoiceRec = (TestAudio::getRecordedSampleCount() > 0) ||
                           (TestSDCard::isMounted() && SD.exists("/recording/rec_latest.wav"));

        if (hasVoiceRec) {
          refreshMediaPlaylist();
          for (int i = 0; i < s40MediaTrackCount; i++) {
            if (s40MediaTracks[i].indexOf("rec_latest.wav") >= 0) {
              s40MediaTrackIdx = i;
              break;
            }
          }
          s40MediaCurSec = 0;
          s40MediaPlaying = true;
          s40MediaSubState = 0;
          currentMode = 12; // Chuyển thẳng sang Trình phát nhạc
          refreshActiveScreen();
          startMediaAudioPlayback();
          showSymbianToast("PHAT QUA TRINH PHAT NHAC");
        } else {
          showSymbianToast("CHUA CO BAN THU AM!");
        }
      } else if (keyIndex == 4) { // PHẢI (RIGHT): Phát thử tiếng bíp test Loa PWM GPIO 15
        showSymbianToast("TEST BIP LOA PWM...");
        TestAudio::playStartupPwmChime(PIN_I2S_SPK_DIN);
      } else if (keyIndex == 5) { // MENU: Buộc hủy liên kết cũ & Lấy mã OTP mới từ XiaoZhi Hub!
        TestAudio::playKeyBeep();
        showSymbianToast("DANG LAY MA OTP MOI...");
        enterXiaoZhiAssistantMode(true);
        drawXiaoZhiAssistantScreen(true);
      } else if (keyIndex == 6) {
        if (s40AiState == AI_STATE_LISTENING) {
          TestAudio::stopVoiceRecording();
          if (TestAudio::getRecordedSampleCount() > 0) {
            TestAudio::saveRecordedVoiceToSd("/recording/rec_latest.wav");
          }
          s40AiState = AI_STATE_IDLE;
          eyeState = 0;
          drawXiaoZhiAssistantScreen(true);
        } else {
          currentMode = 4;
          refreshActiveScreen();
        }
      }
    } else if (currentMode == 12) {
      if (s40MediaSubState == 0) {
        if (keyIndex == 0) { // OK: Play / Pause
          s40MediaPlaying = !s40MediaPlaying;
          if (s40MediaPlaying) startMediaAudioPlayback();
          else stopMediaAudioPlayback();
          drawMediaPlayerAppScreen(false);
        } else if (keyIndex == 1) { // UP: Tăng âm lượng
          TestAudio::setSpeakerVolumePct(TestAudio::getSpeakerVolumePct() + 5);
          drawMediaPlayerAppScreen(false);
        } else if (keyIndex == 2) { // DOWN: Giảm âm lượng
          TestAudio::setSpeakerVolumePct(TestAudio::getSpeakerVolumePct() - 5);
          drawMediaPlayerAppScreen(false);
        } else if (keyIndex == 3) { // LEFT: Bài trước (Prev)
          if (s40MediaTrackCount > 0) {
            s40MediaTrackIdx = (s40MediaTrackIdx + s40MediaTrackCount - 1) % s40MediaTrackCount;
            s40MediaCurSec = 0;
            s40MediaPlaying = true;
            startMediaAudioPlayback();
            drawMediaPlayerAppScreen(false);
          }
        } else if (keyIndex == 4) { // RIGHT: Bài tiếp theo (Next)
          if (s40MediaTrackCount > 0) {
            s40MediaTrackIdx = (s40MediaTrackIdx + 1) % s40MediaTrackCount;
            s40MediaCurSec = 0;
            s40MediaPlaying = true;
            startMediaAudioPlayback();
            drawMediaPlayerAppScreen(false);
          }
        } else if (keyIndex == 6) { // EXIT: Quay lại Menu 4x3
          stopMediaAudioPlayback();
          currentMode = 4;
          refreshActiveScreen();
        }
      } else if (s40MediaSubState == 1) {
        if (keyIndex == 1) {
          s40MediaPopupCursor = (s40MediaPopupCursor + 3) % 4;
          drawMediaPlayerAppScreen(false);
        } else if (keyIndex == 2) {
          s40MediaPopupCursor = (s40MediaPopupCursor + 1) % 4;
          drawMediaPlayerAppScreen(false);
        } else if (keyIndex == 0 || keyIndex == 3 || keyIndex == 4) {
          if (s40MediaPopupCursor == 0) {
            refreshMediaPlaylist();
            s40MediaSubState = 2;
            drawMediaPlayerAppScreen(true);
          } else if (s40MediaPopupCursor == 1) {
            s40MediaVisMode = (s40MediaVisMode + 1) % 3;
            drawMediaPlayerAppScreen(false);
          } else if (s40MediaPopupCursor == 2) {
            s40MediaRepeatMode = (s40MediaRepeatMode + 1) % 3;
            drawMediaPlayerAppScreen(false);
          } else if (s40MediaPopupCursor == 3) {
            s40MediaSubState = 0;
            currentMode = 4;
            refreshActiveScreen();
          }
        } else if (keyIndex == 6) {
          s40MediaSubState = 0;
          drawMediaPlayerAppScreen(true);
        }
      } else if (s40MediaSubState == 2) {
        if (keyIndex == 1 || keyIndex == 3) {
          if (s40MediaTrackCount > 0) {
            s40MediaTrackIdx = (s40MediaTrackIdx + s40MediaTrackCount - 1) % s40MediaTrackCount;
            drawMediaPlayerAppScreen(false);
          }
        } else if (keyIndex == 2 || keyIndex == 4) {
          if (s40MediaTrackCount > 0) {
            s40MediaTrackIdx = (s40MediaTrackIdx + 1) % s40MediaTrackCount;
            drawMediaPlayerAppScreen(false);
          }
        } else if (keyIndex == 0) {
          s40MediaCurSec = 0;
          s40MediaPlaying = true;
          s40MediaSubState = 0;
          startMediaAudioPlayback();
          drawMediaPlayerAppScreen(true);
        } else if (keyIndex == 6) {
          s40MediaSubState = 0;
          drawMediaPlayerAppScreen(true);
        }
      } else if (s40MediaSubState == 3) {
        if (keyIndex == 1) {
          s40MediaPopupCursor = (s40MediaPopupCursor + 3) % 4;
          drawMediaPlayerAppScreen(false);
        } else if (keyIndex == 2) {
          s40MediaPopupCursor = (s40MediaPopupCursor + 1) % 4;
          drawMediaPlayerAppScreen(false);
        } else if (keyIndex == 0) {
          if (s40MediaPopupCursor == 0) {
            s40MediaCurSec = 0;
            s40MediaPlaying = true;
            s40MediaSubState = 0;
            startMediaAudioPlayback();
            drawMediaPlayerAppScreen(true);
          } else if (s40MediaPopupCursor == 1) {
            s40MediaSubState = 4;
            drawMediaPlayerAppScreen(true);
          } else if (s40MediaPopupCursor == 2) {
            deleteSelectedMediaTrack();
            s40MediaSubState = 2;
            drawMediaPlayerAppScreen(true);
          } else if (s40MediaPopupCursor == 3) {
            refreshMediaPlaylist();
            showSymbianToast("ĐÃ CẬP NHẬT DANH SÁCH NHẠC!");
            s40MediaSubState = 2;
            drawMediaPlayerAppScreen(true);
          }
        } else if (keyIndex == 6) {
          s40MediaSubState = 2;
          drawMediaPlayerAppScreen(true);
        }
      } else if (s40MediaSubState == 4) {
        if (keyIndex == 1 || keyIndex == 3) {
          if (s40MediaTrackCount > 0) {
            s40MediaTrackIdx = (s40MediaTrackIdx + s40MediaTrackCount - 1) % s40MediaTrackCount;
            drawMediaPlayerAppScreen(false);
          }
        } else if (keyIndex == 2 || keyIndex == 4) {
          if (s40MediaTrackCount > 0) {
            s40MediaTrackIdx = (s40MediaTrackIdx + 1) % s40MediaTrackCount;
            drawMediaPlayerAppScreen(false);
          }
        } else if (keyIndex == 0) {
          s40MediaCurSec = 0;
          s40MediaPlaying = true;
          s40MediaSubState = 0;
          drawMediaPlayerAppScreen(true);
        } else if (keyIndex == 6) {
          s40MediaSubState = 2;
          drawMediaPlayerAppScreen(true);
        }
      }
    } else if (currentMode == 13) {
      // ======================================================================
      // ĐIỀU KHIỂN CHẾ ĐỘ 13: ĐỒNG HỒ ĐA NĂNG (4 TAB)
      // ======================================================================
      loadClockSuitePrefsIfNeeded();
      if (keyIndex == 3) { // LEFT
        if (s40ClockTab == 0) {
          if (s40AlarmCursor == 3) {
            s40AlarmTuneIdx = (s40AlarmTuneIdx + 3) % 4;
            saveClockSuitePrefs();
            TestAudio::playAlarmTuneStep(s40AlarmTuneIdx, 0);
          } else if (s40AlarmEditField == 1) {
            // Đang sửa GIỜ: Giảm 1 giờ
            s40AlarmHour[s40AlarmCursor] = (s40AlarmHour[s40AlarmCursor] + 23) % 24;
            saveClockSuitePrefs();
          } else if (s40AlarmEditField == 2) {
            // Đang sửa PHÚT: Giảm nhanh 5 phút
            s40AlarmMin[s40AlarmCursor] = (s40AlarmMin[s40AlarmCursor] + 55) % 60;
            saveClockSuitePrefs();
          } else {
            // Đang chọn dòng: Bật/Tắt nhanh báo thức đang chọn
            s40AlarmEnable[s40AlarmCursor] = !s40AlarmEnable[s40AlarmCursor];
            s40LastAlarmTrigMin = -1;
            saveClockSuitePrefs();
          }
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 2 && !s40TimerRunning) {
          // Trong Tab Hẹn giờ (khi tạm dừng): LEFT giảm 10 giây
          if (s40TimerPresetSec > 10) s40TimerPresetSec -= 10;
          else s40TimerPresetSec = 10;
          s40TimerRemainSec = s40TimerPresetSec;
          saveClockSuitePrefs();
          drawClockSuiteAppScreen(false);
        } else {
          s40AlarmEditField = 0;
          s40ClockTab = (s40ClockTab + 3) % 4;
          drawClockSuiteAppScreen(true);
        }
      } else if (keyIndex == 4) { // RIGHT
        if (s40ClockTab == 0) {
          if (s40AlarmCursor == 3) {
            s40AlarmTuneIdx = (s40AlarmTuneIdx + 1) % 4;
            saveClockSuitePrefs();
            TestAudio::playAlarmTuneStep(s40AlarmTuneIdx, 0);
          } else if (s40AlarmEditField == 1) {
            // Đang sửa GIỜ: Tăng 1 giờ
            s40AlarmHour[s40AlarmCursor] = (s40AlarmHour[s40AlarmCursor] + 1) % 24;
            saveClockSuitePrefs();
          } else if (s40AlarmEditField == 2) {
            // Đang sửa PHÚT: Tăng nhanh 5 phút
            s40AlarmMin[s40AlarmCursor] = (s40AlarmMin[s40AlarmCursor] + 5) % 60;
            saveClockSuitePrefs();
          } else {
            // Đang chọn dòng: Bật/Tắt nhanh báo thức đang chọn
            s40AlarmEnable[s40AlarmCursor] = !s40AlarmEnable[s40AlarmCursor];
            s40LastAlarmTrigMin = -1;
            saveClockSuitePrefs();
          }
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 2 && !s40TimerRunning) {
          // Trong Tab Hẹn giờ (khi tạm dừng): RIGHT tăng 10 giây
          s40TimerPresetSec = min((uint32_t)3600, s40TimerPresetSec + 10);
          s40TimerRemainSec = s40TimerPresetSec;
          saveClockSuitePrefs();
          drawClockSuiteAppScreen(false);
        } else {
          s40AlarmEditField = 0;
          s40ClockTab = (s40ClockTab + 1) % 4;
          drawClockSuiteAppScreen(true);
        }
      } else if (keyIndex == 1) { // UP
        if (s40ClockTab == 0) {
          if (s40AlarmEditField == 1) {
            s40AlarmHour[s40AlarmCursor] = (s40AlarmHour[s40AlarmCursor] + 1) % 24;
            saveClockSuitePrefs();
          } else if (s40AlarmEditField == 2) {
            s40AlarmMin[s40AlarmCursor] = (s40AlarmMin[s40AlarmCursor] + 1) % 60;
            saveClockSuitePrefs();
          } else {
            s40AlarmCursor = (s40AlarmCursor + 3) % 4;
          }
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 1) {
          // Trong Tab Bấm giờ: UP lưu vòng (Lap) khi đang chạy, hoặc Reset về 0 khi đang dừng
          if (s40SwRunning) {
            unsigned long curTotal = s40SwElapsedMs + (millis() - s40SwStartMs);
            s40SwLaps[s40SwLapCount % 3] = curTotal;
            s40SwLapCount++;
          } else {
            s40SwElapsedMs = 0;
            s40SwLapCount = 0;
            s40SwLaps[0] = s40SwLaps[1] = s40SwLaps[2] = 0;
          }
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 2) {
          // Tăng +1 phút hẹn giờ
          s40TimerPresetSec = min((uint32_t)3600, s40TimerPresetSec + 60);
          s40TimerRemainSec = s40TimerPresetSec;
          saveClockSuitePrefs();
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 3) {
          if (s40TimeEditField == 0) s40EditHour = (s40EditHour + 1) % 24;
          else if (s40TimeEditField == 1) s40EditMin = (s40EditMin + 1) % 60;
          else if (s40TimeEditField == 2) s40EditDay = (s40EditDay % 31) + 1;
          else if (s40TimeEditField == 3) s40EditMonth = (s40EditMonth % 12) + 1;
          else if (s40TimeEditField == 4) s40EditYear++;
          applyManualTimeToSystem();
          drawClockSuiteAppScreen(false);
        }
      } else if (keyIndex == 2) { // DOWN
        if (s40ClockTab == 0) {
          if (s40AlarmEditField == 1) {
            s40AlarmHour[s40AlarmCursor] = (s40AlarmHour[s40AlarmCursor] + 23) % 24;
            saveClockSuitePrefs();
          } else if (s40AlarmEditField == 2) {
            s40AlarmMin[s40AlarmCursor] = (s40AlarmMin[s40AlarmCursor] + 59) % 60;
            saveClockSuitePrefs();
          } else {
            s40AlarmCursor = (s40AlarmCursor + 1) % 4;
          }
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 1) {
          s40SwRunning = false;
          s40SwElapsedMs = 0;
          s40SwLapCount = 0;
          s40SwLaps[0] = s40SwLaps[1] = s40SwLaps[2] = 0;
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 2) {
          // Giảm -1 phút hẹn giờ (tối thiểu 10 giây)
          if (s40TimerPresetSec > 60) s40TimerPresetSec -= 60;
          else s40TimerPresetSec = 10;
          s40TimerRemainSec = s40TimerPresetSec;
          saveClockSuitePrefs();
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 3) {
          if (s40TimeEditField == 0) s40EditHour = (s40EditHour + 23) % 24;
          else if (s40TimeEditField == 1) s40EditMin = (s40EditMin + 59) % 60;
          else if (s40TimeEditField == 2) s40EditDay = (s40EditDay + 29) % 31 + 1;
          else if (s40TimeEditField == 3) s40EditMonth = (s40EditMonth + 10) % 12 + 1;
          else if (s40TimeEditField == 4 && s40EditYear > 2024) s40EditYear--;
          applyManualTimeToSystem();
          drawClockSuiteAppScreen(false);
        }
      } else if (keyIndex == 0) { // OK
        if (s40ClockTab == 0) {
          if (s40AlarmCursor == 3) {
            TestAudio::playAlarmTuneStep(s40AlarmTuneIdx, 0);
            showSymbianToast(String("ĐÃ CHỌN: ") + TestAudio::getAlarmTuneName(s40AlarmTuneIdx));
          } else {
            // Chu trình OK trên dòng báo thức: 0 (Chọn dòng) -> 1 (Chỉnh Giờ) -> 2 (Chỉnh Phút) -> Lưu & Bật!
            if (s40AlarmEditField == 0) {
              s40AlarmEditField = 1;
            } else if (s40AlarmEditField == 1) {
              s40AlarmEditField = 2;
            } else {
              s40AlarmEditField = 0;
              s40AlarmEnable[s40AlarmCursor] = true;
              s40LastAlarmTrigMin = -1;
              saveClockSuitePrefs();
              TestAudio::playOkChime();
              showSymbianToast("ĐÃ LƯU & BẬT BÁO THỨC!");
            }
          }
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 1) {
          if (s40SwRunning) {
            s40SwElapsedMs += (millis() - s40SwStartMs);
            s40SwRunning = false;
          } else {
            s40SwStartMs = millis();
            s40SwRunning = true;
          }
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 2) {
          if (s40TimerRemainSec == 0) s40TimerRemainSec = s40TimerPresetSec;
          s40TimerRunning = !s40TimerRunning;
          s40TimerLastTickMs = millis();
          drawClockSuiteAppScreen(false);
        } else if (s40ClockTab == 3) {
          s40TimeEditField = (s40TimeEditField + 1) % 5;
          drawClockSuiteAppScreen(false);
        }
      } else if (keyIndex == 6) { // EXIT
        if (s40ClockTab == 0 && s40AlarmEditField > 0) {
          s40AlarmEditField = 0;
          s40AlarmEnable[s40AlarmCursor] = true;
          s40LastAlarmTrigMin = -1;
          saveClockSuitePrefs();
          showSymbianToast("ĐÃ LƯU BÁO THỨC!");
          drawClockSuiteAppScreen(false);
          return;
        }
        if (s40ClockTab == 3) {
          applyManualTimeToSystem();
          showSymbianToast("ĐÃ CẬP NHẬT THỜI GIAN!");
        }
        currentMode = 4;
        refreshActiveScreen();
      }
    } else if (currentMode == 14) {
      // ======================================================================
      // ĐIỀU KHIỂN CHẾ ĐỘ 14: LỊCH VẠN NIÊN (CALENDAR)
      // ======================================================================
      if (keyIndex == 3) { // LEFT: Tháng trước
        s40CalMonth--;
        if (s40CalMonth < 1) { s40CalMonth = 12; s40CalYear--; }
        drawCalendarAppScreen(false);
      } else if (keyIndex == 4) { // RIGHT: Tháng sau
        s40CalMonth++;
        if (s40CalMonth > 12) { s40CalMonth = 1; s40CalYear++; }
        drawCalendarAppScreen(false);
      } else if (keyIndex == 1) { // UP: Năm tiếp theo
        s40CalYear++;
        drawCalendarAppScreen(false);
      } else if (keyIndex == 2) { // DOWN: Năm trước
        if (s40CalYear > 1970) s40CalYear--;
        drawCalendarAppScreen(false);
      } else if (keyIndex == 0) { // OK: Quay về Tháng/Năm hiện tại
        s40CalInitialized = false;
        drawCalendarAppScreen(false);
      } else if (keyIndex == 6) { // EXIT: Quay về Menu 4x3
        currentMode = 4;
        refreshActiveScreen();
      }
    }
  }

  static void initDriver() {
    if (useST7789) {
      tft7789->init(240, 320);
      tft7789->setSPISpeed(80000000); // Tăng tốc độ xung SPI phần cứng lên 80 MHz tối đa của ESP32-S3!
      tft7789->invertDisplay(inverted);
      tft7789->setRotation(rotation);
      tft = tft7789;
    } else {
      tft9341->begin(80000000);
      tft9341->invertDisplay(inverted);
      tft9341->setRotation(rotation);
      tft = tft9341;
    }
  }

  // ============================================================================
  // HOẠT ẢNH BOOT CYBERPUNK & KIỂM TRA TRẠNG THÁI NGOẠI VI (P.O.S.T SYSTEM LOAD)
  // Khởi động hệ thống với chẩn đoán phần cứng thời gian thực và thanh tiến trình
  // ============================================================================
  void runBootSequence(bool playChime) {
    if (!tft) return;
    isScreenSleeping = false;
    setBacklightBrightness(screenBrightnessPct > 0 ? screenBrightnessPct : 80);

    int w = tft->width();
    int h = tft->height();

    // 1. Xóa màn hình về nền không gian sâu (Deep Cyber Black)
    tft->fillScreen(0x0000);

    // 2. Vẽ viền công nghệ Cyberpunk (Outer HUD Box & Tech Brackets)
    tft->drawRect(2, 2, w - 4, h - 4, 0x1186); // Cyber border
    tft->drawRect(3, 3, w - 6, h - 6, 0x01A3);
    // 4 góc ke công nghệ (Corner Brackets)
    tft->drawFastHLine(2, 2, 16, C_NEON_CYAN);
    tft->drawFastVLine(2, 2, 16, C_NEON_CYAN);
    tft->drawFastHLine(w - 18, 2, 16, C_NEON_CYAN);
    tft->drawFastVLine(w - 3, 2, 16, C_NEON_CYAN);
    tft->drawFastHLine(2, h - 3, 16, C_NEON_CYAN);
    tft->drawFastVLine(2, h - 18, 16, C_NEON_CYAN);
    tft->drawFastHLine(w - 18, h - 3, 16, C_NEON_CYAN);
    tft->drawFastVLine(w - 3, h - 18, 16, C_NEON_CYAN);

    // Header Tag nhỏ trên đỉnh
    tft->fillRect(45, 2, 150, 10, 0x0821);
    tft->drawRect(45, 2, 150, 10, 0x1A6B);
    tft->setTextSize(1);
    tft->setTextColor(C_YELLOW, 0x0821);
    tft->setCursor(50, 3);
    tft->print("* SPACE OS BOOTLOADER *");

    // 3. Hoạt ảnh Logo Vệ Tinh / Quỹ Đạo Trung Tâm (Orbital Satellite Core)
    int cx = w / 2; // 120
    int cy = 46;
    // Vòng quỹ đạo ngoài cùng
    tft->drawCircle(cx, cy, 26, 0x1186);
    tft->drawCircle(cx, cy, 20, 0x194B);
    tft->drawCircle(cx, cy, 14, 0x03FF);
    // Lõi năng lượng phát sáng
    tft->fillCircle(cx, cy, 7, C_NEON_CYAN);
    tft->fillCircle(cx, cy, 3, C_WHITE);
    // Chữ thập tâm ngắm
    tft->drawFastHLine(cx - 32, cy, 8, C_NEON_CYAN);
    tft->drawFastHLine(cx + 24, cy, 8, C_NEON_CYAN);
    tft->drawFastVLine(cx, cy - 32, 8, C_NEON_CYAN);
    tft->drawFastVLine(cx, cy + 24, 8, C_NEON_CYAN);
    // Vệ tinh quay quanh quỹ đạo
    tft->fillCircle(cx + 16, cy - 12, 3, C_YELLOW);
    tft->fillCircle(cx - 15, cy + 13, 2, C_NEON_GREEN);

    // Tiêu đề trạm & Vi xử lý
    tft->setTextColor(C_WHITE, 0x0000);
    tft->setTextSize(1);
    tft->setCursor(54, 76);
    tft->print("TRẠM DECOR VŨ TRỤ");
    tft->setTextColor(C_SLATE, 0x0000);
    tft->setCursor(24, 89);
    tft->print("ESP32-S3 N16R8 | DUAL 240MHz");
    tft->setTextColor(C_NEON_GREEN, 0x0000);
    tft->setCursor(52, 101);
    tft->printf("HỆ ĐIỀU HÀNH %s", FIRMWARE_VERSION);

    // Đường kẻ phân cách phát sáng
    tft->drawFastHLine(14, 113, w - 28, 0x194B);
    tft->drawFastHLine(40, 113, w - 80, C_NEON_CYAN);

    // 4. Bảng Kiểm Tra Ngoại Vi (POST Diagnostic Card)
    int cardX = 8;
    int cardY = 118;
    int cardW = w - 16;
    int cardH = 138;
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 5, 0x0842);
    tft->drawRoundRect(cardX, cardY, cardW, cardH, 5, 0x2187);

    // Tiêu đề card POST
    tft->fillRoundRect(cardX + 2, cardY + 2, cardW - 4, 16, 4, 0x0210);
    tft->setTextColor(C_YELLOW, 0x0210);
    tft->setCursor(cardX + 16, cardY + 6);
    tft->print("BẢNG KIỂM TRA NGOẠI VI (P.O.S.T)");

    // Lambda helper vẽ từng hàng kiểm tra ngoại vi
    auto drawPostRow = [&](int rowIdx, const char* label, const char* detail, bool isOk) {
      int ry = cardY + 22 + rowIdx * 19;
      // Khung badge [ OK ] hoặc [ -- ]
      tft->fillRoundRect(cardX + 6, ry, 34, 15, 3, isOk ? 0x0320 : 0x2104);
      tft->drawRoundRect(cardX + 6, ry, 34, 15, 3, isOk ? C_NEON_GREEN : C_GRAY);
      tft->setTextColor(isOk ? C_NEON_GREEN : C_GRAY, isOk ? 0x0320 : 0x2104);
      tft->setCursor(cardX + 8, ry + 4);
      tft->print(isOk ? "[ OK ]" : "[ -- ]");

      // Tên nhãn module
      tft->setTextColor(C_WHITE, 0x0842);
      tft->setCursor(cardX + 44, ry + 4);
      tft->printf("%-10s:", label);

      // Chi tiết thông số
      tft->setTextColor(isOk ? C_NEON_CYAN : C_SLATE, 0x0842);
      tft->setCursor(cardX + 112, ry + 4);
      tft->print(detail);
    };

    // Lambda helper vẽ thanh tiến trình tải hệ thống
    auto updateBootProgress = [&](int pct, const char* statusMsg) {
      pct = constrain(pct, 0, 100);
      // Xóa nhãn trạng thái cũ
      tft->fillRect(14, 260, w - 28, 14, 0x0000);
      tft->setTextColor(C_WHITE, 0x0000);
      tft->setTextSize(1);
      tft->setCursor(20, 263);
      tft->print(statusMsg);

      // Khung thanh tiến trình
      int barX = 16;
      int barY = 277;
      int barW = w - 32;
      int barH = 11;
      tft->drawRoundRect(barX, barY, barW, barH, 3, 0x2965);
      tft->fillRect(barX + 2, barY + 2, barW - 4, barH - 4, 0x0821);

      // Dải màu tiến trình neon
      int fillW = ((barW - 4) * pct) / 100;
      if (fillW > 0) {
        tft->fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 2, C_NEON_CYAN);
        // Đốm sáng đỉnh thanh chạy
        if (fillW < barW - 6) {
          tft->fillRect(barX + 2 + fillW - 2, barY + 2, 3, barH - 4, C_WHITE);
        }
      }

      // Phần trăm tiến trình
      tft->fillRect(16, 292, w - 32, 14, 0x0000);
      tft->setTextColor(C_NEON_GREEN, 0x0000);
      tft->setCursor(20, 294);
      tft->printf("TIẾN TRÌNH: %3d%%", pct);
      tft->setTextColor(C_GRAY, 0x0000);
      tft->setCursor(136, 294);
      tft->print("STANDBY LOAD");
    };

    // --- BẮT ĐẦU CHU KỲ KIỂM TRA TỪNG MODULE NGOẠI VI ---
    updateBootProgress(8, "Khởi tạo nhân hệ thống...");
    delay(70); yield();

    // HÀNG 0: BỘ NHỚ RAM & PSRAM
    uint32_t ramKb = ESP.getFreeHeap() / 1024;
    float psramMb = (float)ESP.getFreePsram() / (1024.0f * 1024.0f);
    char ramBuf[24];
    snprintf(ramBuf, sizeof(ramBuf), "%uK + %.1fMB", (unsigned)ramKb, psramMb);
    drawPostRow(0, "RAM/PSRAM", ramBuf, psramFound() || ramKb > 100);
    TestAudio::playKeyBeep();
    updateBootProgress(24, "Kiểm tra lưu trữ Flash...");
    delay(100); yield();

    // HÀNG 1: BỘ NHỚ FLASH LITTLEFS
    float fsMb = (float)LittleFS.totalBytes() / (1024.0f * 1024.0f);
    char fsBuf[24];
    snprintf(fsBuf, sizeof(fsBuf), "LittleFS %.1fMB", fsMb > 0 ? fsMb : 15.0f);
    drawPostRow(1, "FLASH FS", fsBuf, LittleFS.totalBytes() > 0);
    TestAudio::playKeyBeep();
    updateBootProgress(40, "Kiểm tra màn hình hiển thị...");
    delay(100); yield();

    // HÀNG 2: MÀN HÌNH ST7789 IPS & PWM BACKLIGHT
    drawPostRow(2, "ST7789 LCD", "240x320 SPI 80M", true);
    TestAudio::playKeyBeep();
    updateBootProgress(56, "Quét cảm biến SHT31 & Chạm...");
    delay(100); yield();

    // HÀNG 3: CẢM BIẾN SHT31 & CẢM ỨNG TTP223
    TestSensors::init();
    bool shtOk = TestSensors::isSht31Connected();
    char sensBuf[24];
    if (shtOk) {
      snprintf(sensBuf, sizeof(sensBuf), "0x%02X %.1fC", TestSensors::getDetectedAddress(), TestSensors::getTemperatureC());
    } else {
      snprintf(sensBuf, sizeof(sensBuf), "TTP223 GPIO20");
    }
    drawPostRow(3, "CẢM BIẾN", sensBuf, true);
    TestAudio::playKeyBeep();
    updateBootProgress(72, "Khởi tạo cụm phím ADC & Mic...");
    delay(100); yield();

    // HÀNG 4: BÀN PHÍM 7 NÚT & MICRO INMP441 + LOA PWM
    TestButtons::init();
    TestAudio::init();
    drawPostRow(4, "PHÍM / ÂM", "ADC3 & INMP441", true);
    TestAudio::playKeyBeep();
    updateBootProgress(88, "Kiểm tra khe thẻ nhớ Micro SD...");
    delay(100); yield();

    // HÀNG 5: THẺ NHỚ MICRO SD SPI
    TestSDCard::init();
    bool sdOk = TestSDCard::isMounted();
    char sdBuf[24];
    if (sdOk) {
      snprintf(sdBuf, sizeof(sdBuf), "FAT32 %uMB", (unsigned)TestSDCard::getCardSizeMB());
    } else {
      snprintf(sdBuf, sizeof(sdBuf), "SPI D5-D8 Ready");
    }
    drawPostRow(5, "THẺ NHỚ SD", sdBuf, true);
    TestAudio::playKeyBeep();
    delay(100); yield();

    // HOÀN TẤT: 100% TIẾN TRÌNH & PHÁT ÂM HIỆU STARTUP
    updateBootProgress(100, "HỆ ĐIỀU HÀNH SẴN SÀNG!");
    tft->drawFastHLine(14, 258, w - 28, C_NEON_GREEN);

    if (playChime) {
      TestAudio::playStartupChime();
    }
    delay(450); yield();

    // Chuyển sang Màn hình chờ (Mode 0)
    tft->fillScreen(C_BLACK);
    currentMode = 0;
    refreshActiveScreen();
  }

  void init() {
    Serial.println("\n-------------------------------------------------------");
    Serial.println("🖥️ [DISPLAY] KHỞI TẠO MÀN HÌNH ST7789 240x320 & BỘ ĐIỀU KHIỂN SYMBIAN S40 (4x3)");
    Serial.printf("   + Pins: SCK=%d, MOSI=%d, RST=%d, DC=%d, CS=%d, BL(PWM)=%d\n",
                  PIN_TFT_SCK, PIN_TFT_MOSI, PIN_TFT_RST, PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_BL);
    Serial.println("-------------------------------------------------------");

    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, 0);
    pinMode(PIN_TOUCH_TTP223, INPUT_PULLDOWN); // Cảm biến chạm điện dung TTP223 trên GPIO 20 (Active HIGH, chống nhiễu khi chưa cắm)
    lastTtp223State = (digitalRead(PIN_TOUCH_TTP223) == HIGH);
    lastUserActivityMs = millis();
    isScreenSleeping = false;
    setBacklightBrightness(screenBrightnessPct);

    if (!spiBus) {
      spiBus = new SPIClass(FSPI);
      spiBus->begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
    }
    if (!tft7789) tft7789 = new VnST7789(spiBus, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
    if (!tft9341) tft9341 = new VnILI9341(spiBus, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

    initDriver();
    loadConfigFromFile();
    loadSystemSettingsPrefsIfNeeded();
    loadClockSuitePrefsIfNeeded();
    loadGameScoresPrefsIfNeeded();
    setBacklightBrightness(screenBrightnessPct);

    // Chạy hoạt ảnh Boot Cyberpunk, Kiểm tra ngoại vi POST & Màn hình nạp hệ thống
    runBootSequence(true);

    Serial.println("✅ Đã hiển thị Màn Hình Chờ (Standby Screen) & Menu 4x3 (12 Icon) lên màn hình ST7789!");
    printHelp();
  }

  void updateLiveWeather(float tempC, float humPct) {
    float diffT = fabs(stCfg.temp - tempC);
    int newHum = (int)roundf(humPct);
    int diffH = abs(stCfg.humidity - newHum);
    stCfg.temp = tempC;
    stCfg.humidity = newHum;

    if (tft && !isScreenSleeping && currentMode == 0 && (diffT >= 0.2f || diffH >= 1)) {
      drawStandbyScreenFull();
    }
  }

  void cycleScreenMode() {
    if (isScreenSleeping) {
      isScreenSleeping = false;
      setBacklightBrightness(screenBrightnessPct);
      refreshActiveScreen();
      return;
    }
    currentMode = (currentMode + 1) % 4;
    refreshActiveScreen();
  }

  void cycleHudStyle(int dir) {
    int next = ((int)hudStyle + dir + 4) % 4;
    hudStyle = (uint8_t)next;
    if (currentMode == 2) {
      drawPcHudScreen(true);
    }
  }

  void reloadStandbyConfig() {
    standbyCacheValid = false;
    lastStandbyCacheSig = "";
    loadConfigFromFile();
    if (isScreenSleeping) {
      isScreenSleeping = false;
      setBacklightBrightness(screenBrightnessPct);
    }
    currentMode = 0;
    drawStandbyScreenFull();
  }

  void setScreenMode(const String& mode) {
    uint8_t newMode = currentMode;
    if (mode == "standby") newMode = 0;
    else if (mode == "emoji") newMode = 1;
    else if (mode == "pchud" || mode == "pc_hud") newMode = 2;
    else if (mode == "diag") newMode = 3;
    else if (mode == "symbian" || mode == "menu") newMode = 4;
    else if (mode == "xiaozhi" || mode == "assistant") newMode = 11;
    else if (mode == "media" || mode == "music") newMode = 12;
    else if (mode == "clock" || mode == "alarm") newMode = 13;
    else if (mode == "calendar") newMode = 14;
    else if (mode == "games" || mode == "game") { s40GameActiveId = 0; newMode = 15; }
    else if (mode == "qr_wifi" || mode == "qr" || mode == "wifi_qr") newMode = 16;
    else if (mode == "about" || mode == "info") newMode = 8;

    if (newMode != currentMode) {
      currentMode = newMode;
      refreshActiveScreen();
    } else if (!isScreenSleeping && currentMode == 0) {
      drawStandbyScreenFull();
    } else if (!isScreenSleeping && currentMode == 3) {
      drawHardwareDiagScreen(true);
    }
  }

  void setEmojiState(const String& state, const String& subtitle) {
    uint8_t newState = eyeState;
    if (state == "idle") newState = 0;
    else if (state == "blink") newState = 1;
    else if (state == "happy") newState = 2;
    else if (state == "left") newState = 3;
    else if (state == "right") newState = 4;
    else if (state == "up") newState = 5;
    else if (state == "down") newState = 6;
    else if (state == "angry") newState = 7;
    else if (state == "confused") newState = 8;
    else if (state == "wink-left") newState = 9;
    else if (state == "wink-right") newState = 10;
    else if (state == "sleepy") newState = 11;

    bool stateChanged = (newState != eyeState);
    eyeState = newState;
    lastExternalEmojiSync = millis();
    isAutoBlinkActive = false;

    bool subChanged = false;
    if (subtitle.length() > 0 && subtitle != customSubtitle) {
      customSubtitle = subtitle;
      subChanged = true;
    }

    if (currentMode == 1 && tft && !isScreenSleeping) {
      if (stateChanged) {
        drawXiaoZhiEyesBox(8, 28, tft->width() - 16, 186, eyeState, 2);
      }
      if (subChanged) {
        drawEmojiSubtitleBox();
      }
    }
  }

  void setHudStyle(const String& style) {
    uint8_t newStyle = hudStyle;
    if (style == "bars") newStyle = 0;
    else if (style == "gauges") newStyle = 1;
    else if (style == "graph") newStyle = 2;
    else if (style == "matrix") newStyle = 3;

    if (newStyle != hudStyle) {
      hudStyle = newStyle;
      if (currentMode == 2) {
        drawPcHudScreen(true);
      }
    } else if (currentMode == 2) {
      drawPcHudScreen(true);
    }
  }

  String getScreenModeName() {
    if (currentMode == 1) return "emoji";
    if (currentMode == 2) return "pchud";
    if (currentMode == 3) return "diag";
    if (currentMode == 11) return "xiaozhi";
    if (currentMode == 15) return "games";
    if (currentMode == 16) return "qr_wifi";
    if (currentMode >= 4) return "symbian";
    return "standby";
  }

  String getSubtitle() {
    return customSubtitle;
  }

  String getHudStyleName() {
    const char* styles[] = { "bars", "gauges", "graph", "matrix" };
    return styles[hudStyle % 4];
  }

  void loop() {
    if (!tft) return;
    unsigned long now = millis();

    TestSDCard::loop();

    // 1. Đọc cảm biến chạm điện dung TTP223 trên chân GPIO 20 (PIN_TOUCH_TTP223)
    bool ttpNow = (digitalRead(PIN_TOUCH_TTP223) == HIGH);
    if (ttpNow != lastTtp223State && (now - lastTtp223ChangeMs >= 65)) {
      lastTtp223ChangeMs = now;
      lastTtp223State = ttpNow;
      if (ttpNow) { // Phát hiện sườn lên (Chạm ngón tay vào TTP223)
        lastUserActivityMs = now;
        if (isScreenSleeping) {
          isScreenSleeping = false;
          setBacklightBrightness(screenBrightnessPct);
          Serial.println("👆 [TTP223 GPIO20] Đã đánh thức màn hình từ chế độ AOD / Sleep bằng cảm biến chạm!");
          refreshActiveScreen();
          return;
        } else {
          Serial.println("👆 [TTP223 GPIO20] Chạm cảm biến TTP223!");
          if (currentMode == 0) {
            // Chạm phím Touch TTP223 (GPIO 20) tại Màn hình chờ: Đổi kiểu đồng hồ hiển thị
            const char* clkStyles[4] = { "digital", "retro-flip", "analog", "compact" };
            int curS = 0;
            for (int i = 0; i < 4; i++) {
              if (stCfg.clockStyle == clkStyles[i]) { curS = i; break; }
            }
            curS = (curS + 1) % 4;
            stCfg.clockStyle = clkStyles[curS];
            saveConfigToFile();
            showSymbianToast(String("DONG HO: ") + stCfg.clockStyle);
            refreshActiveScreen();
          } else if (currentMode == 1) {
            eyeState = (eyeState + 1) % 12;
            drawFullEmojiScreen();
          } else if (currentMode == 15 && s40GameActiveId == 2) {
            // Hỗ trợ chạm TTP223 để vỗ cánh trong game Flappy Bird!
            handleGameCenterKey(0);
          }
        }
      }
    }

    // 2. Kiểm tra Hẹn giờ đếm ngược (Countdown Timer) chạy ngầm (Kể cả khi đang ở المàn hình chờ hay AOD/Sleep!)
    loadClockSuitePrefsIfNeeded();
    if (s40TimerRunning && (now - s40TimerLastTickMs >= 1000)) {
      s40TimerLastTickMs = now;
      if (s40TimerRemainSec > 0) {
        s40TimerRemainSec--;
        if (!isScreenSleeping && !s40PushAlertActive && currentMode == 13 && s40ClockTab == 2) {
          drawClockSuiteAppScreen(false);
        }
      }
      if (s40TimerRemainSec == 0) {
        s40TimerRunning = false;
        char sub[48];
        snprintf(sub, sizeof(sub), "Đã đếm ngược xong %02u phút %02u giây!",
                 (unsigned)(s40TimerPresetSec / 60), (unsigned)(s40TimerPresetSec % 60));
        triggerPushAlertModal(2, 0, "ĐÃ HẾT GIỜ HẸN ĐẾM NGƯỢC!", String(sub), "00:00");
        return;
      }
    }

    // 2B. Kiểm tra Báo thức hệ thống (Alarm Clock + Snooze) TRƯỚC KHI kiểm tra AOD/Sleep để luôn đánh thức màn hình!
    {
      static unsigned long s40LastBgAlarmCheckMs = 0;
      if (now - s40LastBgAlarmCheckMs >= 1000) {
        s40LastBgAlarmCheckMs = now;
        int hr, mn, sc, wd, dy, mo, yr;
        getCurrentDateTime(hr, mn, sc, wd, dy, mo, yr);
        int curMinOfDay = hr * 60 + mn;
        if (curMinOfDay != s40LastAlarmTrigMin) {
          // Kiểm tra 3 mốc Báo thức chính
          bool triggered = false;
          for (int a = 0; a < 3; a++) {
            if (s40AlarmEnable[a] && hr == (int)s40AlarmHour[a] && mn == (int)s40AlarmMin[a]) {
              s40LastAlarmTrigMin = curMinOfDay;
              char bigT[12], subT[48];
              snprintf(bigT, sizeof(bigT), "%02u:%02u", s40AlarmHour[a], s40AlarmMin[a]);
              snprintf(subT, sizeof(subT), "Đến giờ Báo thức #%d (Hằng ngày)", a + 1);
              triggerPushAlertModal(1, (uint8_t)a, "CHUÔNG BÁO THỨC ĐẾN GIỜ!", String(subT), String(bigT));
              triggered = true;
              break;
            }
          }
          // Kiểm tra mốc Hoãn báo thức 5 phút (Snooze)
          if (!triggered && s40SnoozeActive && hr == (int)s40SnoozeHour && mn == (int)s40SnoozeMin) {
            s40LastAlarmTrigMin = curMinOfDay;
            s40SnoozeActive = false;
            char bigT[12], subT[48];
            snprintf(bigT, sizeof(bigT), "%02u:%02u", s40SnoozeHour, s40SnoozeMin);
            snprintf(subT, sizeof(subT), "Báo lại sau 5 phút (Báo thức #%u)", (unsigned)(s40SnoozeAlarmIdx + 1));
            triggerPushAlertModal(1, s40SnoozeAlarmIdx, "BÁO THỨC LẠI (SNOOZE)!", String(subT), String(bigT));
          }
        }
      }
    }

    // 2C. Nếu đang hiển thị THÔNG BÁO ĐẨY TOÀN MÀN HÌNH (Push Alert Modal):
    if (s40PushAlertActive) {
      lastUserActivityMs = now; // Giữ màn hình luôn sáng 100% khi đang có thông báo đẩy
      if (now - s40PushAlertLastAnimMs >= 360) {
        s40PushAlertLastAnimMs = now;
        s40PushAlertAnimPhase++;
        drawPushAlertModal(false);
      }
      // Phát chuông báo theo giai điệu S40 đã chọn (Nokia Tune, Standard Beeps, SMS Morse, 4 Nốt)
      unsigned long stepInterval = (s40PushAlertType == 1 && s40AlarmTuneIdx == 1) ? 1400UL : 750UL;
      if ((now - s40PushAlertStartMs < 60000UL) && (now - s40PushAlertLastBeepMs >= stepInterval)) {
        s40PushAlertLastBeepMs = now;
        TestAudio::playAlarmTuneStep(s40PushAlertType == 1 ? s40AlarmTuneIdx : 0, s40PushAlertAnimPhase);
      }
      return;
    }

    // 3. Kiểm tra hết thời gian sáng màn hình (Screen Timeout) -> Chuyển sang AOD hoặc Sleep
    uint16_t timeoutSec = TIMEOUT_SECONDS_LIST[screenTimeoutIdx % 6];
    if (timeoutSec > 0 && !isScreenSleeping) {
      if (now - lastUserActivityMs >= (unsigned long)timeoutSec * 1000UL) {
        isScreenSleeping = true;
        if (alwaysOnDisplayEnabled) {
          // Chế độ Always On Display (AOD): Nền đen tuyệt đối 100%, hạ đèn nền xuống siêu mờ ~1.5% (PWM = 4)
          ledcWrite(0, 4);
          tft->fillScreen(C_BLACK);
          drawAlwaysOnDisplayScreen(true);
          Serial.printf("🌙 [AOD MODE] Chuyển sang Always On Display (Đèn nền siêu mờ PWM=4) sau %u giây.\n", (unsigned)timeoutSec);
        } else {
          // Chế độ Sleep: Xóa đen màn hình và tắt hoàn toàn đèn nền (PWM=0)
          tft->fillScreen(C_BLACK);
          ledcWrite(0, 0);
          Serial.printf("🌙 [SLEEP MODE] Đã tắt hoàn toàn màn hình sau %u giây.\n", (unsigned)timeoutSec);
        }
      }
    }

    // Nếu đang ở trạng thái AOD / Sleep:
    if (isScreenSleeping) {
      if (alwaysOnDisplayEnabled && (now - lastSecondTick >= 1000)) {
        lastSecondTick = now;
        drawAlwaysOnDisplayScreen(false);
      }
      return;
    }

    bool wifiNow = (WiFi.status() == WL_CONNECTED);
    if (wifiNow && !ntpConfigured) {
      configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
      ntpConfigured = true;
      Serial.println("🕒 [NTP] Đã kích hoạt đồng bộ thời gian thực GMT+7 (pool.ntp.org)");
    }

    if (wifiNow != lastWifiState) {
      lastWifiState = wifiNow;
      refreshActiveScreen();
    }

    if (now - lastSecondTick >= 1000) {
      lastSecondTick = now;

      // Tự động cập nhật đồng hồ nhỏ trên thanh trạng thái (cạnh biểu tượng Pin) ở Menu 4x3 và mọi ứng dụng (Mode 4..15)
      if (currentMode >= 4) {
        updateSymbianTopStatusBarClock();
      }

      if (currentMode == 0) {
        updateStandbyClockDigits(false);
      } else if (currentMode == 2) {
        drawPcHudScreen(false);
      } else if (currentMode == 9) {
        drawSdCardTestScreen(false);
      } else if (currentMode == 13 && s40ClockTab == 2 && s40TimerRunning) {
        drawClockSuiteAppScreen(false);
      } else if (currentMode == 12 && s40MediaSubState == 0 && s40MediaPlaying) {
        uint16_t tot = (s40MediaTrackCount > 0) ? s40MediaDurations[s40MediaTrackIdx] : 200;
        s40MediaCurSec++;
        bool taskFinished = !TestAudio::isVoicePlaybackAsyncRunning() && (s40MediaCurSec >= 2);
        if (s40MediaCurSec >= tot || taskFinished) {
          s40MediaCurSec = 0;
          if (s40MediaTrackCount > 0 && s40MediaTracks[s40MediaTrackIdx].indexOf("rec_latest") >= 0 && taskFinished) {
            // Đã phát xong bản ghi âm -> Tạm dừng để người dùng chủ động bấm nghe lại hoặc đổi bài
            s40MediaPlaying = false;
            stopMediaAudioPlayback();
          } else if (s40MediaRepeatMode == 0 && s40MediaTrackCount > 0) {
            s40MediaTrackIdx = (s40MediaTrackIdx + 1) % s40MediaTrackCount;
            startMediaAudioPlayback();
          } else if (s40MediaRepeatMode == 1) {
            startMediaAudioPlayback();
          } else if (s40MediaRepeatMode == 2 && s40MediaTrackCount > 0) {
            s40MediaTrackIdx = (s40MediaTrackIdx + 3) % s40MediaTrackCount;
            startMediaAudioPlayback();
          } else {
            s40MediaPlaying = false;
            stopMediaAudioPlayback();
          }
          drawMediaPlayerAppScreen(false);
        } else {
          drawMediaProgressAndTimeOnly();
        }
      }
    }

    if (currentMode == 1 && (now - lastExternalEmojiSync > 5500)) {
      if (!isAutoBlinkActive && (now - lastAnimTick >= 4500)) {
        lastAnimTick = now;
        if (eyeState != 1 && eyeState != 8) {
          isAutoBlinkActive = true;
          drawXiaoZhiEyesBox(8, 28, tft->width() - 16, 186, 1, 2);
        }
      } else if (isAutoBlinkActive && (now - lastAnimTick >= 140)) {
        isAutoBlinkActive = false;
        drawXiaoZhiEyesBox(8, 28, tft->width() - 16, 186, eyeState, 2);
      }
    } else if (currentMode == 3 && (now - lastAnimTick >= 65)) {
      lastAnimTick = now;
      drawHardwareDiagScreen(false);
    } else if (currentMode == 12 && s40MediaSubState == 0 && (now - lastAnimTick >= 65)) {
      lastAnimTick = now;
      if (s40MediaPlaying) {
        s40MediaDiscAngle += 0.16f;
        if (s40MediaDiscAngle > 6.28318f) s40MediaDiscAngle -= 6.28318f;
        s40MediaWavePhase += 0.32f;
      }
      drawMediaTopVisualizerOnly();
    } else if (currentMode == 13 && s40ClockTab == 1 && s40SwRunning && (now - lastAnimTick >= 85)) {
      lastAnimTick = now;
      drawClockSuiteAppScreen(false);
    } else if (currentMode == 15) {
      updateActiveGameLoop(now);
    } else if (currentMode == 11) {
      if (s40AiState == AI_STATE_BINDING || s40AiState == AI_STATE_BINDING_CHECKING) {
        // Tự động kiểm tra trạng thái liên kết với XiaoZhi Cloud mỗi 7 giây nếu có Wi-Fi
        if (now - s40AiLastOtpPollMs >= 7000) {
          s40AiLastOtpPollMs = now;
          if (WiFi.status() == WL_CONNECTED) {
            XiaoZhiClient::queryOTA(false);
            if (XiaoZhiClient::isDeviceBound()) {
              TestAudio::playOkChime();
              showSymbianToast("KET NOI XIAOZHI THANH CONG!");
              s40AiState = AI_STATE_IDLE;
              s40AiReplyText = "Xin chao! Thiet bi da ket noi thanh cong voi XiaoZhi Hub! Nhan [OK] de bat dau hoi thoai.";
              drawXiaoZhiAssistantScreen(true);
            } else {
              String authCode = XiaoZhiClient::getLastAuthCode();
              if (authCode.length() > 0 && authCode != s40AiOtpCode) {
                s40AiOtpCode = authCode;
                s40AiBindStatus = "Da cap nhat OTP! Nhap MAC & OTP tren Hub.";
                drawXiaoZhiAssistantScreen(true);
              }
            }
          }
        }
        return;
      }

      // Nếu đang thu âm giọng nói từ Mic INMP441 -> Đọc liên tục DMA I2S không bị trễ
      if (s40AiState == AI_STATE_LISTENING) {
        TestAudio::pollVoiceRecording();
        int peak = TestAudio::getLastMicLevelPct();
        if (peak >= 6) {
          s40AiSpeechDetected = true;
          s40AiLastVoiceMs = now;
        }

        // Cơ chế Smart VAD (Voice Activity Detection thông minh):
        // 1. Đã nhận diện tiếng nói: Yêu cầu nói tối thiểu 1.5s và im lặng ít nhất 1.6s
        //    (đảm bảo người dùng thoải mái ngắt nghỉ câu, lấy hơi giữa câu mà không bị ngắt cụt)
        // 2. Chưa bắt được tiếng nói (chờ người dùng bắt đầu nói): Giới hạn chờ 6.5s
        // 3. Hoặc đạt thời gian thu âm tối đa (8.0s) hoặc đầy bộ đệm PSRAM (isVoiceRecording() == false)
        unsigned long elapsedListeningMs = now - s40AiStateChangeMs;
        unsigned long silenceDurationMs  = now - s40AiLastVoiceMs;

        bool speechDoneBySilence = s40AiSpeechDetected && (elapsedListeningMs >= 1500) && (silenceDurationMs >= 1600);
        bool noSpeechTimeout     = !s40AiSpeechDetected && (elapsedListeningMs >= 6500);
        bool maxDurationReached  = (elapsedListeningMs >= 8000) || !TestAudio::isVoiceRecording();

        if (speechDoneBySilence || noSpeechTimeout || maxDurationReached) {
          triggerXiaoZhiThinkingAndReply();
          return;
        }
      }

      if (now - lastAnimTick >= 180) {
        lastAnimTick = now;
        drawXiaoZhiAssistantStatusStrip();
        if (s40AiState == AI_STATE_LISTENING) {
          drawXiaoZhiConversationBoxOnly();
        }
      }

      if (s40AiState == AI_STATE_REPLYING && s40AiAutoScroll && s40AiTotalLines > 5) {
        int maxStart = s40AiTotalLines - 5;
        unsigned long interval = (s40AiScrollLine >= maxStart) ? 4200UL : 1650UL;
        if (now - s40AiLastScrollMs >= interval) {
          s40AiLastScrollMs = now;
          if (s40AiScrollLine < maxStart) {
            s40AiScrollLine++;
          } else {
            s40AiScrollLine = 0;
          }
          drawXiaoZhiConversationBoxOnly();
        }
      }
    }
  }

  // ============================================================================
  // ĐỒNG BỘ & ĐIỀU KHIỂN TRÌNH PHÁT NHẠC ĐA PHƯƠNG TIỆN (MODE 12 <-> WEB UI)
  // ============================================================================
  void getMediaPlayerState(String& trackName, int& trackIdx, int& trackCount, bool& playing,
                           uint8_t& visMode, uint8_t& repeatMode, uint16_t& curSec,
                           uint16_t& totalSec, int& volumePct, bool& fromSd) {
    if (s40MediaTrackCount == 0) refreshMediaPlaylist();
    trackIdx   = constrain(s40MediaTrackIdx, 0, max(0, s40MediaTrackCount - 1));
    trackCount = s40MediaTrackCount;
    trackName  = (s40MediaTrackCount > 0) ? s40MediaTracks[trackIdx] : "";
    playing    = s40MediaPlaying;
    visMode    = s40MediaVisMode % 3;
    repeatMode = s40MediaRepeatMode % 3;
    curSec     = s40MediaCurSec;
    totalSec   = (s40MediaTrackCount > 0) ? s40MediaDurations[trackIdx] : 180;
    volumePct  = TestAudio::getSpeakerVolumePct();
    fromSd     = (s40MediaTrackCount > 0) ? s40MediaFromSd[trackIdx] : false;
  }

  int getMediaPlaylistItems(String names[], uint16_t durations[], bool fromSd[], size_t sizes[], int maxCount) {
    refreshMediaPlaylist();
    int n = min(s40MediaTrackCount, maxCount);
    for (int i = 0; i < n; i++) {
      if (names)     names[i]     = s40MediaTracks[i];
      if (durations) durations[i] = s40MediaDurations[i];
      if (fromSd)    fromSd[i]    = s40MediaFromSd[i];
      if (sizes)     sizes[i]     = s40MediaFileSizes[i];
    }
    return n;
  }

  void controlMediaPlayer(const String& action, int value, const String& trackName) {
    lastUserActivityMs = millis();
    if (isScreenSleeping) {
      isScreenSleeping = false;
      setBacklightBrightness(screenBrightnessPct);
    }
    if (s40MediaTrackCount == 0) refreshMediaPlaylist();

    if (action == "refresh") {
      refreshMediaPlaylist();
      if (currentMode == 12) drawMediaPlayerAppScreen(false);
      return;
    }

    if (action == "open_on_esp") {
      s40MediaSubState = 0;
      currentMode = 12;
      refreshActiveScreen();
      return;
    }

    if (action == "play") {
      s40MediaPlaying = true;
      startMediaAudioPlayback();
    } else if (action == "pause") {
      s40MediaPlaying = false;
      stopMediaAudioPlayback();
    } else if (action == "toggle") {
      s40MediaPlaying = !s40MediaPlaying;
      if (s40MediaPlaying) startMediaAudioPlayback();
      else stopMediaAudioPlayback();
    } else if (action == "next") {
      if (s40MediaTrackCount > 0) {
        s40MediaTrackIdx = (s40MediaTrackIdx + 1) % s40MediaTrackCount;
        s40MediaCurSec = 0;
        s40MediaPlaying = true;
        startMediaAudioPlayback();
      }
    } else if (action == "prev") {
      if (s40MediaTrackCount > 0) {
        s40MediaTrackIdx = (s40MediaTrackIdx + s40MediaTrackCount - 1) % s40MediaTrackCount;
        s40MediaCurSec = 0;
        s40MediaPlaying = true;
        startMediaAudioPlayback();
      }
    } else if (action == "select") {
      if (trackName.length() > 0) {
        refreshMediaPlaylist();
        for (int i = 0; i < s40MediaTrackCount; i++) {
          if (s40MediaTracks[i].equalsIgnoreCase(trackName) || ("/Musics/" + s40MediaTracks[i]).equalsIgnoreCase(trackName)) {
            s40MediaTrackIdx = i;
            break;
          }
        }
      } else if (value >= 0 && value < s40MediaTrackCount) {
        s40MediaTrackIdx = value;
      }
      s40MediaCurSec = 0;
      s40MediaPlaying = true;
      s40MediaSubState = 0;
      startMediaAudioPlayback();
      if (currentMode != 12) {
        currentMode = 12;
        refreshActiveScreen();
        return;
      }
    } else if (action == "seek") {
      uint16_t tot = (s40MediaTrackCount > 0) ? s40MediaDurations[s40MediaTrackIdx] : 180;
      s40MediaCurSec = (uint16_t)constrain(value, 0, (int)tot);
    } else if (action == "volume") {
      TestAudio::setSpeakerVolumePct(constrain(value, 0, 100));
    } else if (action == "vis") {
      if (value >= 0 && value <= 2) s40MediaVisMode = (uint8_t)value;
      else s40MediaVisMode = (s40MediaVisMode + 1) % 3;
    } else if (action == "repeat") {
      if (value >= 0 && value <= 2) s40MediaRepeatMode = (uint8_t)value;
      else s40MediaRepeatMode = (s40MediaRepeatMode + 1) % 3;
    } else if (action == "theme") {
      applyS40UiTheme(value);
      showSymbianToast(String("GIAO DIEN: ") + curTheme().headerTag);
      refreshActiveScreen();
      return;
    }

    // Nếu người dùng điều khiển từ Web nhưng ESP32 đang ở màn hình khác -> Tự động chuyển ESP32 sang Mode 12 (Đa phương tiện) để đồng bộ trực quan!
    if (currentMode != 12 && (action == "play" || action == "toggle" || action == "next" || action == "prev" || action == "vis")) {
      s40MediaSubState = 0;
      currentMode = 12;
      refreshActiveScreen();
      return;
    }

    if (currentMode == 12) {
      s40MediaSubState = 0;
      drawMediaPlayerAppScreen(false);
    }
  }

  int getUiThemeIdx() {
    return ((s40UiThemeIdx % S40_THEME_COUNT) + S40_THEME_COUNT) % S40_THEME_COUNT;
  }

  void setUiThemeIdx(int idx) {
    applyS40UiTheme(idx);
    showSymbianToast(String("GIAO DIEN: ") + curTheme().headerTag);
    refreshActiveScreen();
  }

  void printHelp() {
    Serial.println("================= ĐIỀU KHIỂN MÀN HÌNH ST7789 =================");
    Serial.println("  [1] : Chế độ MÀN HÌNH CHỜ (Standby Clock + Thời tiết + NTP)");
    Serial.println("  [2] : Chế độ BIỂU CẢM XIAOZHI AI (70% Mắt + 30% Phụ đề)");
    Serial.println("  [3] : Chế độ PC STATUS HUD (Thông số PC + Cảnh báo Offline)");
    Serial.println("  [4] : Chế độ BẢNG DIAGNOSTIC SƠ ĐỒ CHÂN & SÓNG ÂM INMP441");
    Serial.println("  [5] : Mở BỘ ĐIỀU KHIỂN SYMBIAN S40 MENU 4x3 (14 Icon)");
    Serial.println("  [6] : Mở TRỢ LÝ XIAOZHI AI (2/3 Biểu Cảm + 1/3 Hội Thoại Tự Cuộn)");
    Serial.println("  [u] : HỦY LIÊN KẾT XIAOZHI (Xóa Token & Hiện Mã OTP Kích Hoạt Mới)");
    Serial.println("  [q] : Mở MÃ QR CÀI ĐẶT & KẾT NỐI WI-FI (Nối Wi-Fi & Quét Mạng)");
    Serial.println("==============================================================");
  }

  bool handleSerial(char cmd) {
    lastUserActivityMs = millis();
    if (isScreenSleeping) {
      isScreenSleeping = false;
      setBacklightBrightness(screenBrightnessPct);
    }
    switch (cmd) {
      case '1':
        currentMode = 0;
        refreshActiveScreen();
        return true;
      case '2':
        currentMode = 1;
        refreshActiveScreen();
        return true;
      case '3':
        currentMode = 2;
        refreshActiveScreen();
        return true;
      case 's':
      case 'S':
        hudStyle = (hudStyle + 1) % 4;
        currentMode = 2;
        refreshActiveScreen();
        return true;
      case '4':
        currentMode = 3;
        refreshActiveScreen();
        return true;
      case '5':
      case 'm':
      case 'M':
        currentMode = 4;
        refreshActiveScreen();
        return true;
      case '6':
      case 'z':
      case 'Z':
        enterXiaoZhiAssistantMode(false);
        refreshActiveScreen();
        return true;
      case 'u':
      case 'U':
        Serial.println("🗑️ [SERIAL] Đã nhận lệnh Hủy liên kết XiaoZhi (Unbind & Lấy OTP mới)!");
        showSymbianToast("DANG LAY MA OTP MOI...");
        enterXiaoZhiAssistantMode(true);
        refreshActiveScreen();
        return true;
      case 'q':
      case 'Q':
        currentMode = 16;
        refreshActiveScreen();
        return true;
      case 'i':
      case 'I':
        inverted = !inverted;
        if (tft) tft->invertDisplay(inverted);
        return true;
      case 'x':
      case 'X':
        rotation = (rotation + 1) % 4;
        if (tft) {
          tft->setRotation(rotation);
          refreshActiveScreen();
        }
        return true;
      case 'd':
      case 'D':
        useST7789 = !useST7789;
        inverted = useST7789;
        initDriver();
        refreshActiveScreen();
        return true;
      case 'c':
      case 'C':
        runBootSequence(true);
        return true;
      default:
        return false;
    }
  }

  static int lastOtaDrawnPct = -1;
  void drawOtaProgressScreen(const char* status, int pct) {
    if (!tft) return;
    pct = constrain(pct, 0, 100);

    int w = tft->width();
    int h = tft->height();

    // Lần đầu vẽ hoặc khi pct == 0: Xóa màn hình và vẽ bộ khung tối giản
    if (lastOtaDrawnPct < 0 || pct == 0) {
      isScreenSleeping = false;
      setBacklightBrightness(100);
      tft->fillScreen(C_BLACK);

      // 1. Tiêu đề tối giản trên cùng
      tft->fillRect(0, 0, w, 28, 0x18C3);
      tft->drawFastHLine(0, 28, w, 0x52AA);
      tft->setTextSize(1);
      tft->setTextColor(C_WHITE, 0x18C3);
      tft->setCursor(14, 10);
      tft->print("HE THONG // CAP NHAT PHAN MEM (OTA)");

      // 2. Khung viền trung tâm
      tft->drawRect(12, 44, w - 24, 220, 0x52AA);
      tft->drawRect(14, 46, w - 28, 216, 0x2104);

      // Icon vi điều khiển / tải xuống tối giản ở giữa
      tft->drawRect(w / 2 - 24, 62, 48, 48, 0x7BEF);
      tft->fillRect(w / 2 - 20, 66, 40, 40, 0x10A2);
      tft->drawLine(w / 2, 74, w / 2, 94, C_WHITE);
      tft->drawLine(w / 2 - 1, 74, w / 2 - 1, 94, C_WHITE);
      tft->fillTriangle(w / 2 - 8, 92, w / 2 + 8, 92, w / 2, 100, C_WHITE);

      // 3. Khung cảnh báo ở đáy
      tft->fillRect(12, h - 38, w - 24, 24, 0x2000);
      tft->drawRect(12, h - 38, w - 24, 24, C_RED);
      tft->setTextSize(1);
      tft->setTextColor(C_WHITE, 0x2000);
      const char* warn = "! KHONG DUOC NGAT NGUON DIEN !";
      tft->setCursor(max(14, (w - vnStrLen(warn) * 6) / 2), h - 30);
      tft->print(warn);

      lastOtaDrawnPct = -1;
    }

    // Cập nhật vùng trạng thái & tiến trình
    if (pct != lastOtaDrawnPct) {
      lastOtaDrawnPct = pct;

      // Dòng trạng thái (Status text)
      tft->fillRect(16, 126, w - 32, 16, C_BLACK);
      tft->setTextSize(1);
      tft->setTextColor(0x7BEF, C_BLACK);
      int sw = strlen(status) * 6;
      tft->setCursor(max(20, (w - sw) / 2), 128);
      tft->print(status);

      // Thanh Progress Bar (w - 50 px)
      int barX = 25;
      int barY = 152;
      int barW = w - 50;
      int barH = 14;
      tft->drawRect(barX, barY, barW, barH, 0x7BEF);
      tft->fillRect(barX + 2, barY + 2, barW - 4, barH - 4, 0x10A2);

      int fillW = ((barW - 4) * pct) / 100;
      if (fillW > 0) {
        tft->fillRect(barX + 2, barY + 2, fillW, barH - 4, 0x07E0);
      }

      // Phần trăm số to rõ ở giữa
      char pctBuf[16];
      snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
      tft->fillRect(w / 2 - 40, 180, 80, 26, C_BLACK);
      tft->setTextSize(3);
      tft->setTextColor(C_WHITE, C_BLACK);
      int pw = strlen(pctBuf) * 18;
      tft->setCursor((w - pw) / 2, 182);
      tft->print(pctBuf);

      if (pct >= 100) {
        tft->fillRect(16, 218, w - 32, 28, C_BLACK);
        tft->setTextSize(1);
        tft->setTextColor(C_GREEN, C_BLACK);
        const char* done = "HOAN TAT! DANG REBOOT...";
        tft->setCursor((w - strlen(done) * 6) / 2, 222);
        tft->print(done);
        lastOtaDrawnPct = -1;
      }
    }
  }

} // namespace TestDisplay
