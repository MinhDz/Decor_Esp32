import subprocess, os, re

dsn_path = r'hardware/Keypad_Power_Shield/Keypad_Power_Shield.dsn'
ses_path = r'hardware/Keypad_Power_Shield/Keypad_Power_Shield.ses'

with open(dsn_path, 'r', encoding='utf-8') as f:
    dsn = f.read()

# Configure for 1-layer B.Cu only
dsn_1layer = dsn.replace(
    '(circuit\n        (use_via "Via[0-1]_600:300_um"',
    '(circuit\n        (use_layer B.Cu)'
)

dsn_1layer = dsn_1layer.replace(
    '(rule\n      (width 200)\n      (clearance 200)',
    '(rule\n      (width 350)\n      (clearance 300)\n      (layer F.Cu (rule (use_layer off)))'
)

with open(dsn_path, 'w', encoding='utf-8') as f:
    f.write(dsn_1layer)

print("Configured DSN for single-layer B.Cu! Now running Freerouting...")

cmd = [
    r'C:\Program Files\Java\jdk-21.0.10\bin\java.EXE',
    '-jar',
    os.path.expanduser(r'~/.kicad-mcp/freerouting.jar'),
    '-de', dsn_path,
    '-do', ses_path,
    '-mp', '60'
]
res = subprocess.run(cmd, capture_output=True, text=True)
print("Return code:", res.returncode)
print("Stdout:", res.stdout[-600:] if res.stdout else "None")
print("Stderr:", res.stderr[-500:] if res.stderr else "None")

if os.path.exists(ses_path):
    with open(ses_path, 'r', encoding='utf-8') as f:
        ses_text = f.read()
    f_cu_wires = len(re.findall(r'\(wire\s+\(path\s+F\.Cu', ses_text))
    b_cu_wires = len(re.findall(r'\(wire\s+\(path\s+B\.Cu', ses_text))
    vias_count = len(re.findall(r'\(via\b', ses_text))
    print(f"\nSES Results: F.Cu wires = {f_cu_wires}, B.Cu wires = {b_cu_wires}, Vias = {vias_count}")
