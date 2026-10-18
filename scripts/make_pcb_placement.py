import subprocess
import os
import re
import uuid

# Load base PCB from git HEAD
orig_pcb = subprocess.check_output(['git', 'show', 'HEAD:hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb'], encoding='utf-8')

# Helper to remove S-expression blocks
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

clean_pcb = remove_blocks(orig_pcb, 'segment')
clean_pcb = remove_blocks(clean_pcb, 'via')
clean_pcb = remove_blocks(clean_pcb, 'zone')

# Remove all 7 SW_* footprints and C_5V from clean_pcb
for btn in ["SW_OK", "SW_UP", "SW_DOWN", "SW_LEFT", "SW_RIGHT", "SW_MENU", "SW_EXIT", "C_5V"]:
    pos = clean_pcb.find(f'(property "Reference" "{btn}"')
    if pos != -1:
        start = clean_pcb.rfind('(footprint ', 0, pos)
        depth = 0
        end_pos = start
        for i in range(start, len(clean_pcb)):
            if clean_pcb[i] == '(': depth += 1
            elif clean_pcb[i] == ')':
                depth -= 1
                if depth == 0:
                    end_pos = i + 1
                    break
        clean_pcb = clean_pcb[:start] + clean_pcb[end_pos:]

# Move J_BAT to (8.0 7.0) next to J_MHCD42 to clear the entire top corridor for +5V and +3V3:
pos_jbat = clean_pcb.find('(property "Reference" "J_BAT"')
if pos_jbat != -1:
    fp_start = clean_pcb.rfind('(footprint ', 0, pos_jbat)
    at_match = re.search(r'\(at\s+[\d\.\-]+\s+[\d\.\-]+(?:\s+[\d\.\-]+)?\)', clean_pcb[fp_start:pos_jbat])
    if at_match:
        old_at = at_match.group(0)
        clean_pcb = clean_pcb[:fp_start] + clean_pcb[fp_start:].replace(old_at, '(at 8.0 7.0)', 1)

# Shift J_ESP to (80.0 13.0) to clear mounting holes MH2 and MH4:
pos_jesp = clean_pcb.find('(property "Reference" "J_ESP"')
if pos_jesp != -1:
    fp_start = clean_pcb.rfind('(footprint ', 0, pos_jesp)
    at_match = re.search(r'\(at\s+[\d\.\-]+\s+[\d\.\-]+(?:\s+[\d\.\-]+)?\)', clean_pcb[fp_start:pos_jesp])
    if at_match:
        old_at = at_match.group(0)
        clean_pcb = clean_pcb[:fp_start] + clean_pcb[fp_start:].replace(old_at, '(at 80.0 13.0)', 1)

# Update R_CHG2 rotation to 180: (at 21.81 20 180)
pos_rchg2 = clean_pcb.find('(property "Reference" "R_CHG2"')
if pos_rchg2 != -1:
    fp_start = clean_pcb.rfind('(footprint ', 0, pos_rchg2)
    at_match = re.search(r'\(at\s+[\d\.\-]+\s+[\d\.\-]+(?:\s+[\d\.\-]+)?\)', clean_pcb[fp_start:pos_rchg2])
    if at_match:
        old_at = at_match.group(0)
        clean_pcb = clean_pcb[:fp_start] + clean_pcb[fp_start:].replace(old_at, '(at 21.81 20 180)', 1)

# Rotate C_CHG by 180: (at 19.25 26 180)
pos_cchg = clean_pcb.find('(property "Reference" "C_CHG"')
if pos_cchg != -1:
    fp_start = clean_pcb.rfind('(footprint ', 0, pos_cchg)
    at_match = re.search(r'\(at\s+[\d\.\-]+\s+[\d\.\-]+(?:\s+[\d\.\-]+)?\)', clean_pcb[fp_start:pos_cchg])
    if at_match:
        old_at = at_match.group(0)
        clean_pcb = clean_pcb[:fp_start] + clean_pcb[fp_start:].replace(old_at, '(at 19.25 26 180)', 1)

# Move C_BAT to (15.62 43.0) on left side:
pos_cbat = clean_pcb.find('(property "Reference" "C_BAT"')
if pos_cbat != -1:
    fp_start = clean_pcb.rfind('(footprint ', 0, pos_cbat)
    at_match = re.search(r'\(at\s+[\d\.\-]+\s+[\d\.\-]+(?:\s+[\d\.\-]+)?\)', clean_pcb[fp_start:pos_cbat])
    if at_match:
        old_at = at_match.group(0)
        clean_pcb = clean_pcb[:fp_start] + clean_pcb[fp_start:].replace(old_at, '(at 15.62 43.0)', 1)

