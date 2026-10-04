import re

pcb_file = 'hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb'

with open(pcb_file, 'r', encoding='utf-8') as f:
    content = f.read()

# Target placements: dict of ref -> (x, y, rot)
# Rot defaults to 0 if not specified
target_pos = {
    # Mounting holes
    "MH1": (4.0, 4.0, 0),
    "MH2": (86.0, 4.0, 0),
    "MH3": (4.0, 56.0, 0),
    "MH4": (86.0, 56.0, 0),

    # Connectors
    "J_BAT": (74.0 - 1.25, 7.0, 0),          # (72.75, 7.0)
    "J_ESP": (84.0, 19.35 - 6.35, 0),        # (84.0, 13.0)
    "J_MHCD42": (8.0, 19.35 - 6.35, 0),      # (8.0, 13.0)

    # Power & Charger
    "R_CHG1": (18.0 - 3.81, 14.0, 0),        # (14.19, 14.0)
    "R_CHG2": (18.0 - 3.81, 20.0, 0),        # (14.19, 20.0)
    "C_CHG": (18.0 - 1.25, 26.0, 0),         # (16.75, 26.0)
    "SW_PWR": (26.0 - 2.0, 54.0, 0),         # (24.0, 54.0)
    "R_LED": (20.0 - 3.81, 47.0, 0),         # (16.19, 47.0)
    "D_PWR": (12.0 - 1.27, 47.0, 0),         # (10.73, 47.0)

    # Battery ADC divider
    "R_BAT1": (83.0 - 3.81, 33.0, 0),        # (79.19, 33.0)
    "R_BAT2": (83.0 - 3.81, 39.0, 0),        # (79.19, 39.0)
    "C_BAT": (83.0 - 1.25, 46.0, 0),         # (81.75, 46.0)

    # Keypad pullup & cap
    "R_PU": (42.0 - 3.81, 9.5, 0),           # (38.19, 9.5)
    "C_KEY": (32.0 - 1.25, 9.5, 0),          # (30.75, 9.5)

    # Keypad: Up
    "R_UP": (56.0 - 3.81, 9.5, 0),           # (52.19, 9.5)
    "SW_UP": (56.0 - 3.25, 17.0 - 2.25, 0),  # (52.75, 14.75)

    # Keypad: Left
    "R_LEFT": (42.0 - 3.81, 22.5, 0),        # (38.19, 22.5)
    "SW_LEFT": (42.0 - 3.25, 30.0 - 2.25, 0),# (38.75, 27.75)

    # Keypad: Center OK
    "SW_OK": (56.0 - 3.25, 30.0 - 2.25, 0),  # (52.75, 27.75)

    # Keypad: Right
    "R_RIGHT": (70.0 - 3.81, 22.5, 0),       # (66.19, 22.5)
    "SW_RIGHT": (70.0 - 3.25, 30.0 - 2.25, 0),# (66.75, 27.75)

    # Keypad: Down
    "R_DOWN": (56.0 - 3.81, 37.5, 0),        # (52.19, 37.5)
    "SW_DOWN": (56.0 - 3.25, 45.0 - 2.25, 0),# (52.75, 42.75)

    # Keypad: Menu
    "R_MENU": (42.0 - 3.81, 44.5, 0),        # (38.19, 44.5)
    "SW_MENU": (42.0 - 3.25, 52.0 - 2.25, 0),# (38.75, 49.75)

    # Keypad: Exit
    "R_EXIT": (70.0 - 3.81, 44.5, 0),        # (66.19, 44.5)
    "SW_EXIT": (70.0 - 3.25, 52.0 - 2.25, 0),# (66.75, 49.75)
}

# Update footprints in kicad_pcb
lines = content.split('\n')
new_lines = []
in_fp = False
curr_ref = None
fp_start_idx = -1

i = 0
while i < len(lines):
    line = lines[i]
    stripped = line.strip()
    
    if stripped.startswith('(footprint '):
        in_fp = True
        curr_ref = None
        # look ahead for reference
        for j in range(i, min(i + 50, len(lines))):
            if '(property "Reference"' in lines[j]:
                curr_ref = lines[j].split('"')[3]
                break
        new_lines.append(line)
        i += 1
        continue
        
    if in_fp:
        if stripped.startswith('(at ') and curr_ref in target_pos:
            x, y, rot = target_pos[curr_ref]
            indent = line[:line.find('(at ')]
            if rot == 0:
                new_lines.append(f"{indent}(at {x:.4f} {y:.4f})")
            else:
                new_lines.append(f"{indent}(at {x:.4f} {y:.4f} {rot})")
            # Consume this line only once
            target_pos.pop(curr_ref)
            i += 1
            continue
        if line.startswith('\t)') and in_fp:
            in_fp = False
            curr_ref = None
            
    new_lines.append(line)
    i += 1

# Remove any old segment, via, zone blocks to ensure clean board
clean_text = '\n'.join(new_lines)
# Remove segments
clean_text = re.sub(r'\t\(segment\s+[^\)]+\s+[^\)]+\s+[^\)]+\s+[^\)]+\s+[^\)]+\s+[^\)]+\)\n', '', clean_text)
clean_text = re.sub(r'\t\(via\s+[^\)]+\)\n', '', clean_text)
# Remove zones if any
clean_text = re.sub(r'\t\(zone\s+.*?\n\t\)\n', '', clean_text, flags=re.DOTALL)

with open(pcb_file, 'w', encoding='utf-8') as f:
    f.write(clean_text)

print("PCB footprints updated and old routing cleared successfully!")
