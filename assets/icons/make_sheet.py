import os
from PIL import Image, ImageDraw, ImageFont

folder = r'assets/icons/gome'
files = sorted([f for f in os.listdir(folder) if f.endswith('.png')])

cols = 5
rows = 5
thumb_w, thumb_h = 100, 100
sheet = Image.new('RGBA', (cols * thumb_w, rows * thumb_h), (20, 25, 45, 255))
draw = ImageDraw.Draw(sheet)

for idx, f in enumerate(files):
    r = idx // cols
    c = idx % cols
    x = c * thumb_w
    y = r * thumb_h
    
    img = Image.open(os.path.join(folder, f))
    img.thumbnail((thumb_w - 10, thumb_h - 24), Image.Resampling.LANCZOS)
    
    # center image in slot
    ix = x + (thumb_w - img.width) // 2
    iy = y + (thumb_h - 24 - img.height) // 2
    sheet.paste(img, (ix, iy), img if img.mode == 'RGBA' else None)
    
    # draw filename
    draw.text((x + 10, y + thumb_h - 18), f.replace('.png',''), fill=(255, 255, 255, 255))

sheet.save(r'assets/icons/gome_contact_sheet.png')
print("Saved gome_contact_sheet.png")

