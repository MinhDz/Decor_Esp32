from PIL import Image
import numpy as np

img = Image.open("sample/switch_exact.jpg")
arr = np.array(img)

# The pins are bright silver/white against the green cutting mat
# Let's find white/bright pixels corresponding to the two pins
# On the cutting mat, green is (R<100, G>100, B<100)
# White pins have high R, G, B (>180, >180, >180)
white_mask = (arr[:, :, 0] > 160) & (arr[:, :, 1] > 160) & (arr[:, :, 2] > 160)

# Save mask to visualize
mask_img = Image.fromarray((white_mask * 255).astype(np.uint8))
mask_img.save("sample/pins_mask.png")

# Also find the white grid lines on the cutting mat to calibrate pixels per mm!
# The cutting mat has white grid lines with 10mm spacing.
# Let's inspect the lines in switch_exact.jpg
print("Saved pins_mask.png")

