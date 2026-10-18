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

# A. 6 đoạn thẳng nối phím vào điện trở thang:
add_seg(58.25, 17.0, 58.25, 9.5, "NET_UP", 0.75)
add_seg(58.25, 45.0, 58.25, 37.5, "NET_DOWN", 0.75)
add_seg(44.25, 30.0, 44.25, 22.5, "NET_LEFT", 0.75)
add_seg(72.25, 30.0, 72.25, 22.5, "NET_RIGHT", 0.75)
add_seg(44.25, 52.0, 44.25, 44.5, "NET_MENU", 0.75)
add_seg(72.25, 52.0, 72.25, 44.5, "NET_EXIT", 0.75)

# B. NET_LED_A:
add_seg(13.27, 47.0, 16.19, 47.0, "NET_LED_A", 0.75)

# C. Cụm đo pin BAT_ADC:
add_seg(15.62, 32.0, 15.62, 38.0, "BAT_ADC", 0.75)
add_seg(15.62, 38.0, 15.62, 43.0, "BAT_ADC", 0.75)

# D. VBAT bên trái (J_BAT.1 -> J_MHCD42.2 -> R_BAT1.1):
add_seg(8.0, 7.0, 5.0, 7.0, "VBAT", 1.20)
add_seg(5.0, 7.0, 5.0, 32.0, "VBAT", 1.20)
add_seg(5.0, 32.0, 8.0, 32.0, "VBAT", 1.20)
add_seg(5.0, 15.54, 8.0, 15.54, "VBAT", 1.20)

# E. VIN_5V bên trái (J_MHCD42.1 -> R_CHG1.1):
add_seg(8.0, 13.0, 14.19, 14.0, "VIN_5V", 0.75)

# F. VOUT_5V bên trái (J_MHCD42.4 -> SW_PWR.1):
add_seg(8.0, 20.62, 6.0, 20.62, "VOUT_5V", 1.20)
add_seg(6.0, 20.62, 6.0, 50.0, "VOUT_5V", 1.20)
add_seg(6.0, 50.0, 24.0, 50.0, "VOUT_5V", 1.20)
add_seg(24.0, 50.0, 24.0, 54.0, "VOUT_5V", 1.20)

# G. +5V (J_ESP.1 -> đỉnh y=2.5mm -> C_5V.1 -> R_LED.1 -> SW_PWR.2):
add_seg(80.0, 13.0, 80.0, 2.5, "+5V", 1.20)
add_seg(80.0, 2.5, 25.5, 2.5, "+5V", 1.20)
add_seg(25.5, 2.5, 25.5, 36.0, "+5V", 1.20)
add_seg(25.5, 36.0, 23.81, 47.0, "+5V", 1.20)
add_seg(23.81, 47.0, 26.0, 54.0, "+5V", 1.20)

# H. +3V3 (J_ESP.2 -> y=5.0mm -> R_PU.1):
add_seg(80.0, 15.54, 76.0, 15.54, "+3V3", 0.75)
add_seg(76.0, 15.54, 76.0, 5.0, "+3V3", 0.75)
add_seg(76.0, 5.0, 45.81, 5.0, "+3V3", 0.75)
add_seg(45.81, 5.0, 45.81, 9.5, "+3V3", 0.75)

# I. BAT_ADC (J_ESP.6 -> đáy y=57.5mm -> C_BAT.1):
add_seg(80.0, 25.7, 80.0, 57.5, "BAT_ADC", 0.75)
add_seg(80.0, 57.5, 15.62, 57.5, "BAT_ADC", 0.75)
add_seg(15.62, 57.5, 15.62, 43.0, "BAT_ADC", 0.75)

# J. CHG_STAT (J_ESP.5 -> đáy y=55.0mm -> C_CHG.1 -> R_CHG2.1 -> R_CHG1.2):
add_seg(80.0, 23.16, 76.5, 23.16, "CHG_STAT", 0.75)
add_seg(76.5, 23.16, 76.5, 55.0, "CHG_STAT", 0.75)
add_seg(76.5, 55.0, 19.25, 55.0, "CHG_STAT", 0.75)
add_seg(19.25, 55.0, 19.25, 26.0, "CHG_STAT", 0.75)
add_seg(19.25, 26.0, 21.81, 20.0, "CHG_STAT", 0.75)
add_seg(21.81, 20.0, 21.81, 14.0, "CHG_STAT", 0.75)

# K. KEY_ADC (trục xương sống nối 7 nút và C_KEY / R_PU):
add_seg(80.0, 20.62, 74.0, 20.62, "KEY_ADC", 0.75)
add_seg(74.0, 20.62, 74.0, 24.5, "KEY_ADC", 0.75)
add_seg(74.0, 24.5, 67.75, 24.5, "KEY_ADC", 0.75)
add_seg(67.75, 24.5, 67.75, 30.0, "KEY_ADC", 0.75) # SW_RIGHT.1
add_seg(67.75, 30.0, 67.75, 52.0, "KEY_ADC", 0.75) # SW_EXIT.1
add_seg(67.75, 24.5, 53.75, 24.5, "KEY_ADC", 0.75)
add_seg(53.75, 24.5, 53.75, 17.0, "KEY_ADC", 0.75) # SW_UP.1
add_seg(53.75, 24.5, 53.75, 30.0, "KEY_ADC", 0.75) # SW_OK.1
add_seg(53.75, 30.0, 53.75, 45.0, "KEY_ADC", 0.75) # SW_DOWN.1
add_seg(53.75, 24.5, 39.75, 24.5, "KEY_ADC", 0.75)
add_seg(39.75, 24.5, 39.75, 30.0, "KEY_ADC", 0.75) # SW_LEFT.1
add_seg(39.75, 30.0, 39.75, 52.0, "KEY_ADC", 0.75) # SW_MENU.1
add_seg(39.75, 24.5, 34.0, 24.5, "KEY_ADC", 0.75)
add_seg(34.0, 24.5, 34.0, 9.5, "KEY_ADC", 0.75)
add_seg(34.0, 9.5, 30.75, 9.5, "KEY_ADC", 0.75)   # C_KEY.1
add_seg(34.0, 9.5, 38.19, 9.5, "KEY_ADC", 0.75)   # R_PU.2

board.Save(board_path)
print("Đã hoàn tất toàn bộ các đường dẫn tín hiệu và nguồn chính!")

# Xuất DSN để Freerouting hoàn thành nối mass GND:
pcbnew.ExportSpecctraDSN(board, dsn_path)

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
    '-mp', '30'
]

print("Bắt đầu chạy Freerouting nối mass GND...")
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
