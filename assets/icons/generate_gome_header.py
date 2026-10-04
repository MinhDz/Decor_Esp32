import os
from PIL import Image

folder = r'assets/icons/gome'
output_header = r'include/GomeIcons.h'

mapping = [
    (0, "gome_09.png", "HINH_NEN"),
    (1, "gome_10.png", "CAI_DAT"),
    (2, "gome_11.png", "MAY_TINH"),
    (3, "gome_12.png", "THU_VIEN"),
    (4, "gome_13.png", "BO_NHO"),
    (5, "gome_16.png", "TRO_LY_AI"),
    (6, "gome_17.png", "AM_NHAC"),
    (7, "gome_18.png", "DONG_HO"),
    (8, "gome_19.png", "LICH"),
    (9, "gome_38.png", "THIET_BI"),
    (10, "gome_32.png", "SONG_AM"),
    (11, "gome_33.png", "THE_NHO"),
    (12, "gome_39.png", "TRO_CHOI"),
]

ICON_SIZE = 36

def rgba_to_rgb565(r, g, b, a):
    if a < 64:
        return 0x0000
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    val = (r5 << 11) | (g6 << 5) | b5
    if val == 0x0000:
        val = 0x0001
    return val

header_content = []
header_content.append("// ============================================================================")
header_content.append("// BỘ BIỂU TƯỢNG MÈO GOME CHIBI (ANIME THEME - 36x36 PIXELS RGB565)")
header_content.append("// Tự động trích xuất & tối ưu hoá từ assets/icons/gome/")
header_content.append("// Màu chủ đạo: Xanh Cyan/Sky Blue, Trắng, Hồng Chibi")
header_content.append("// ============================================================================\n")
header_content.append("#pragma once")
header_content.append("#include <Arduino.h>\n")
header_content.append("#define GOME_ICON_W 36")
header_content.append("#define GOME_ICON_H 36")
header_content.append("#define GOME_ICON_COUNT 13\n")

for idx, fname, name_tag in mapping:
    p = os.path.join(folder, fname)
    img = Image.open(p).convert('RGBA')
    w, h = img.size
    
    # Crop off bottom text pill
    crop_h = int(h * 0.78)
    cropped = img.crop((0, 0, w, crop_h))
    
    # Scale to 36x36 with high quality Lanczos resampling
    resized = cropped.resize((ICON_SIZE, ICON_SIZE), Image.Resampling.LANCZOS)
    
    pixels = list(resized.getdata())
    header_content.append(f"// Icon {idx}: {fname} ({name_tag})")
    header_content.append(f"static const uint16_t GOME_ICON_{idx}[{ICON_SIZE * ICON_SIZE}] PROGMEM = {{")
    
    row_strings = []
    for r in range(ICON_SIZE):
        row_vals = []
        for c in range(ICON_SIZE):
            r_val, g_val, b_val, a_val = pixels[r * ICON_SIZE + c]
            c565 = rgba_to_rgb565(r_val, g_val, b_val, a_val)
            row_vals.append(f"0x{c565:04X}")
        row_strings.append("  " + ", ".join(row_vals))
    header_content.append(",\n".join(row_strings))
    header_content.append("};\n")

header_content.append("// Mảng con trỏ 13 Icon tương ứng 13 mục Menu S40")
header_content.append("static const uint16_t* const GOME_ICONS[GOME_ICON_COUNT] PROGMEM = {")
for idx, _, _ in mapping:
    comma = "," if idx < len(mapping) - 1 else ""
    header_content.append(f"  GOME_ICON_{idx}{comma}")
header_content.append("};\n")

with open(output_header, 'w', encoding='utf-8') as f:
    f.write("\n".join(header_content))

print(f"Generated {output_header} successfully with {len(mapping)} icons of {ICON_SIZE}x{ICON_SIZE}!")

