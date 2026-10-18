import subprocess, os, re, sys
import pcbnew

board_path = 'hardware/Key_pad/Key_pad.kicad_pcb'
dsn_path = 'hardware/Key_pad/Key_pad.dsn'
ses_path = 'hardware/Key_pad/Key_pad.ses'

# 1. Tái tạo layout sắp đặt linh kiện
subprocess.run([sys.executable, 'scripts/make_pcb_placement.py'], check=True)

board = pcbnew.LoadBoard(board_path)

# 2. Cập nhật kích thước Pad theo yêu cầu DIY của người dùng:
for fp in board.GetFootprints():
    ref = fp.GetReference()
    if ref.startswith('MH'):
        continue  # Giữ nguyên lỗ bắt ốc M3 (drill 3.2mm, pad 6.0mm)
        
    for p in fp.Pads():
        p_name = p.GetName()
        p.SetDrillSize(pcbnew.VECTOR2I(int(0.8 * 1e6), int(0.8 * 1e6)))
        
        if ref.startswith('SW_') and ref != 'SW_PWR':
            p.SetSize(pcbnew.VECTOR2I(int(2.2 * 1e6), int(2.2 * 1e6)))
        elif ref.startswith('R_'):
            p.SetSize(pcbnew.VECTOR2I(int(2.2 * 1e6), int(2.2 * 1e6)))
        elif ref == 'C_5V':
            p.SetSize(pcbnew.VECTOR2I(int(2.2 * 1e6), int(2.2 * 1e6)))
        elif ref in ['C_KEY', 'C_CHG', 'C_BAT', 'J_BAT']:
            p.SetSize(pcbnew.VECTOR2I(int(2.0 * 1e6), int(2.0 * 1e6)))
        elif ref in ['J_ESP', 'J_MHCD42']:
            p.SetSize(pcbnew.VECTOR2I(int(2.0 * 1e6), int(2.0 * 1e6)))
        elif ref == 'D_PWR':
            p.SetSize(pcbnew.VECTOR2I(int(2.0 * 1e6), int(2.0 * 1e6)))
        elif ref == 'SW_PWR':
            if p_name in ['1', '2', '3']:
                p.SetSize(pcbnew.VECTOR2I(int(1.5 * 1e6), int(2.5 * 1e6)))

def add_seg(x1, y1, x2, y2, net_name, width_mm):
    net = board.FindNet(net_name)
    if not net:
        print(f"Warning: Net {net_name} not found!")
        return
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I(int(x1 * 1e6), int(y1 * 1e6)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2 * 1e6), int(y2 * 1e6)))
    t.SetWidth(int(width_mm * 1e6))
    t.SetLayer(pcbnew.B_Cu)
    t.SetNet(net)
    t.SetLocked(True)
    board.Add(t)

# 6 đoạn thẳng thẳng đứng nối phím vào điện trở thang tương ứng:
add_seg(58.25, 17.0, 58.25, 9.5, "NET_UP", 0.75)
add_seg(58.25, 45.0, 58.25, 37.5, "NET_DOWN", 0.75)
add_seg(44.25, 30.0, 44.25, 22.5, "NET_LEFT", 0.75)
add_seg(72.25, 30.0, 72.25, 22.5, "NET_RIGHT", 0.75)
add_seg(44.25, 52.0, 44.25, 44.5, "NET_MENU", 0.75)
add_seg(72.25, 52.0, 72.25, 44.5, "NET_EXIT", 0.75)

# Nối NET_LED_A từ D_PWR sang R_LED:
add_seg(13.27, 47.0, 16.19, 47.0, "NET_LED_A", 0.75)

# Nối BAT_ADC giữa R_BAT1 và R_BAT2 và C_BAT:
add_seg(86.81, 33.0, 86.81, 39.0, "BAT_ADC", 0.75)
add_seg(84.0, 25.7, 86.81, 28.5, "BAT_ADC", 0.75)
add_seg(86.81, 28.5, 86.81, 33.0, "BAT_ADC", 0.75)

