import subprocess, os, re, sys
import pcbnew

# 1. Generate placement with aligned resistors
with open('scripts/make_pcb_placement.py', 'r', encoding='utf-8') as f:
    code = f.read()

# Add resistor shift code right after R_CHG2 and C_CHG
resistor_shifts = """
# Align the 6 ladder resistors with button Pad 2 (58.25, 44.25, 72.25)
# to avoid colliding with the vertical KEY_ADC trunk lines:
shifts = {
    "R_UP": "(at 50.63 9.5)",
    "R_DOWN": "(at 50.63 37.5)",
    "R_LEFT": "(at 36.63 22.5)",
    "R_MENU": "(at 36.63 44.5)",
    "R_RIGHT": "(at 64.63 22.5)",
    "R_EXIT": "(at 64.63 44.5)",
}
for r_ref, new_at in shifts.items():
    pos = clean_pcb.find(f'(property "Reference" "{r_ref}"')
    if pos != -1:
        fp_start = clean_pcb.rfind('(footprint ', 0, pos)
        at_match = re.search(r'\(at\s+[\d\.\-]+\s+[\d\.\-]+(?:\s+[\d\.\-]+)?\)', clean_pcb[fp_start:pos])
        if at_match:
            old_at = at_match.group(0)
            clean_pcb = clean_pcb[:fp_start] + clean_pcb[fp_start:].replace(old_at, new_at, 1)
"""

if 'resistor_shifts' not in code:
    code = code.replace(
        '# Build 2-pin switch footprints for each of the 7 buttons',
        resistor_shifts + '\n# Build 2-pin switch footprints for each of the 7 buttons'
    )
    with open('scripts/make_pcb_placement.py', 'w', encoding='utf-8') as f:
        f.write(code)
    print("Updated make_pcb_placement.py with aligned resistors!")

# Run make_pcb_placement.py
subprocess.run([sys.executable, 'scripts/make_pcb_placement.py'], check=True)
