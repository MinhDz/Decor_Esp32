#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ILI9341.h>

namespace SpaceOS {
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

  inline bool decodeVnCodepoint(uint32_t cp, char& baseOut, uint8_t& modOut, uint8_t& toneOut) {
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

} // namespace SpaceOS

// Backward compatibility for TestDisplay namespace
namespace TestDisplay {
  using SpaceOS::VnModType;
  using SpaceOS::VnToneType;
  using SpaceOS::VnExtEntry;
  using SpaceOS::VN_EXT_TABLE;
  using SpaceOS::decodeVnCodepoint;
  using SpaceOS::VnGfxAdapter;
  using SpaceOS::VnST7789;
  using SpaceOS::VnILI9341;
  using SpaceOS::VnCanvas16;
}
