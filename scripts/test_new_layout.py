import re, subprocess, os

pcb_path = r'hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb'
with open(pcb_path, 'r', encoding='utf-8') as f:
    text = f.read()

# Helper to update component position
def update_comp(content, ref, new_x, new_y, new_rot=None):
    pos = content.find(f'(property "Reference" "{ref}"')
    if pos == -1: return content
    start = content.rfind('(footprint', 0, pos)
    end = content.find('\n\t)', pos)
    chunk = content[start:end+50]
    if new_rot is not None:
        new_at = f'(at {new_x} {new_y} {new_rot})'
    else:
        new_at = f'(at {new_x} {new_y})'
    chunk_new = re.sub(r'\(at\s+[-\d.]+\s+[-\d.]+(?:\s+[-\d.]+)?\)', new_at, chunk, count=1)
    return content[:start] + chunk_new + content[start+len(chunk):]

# 1. Move J_MHCD42 to red arrow: X = 8.0, Y = 22.0, rotation 0
# 2. Move J_BAT to blue arrow: X = 76.0, Y = 8.0, rotation 0 (or 180)
mod_text = update_comp(text, 'J_MHCD42', 8.0, 22.0, 0)
mod_text = update_comp(mod_text, 'J_BAT', 76.0, 8.0, 0)

# Check R_BAT1, R_BAT2 which were at (18, 20) and (18, 26)
# If J_MHCD42 is at X=8, width is ~3mm, so its right side is X ~ 9.5.
# R_BAT1 at (18, 20) has left side at 18 - 3.81 - 1.5 = 12.69.
# Gap between J_MHCD42 and R_BAT1 is 12.69 - 9.5 = 3.19mm! Perfectly clean!

# Clean old tracks, vias, zones
def remove_blocks(content, keyword):
    res = []
    idx = 0
    while True:
        pos = content.find(f'({keyword}\n', idx)
        if pos == -1: pos = content.find(f'({keyword} ', idx)
        if pos == -1:
            res.append(content[idx:])
            break
        res.append(content[idx:pos])
        depth = 0
        end_pos = pos
        for i in range(pos, len(content)):
            if content[i] == '(': depth += 1
            elif content[i] == ')':
                depth -= 1
                if depth == 0:
                    end_pos = i + 1
                    break
        idx = end_pos
    return ''.join(res)

clean_pcb = remove_blocks(mod_text, 'segment')
clean_pcb = remove_blocks(clean_pcb, 'via')
clean_pcb = remove_blocks(clean_pcb, 'zone')

test_pcb_path = r'hardware/Keypad_Power_Shield/test_layout.kicad_pcb'
with open(test_pcb_path, 'w', encoding='utf-8') as f:
    f.write(clean_pcb)

print("Saved test_layout.kicad_pcb")
