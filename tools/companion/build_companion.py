#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - Build Script for Decor Space Companion
Tự động đóng gói toàn bộ ứng dụng thành 1 file thực thi .exe duy nhất bằng PyInstaller.
"""

import os
import sys
import shutil
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MAIN_PY = os.path.join(SCRIPT_DIR, "main.py")
ICON_ICO = os.path.join(SCRIPT_DIR, "icon.ico")
TRANSCODER_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "..", "transcoder"))
DIST_DIR = os.path.join(SCRIPT_DIR, "dist")
BUILD_DIR = os.path.join(SCRIPT_DIR, "build")

def build_exe():
    print("=" * 65)
    print("  🔨 ĐANG ĐÓNG GÓI DECOR SPACE COMPANION SANG TỆP .EXE")
    print("=" * 65)

    pyinstaller_cmd = [
        sys.executable, "-m", "PyInstaller",
        "--name=DecorSpaceCompanion",
        "--noconsole",          # Không hiển thị cửa sổ console đen
        "--onefile",            # Đóng gói thành 1 tệp .exe duy nhất
        f"--icon={ICON_ICO}",   # Biểu tượng ứng dụng
        "--clean",              # Dọn dẹp cache build cũ
        f"--distpath={DIST_DIR}",
        f"--workpath={BUILD_DIR}",
        # Nhúng thư mục transcoder để phục vụ Dashboard & Streaming
        f"--add-data={TRANSCODER_DIR};transcoder",
        # Các hidden imports cần thiết
        "--hidden-import=pystray",
        "--hidden-import=pystray._win32",
        "--hidden-import=PIL",
        "--hidden-import=PIL.Image",
        "--hidden-import=PIL.ImageDraw",
        "--hidden-import=tkinter",
        "--hidden-import=tkinter.ttk",
        "--hidden-import=tkinter.messagebox",
        "--hidden-import=werkzeug",
        "--hidden-import=werkzeug.serving",
        "--hidden-import=flask",
        "--hidden-import=requests",
        "--hidden-import=psutil",
        "--hidden-import=win32pdh",
        "--hidden-import=win32com",
        "--hidden-import=win32com.client",
        "--hidden-import=pynvml",
        "--hidden-import=imageio_ffmpeg",
        "--hidden-import=yt_dlp",
        MAIN_PY
    ]

    print("Đang thực thi lệnh PyInstaller...")
    ret = subprocess.run(pyinstaller_cmd)
    if ret.returncode == 0:
        exe_path = os.path.join(DIST_DIR, "DecorSpaceCompanion.exe")
        print("\n" + "=" * 65)
        print(f"🎉 ĐÓNG GÓI THÀNH CÔNG!")
        print(f"📁 Tệp thực thi: {exe_path}")
        if os.path.exists(exe_path):
            size_mb = os.path.getsize(exe_path) / (1024 * 1024)
            print(f"📦 Kích thước  : {size_mb:.2f} MB")
        print("=" * 65)
    else:
        print("\n❌ Quá trình đóng gói thất bại. Mã lỗi:", ret.returncode)

if __name__ == "__main__":
    build_exe()
