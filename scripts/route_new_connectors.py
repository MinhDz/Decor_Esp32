import subprocess, os, re

pcb_path = r'hardware/Keypad_Power_Shield/Keypad_Power_Shield.kicad_pcb'
dsn_path = r'hardware/Keypad_Power_Shield/Keypad_Power_Shield.dsn'
ses_path = r'hardware/Keypad_Power_Shield/Keypad_Power_Shield.ses'

with open(pcb_path, 'r', encoding='utf-8') as f:
    text = f.read()

# 1. Clean existing tracks, vias, zones
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

clean_pcb = remove_blocks(text, 'segment')
clean_pcb = remove_blocks(clean_pcb, 'via')
clean_pcb = remove_blocks(clean_pcb, 'zone')

with open(pcb_path, 'w', encoding='utf-8') as f:
    f.write(clean_pcb)

print("Saved clean PCB!")