# --- 4 HÀNH LANG PHÂN LUỒNG MẠCH ĐỘC LẬP TRÊN 1 LỚP B.CU ---

# 1. HÀNH LANG ĐỈNH (y=2.5mm): Nguồn +5V (1.20mm) từ J_ESP.1 sang C_5V.1
add_seg(84.0, 13.0, 81.0, 13.0, "+5V", 1.20)
add_seg(81.0, 13.0, 81.0, 2.5, "+5V", 1.20)
add_seg(81.0, 2.5, 25.5, 2.5, "+5V", 1.20)
add_seg(25.5, 2.5, 25.5, 36.0, "+5V", 1.20)

# 2. HÀNH LANG CẬN ĐỈNH (y=4.8mm): Tín hiệu +3V3 (0.75mm) từ J_ESP.2 sang R_PU.1
add_seg(84.0, 15.54, 78.5, 15.54, "+3V3", 0.75)
add_seg(78.5, 15.54, 78.5, 4.8, "+3V3", 0.75)
add_seg(78.5, 4.8, 45.81, 4.8, "+3V3", 0.75)
add_seg(45.81, 4.8, 45.81, 9.5, "+3V3", 0.75)

# 3. HÀNH LANG GIỮA (y=26.5mm): Trục xương sống KEY_ADC (0.75mm) nối J_ESP.4 và các phím
add_seg(84.0, 20.62, 76.0, 20.62, "KEY_ADC", 0.75)
add_seg(76.0, 20.62, 76.0, 26.5, "KEY_ADC", 0.75)
add_seg(76.0, 26.5, 67.75, 26.5, "KEY_ADC", 0.75)
add_seg(67.75, 26.5, 67.75, 30.0, "KEY_ADC", 0.75) # Nối SW_RIGHT.1
add_seg(67.75, 26.5, 53.75, 26.5, "KEY_ADC", 0.75)
add_seg(53.75, 26.5, 53.75, 17.0, "KEY_ADC", 0.75) # Nối SW_UP.1
add_seg(53.75, 26.5, 53.75, 30.0, "KEY_ADC", 0.75) # Nối SW_OK.1
add_seg(53.75, 30.0, 53.75, 45.0, "KEY_ADC", 0.75) # Nối SW_DOWN.1
add_seg(53.75, 26.5, 39.75, 26.5, "KEY_ADC", 0.75)
add_seg(39.75, 26.5, 39.75, 30.0, "KEY_ADC", 0.75) # Nối SW_LEFT.1
add_seg(39.75, 26.5, 39.75, 14.0, "KEY_ADC", 0.75)
add_seg(39.75, 14.0, 38.19, 14.0, "KEY_ADC", 0.75)
add_seg(38.19, 14.0, 38.19, 9.5, "KEY_ADC", 0.75)  # Nối R_PU.2
add_seg(38.19, 9.5, 30.75, 9.5, "KEY_ADC", 0.75)   # Nối C_KEY.1
add_seg(39.75, 30.0, 39.75, 52.0, "KEY_ADC", 0.75) # Nối SW_MENU.1
add_seg(67.75, 30.0, 67.75, 52.0, "KEY_ADC", 0.75) # Nối SW_EXIT.1

# 4. HÀNH LANG ĐÁY (y=57.5mm): Tín hiệu CHG_STAT (0.75mm) từ J_ESP.5 sang R_CHG / C_CHG
add_seg(84.0, 23.16, 75.5, 23.16, "CHG_STAT", 0.75)
add_seg(75.5, 23.16, 75.5, 57.5, "CHG_STAT", 0.75)
add_seg(75.5, 57.5, 19.25, 57.5, "CHG_STAT", 0.75)
add_seg(19.25, 57.5, 19.25, 26.0, "CHG_STAT", 0.75) # Nối C_CHG.1
add_seg(19.25, 26.0, 21.81, 20.0, "CHG_STAT", 0.75) # Nối R_CHG2.1
add_seg(21.81, 20.0, 21.81, 14.0, "CHG_STAT", 0.75) # Nối R_CHG1.2