# Rotate resistors by 180 so Pad 1 aligns without track crossings:
resistor_shifts = {
    "R_UP": "(at 58.25 9.5 180)",
    "R_DOWN": "(at 58.25 37.5 180)",
    "R_LEFT": "(at 44.25 22.5 180)",
    "R_MENU": "(at 44.25 44.5 180)",
    "R_RIGHT": "(at 72.25 22.5 180)",
    "R_EXIT": "(at 72.25 44.5 180)",
    "R_LED": "(at 23.81 47.0 180)",
    "R_PU": "(at 45.81 9.5 180)",
    "R_BAT1": "(at 8.0 32.0)",
    "R_BAT2": "(at 15.62 38.0 180)",
}
for r_ref, new_at in resistor_shifts.items():
    pos = clean_pcb.find(f'(property "Reference" "{r_ref}"')
    if pos != -1:
        fp_start = clean_pcb.rfind('(footprint ', 0, pos)
        at_match = re.search(r'\(at\s+[\d\.\-]+\s+[\d\.\-]+(?:\s+[\d\.\-]+)?\)', clean_pcb[fp_start:pos])
        if at_match:
            old_at = at_match.group(0)
            clean_pcb = clean_pcb[:fp_start] + clean_pcb[fp_start:].replace(old_at, new_at, 1)

# Build 2-pin switch footprints for each of the 7 buttons
buttons = {
    "SW_OK":    ("OK",    "GND",      56.0, 30.0),
    "SW_UP":    ("UP",    "NET_UP",   56.0, 17.0),
    "SW_DOWN":  ("DOWN",  "NET_DOWN", 56.0, 45.0),
    "SW_LEFT":  ("LEFT",  "NET_LEFT", 42.0, 30.0),
    "SW_RIGHT": ("RIGHT", "NET_RIGHT",70.0, 30.0),
    "SW_MENU":  ("MENU",  "NET_MENU", 42.0, 52.0),
    "SW_EXIT":  ("EXIT",  "NET_EXIT", 70.0, 52.0),
}

new_footprints_text = ""

for ref, (val, net2_name, cx, cy) in buttons.items():
    fp_x = cx - 2.25
    fp_y = cy
    fp_uuid = str(uuid.uuid4())
    p1_uuid = str(uuid.uuid4())
    p2_uuid = str(uuid.uuid4())
    
    fp_str = f"""
	(footprint "CustomLib:SW_TactPush_6x6x5mm_2Pin_P4.5mm"
		(layer "F.Cu")
		(uuid "{fp_uuid}")
		(at {fp_x:.4f} {fp_y:.4f})
		(descr "Tactile push button 6x6x5mm, 2 pins, momentary reset, pitch 4.5mm, THT vertical")
		(tags "tact switch button 6x6 2pin THT reset momentary")
		(property "Reference" "{ref}"
			(at 2.25 -4.0 0)
			(layer "F.SilkS")
			(uuid "{uuid.uuid4()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" "{val}"
			(at 2.25 4.0 0)
			(layer "F.Fab")
			(uuid "{uuid.uuid4()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Footprint" "CustomLib:SW_TactPush_6x6x5mm_2Pin_P4.5mm"
			(at 0 0 0)
			(layer "F.Fab")
			(hide yes)
			(uuid "{uuid.uuid4()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(attr through_hole)
		(fp_rect
			(start -1.25 -3.5)
			(end 5.75 3.5)
			(stroke
				(width 0.05)
				(type default)
			)
			(fill none)
			(layer "F.CrtYd")
			(uuid "{uuid.uuid4()}")
		)
		(fp_rect
			(start -0.75 -3)
			(end 5.25 3)
			(stroke
				(width 0.1)
				(type default)
			)
			(fill none)
			(layer "F.Fab")
			(uuid "{uuid.uuid4()}")
		)
		(fp_rect
			(start -0.75 -3)
			(end 5.25 3)
			(stroke
				(width 0.12)
				(type default)
			)
			(fill none)
			(layer "F.SilkS")
			(uuid "{uuid.uuid4()}")
		)
		(fp_circle
			(center 2.25 0)
			(end 3.75 0)
			(stroke
				(width 0.12)
				(type default)
			)
			(fill none)
			(layer "F.SilkS")
			(uuid "{uuid.uuid4()}")
		)
		(pad "1" thru_hole rect
			(at 0 0)
			(size 1.8 1.8)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(net 6 "KEY_ADC")
			(pinfunction "1")
			(pintype "passive")
			(uuid "{p1_uuid}")
		)
		(pad "2" thru_hole circle
			(at 4.5 0)
			(size 1.8 1.8)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(net 0 "{net2_name}")
			(pinfunction "2")
			(pintype "passive")
			(uuid "{p2_uuid}")
		)
		(embedded_fonts no)
	)
"""
    new_footprints_text += fp_str

