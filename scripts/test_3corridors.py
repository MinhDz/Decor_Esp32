import subprocess, os, re, sys
import pcbnew

board_path = 'hardware/Key_pad/Key_pad.kicad_pcb'
dsn_path = 'hardware/Key_pad/Key_pad.dsn'
ses_path = 'hardware/Key_pad/Key_pad.ses'

# 1. Tái tạo layout sắp đặt linh kiện
subprocess.run(['python', 'scripts/make_pcb_placement.py'], check=True)

board = pcbnew.LoadBoard(board_path)

# 2. Cập nhật kích thước Pad theo yêu cầu DIY của người dùng:
# Đường kính Pad: 2.0mm - 2.2mm, Lỗ khoan: 0.8mm
for fp in board.GetFootprints():
    ref = fp.GetReference()
    if ref.startswith('MH'):
        continue  # Giữ nguyên lỗ bắt ốc M3 (drill 3.2mm, pad 6.0mm)
        
    for p in fp.Pads():
        p_name = p.GetName()
        # Lỗ khoan 0.8mm chuẩn cho gia công khoan tay DIY
        p.SetDrillSize(pcbnew.VECTOR2I(int(0.8 * 1e6), int(0.8 * 1e6)))
        
        if ref.startswith('SW_') and ref != 'SW_PWR':
            # Phím bấm 6x6mm 2 chân (pitch 4.5mm): Pad 2.2mm
            p.SetSize(pcbnew.VECTOR2I(int(2.2 * 1e6), int(2.2 * 1e6)))
        elif ref.startswith('R_'):
            # Điện trở 1/4W (pitch 7.62mm): Pad 2.2mm
            p.SetSize(pcbnew.VECTOR2I(int(2.2 * 1e6), int(2.2 * 1e6)))
        elif ref == 'C_5V':
            # Tụ hóa 2200uF (pitch 5.0mm): Pad 2.2mm
            p.SetSize(pcbnew.VECTOR2I(int(2.2 * 1e6), int(2.2 * 1e6)))
        elif ref in ['C_KEY', 'C_CHG', 'C_BAT', 'J_BAT']:
            # Tụ gốm & JST XH (pitch 2.50mm): Pad 2.0mm tròn (clearance 0.50mm)
            p.SetSize(pcbnew.VECTOR2I(int(2.0 * 1e6), int(2.0 * 1e6)))
        elif ref in ['J_ESP', 'J_MHCD42']:
            # Header cắm 2.54mm: Pad 2.0mm tròn (clearance 0.54mm)
            p.SetSize(pcbnew.VECTOR2I(int(2.0 * 1e6), int(2.0 * 1e6)))
        elif ref == 'D_PWR':
            # LED 3mm (pitch 2.54mm): Pad 2.0mm tròn
            p.SetSize(pcbnew.VECTOR2I(int(2.0 * 1e6), int(2.0 * 1e6)))
        elif ref == 'SW_PWR':
            # Công tắc gạt SPDT: chân 1, 2, 3 pitch 2.0mm
            if p_name in ['1', '2', '3']:
                p.SetSize(pcbnew.VECTOR2I(int(1.5 * 1e6), int(2.5 * 1e6)))

# 3. Đặt các đường dẫn phân luồng hành lang ưu tiên (Pre-routes) để đảm bảo không bị chồng chéo trên 1 lớp B.Cu:
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

# A. Đường nguồn +5V (1.2mm): từ J_ESP pin 1 (84, 13) lên y=2.5mm qua đỉnh tới C_5V (23, 33.5)
add_seg(84.0, 13.0, 81.0, 13.0, "+5V", 1.2)
add_seg(81.0, 13.0, 81.0, 2.5, "+5V", 1.2)
add_seg(81.0, 2.5, 23.0, 2.5, "+5V", 1.2)
add_seg(23.0, 2.5, 23.0, 33.5, "+5V", 1.2)

# B. Đường nguồn +3V3 (0.75mm): từ J_ESP pin 2 (84, 15.54) qua y=4.8mm tới R_PU (38.19, 9.5)
add_seg(84.0, 15.54, 78.0, 15.54, "+3V3", 0.75)
add_seg(78.0, 15.54, 78.0, 4.8, "+3V3", 0.75)
add_seg(78.0, 4.8, 38.19, 4.8, "+3V3", 0.75)
add_seg(38.19, 4.8, 38.19, 9.5, "+3V3", 0.75)

# C. Đường tín hiệu CHG_STAT (0.75mm): từ J_ESP pin 5 (84, 23.16) qua hành lang đáy y=57.0mm tới C_CHG/R_CHG2
add_seg(84.0, 23.16, 75.0, 23.16, "CHG_STAT", 0.75)
add_seg(75.0, 23.16, 75.0, 57.0, "CHG_STAT", 0.75)
add_seg(75.0, 57.0, 19.25, 57.0, "CHG_STAT", 0.75)
add_seg(19.25, 57.0, 19.25, 26.0, "CHG_STAT", 0.75)

board.Save(board_path)
print("Đã đặt 3 luồng ưu tiên: +5V (đỉnh), +3V3 (cận đỉnh), CHG_STAT (đáy)!")

# 4. Xuất DSN
pcbnew.ExportSpecctraDSN(board, dsn_path)

# 5. Cấu hình DSN cho chế độ 1 lớp duy nhất B.Cu
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
    '-mp', '60'
]

print("Bắt đầu chạy Freerouting với 3 hành lang phân luồng...")
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
    if any(k in l_s for k in ['pass #', 'completed', 'Restoring', 'Routing completed']):
        print(l_s, flush=True)

p.stdout.close()
p.wait()

if os.path.exists(ses_path):
    print("SES file created!")
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
