from PIL import Image
import numpy as np

img = Image.open("sample/switch_zoom.jpg")
print("Zoom image size:", img.size)

# In switch_zoom.jpg:
# The switch has two pins. Let's find their coordinates by user inspection or brightness.

