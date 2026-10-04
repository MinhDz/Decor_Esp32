import subprocess, os, re, sys

dsn_path = r'hardware/Keypad_Power_Shield/Keypad_Power_Shield.dsn'
ses_path = r'hardware/Keypad_Power_Shield/Keypad_Power_Shield.ses'

with open(dsn_path, 'r', encoding='utf-8') as f:
    dsn = f.read()

# Fix syntax if needed:
# Ensure circuit only has (use_layer B.Cu)
dsn = re.sub(r'\(circuit\s+\(use_via[^\)]+\)\s*\)', '(circuit\n        (use_layer B.Cu)\n      )', dsn)
dsn = re.sub(r'\(circuit\s+\(use_layer B\.Cu\)\)\s*\)', '(circuit\n        (use_layer B.Cu)\n      )', dsn)

# Ensure structure has single layer off and width 350 clearance 300
if '(layer F.Cu (rule (use_layer off)))' not in dsn:
    dsn = dsn.replace(
        '(rule\n      (width 200)\n      (clearance 200)',
        '(rule\n      (width 350)\n      (clearance 300)\n      (layer F.Cu (rule (use_layer off)))'
    )

# Also in class kicad_default rule:
dsn = re.sub(r'\(class kicad_default.*?\n\s+\(circuit.*?\n\s+\)\n\s+\(rule\s+\(width \d+\)\s+\(clearance \d+\)\s+\)',
    lambda m: m.group(0).replace('(width 200)', '(width 350)').replace('(clearance 200)', '(clearance 300)'),
    dsn, flags=re.DOTALL)

with open(dsn_path, 'w', encoding='utf-8') as f:
    f.write(dsn)

print("DSN updated cleanly. Running Freerouting in headless mode (--gui.enabled=false)...", flush=True)

cmd = [
    r'C:\Program Files\Java\jdk-21.0.10\bin\java.EXE',
    '-jar',
    os.path.expanduser(r'~/.kicad-mcp/freerouting.jar'),
    '--gui.enabled=false',
    '-de', dsn_path,
    '-do', ses_path,
    '-mp', '60'
]

proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
output_lines = []
for line in iter(proc.stdout.readline, ''):
    output_lines.append(line)
    if 'pass #' in line or 'completed' in line or 'unrouted' in line or 'Saving' in line:
        print(line.strip(), flush=True)

proc.stdout.close()
return_code = proc.wait()
print(f"\nFreerouting finished with exit code {return_code}", flush=True)

if os.path.exists(ses_path):
    with open(ses_path, 'r', encoding='utf-8') as f:
        ses_text = f.read()
    f_cu_wires = len(re.findall(r'\(wire\s+\(path\s+F\.Cu', ses_text))
    b_cu_wires = len(re.findall(r'\(wire\s+\(path\s+B\.Cu', ses_text))
    vias_count = len(re.findall(r'\(via\b', ses_text))
    print(f"SES Results: F.Cu wires = {f_cu_wires}, B.Cu wires = {b_cu_wires}, Vias = {vias_count}", flush=True)
else:
    print("SES file was not generated!", flush=True)
    print("Last 20 lines of output:")
    for l in output_lines[-20:]:
        print(l.strip())
