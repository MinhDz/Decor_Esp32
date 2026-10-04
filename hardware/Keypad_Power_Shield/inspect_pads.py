import re

with open('hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb', 'r', encoding='utf-8') as f:
    content = f.read()

# Find footprints of interest
fps = ['SW_PUSH_6mm', 'PinHeader_1x06', 'JST_XH', 'SW_Slide', 'R_Axial']
for fp_name in fps:
    pos = content.find(f'footprint "{fp_name}')
    if pos == -1:
        pos = content.find(fp_name)
    if pos != -1:
        end_pos = content.find('\n\t)', pos)
        block = content[pos:end_pos]
        ref = re.search(r'\(property "Reference" "([^"]+)"', block)
        ref_str = ref.group(1) if ref else '?'
        print(f"\n=== Footprint: {fp_name} (Example: {ref_str}) ===")
        pads = re.findall(r'\(pad\s+"([^"]+)".*?\(at\s+([^\)]+)\).*?\(net\s+(\d+)\s+"([^"]+)"\)', block)
        for num, at, net_id, net_name in pads:
            print(f"  Pad {num:4} at ({at:12}) -> Net {net_name}")
