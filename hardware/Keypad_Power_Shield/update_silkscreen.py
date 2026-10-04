import re

pcb_file = 'hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb'

with open(pcb_file, 'r', encoding='utf-8') as f:
    content = f.read()

# Remove old gr_text blocks
content = re.sub(r'\t\(gr_text\s+"[^"]+".*?\n\t\)\n', '', content, flags=re.DOTALL)

# New silkscreen texts perfectly aligned with buttons and headers
new_texts = [
    # Title on top left empty area
    ('ESP32-S3 KEYPAD & POWER SHIELD', 24.0, 5.0, 1.3),
    
    # Connectors
    ('MH-CD42 (VIN)', 9.0, 8.5, 1.0),
    ('BAT 3.7V', 74.0, 3.5, 1.0),
    ('TO ESP32', 84.0, 8.5, 1.0),
    
    # Power switch
    ('PWR ON', 26.0, 49.5, 1.1),
    
    # Buttons
    ('UP', 56.0, 17.0, 1.1),
    ('LEFT', 42.0, 30.0, 1.1),
    ('OK', 56.0, 30.0, 1.2),
    ('RIGHT', 70.0, 30.0, 1.1),
    ('DOWN', 56.0, 45.0, 1.1),
    ('MENU', 42.0, 57.2, 1.1),
    ('EXIT', 70.0, 57.2, 1.1),
]

import uuid

text_blocks = []
for text, x, y, sz in new_texts:
    block = f"""\t(gr_text "{text}"
\t\t(at {x:.2f} {y:.2f} 0)
\t\t(layer "F.SilkS")
\t\t(uuid "{uuid.uuid4()}")
\t\t(effects
\t\t\t(font
\t\t\t\t(size {sz} {sz})
\t\t\t\t(thickness 0.15)
\t\t\t)
\t\t)
\t)
"""
    text_blocks.append(block)

# Insert before the first segment or zone
insert_pos = content.find('\t(segment')
if insert_pos == -1:
    insert_pos = content.find('\t(zone')
if insert_pos == -1:
    insert_pos = content.rfind(')')

content = content[:insert_pos] + ''.join(text_blocks) + content[insert_pos:]

with open(pcb_file, 'w', encoding='utf-8') as f:
    f.write(content)

print("Updated silkscreen texts successfully!")
