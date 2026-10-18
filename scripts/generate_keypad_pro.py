import subprocess
import json

orig_pro = subprocess.check_output(['git', 'show', 'HEAD:hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pro'], encoding='utf-8')
data = json.loads(orig_pro)

data['meta']['filename'] = 'Key_pad.kicad_pro'
data['schematic']['top_level_sheets'] = [
    {
        "filename": "Key_pad.kicad_sch",
        "name": "Key_pad",
        "uuid": "00000000-0000-0000-0000-000000000000"
    }
]

with open('hardware/Key_pad/Key_pad.kicad_pro', 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2)

orig_prl = subprocess.check_output(['git', 'show', 'HEAD:hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_prl'], encoding='utf-8')
data_prl = json.loads(orig_prl)
data_prl['meta']['filename'] = 'Key_pad.kicad_prl'

with open('hardware/Key_pad/Key_pad.kicad_prl', 'w', encoding='utf-8') as f:
    json.dump(data_prl, f, indent=2)

print("Created Key_pad.kicad_pro and Key_pad.kicad_prl successfully!")
