with open('hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb', 'r', encoding='utf-8') as f:
    lines = f.readlines()

in_fp = False
curr_ref = None
curr_val = None
curr_at = None
curr_name = None

footprints = []

for line in lines:
    stripped = line.strip()
    if stripped.startswith('(footprint '):
        in_fp = True
        curr_name = stripped.split('"')[1] if '"' in stripped else stripped
        curr_ref = None
        curr_val = None
        curr_at = None
    elif in_fp:
        if stripped.startswith('(at ') and curr_at is None:
            curr_at = stripped[4:-1].strip()
        elif '(property "Reference"' in stripped:
            curr_ref = stripped.split('"')[3]
        elif '(property "Value"' in stripped:
            curr_val = stripped.split('"')[3]
        elif stripped == '(embedded_fonts no)' or (stripped == ')' and curr_ref is not None and curr_val is not None):
            # footprint end or nearly end
            pass
        if line.startswith('\t)') and in_fp:
            footprints.append({
                'ref': curr_ref,
                'val': curr_val,
                'at': curr_at,
                'footprint': curr_name
            })
            in_fp = False

for fp in footprints:
    print(f"{str(fp['ref']):12} | {str(fp['val']):15} | at: {str(fp['at']):15} | {fp['footprint']}")
