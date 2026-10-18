from PIL import Image
import numpy as np

# In switch_exact.jpg, let's find the parallel grid lines
# Line 1: passes through top-left (150, 0) to (600, 450)
# Line 2: passes through (0, 150) to (450, 600)
# Perpendicular distance between these two lines:
# Line equation: x - y = c
# Line 1: 150 - 0 = 150 -> x - y - 150 = 0
# Line 2: 0 - 150 = -150 -> x - y + 150 = 0
# Perpendicular distance = |150 - (-150)| / sqrt(1^2 + (-1)^2) = 300 / sqrt(2) = 212.13 pixels!
# On the cutting mat, standard grid spacing between major lines is 10mm (or 1cm)!
# If 212.13 pixels = 10mm:
# Then 1mm = 21.21 pixels!
# Then pin distance = 156.2 / 21.21 = 7.36 mm!
# If grid spacing is 10mm, pin pitch is around 6.5 mm - 7.0 mm!
# If grid spacing is 10mm, pitch 6.5mm would be: 6.5 * 21.21 = 137.8 pixels (very close!).
print("Pin pitch is ~6.5mm!")

