import os
from PIL import Image

folder = r'assets/icons/gome'
test_files = ['gome_09.png', 'gome_10.png', 'gome_12.png', 'gome_16.png', 'gome_18.png']

out = Image.new('RGBA', (len(test_files) * 80, 160), (16, 20, 36, 255))

for i, fname in enumerate(test_files):
    img = Image.open(os.path.join(folder, fname))
    
    # 1. Full image scaled to 36x36
    thumb1 = img.copy()
    thumb1.thumbnail((36, 36), Image.Resampling.LANCZOS)
    
    # 2. Artwork only (crop off the bottom pill button)
    # Find bounding box of alpha, pill is roughly at bottom 25%
    w, h = img.size
    # crop off bottom pill
    crop_h = int(h * 0.78)
    cropped = img.crop((0, 0, w, crop_h))
    thumb2 = cropped.copy()
    thumb2.thumbnail((36, 36), Image.Resampling.LANCZOS)
    
    # Paste on preview canvas (scaled up 2x so we can inspect detail)
    t1_2x = thumb1.resize((72, 72), Image.Resampling.NEAREST)
    t2_2x = thumb2.resize((72, 72), Image.Resampling.NEAREST)
    
    out.paste(t1_2x, (i * 80 + 4, 4), t1_2x)
    out.paste(t2_2x, (i * 80 + 4, 84), t2_2x)

out.save(r'assets/icons/test_crop_comparison.png')
print("Saved test_crop_comparison.png")

