#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - PC Companion Unified Application
Gói ứng dụng hợp nhất chạy ngầm dưới khay hệ thống Windows (System Tray),
tích hợp Transcoder Stream Server (port 5050) và PC Telemetry Monitor (PC HUD),
hỗ trợ bật/tắt tự khởi động cùng Windows.
"""

import os
import sys
import time
import signal
import logging
import argparse

# Thiết lập encoding UTF-8 cho Windows console
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# Thiết lập Logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(name)s/%(levelname)s] %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger("MainCompanion")

# Thêm thư mục hiện tại vào sys.path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import config as cfg_mgr
from telemetry_worker import TelemetryWorker
from transcoder_worker import TranscoderWorker
from settings_dialog import SettingsDialog
from tray_app import SystemTrayApp

def main():
    parser = argparse.ArgumentParser(description="Decor Space Station Companion")
    parser.add_argument("--silent", action="store_true", help="Chạy ở chế độ nền không in log console")
    parser.add_argument("--ip", type=str, help="Ghi đè địa chỉ IP của ESP32")
    parser.add_argument("--port", type=int, help="Ghi đè cổng Transcoder (mặc định 5050)")
    args = parser.parse_args()

    logger.info("=" * 65)
    logger.info("  🚀 TRẠM DECOR VŨ TRỤ - PC COMPANION SERVICE")
    logger.info("  System Tray + PC Hardware Telemetry + Web Transcoder")
    logger.info("=" * 65)

    # 1. Tải cấu hình
    cfg = cfg_mgr.load_config()
    if args.ip:
        cfg["esp32_ip"] = args.ip.strip()
    if args.port:
        cfg["transcoder_port"] = args.port

    esp_ip = cfg.get("esp32_ip", "192.168.1.233")
    interval = cfg.get("monitor_interval", 1.2)
    mon_enabled = cfg.get("monitor_enabled", True)
    tran_enabled = cfg.get("transcoder_enabled", True)
    tran_port = cfg.get("transcoder_port", 5050)
    autostart = cfg.get("autostart", True)

    logger.info(f"Mục tiêu ESP32        : http://{esp_ip}")
    logger.info(f"Dịch vụ PC Monitor    : {'Bật' if mon_enabled else 'Tắt'} (chu kỳ {interval}s)")
    logger.info(f"Dịch vụ Web Transcoder: {'Bật' if tran_enabled else 'Tắt'} (cổng {tran_port})")
    logger.info(f"Khởi động cùng Windows: {'Bật' if autostart else 'Tắt'}")

    # Đảm bảo Registry Windows đồng bộ theo cấu hình
    if autostart and not cfg_mgr.is_windows_autostart_enabled():
        cfg_mgr.set_windows_autostart(True)

    # 2. Khởi tạo Telemetry Worker
    telemetry = TelemetryWorker(esp_ip=esp_ip, interval=interval, enabled=mon_enabled)
    telemetry.start()

    # 3. Khởi tạo Transcoder Worker
    transcoder = TranscoderWorker(port=tran_port, esp_ip=esp_ip, enabled=tran_enabled)
    if tran_enabled:
        transcoder.start()

    # 4. Định nghĩa callback cập nhật khi người dùng lưu cài đặt từ Settings Dialog
    def on_settings_saved(new_cfg):
        new_ip = new_cfg.get("esp32_ip", esp_ip)
        new_interval = new_cfg.get("monitor_interval", 1.2)
        new_mon = new_cfg.get("monitor_enabled", True)
        new_tran = new_cfg.get("transcoder_enabled", True)
        new_port = new_cfg.get("transcoder_port", 5050)

        logger.info(f"Áp dụng cấu hình mới: IP={new_ip}, Mon={new_mon}, Tran={new_tran}:{new_port}")
        telemetry.set_ip(new_ip)
        telemetry.set_interval(new_interval)
        telemetry.set_enabled(new_mon)

        transcoder.set_esp_ip(new_ip)
        if new_tran:
            if not transcoder.is_running() or transcoder.port != new_port:
                transcoder.restart(port=new_port, esp_ip=new_ip)
        else:
            transcoder.stop()

    settings_dlg = SettingsDialog(cfg_mgr, on_save_callback=on_settings_saved)

    # 5. Khởi tạo System Tray App
    tray = SystemTrayApp(
        cfg_manager=cfg_mgr,
        telemetry_worker=telemetry,
        transcoder_worker=transcoder,
        settings_dialog=settings_dlg
    )

    # Xử lý đóng an toàn khi nhận Ctrl+C
    def handle_sigint(signum, frame):
        logger.info("Nhận tín hiệu dừng (SIGINT)...")
        telemetry.stop()
        transcoder.stop()
        if tray.icon:
            tray.icon.stop()
        sys.exit(0)

    try:
        signal.signal(signal.SIGINT, handle_sigint)
    except Exception:
        pass

    # 6. Chạy System Tray (blocking loop trên main thread)
    tray.run()

if __name__ == "__main__":
    main()