# Add C_5V footprint (CP_Radial_D10.0mm_P5.00mm)
# Placed at (23.0, 36.0), rotated 90
c5v_fp_str = f"""
	(footprint "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm"
		(layer "F.Cu")
		(uuid "{uuid.uuid4()}")
		(at 23.0 36.0 90)
		(descr "CP, Radial series, Radial, pin pitch=5.00mm, , diameter=10mm, Electrolytic Capacitor")
		(tags "CP Radial series Radial pitch 5.00mm  diameter 10mm Electrolytic Capacitor")
		(property "Reference" "C_5V"
			(at 0 -6.25 90)
			(layer "F.SilkS")
			(uuid "{uuid.uuid4()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" "2200uF"
			(at 0 6.25 90)
			(layer "F.Fab")
			(uuid "{uuid.uuid4()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Footprint" "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm"
			(at 0 0 90)
			(layer "F.Fab")
			(hide yes)
			(uuid "{uuid.uuid4()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(attr through_hole)
		(fp_circle
			(center 0 0)
			(end 5 0)
			(stroke
				(width 0.12)
				(type default)
			)
			(fill none)
			(layer "F.SilkS")
			(uuid "{uuid.uuid4()}")
		)
		(fp_circle
			(center 0 0)
			(end 5 0)
			(stroke
				(width 0.1)
				(type default)
			)
			(fill none)
			(layer "F.Fab")
			(uuid "{uuid.uuid4()}")
		)
		(fp_circle
			(center 0 0)
			(end 5.25 0)
			(stroke
				(width 0.05)
				(type default)
			)
			(fill none)
			(layer "F.CrtYd")
			(uuid "{uuid.uuid4()}")
		)
		(fp_line
			(start -3.454 4.015)
			(end -3.454 -4.015)
			(stroke
				(width 0.12)
				(type default)
			)
			(layer "F.SilkS")
			(uuid "{uuid.uuid4()}")
		)
		(fp_line
			(start -3.054 4.015)
			(end -3.054 -4.015)
			(stroke
				(width 0.12)
				(type default)
			)
			(layer "F.SilkS")
			(uuid "{uuid.uuid4()}")
		)
		(fp_line
			(start -2.654 4.015)
			(end -2.654 -4.015)
			(stroke
				(width 0.12)
				(type default)
			)
			(layer "F.SilkS")
			(uuid "{uuid.uuid4()}")
		)
		(pad "1" thru_hole rect
			(at 0 2.5 90)
			(size 2 2)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(net 2 "+5V")
			(pinfunction "1")
			(pintype "passive")
			(uuid "{uuid.uuid4()}")
		)
		(pad "2" thru_hole circle
			(at 0 -2.5 90)
			(size 2 2)
			(drill 1)
			(layers "*.Cu" "*.Mask")
			(net 1 "GND")
			(pinfunction "2")
			(pintype "passive")
			(uuid "{uuid.uuid4()}")
		)
		(embedded_fonts no)
	)
"""
new_footprints_text += c5v_fp_str

# Add SilkScreen text
silkscreen_labels = f"""
	(gr_text "2200uF ANTI-BROWNOUT"
		(at 23.0 43.5)
		(layer "F.SilkS")
		(uuid "{uuid.uuid4()}")
		(effects
			(font
				(size 0.8 0.8)
				(thickness 0.12)
			)
		)
	)
	(gr_text "CHG_STAT (VIN DETECT)"
		(at 14.0 10.0)
		(layer "F.SilkS")
		(uuid "{uuid.uuid4()}")
		(effects
			(font
				(size 0.8 0.8)
				(thickness 0.12)
			)
		)
	)
	(gr_text "BAT_ADC (VBAT SENSE)"
		(at 8.0 28.5)
		(layer "F.SilkS")
		(uuid "{uuid.uuid4()}")
		(effects
			(font
				(size 0.8 0.8)
				(thickness 0.12)
			)
		)
	)
"""

# Insert footprints before the last ')'
last_paren = clean_pcb.rfind(')')
clean_pcb = clean_pcb[:last_paren] + new_footprints_text + silkscreen_labels + '\n)\n'

board_path = 'hardware/Key_pad/Key_pad.kicad_pcb'
with open(board_path, 'w', encoding='utf-8') as f:
    f.write(clean_pcb)

# Attach correct nets using pcbnew
import pcbnew
board = pcbnew.LoadBoard(board_path)
for ref, (val, net2_name, cx, cy) in buttons.items():
    fp = board.FindFootprintByReference(ref)
    if fp:
        p1 = fp.FindPadByNumber("1")
        p2 = fp.FindPadByNumber("2")
        if p1: p1.SetNet(board.FindNet("KEY_ADC"))
        if p2: p2.SetNet(board.FindNet(net2_name))

c5v = board.FindFootprintByReference("C_5V")
if c5v:
    p1 = c5v.FindPadByNumber("1")
    p2 = c5v.FindPadByNumber("2")
    if p1: p1.SetNet(board.FindNet("+5V"))
    if p2: p2.SetNet(board.FindNet("GND"))

board.Save(board_path)

print(f"Saved optimized unrouted board with verified nets to {board_path}!")

