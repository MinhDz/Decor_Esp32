import subprocess, os, re, sys
import pcbnew

board_path = 'hardware/Key_pad/Key_pad.kicad_pcb'
dsn_path = 'hardware/Key_pad/Key_pad.dsn'
ses_path = 'hardware/Key_pad/Key_pad.ses'

# 1. Tái tạo layout sắp đặt linh kiện
subprocess.run([sys.executable, 'scripts/make_pcb_placement.py'], check=True)

board = pcbnew.LoadBoard(board_path)

# 2. Cập nhật kích thước Pad theo thông số DIY (FeCl3)
# Đường kính pad: 2.0mm - 2.2mm, lỗ khoan: 0.8mm
for fp in board.GetFootprints():
    ref = fp.GetReference()
    if ref.startswith('MH'):
        continue  # Giữ nguyên lỗ bắt ốc M3 (drill 3.2mm, pad 6.0mm)
        
    rot = round(fp.GetOrientationDegrees()) % 180
    for p in fp.Pads():
        p_name = p.GetName()
        # Set drill 0.8mm cho tất cả chân cắm THT
        p.SetDrillSize(pcbnew.VECTOR2I(int(0.8 * 1e6), int(0.8 * 1e6)))
        
        if ref.startswith('SW_') and ref != 'SW_PWR':
            # Phím bấm 2 chân 6x6mm (pitch 4.5mm): Pad 2.2mm tròn
            p.SetSize(pcbnew.VECTOR2I(int(2.2 * 1e6), int(2.2 * 1e6)))
        elif ref.startswith('R_'):
            # Điện trở 1/4W (pitch 7.62mm): Pad 2.2mm tròn
            p.SetSize(pcbnew.VECTOR2I(int(2.2 * 1e6), int(2.2 * 1e6)))
        elif ref == 'C_5V':
            # Tụ hóa 2200uF (pitch 5.0mm): Pad 2.2mm tròn
            p.SetSize(pcbnew.VECTOR2I(int(2.2 * 1e6), int(2.2 * 1e6)))
        elif ref in ['C_KEY', 'C_CHG', 'C_BAT', 'J_BAT']:
            # Tụ gốm & JST XH (pitch 2.50mm): Pad 2.0mm tròn
            p.SetSize(pcbnew.VECTOR2I(int(2.0 * 1e6), int(2.0 * 1e6)))
        elif ref in ['J_ESP', 'J_MHCD42']:
            # Header 2.54mm: Pad 2.0mm tròn (hoặc oval 2.2x2.0)
            # Dùng 2.0mm tròn để clearance giữa các chân luôn >= 0.54mm
            p.SetSize(pcbnew.VECTOR2I(int(2.0 * 1e6), int(2.0 * 1e6)))
        elif ref == 'D_PWR':
            # LED 3mm (pitch 2.54mm): Pad 2.0mm tròn
            p.SetSize(pcbnew.VECTOR2I(int(2.0 * 1e6), int(2.0 * 1e6)))
        elif ref == 'SW_PWR':
            # Công tắc gạt SPDT: 3 chân chính pitch 2.0mm
            if p_name in ['1', '2', '3']:
                p.SetSize(pcbnew.VECTOR2I(int(1.5 * 1e6), int(2.5 * 1e6)))

# 3. Đặt đường dẫn ưu tiên cho tín hiệu CHG_STAT qua hành lang đỉnh y=2.5mm
net_chg = board.FindNet("CHG_STAT")
def add_seg(x1, y1, x2, y2, net, width_mm=0.75):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I(int(x1 * 1e6), int(y1 * 1e6)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2 * 1e6), int(y2 * 1e6)))
    t.SetWidth(int(width_mm * 1e6))
    t.SetLayer(pcbnew.B_Cu)
    t.SetNet(net)
    t.SetLocked(True)
    board.Add(t)

