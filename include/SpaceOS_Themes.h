#pragma once
#include <Arduino.h>

// ============================================================================
// BẢNG MÀU 16-BIT RGB565 GỐC (HẰNG SỐ CỐ ĐỊNH CỦA SPACE OS)
// ============================================================================
#ifndef C_BLACK
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
#endif

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