board.Save(board_path)
print("Đã thiết lập hoàn chỉnh 4 trục hành lang phân luồng B.Cu!")

# Xuất DSN
pcbnew.ExportSpecctraDSN(board, dsn_path)

# Cấu hình DSN cho chế độ 1 lớp duy nhất B.Cu
with open(dsn_path, 'r', encoding='utf-8') as f:
    dsn = f.read()

dsn = re.sub(r'\(circuit\s+\(use_via[^\)]+\)\s*\)', '(circuit\n        (use_layer B.Cu)\n      )', dsn)
dsn = re.sub(r'\(circuit\s+\(use_layer B\.Cu\)\)\s*\)', '(circuit\n        (use_layer B.Cu)\n      )', dsn)

dsn = re.sub(
    r'\(rule\s+\(width \d+\)\s+\(clearance \d+\)\s*\)',
    '(rule\n      (width 750)\n      (clearance 500)\n      (layer F.Cu (rule (use_layer off)))\n    )',
    dsn
)

# Power class: 1.2mm width, 0.50mm clearance
power_nets = "+5V GND VBAT VOUT_5V"
# Signal class: 0.75mm width, 0.50mm clearance
signal_nets = "+3V3 BAT_ADC CHG_STAT KEY_ADC MH_KEY NET_DOWN NET_EXIT NET_LED_A NET_LEFT NET_MENU NET_RIGHT NET_UP VIN_5V"

classes_def = f"""    (class Power {power_nets}
      (circuit
        (use_layer B.Cu)
      )
      (rule
        (width 1200)
        (clearance 500)
      )
    )
    (class Signal {signal_nets}
      (circuit
        (use_layer B.Cu)
      )
      (rule
        (width 750)
        (clearance 500)
      )
    )"""

dsn = re.sub(r'\(class kicad_default.*?\n\s*\)', classes_def, dsn, flags=re.DOTALL)
dsn = re.sub(r'\(path B\.Cu \d+', '(path B.Cu 750', dsn)

with open(dsn_path, 'w', encoding='utf-8') as f:
    f.write(dsn)

if os.path.exists(ses_path):
    os.remove(ses_path)

cmd = [
    r'C:\Program Files\Java\jdk-21.0.10\bin\java.EXE',
    '-jar', os.path.expanduser(r'~/.kicad-mcp/freerouting.jar'),
    '--gui.enabled=false',
    '-de', dsn_path,
    '-do', ses_path,
    '-mp', '40'
]

print("Bắt đầu chạy Freerouting...")
p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
best_unrouted = 999
for l in iter(p.stdout.readline, ''):
    l_s = l.strip()
    if 'unrouted' in l_s:
        m = re.search(r'\((\d+)\s+unrouted\)', l_s)
        if m:
            unrouted = int(m.group(1))
            if unrouted < best_unrouted:
                best_unrouted = unrouted
                print(f"--> Kỷ lục mới: {best_unrouted} unrouted!", flush=True)
            if unrouted == 0:
                print("--> ĐÃ ĐẠT 0 UNROUTED! 100% HOÀN TẤT!", flush=True)
    if any(k in l_s for k in ['pass #', 'completed', 'Restoring', 'Routing completed']):
        print(l_s, flush=True)

p.stdout.close()
p.wait()

if os.path.exists(ses_path):
    with open(ses_path, 'r', encoding='utf-8') as f:
        ses_text = f.read()
    b_cu = len(re.findall(r'\(wire\s+\(path\s+B\.Cu', ses_text))
    vias = len(re.findall(r'\(via\b', ses_text))
    print(f"Kết quả SES: {b_cu} tracks B.Cu, {vias} vias.")
    
    board = pcbnew.LoadBoard(board_path)
    for t in list(board.GetTracks()):
        if not t.IsLocked():
            board.Remove(t)
    pcbnew.ImportSpecctraSES(board, ses_path)
    board.Save(board_path)
    print("Đã lưu board hoàn chỉnh!")
