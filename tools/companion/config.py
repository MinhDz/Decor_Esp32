#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - PC Companion Configuration & Windows Registry Manager
Quản lý cấu hình dịch vụ và cơ chế tự khởi động cùng hệ điều hành Windows.
"""

import os
import sys
import json
import logging

logger = logging.getLogger("CompanionConfig")

APP_NAME = "DecorSpaceCompanion"
APP_TITLE = "Decor Space Station Companion"
REG_RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"

DEFAULT_CONFIG = {
    "esp32_ip": "192.168.1.233",
    "monitor_enabled": True,
    "monitor_interval": 1.2,
    "transcoder_enabled": True,
    "transcoder_port": 5050,
    "transcoder_fps": 15,
    "transcoder_quality": 5,
    "transcoder_res": "240x240",
    "autostart": True,
    "notify_on_startup": True
}

def get_base_dir():
    """Lấy thư mục gốc chứa file thực thi (.exe) hoặc script chính."""
    if getattr(sys, 'frozen', False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(__file__))

def get_config_path():
    """
    Xác định đường dẫn lưu config.json.
    Ưu tiên lưu ngay tại thư mục chứa file .exe (Portable).
    Nếu không có quyền ghi (ví dụ thư mục Program Files), chuyển sang %APPDATA%.
    """
    base_dir = get_base_dir()
    portable_path = os.path.join(base_dir, "companion_config.json")
    
    # Kiểm tra quyền ghi tại thư mục exe
    try:
        test_file = os.path.join(base_dir, ".write_test")
        with open(test_file, "w") as f:
            f.write("ok")
        os.remove(test_file)
        return portable_path
    except Exception:
        appdata = os.getenv("APPDATA", os.path.expanduser("~"))
        target_dir = os.path.join(appdata, APP_NAME)
        os.makedirs(target_dir, exist_ok=True)
        return os.path.join(target_dir, "companion_config.json")

def load_config():
    """Tải cấu hình từ JSON file, nếu chưa có thì tạo mới với giá trị mặc định."""
    cfg = dict(DEFAULT_CONFIG)
    path = get_config_path()
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                loaded = json.load(f)
                cfg.update(loaded)
        except Exception as e:
            logger.warning(f"Lỗi đọc {path}: {e}")
    else:
        save_config(cfg)
    return cfg

def save_config(cfg):
    """Lưu cấu hình ra file JSON."""
    path = get_config_path()
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2, ensure_ascii=False)
        return True
    except Exception as e:
        logger.error(f"Lỗi lưu {path}: {e}")
        return False

# ============================================================================
#                  QUẢN LÝ WINDOWS REGISTRY (TỰ KHỞI ĐỘNG)
# ============================================================================
def is_windows_autostart_enabled():
    """Kiểm tra xem ứng dụng đã được đăng ký tự khởi động cùng Windows chưa."""
    if sys.platform != "win32":
        return False
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG_RUN_KEY, 0, winreg.KEY_READ) as key:
            val, _ = winreg.QueryValueEx(key, APP_NAME)
            return bool(val)
    except FileNotFoundError:
        return False
    except Exception as e:
        logger.debug(f"Query registry run key error: {e}")
        return False

def set_windows_autostart(enable: bool):
    """
    Bật hoặc tắt tự khởi động cùng Windows thông qua Registry:
    HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run
    """
    if sys.platform != "win32":
        return False
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG_RUN_KEY, 0, winreg.KEY_SET_VALUE) as key:
            if enable:
                if getattr(sys, 'frozen', False):
                    # Nếu là file .exe đã build
                    exe_path = sys.executable
                    cmd = f'"{exe_path}" --silent'
                else:
                    # Nếu đang chạy từ mã nguồn Python
                    python_exe = sys.executable
                    # Thay python.exe bằng pythonw.exe để chạy ẩn không hiện cmd
                    pythonw_exe = python_exe.replace("python.exe", "pythonw.exe")
                    if not os.path.exists(pythonw_exe):
                        pythonw_exe = python_exe
                    main_py = os.path.abspath(os.path.join(get_base_dir(), "main.py"))
                    cmd = f'"{pythonw_exe}" "{main_py}" --silent'

                winreg.SetValueEx(key, APP_NAME, 0, winreg.REG_SZ, cmd)
                logger.info(f"Đã đăng ký khởi động cùng Windows: {cmd}")
            else:
                try:
                    winreg.DeleteValue(key, APP_NAME)
                    logger.info("Đã hủy đăng ký khởi động cùng Windows.")
                except FileNotFoundError:
                    pass
        return True
    except Exception as e:
        logger.error(f"Lỗi cài đặt registry Run: {e}")
        return False
