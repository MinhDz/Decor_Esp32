"""
Script tự động chạy autoroute cho board Key_pad bằng Freerouting ở chế độ 1 lớp (Bottom Copper B.Cu)
Không sử dụng Via, độ rộng đường mạch 0.35mm, khoảng cách cách điện 0.30mm.
"""
import subprocess, os, re, sys
import pcbnew

board_path = 'hardware/Key_pad/Key_pad.kicad_pcb'
dsn_path = 'hardware/Key_pad/Key_pad.dsn'
ses_path = 'hardware/Key_pad/Key_pad.ses'

# 1. Tái tạo layout sắp đặt linh kiện
subprocess.run([sys.executable, 'scripts/make_pcb_placement.py'], check=True)

board = pcbnew.LoadBoard(board_path)
net_chg = board.FindNet("CHG_STAT")

# 2. Định tuyến đường dẫn ưu tiên cho CHG_STAT qua hành lang đỉnh y=2.5mm
def add_seg(x1, y1, x2, y2, net):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pcbnew.VECTOR2I(int(x1 * 1e6), int(y1 * 1e6)))
    t.SetEnd(pcbnew.VECTOR2I(int(x2 * 1e6), int(y2 * 1e6)))
    t.SetWidth(int(0.35 * 1e6))
    t.SetLayer(pcbnew.B_Cu)
    t.SetNet(net)
    t.SetLocked(True)
    board.Add(t)

add_seg(84.0, 23.16, 80.0, 23.16, net_chg)
add_seg(80.0, 23.16, 80.0, 2.5, net_chg)
add_seg(80.0, 2.5, 21.81, 2.5, net_chg)
add_seg(21.81, 2.5, 21.81, 14.0, net_chg)
add_seg(21.81, 14.0, 21.81, 20.0, net_chg)
add_seg(21.81, 20.0, 19.25, 26.0, net_chg)

board.Save(board_path)
print("Đã đặt đường dẫn ưu tiên cho tín hiệu CHG_STAT.")

# 3. Xuất file DSN
pcbnew.ExportSpecctraDSN(board, dsn_path)

# 4. Cấu hình DSN cho chế độ 1 lớp duy nhất B.Cu
with open(dsn_path, 'r', encoding='utf-8') as f:
    dsn = f.read()

dsn = re.sub(r'\(circuit\s+\(use_via[^\)]+\)\s*\)', '(circuit\n        (use_layer B.Cu)\n      )', dsn)
dsn = re.sub(r'\(circuit\s+\(use_layer B\.Cu\)\)\s*\)', '(circuit\n        (use_layer B.Cu)\n      )', dsn)

if '(layer F.Cu (rule (use_layer off)))' not in dsn:
    dsn = dsn.replace(
        '(rule\n      (width 200)\n      (clearance 200)',
        '(rule\n      (width 350)\n      (clearance 300)\n      (layer F.Cu (rule (use_layer off)))'
    )

with open(dsn_path, 'w', encoding='utf-8') as f:
    f.write(dsn)

# 5. Chạy Freerouting không giao diện
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
    if any(k in line_s for k in ['pass #', 'completed', 'unrouted', 'score']):
        print(" ", line_s, flush=True)

proc.stdout.close()
proc.wait()

if os.path.exists(ses_path):
    with open(ses_path, 'r', encoding='utf-8') as f:
        ses_text = f.read()
    b_cu = len(re.findall(r'\(wire\s+\(path\s+B\.Cu', ses_text))
    vias = len(re.findall(r'\(via\b', ses_text))
    print(f"Đã tạo file SES thành công: {b_cu} đường mạch B.Cu, {vias} Via.")

    # Nạp lại board và nhập file SES
    board = pcbnew.LoadBoard(board_path)
    for t in list(board.GetTracks()):
        if not t.IsLocked():
            board.Remove(t)
    pcbnew.ImportSpecctraSES(board, ses_path)
    board.Save(board_path)
    print("Đã lưu board hoàn chỉnh!")
