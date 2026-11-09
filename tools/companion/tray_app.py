#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - System Tray Companion Application
Biểu tượng khay hệ thống (System Tray Icon) với Menu điều khiển nhanh,
tự động cập nhật trạng thái kết nối trạm và hỗ trợ tùy chọn khởi động cùng Windows.
"""

import os
import sys
import time
import logging
import webbrowser
import threading
from PIL import Image, ImageDraw
import pystray
from pystray import MenuItem as item, Menu

logger = logging.getLogger("TrayApp")

def create_station_icon(online=True):
    """Tạo biểu tượng trạm vũ trụ vector sắc nét cho khay hệ thống."""
    size = 64
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Vòng tròn nền trạm (Xanh vũ trụ sâu)
    draw.ellipse((4, 4, 60, 60), fill=(24, 24, 37, 255), outline=(69, 71, 90, 255), width=2)

    # Cánh đĩa anten / Trạm không gian
    draw.arc((12, 14, 52, 54), start=200, end=340, fill=(137, 180, 250, 255), width=4)
    draw.line((32, 28, 32, 46), fill=(180, 190, 254, 255), width=3)
    draw.line((24, 46, 40, 46), fill=(180, 190, 254, 255), width=3)

    # Đèn LED chỉ báo trạng thái ở góc dưới bên phải
    led_color = (166, 227, 161, 255) if online else (243, 139, 168, 255)
    draw.ellipse((44, 44, 58, 58), fill=led_color, outline=(255, 255, 255, 255), width=2)

    return img

class SystemTrayApp:
    def __init__(self, cfg_manager, telemetry_worker, transcoder_worker, settings_dialog):
        self.cfg_manager = cfg_manager
        self.telemetry = telemetry_worker
        self.transcoder = transcoder_worker
        self.settings_dialog = settings_dialog
        self.icon = None
        self._running = False

    def _on_open_transcoder(self, icon, item):
        cfg = self.cfg_manager.load_config()
        port = cfg.get("transcoder_port", 5050)
        webbrowser.open(f"http://localhost:{port}")

    def _on_open_esp_web(self, icon, item):
        cfg = self.cfg_manager.load_config()
        ip = cfg.get("esp32_ip", "192.168.1.233")
        webbrowser.open(f"http://{ip}")

    def _on_toggle_monitor(self, icon, item):
        cfg = self.cfg_manager.load_config()
        new_val = not cfg.get("monitor_enabled", True)
        cfg["monitor_enabled"] = new_val
        self.cfg_manager.save_config(cfg)
        self.telemetry.set_enabled(new_val)
        logger.info(f"Đã chuyển trạng thái PC Monitor: {new_val}")

    def _on_toggle_transcoder(self, icon, item):
        cfg = self.cfg_manager.load_config()
        new_val = not cfg.get("transcoder_enabled", True)
        cfg["transcoder_enabled"] = new_val
        self.cfg_manager.save_config(cfg)
        if new_val:
            self.transcoder.start()
        else:
            self.transcoder.stop()
        logger.info(f"Đã chuyển trạng thái Transcoder: {new_val}")

    def _on_toggle_autostart(self, icon, item):
        cfg = self.cfg_manager.load_config()
        new_val = not cfg.get("autostart", True)
        cfg["autostart"] = new_val
        self.cfg_manager.save_config(cfg)
        self.cfg_manager.set_windows_autostart(new_val)
        logger.info(f"Đã chuyển tùy chọn Khởi động cùng Windows: {new_val}")

    def _on_open_settings(self, icon, item):
        threading.Thread(target=self.settings_dialog.show, daemon=True).start()

    def _on_restart_services(self, icon, item):
        cfg = self.cfg_manager.load_config()
        ip = cfg.get("esp32_ip", "192.168.1.233")
        port = cfg.get("transcoder_port", 5050)
        self.telemetry.set_ip(ip)
        self.telemetry.set_interval(cfg.get("monitor_interval", 1.2))
        self.telemetry.set_enabled(cfg.get("monitor_enabled", True))
        
        self.transcoder.restart(port=port, esp_ip=ip)
        if self.icon:
            self.icon.notify("Đã khởi động lại toàn bộ dịch vụ!", "Trạm Decor Vũ Trụ")

    def _on_exit(self, icon, item):
        logger.info("Người dùng yêu cầu thoát ứng dụng.")
        self._running = False
        self.telemetry.stop()
        self.transcoder.stop()
        icon.stop()

    def _build_menu(self):
        cfg = self.cfg_manager.load_config()
        ip = cfg.get("esp32_ip", "192.168.1.233")
        is_online = self.telemetry.is_station_online
        status_text = f"🛰️ Trạm: {ip} [{'🟢 Online' if is_online else '🔴 Offline'}]"

        return Menu(
            item(status_text, self._on_open_settings),
            Menu.SEPARATOR,
            item("🌐 Mở Bảng Điều Khiển Video (Port 5050)", self._on_open_transcoder),
            item("🖥️ Mở Trang Web Trạm ESP32", self._on_open_esp_web),
            Menu.SEPARATOR,
            item(
                "☑️ Gửi thông số PC (PC HUD)",
                self._on_toggle_monitor,
                checked=lambda it: self.cfg_manager.load_config().get("monitor_enabled", True)
            ),
            item(
                "☑️ Web Transcoder Engine",
                self._on_toggle_transcoder,
                checked=lambda it: self.transcoder.is_running()
            ),
            item(
                "☑️ Khởi động cùng Windows",
                self._on_toggle_autostart,
                checked=lambda it: self.cfg_manager.is_windows_autostart_enabled()
            ),
            Menu.SEPARATOR,
            item("⚙️ Cài đặt & Cấu hình...", self._on_open_settings),
            item("🔄 Khởi động lại dịch vụ", self._on_restart_services),
            item("❌ Thoát ứng dụng", self._on_exit)
        )

    def _status_updater(self):
        """Vòng lặp ngầm cập nhật trạng thái online/offline và tooltip."""
        last_online = None
        while self._running:
            try:
                curr_online = self.telemetry.is_station_online
                cfg = self.cfg_manager.load_config()
                ip = cfg.get("esp32_ip", "192.168.1.233")

                if self.icon:
                    tooltip = f"Trạm Decor Vũ Trụ - {ip} ({'🟢 Online' if curr_online else '🔴 Offline'})"
                    if curr_online and self.telemetry.last_metrics:
                        cpu_u = self.telemetry.last_metrics.get("cpu", {}).get("usage", 0)
                        ram_u = self.telemetry.last_metrics.get("ram", {}).get("usage", 0)
                        tooltip += f"\nCPU: {cpu_u}% | RAM: {ram_u}%"
                    self.icon.title = tooltip[:120]

                    if curr_online != last_online:
                        self.icon.icon = create_station_icon(online=curr_online)
                        self.icon.menu = self._build_menu()
                        last_online = curr_online
            except Exception as e:
                logger.debug(f"Status update tick error: {e}")
            time.sleep(2.0)

    def run(self):
        self._running = True
        self.icon = pystray.Icon(
            "DecorSpaceCompanion",
            create_station_icon(online=False),
            "Trạm Decor Vũ Trụ - Đang kết nối...",
            menu=self._build_menu()
        )

        # Chạy thread cập nhật trạng thái khay
        threading.Thread(target=self._status_updater, name="TrayStatusUpdater", daemon=True).start()

        # Hiển thị thông báo chào mừng
        cfg = self.cfg_manager.load_config()
        if cfg.get("notify_on_startup", True):
            def send_welcome():
                time.sleep(1.0)
                if self.icon:
                    ip = cfg.get("esp32_ip", "192.168.1.233")
                    self.icon.notify(
                        f"Dịch vụ Trạm Decor Vũ Trụ đang chạy ngầm.\nĐích đến: {ip}",
                        "Decor Space Companion"
                    )
            threading.Thread(target=send_welcome, daemon=True).start()

        logger.info("System Tray App đang chạy...")
        self.icon.run()
