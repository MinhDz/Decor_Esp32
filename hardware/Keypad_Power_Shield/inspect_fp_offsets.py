with open('hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb', 'r', encoding='utf-8') as f:
    content = f.read()

import re

fp_blocks = re.findall(r'(\(footprint\s+"([^"]+)".*?\n\t\))', content, re.DOTALL)

seen = set()
for b, fp_name in fp_blocks:
    if fp_name in seen:
        continue
    seen.add(fp_name)
    ref_m = re.search(r'\(property\s+"Reference"\s+"([^"]+)"', b)
    ref = ref_m.group(1) if ref_m else '?'
    at_m = re.search(r'\(at\s+([^\)]+)\)', b)
    at_str = at_m.group(1) if at_m else '?'
    
    print(f"\nFootprint: {fp_name} (Example ref: {ref}) at: {at_str}")
    pads = re.findall(r'\(pad\s+"([^"]+)".*?\(at\s+([^\)]+)\)', b, re.DOTALL)
    for p_num, p_at in pads:
        print(f"   Pad {p_num:4} at ({p_at})")
