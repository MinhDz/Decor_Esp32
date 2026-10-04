import os
from PIL import Image, ImageDraw, ImageFont

folder = r'assets/icons/gome'

mapping = [
    (0, "Hình nền", "gome_09.png"),
    (1, "Cài đặt",  "gome_10.png"),
    (2, "Máy tính", "gome_11.png"),
    (3, "Thư viện", "gome_12.png"),
    (4, "Bộ nhớ",   "gome_13.png"),
    (5, "Trợ lý AI","gome_16.png"),
    (6, "Âm nhạc",  "gome_17.png"),
    (7, "Đồng hồ",  "gome_18.png"),
    (8, "Lịch",     "gome_19.png"),
    (9, "Thiết bị", "gome_38.png"),
    (10, "Sóng âm", "gome_32.png"),
    (11, "Thẻ nhớ", "gome_33.png"),
    (12, "Trò chơi","gome_39.png"),
]

# Simulate S40 grid: 4 columns, 4 rows (240x320 screen)
# cellW = 54, cellH = 76, gapX = 4, gapY = 5, startX = 4, startY = 51
canvas = Image.new('RGB', (240, 320), (10, 14, 28)) # Dark pastel navy background
draw = ImageDraw.Draw(canvas)

# Header
draw.rectangle([0, 0, 240, 24], fill=(18, 26, 48))
draw.text((10, 6), "GOME ANIME OS", fill=(120, 200, 255))
draw.text((195, 6), "100%", fill=(255, 180, 220))

# Sub-banner
draw.rectangle([4, 26, 236, 47], fill=(22, 32, 58), outline=(60, 130, 200))
draw.text((10, 32), "Theme 6: Mèo Gome Chibi", fill=(255, 230, 100))

# Cells
cellW = 54
cellH = 76
startX = 4
startY = 51
gapX = 4
gapY = 5

for idx, title, fname in mapping[:12]:
    visRow = idx // 4
    col = idx % 4
    x = startX + col * (cellW + gapX)
    y = startY + visRow * (cellH + gapY)
    
    sel = (idx == 0)
    # Cell box (Pastel blue border, pink highlight if selected)
    box_bg = (30, 44, 76) if sel else (16, 24, 44)
    border_col = (255, 160, 210) if sel else (60, 120, 190)
    
    # Rounded rect
    draw.rounded_rectangle([x, y, x + cellW, y + cellH], radius=8, fill=box_bg, outline=border_col, width=2 if sel else 1)
    if sel:
        # Cat ear accents
        draw.polygon([(x + 4, y + 6), (x + 10, y + 1), (x + 14, y + 6)], fill=(255, 160, 210))
        draw.polygon([(x + cellW - 14, y + 6), (x + cellW - 10, y + 1), (x + cellW - 4, y + 6)], fill=(255, 160, 210))
    
    # Load and process icon
    img = Image.open(os.path.join(folder, fname))
    w, h = img.size
    crop_h = int(h * 0.78)
    cropped = img.crop((0, 0, w, crop_h))
    
    # Resize to 36x36
    icon = cropped.resize((36, 36), Image.Resampling.LANCZOS)
    
    # Center icon
    ix = x + (cellW - 36) // 2
    iy = y + 10
    canvas.paste(icon, (ix, iy), icon if icon.mode == 'RGBA' else None)
    
    # Label text below
    txt_col = (255, 240, 120) if sel else (255, 255, 255)
    draw.text((x + 8, y + 58), title, fill=txt_col)

canvas.save(r'assets/icons/gome_s40_simulation.png')
print("Saved gome_s40_simulation.png")

