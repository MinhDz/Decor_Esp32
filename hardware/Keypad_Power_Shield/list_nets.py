with open('hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb', 'r', encoding='utf-8') as f:
    content = f.read()

import re

# Match each footprint block
fp_blocks = re.findall(r'(\(footprint\s+"[^"]+".*?\n\t\))', content, re.DOTALL)

nets = {}

for b in fp_blocks:
    ref_m = re.search(r'\(property\s+"Reference"\s+"([^"]+)"', b)
    ref = ref_m.group(1) if ref_m else '?'
    
    # Match pads inside footprint
    pads = re.findall(r'\(pad\s+"([^"]+)".*?\(net\s+"([^"]+)"\)', b, re.DOTALL)
    for p_num, net_name in pads:
        if net_name not in nets:
            nets[net_name] = []
        nets[net_name].append(f"{ref}:{p_num}")

for net, pins in sorted(nets.items()):
    print(f"Net: {net:12} ({len(pins)} pins): {', '.join(pins)}")
