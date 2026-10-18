from PIL import Image

img = Image.open("sample/IMG20261011100222.jpg")
w, h = img.size

# Crop the other switch on the mat
crop2 = img.crop((w * 0.15, h * 0.40, w * 0.35, h * 0.60))
crop2.save("sample/switch_zoom2.jpg")

# Crop the bag of switches
crop3 = img.crop((w * 0.50, h * 0.70, w * 0.90, h * 0.95))
crop3.save("sample/bag_zoom.jpg")

print("Saved zoom images.")

