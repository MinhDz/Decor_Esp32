from PIL import Image

# Let's inspect the exact switch and measure pins
# In switch_exact.jpg, let's find the pin positions and the grid lines
# On the mat:
# The grid lines form squares of 10mm or 50mm.
# Let's find the grid spacing in pixels.
import numpy as np

arr = np.array(Image.open("sample/switch_exact.jpg").convert("L"))
# print shape
print("Array shape:", arr.shape)

