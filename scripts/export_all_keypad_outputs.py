import subprocess
import os

cli = r'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe'
sch_path = r'hardware/Key_pad/Key_pad.kicad_sch'
pcb_path = r'hardware/Key_pad/Key_pad.kicad_pcb'
out_dir = r'hardware/Key_pad'
gerber_dir = r'hardware/Key_pad/gerbers'
os.makedirs(gerber_dir, exist_ok=True)

print("1. Exporting Schematic PDF & SVG...")
subprocess.run([cli, 'sch', 'export', 'pdf', sch_path, '-o', os.path.join(out_dir, 'Key_pad_Schematic.pdf')], check=True)
subprocess.run([cli, 'sch', 'export', 'svg', sch_path, '-o', os.path.join(out_dir, 'Key_pad_Schematic.svg')], check=True)

print("2. Exporting Schematic BOM...")
subprocess.run([cli, 'sch', 'export', 'bom', sch_path, '-o', os.path.join(out_dir, 'Key_pad_BOM.csv')], check=True)

print("3. Exporting PCB PDFs...")
# All layers PDF
subprocess.run([
    cli, 'pcb', 'export', 'pdf', pcb_path,
    '--layers', 'F.Cu,B.Cu,Edge.Cuts,F.SilkS,B.SilkS',
    '--mode-single',
    '-o', os.path.join(out_dir, 'Key_pad_PCB.pdf')
], check=True)

# Bottom Copper PDF (Normal)
subprocess.run([
    cli, 'pcb', 'export', 'pdf', pcb_path,
    '--layers', 'B.Cu,Edge.Cuts',
    '--black-and-white',
    '--mode-single',
    '-o', os.path.join(out_dir, 'Key_pad_BottomCopper.pdf')
], check=True)

# Bottom Copper PDF (Mirrored for Iron-On / Toner Transfer DIY etching)
subprocess.run([
    cli, 'pcb', 'export', 'pdf', pcb_path,
    '--layers', 'B.Cu,Edge.Cuts',
    '--black-and-white',
    '--mirror',
    '--mode-single',
    '-o', os.path.join(out_dir, 'Key_pad_BottomCopper_IronOn_Mirrored.pdf')
], check=True)

print("4. Exporting PCB SVGs...")
# Board all layers SVG
subprocess.run([
    cli, 'pcb', 'export', 'svg', pcb_path,
    '--layers', 'F.Cu,B.Cu,Edge.Cuts,F.SilkS',
    '--page-size-mode', '2',
    '--mode-single',
    '-o', os.path.join(out_dir, 'Key_pad_Board.svg')
], check=True)

# Bottom Copper SVG
subprocess.run([
    cli, 'pcb', 'export', 'svg', pcb_path,
    '--layers', 'B.Cu,Edge.Cuts',
    '--black-and-white',
    '--page-size-mode', '2',
    '--mode-single',
    '-o', os.path.join(out_dir, 'Key_pad_BottomCopper.svg')
], check=True)

print("5. Rendering 2D / 3D Top and Bottom view PNGs...")
subprocess.run([
    cli, 'pcb', 'render', pcb_path,
    '--side', 'top',
    '--width', '1600', '--height', '1067',
    '-o', os.path.join(out_dir, 'Key_pad_2d_top_view.png')
], check=True)

subprocess.run([
    cli, 'pcb', 'render', pcb_path,
    '--side', 'bottom',
    '--width', '1600', '--height', '1067',
    '-o', os.path.join(out_dir, 'Key_pad_2d_bottom_view.png')
], check=True)

subprocess.run([
    cli, 'pcb', 'render', pcb_path,
    '--perspective',
    '--rotate', '-45,0,45',
    '--width', '1600', '--height', '1067',
    '-o', os.path.join(out_dir, 'Key_pad_3d_iso_view.png')
], check=True)

print("6. Exporting Gerbers & Drill files...")
subprocess.run([
    cli, 'pcb', 'export', 'gerbers', pcb_path,
    '-o', gerber_dir
], check=True)

subprocess.run([
    cli, 'pcb', 'export', 'drill', pcb_path,
    '-o', gerber_dir
], check=True)

print("ALL EXPORTS COMPLETED SUCCESSFULLY!")
