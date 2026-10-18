from PIL import Image
import numpy as np

img = Image.open("sample/IMG20261011100222.jpg")
# Let's inspect the cutting mat lines near the switch
# Switch is around (1500, 1700)
# Let's crop a 600x600 region around the switch and print dimensions
crop = img.crop((1200, 1400, 1800, 2000))
crop.save("sample/switch_exact.jpg")
print("Saved switch_exact.jpg")

