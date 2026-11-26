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

# Thêm thư mục hiện tại vào sys.path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import config as cfg_mgr

# Thiết lập Logging đồng thời ra File và Console
LOG_FILE = os.path.join(cfg_mgr.get_base_dir(), "companion.log")
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(name)s/%(levelname)s] %(message)s',
    datefmt='%H:%M:%S',
    handlers=[
        logging.FileHandler(LOG_FILE, encoding='utf-8', mode='a'),
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger("MainCompanion")
from telemetry_worker import TelemetryWorker
from transcoder_worker import TranscoderWorker
from ai_gateway_worker import AiGatewayWorker
from discovery_worker import DiscoveryWorker
from settings_dialog import SettingsDialog
from tray_app import SystemTrayApp

_app_mutex = None

def main():
    global _app_mutex
    # Kiểm tra Single Instance trên Windows để tránh chạy trùng 2 bản gây xung đột cổng 8765 / 5050
    if sys.platform == "win32":
        import ctypes
        kernel32 = ctypes.windll.kernel32
        MUTEX_NAME = "Global\\DecorSpaceCompanion_SingleInstance_Mutex"
        _app_mutex = kernel32.CreateMutexW(None, False, MUTEX_NAME)
        if kernel32.GetLastError() == 183: # ERROR_ALREADY_EXISTS
            logger.warning("⚠️ Ứng dụng DecorSpaceCompanion đã đang chạy dưới khay hệ thống (System Tray).")
            print("\n[THÔNG BÁO] Decor Space Companion đã đang chạy sẵn dưới khay hệ thống (System Tray).")
            print("Vui lòng nhấp chuột phải vào biểu tượng khay hệ thống để mở Cài Đặt hoặc điều khiển!\n")
            sys.exit(0)

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

    esp_ip = cfg.get("esp32_ip", "192.168.1.82")
    interval = cfg.get("monitor_interval", 1.2)
    mon_enabled = cfg.get("monitor_enabled", True)
    tran_enabled = cfg.get("transcoder_enabled", True)
    tran_port = cfg.get("transcoder_port", 5050)
    ai_enabled = cfg.get("ai_gateway_enabled", True)
    ai_port = cfg.get("ai_gateway_port", 8765)
    autostart = cfg.get("autostart", True)

    logger.info(f"Mục tiêu ESP32        : http://{esp_ip}")
    logger.info(f"Dịch vụ PC Monitor    : {'Bật' if mon_enabled else 'Tắt'} (chu kỳ {interval}s)")
    logger.info(f"Dịch vụ Web Transcoder: {'Bật' if tran_enabled else 'Tắt'} (cổng {tran_port})")
    logger.info(f"Dịch vụ AI Gateway 2.0: {'Bật' if ai_enabled else 'Tắt'} (cổng {ai_port})")
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

    # 4. Khởi tạo AI Gateway Worker
    ai_gateway = AiGatewayWorker(cfg_mgr, port=ai_port, enabled=ai_enabled)
    if ai_enabled:
        ai_gateway.start()

    # 4B. Khởi tạo UDP Discovery Worker (Cổng 8767 - Đồng bộ IP 2 chiều tự động)
    def on_station_discovered(discovered_ip):
        logger.info(f"🎯 [AUTO SYNC] Đồng bộ Trạm Decor sang IP mới: {discovered_ip}")
        c = cfg_mgr.load_config()
        if c.get("esp32_ip") != discovered_ip:
            c["esp32_ip"] = discovered_ip
            cfg_mgr.save_config(c)
        telemetry.set_ip(discovered_ip)
        transcoder.set_esp_ip(discovered_ip)
        if hasattr(settings_dlg, 'ip_entry'):
            try:
                settings_dlg.ip_entry.delete(0, 'end')
                settings_dlg.ip_entry.insert(0, discovered_ip)
            except Exception:
                pass

    discovery = DiscoveryWorker(
        current_esp_ip=esp_ip,
        gateway_port=ai_port,
        transcoder_port=tran_port,
        on_station_discovered=on_station_discovered
    )
    discovery.start()

    # 5. Định nghĩa callback cập nhật khi người dùng lưu cài đặt từ Settings Dialog
    def on_settings_saved(new_cfg):
        new_ip = new_cfg.get("esp32_ip", esp_ip)
        new_interval = new_cfg.get("monitor_interval", 1.2)
        new_mon = new_cfg.get("monitor_enabled", True)
        new_tran = new_cfg.get("transcoder_enabled", True)
        new_port = new_cfg.get("transcoder_port", 5050)
        new_ai_enabled = new_cfg.get("ai_gateway_enabled", True)
        new_ai_port = new_cfg.get("ai_gateway_port", 8765)

        logger.info(f"Áp dụng cấu hình mới: IP={new_ip}, Mon={new_mon}, Tran={new_tran}:{new_port}, AI={new_ai_enabled}:{new_ai_port}")
        telemetry.set_ip(new_ip)
        telemetry.set_interval(new_interval)
        telemetry.set_enabled(new_mon)
        discovery.set_esp_ip(new_ip)

        transcoder.set_esp_ip(new_ip)
        if new_tran:
            if not transcoder.is_running() or transcoder.port != new_port:
                transcoder.restart(port=new_port, esp_ip=new_ip)
        else:
            transcoder.stop()

        if new_ai_enabled:
            if not ai_gateway.is_running() or ai_gateway.port != new_ai_port:
                ai_gateway.restart(port=new_ai_port)
        else:
            ai_gateway.stop()

    settings_dlg = SettingsDialog(cfg_mgr, on_save_callback=on_settings_saved)

    # 6. Khởi tạo System Tray App
    tray = SystemTrayApp(
        cfg_manager=cfg_mgr,
        telemetry_worker=telemetry,
        transcoder_worker=transcoder,
        ai_gateway_worker=ai_gateway,
        settings_dialog=settings_dlg
    )

    # Xử lý đóng an toàn khi nhận Ctrl+C
    def handle_sigint(signum, frame):
        logger.info("Nhận tín hiệu dừng (SIGINT)...")
        discovery.stop()
        telemetry.stop()
        transcoder.stop()
        ai_gateway.stop()
        if tray.icon:
            tray.icon.stop()
        settings_dlg.close_completely()
        sys.exit(0)

    try:
        signal.signal(signal.SIGINT, handle_sigint)
    except Exception:
        pass

    # 7. Chạy System Tray (chạy nền) & Tkinter event loop trên Main Thread
    tray.start()
    settings_dlg.run()

    # Khi người dùng chọn Thoát từ khay hệ thống, mainloop kết thúc:
    logger.info("Main loop đã kết thúc, tiến hành dọn dẹp các dịch vụ...")
    discovery.stop()
    telemetry.stop()
    transcoder.stop()
    ai_gateway.stop()
    if tray.icon:
        tray.icon.stop()
    sys.exit(0)

if __name__ == "__main__":
    main()
