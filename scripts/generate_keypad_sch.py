import os
import re
import uuid
import subprocess

# Ensure directory exists
os.makedirs('hardware/Key_pad', exist_ok=True)

# 1. Read original schematic from git HEAD
orig_sch = subprocess.check_output(['git', 'show', 'HEAD:hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_sch'], encoding='utf-8')

# Read C_Polarized symbol text
with open('c_pol_symbol.txt', 'r', encoding='utf-8') as f:
    c_pol_symbol = f.read().strip()

# 2. Add C_Polarized to lib_symbols
lib_sym_start = orig_sch.find('(lib_symbols')
depth = 0
lib_sym_end = -1
for i in range(lib_sym_start, len(orig_sch)):
    if orig_sch[i] == '(':
        depth += 1
    elif orig_sch[i] == ')':
        depth -= 1
        if depth == 0:
            lib_sym_end = i
            break

c_pol_formatted = c_pol_symbol.replace('(symbol "C_Polarized"', '(symbol "Device:C_Polarized"')
c_pol_formatted = '\t\t' + c_pol_formatted.replace('\n', '\n\t\t')

new_lib_symbols = orig_sch[:lib_sym_end] + '\n' + c_pol_formatted + '\n\t)'
rest_of_sch = orig_sch[lib_sym_end+1:]

sch_content = new_lib_symbols + rest_of_sch

# 3. Replace footprints for the 7 push buttons with CustomLib:SW_TactPush_6x6x5mm_2Pin_P4.5mm
button_refs = ["SW_OK", "SW_UP", "SW_DOWN", "SW_LEFT", "SW_RIGHT", "SW_MENU", "SW_EXIT"]
for btn in button_refs:
    pattern = rf'(\(property "Reference" "{btn}".*?\(property "Footprint" ")Button_Switch_THT:SW_PUSH_6mm(")'
    sch_content = re.sub(pattern, r'\1CustomLib:SW_TactPush_6x6x5mm_2Pin_P4.5mm\2', sch_content, flags=re.DOTALL)

# 4. Replace project name in all instances from Keypad_Power_Shield to Key_pad
sch_content = sch_content.replace('(project "Keypad_Power_Shield"', '(project "Key_pad"')

# 5. Add C_5V symbol instance
c_5v_uuid = str(uuid.uuid4())
c_5v_pin1_uuid = str(uuid.uuid4())
c_5v_pin2_uuid = str(uuid.uuid4())

c_5v_symbol_text = f"""
	(symbol
		(lib_id "Device:C_Polarized")
		(at 96.52 59.69 0)
		(unit 1)
		(exclude_from_sim no)
		(in_bom yes)
		(on_board yes)
		(dnp no)
		(uuid "{c_5v_uuid}")
		(property "Reference" "C_5V"
			(at 99.06 58.42 0)
			(effects
				(font
					(size 1.27 1.27)
				)
				(justify left)
			)
		)
		(property "Value" "2200uF"
			(at 99.06 60.96 0)
			(effects
				(font
					(size 1.27 1.27)
				)
				(justify left)
			)
		)
		(property "Footprint" "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm"
			(at 97.485 63.5 0)
			(hide yes)
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
		(property "Datasheet" ""
			(at 96.52 59.69 0)
			(hide yes)
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
		(property "Description" "Polarized capacitor (2200uF 16V anti-brownout buffer)"
			(at 96.52 59.69 0)
			(hide yes)
			(effects
				(font
					(size 1.27 1.27)
				)
			)
		)
		(pin "1"
			(uuid "{c_5v_pin1_uuid}")
		)
		(pin "2"
			(uuid "{c_5v_pin2_uuid}")
		)
		(instances
			(project "Key_pad"
				(path "/4581a797-5cea-4770-869b-05f7473ee6ab"
					(reference "C_5V")
					(unit 1)
				)
			)
		)
	)
"""

c_5v_labels_text = f"""
	(label "+5V"
		(at 96.52 55.88 0)
		(fields_autoplaced yes)
		(effects
			(font
				(size 1.27 1.27)
			)
			(justify left)
		)
		(uuid "{uuid.uuid4()}")
	)
	(label "GND"
		(at 96.52 63.50 0)
		(fields_autoplaced yes)
		(effects
			(font
				(size 1.27 1.27)
			)
			(justify left)
		)
		(uuid "{uuid.uuid4()}")
	)
"""

# Insert C_5V before the last closing ')' of the schematic
last_paren_idx = sch_content.rfind(')')
sch_content = sch_content[:last_paren_idx] + c_5v_symbol_text + c_5v_labels_text + '\n)\n'

sch_path = 'hardware/Key_pad/Key_pad.kicad_sch'
with open(sch_path, 'w', encoding='utf-8') as f:
    f.write(sch_content)

print(f"Successfully generated {sch_path}!")
