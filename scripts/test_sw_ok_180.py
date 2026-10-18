import subprocess, os, re, sys
import pcbnew

# 1. Update make_pcb_placement.py so SW_OK is placed with rot 180:
# Pad 1 (KEY_ADC) at 58.25, Pad 2 (GND) at 53.75!
with open('scripts/make_pcb_placement.py', 'r', encoding='utf-8') as f:
    code = f.read()

# Replace SW_OK generation in make_pcb_placement.py
old_btn_loop = 'for ref, (val, net2_name, cx, cy) in buttons.items():\n    fp_x = cx - 2.25\n    fp_y = cy'
new_btn_loop = """for ref, (val, net2_name, cx, cy) in buttons.items():
    if ref == "SW_OK":
        fp_x = cx + 2.25
        fp_rot = 180
    else:
        fp_x = cx - 2.25
        fp_rot = 0
    fp_y = cy"""

if 'fp_rot' not in code:
    code = code.replace(old_btn_loop, new_btn_loop)
    code = code.replace('(at {fp_x:.4f} {fp_y:.4f})', '(at {fp_x:.4f} {fp_y:.4f} {fp_rot})')
    with open('scripts/make_pcb_placement.py', 'w', encoding='utf-8') as f:
        f.write(code)
    print("Updated make_pcb_placement.py: SW_OK rot 180!")

subprocess.run([sys.executable, 'scripts/make_pcb_placement.py'], check=True)

# Run test_3corridors.py
subprocess.run([sys.executable, 'scripts/test_3corridors.py'], check=True)
