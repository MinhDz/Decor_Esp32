import re

dsn_path = r'c:\Users\admin\Documents\GitHub\Decor_Esp32\hardware\Keypad_Power_Shield\Keypad_Power_Shield.dsn'

with open(dsn_path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Disable F.Cu layer in structure
old_fcu = """    (layer F.Cu
      (type signal)
      (property
        (index 0)
      )
    )"""

new_fcu = """    (layer F.Cu
      (type signal)
      (property
        (index 0)
      )
      (rule
        (use_layer off)
      )
    )"""

if old_fcu in content:
    content = content.replace(old_fcu, new_fcu)
    print("F.Cu rule disabled")
else:
    print("Warning: old_fcu not matched verbatim")

# 2. Width and clearance
content = content.replace('(width 200)', '(width 350)')
content = content.replace('(clearance 200)', '(clearance 300)')

# 3. Class kicad_default: replace use_via with use_layer B.Cu
content = re.sub(r'\(use_via [^\)]+\)', '(use_layer B.Cu)', content)

# Also remove via definition in structure if present to prevent Freerouting from considering vias
# (via "Via[0-1]_600:300_um")
content = re.sub(r'\(via [^\)]+\)', '', content)

with open(dsn_path, 'w', encoding='utf-8') as f:
    f.write(content)

print("DSN updated successfully.")