add_seg(84.0, 23.16, 80.0, 23.16, net_chg, 0.75)
add_seg(80.0, 23.16, 80.0, 2.5, net_chg, 0.75)
add_seg(80.0, 2.5, 21.81, 2.5, net_chg, 0.75)
add_seg(21.81, 2.5, 21.81, 14.0, net_chg, 0.75)
add_seg(21.81, 14.0, 21.81, 20.0, net_chg, 0.75)
add_seg(21.81, 20.0, 19.25, 26.0, net_chg, 0.75)

board.Save(board_path)
print("Đã cập nhật pad DIY và đặt đường dẫn ưu tiên CHG_STAT.")

# 4. Xuất file DSN
pcbnew.ExportSpecctraDSN(board, dsn_path)

# 5. Cấu hình DSN cho chế độ 1 lớp duy nhất B.Cu với Netclasses:
# Power: width 1.2mm, clearance 0.50mm
# Signal: width 0.75mm, clearance 0.50mm
with open(dsn_path, 'r', encoding='utf-8') as f:
    dsn = f.read()

# Tắt via và chỉ cho phép B.Cu
dsn = re.sub(r'\(circuit\s+\(use_via[^\)]+\)\s*\)', '(circuit\n        (use_layer B.Cu)\n      )', dsn)
dsn = re.sub(r'\(circuit\s+\(use_layer B\.Cu\)\)\s*\)', '(circuit\n        (use_layer B.Cu)\n      )', dsn)

# Thay thế global rule
dsn = re.sub(
    r'\(rule\s+\(width \d+\)\s+\(clearance \d+\)\s*\)',
    '(rule\n      (width 750)\n      (clearance 500)\n      (layer F.Cu (rule (use_layer off)))\n    )',
    dsn
)

# Thay thế class kicad_default bằng 2 classes: Power và Signal
power_nets = "+3V3 +5V GND VBAT VIN_5V VOUT_5V"
signal_nets = "BAT_ADC CHG_STAT KEY_ADC MH_KEY NET_DOWN NET_EXIT NET_LED_A NET_LEFT NET_MENU NET_RIGHT NET_UP"

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

dsn = re.sub(
    r'\(class kicad_default.*?\n\s*\)',
    classes_def,
    dsn,
    flags=re.DOTALL
)

# Cập nhật độ rộng đường locked pre-route trong wiring
dsn = dsn.replace('(path B.Cu 350', '(path B.Cu 750')

with open(dsn_path, 'w', encoding='utf-8') as f:
    dsn = f.write(dsn)

# 6. Chạy Freerouting
if os.path.exists(ses_path):
    os.remove(ses_path)

cmd = [
    r'C:\Program Files\Java\jdk-21.0.10\bin\java.EXE',
    '-jar',
    os.path.expanduser(r'~/.kicad-mcp/freerouting.jar'),
    '--gui.enabled=false',
    '-de', dsn_path,
    '-do', ses_path,
    '-mp', '60'
]

print("Đang chạy Freerouting tự động...")
proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
for line in iter(proc.stdout.readline, ''):
    line_s = line.strip()
    if any(k in line_s for k in ['pass #', 'completed', 'unrouted', 'score', 'items']):
        print(" ", line_s, flush=True)

proc.stdout.close()
proc.wait()

if os.path.exists(ses_path):
    with open(ses_path, 'r', encoding='utf-8') as f:
        ses_text = f.read()
    b_cu = len(re.findall(r'\(wire\s+\(path\s+B\.Cu', ses_text))
    vias = len(re.findall(r'\(via\b', ses_text))
    print(f"Kết quả: {b_cu} đường mạch B.Cu, {vias} Via.")
    
    board = pcbnew.LoadBoard(board_path)
    for t in list(board.GetTracks()):
        if not t.IsLocked():
            board.Remove(t)
    pcbnew.ImportSpecctraSES(board, ses_path)
    board.Save(board_path)
    print("Đã import và lưu board!")
else:
    print("LỖI: Không tạo được file SES!")
