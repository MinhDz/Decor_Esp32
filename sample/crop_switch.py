from PIL import Image

img = Image.open("sample/IMG20261011100222.jpg")
w, h = img.size
print(f"Image size: {w} x {h}")

# Crop the switch on the mat (around the center / 31x21mm oval)
# Let's find the crop coordinates
crop1 = img.crop((w * 0.35, h * 0.35, w * 0.65, h * 0.55))
crop1.save("sample/switch_zoom.jpg")
print("Saved switch_zoom.jpg")

