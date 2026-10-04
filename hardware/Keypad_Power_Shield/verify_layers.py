with open('hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb', 'r', encoding='utf-8') as f:
    content = f.read()

import re

segments = re.findall(r'\(segment\s+.*?\(layer\s+"([^"]+)".*?\(net\s+"([^"]+)"\)', content, re.DOTALL)
vias = re.findall(r'\(via\b', content)
zones = re.findall(r'\(zone\b', content)

f_cu_count = sum(1 for layer, net in segments if layer == 'F.Cu')
b_cu_count = sum(1 for layer, net in segments if layer == 'B.Cu')

print("=== TRACE VERIFICATION RESULTS ===")
print(f"Total track segments: {len(segments)}")
print(f"  - Top Copper (F.Cu) segments:    {f_cu_count}")
print(f"  - Bottom Copper (B.Cu) segments: {b_cu_count}")
print(f"Total Vias:                       {len(vias)}")
print(f"Copper Pour Zones:                {len(zones)}")

nets_routed = set(net for layer, net in segments)
print(f"\nUnique nets with routed traces ({len(nets_routed)}):")
for n in sorted(nets_routed):
    count = sum(1 for layer, net in segments if net == n)
    print(f"  - {n:15}: {count} segments")
