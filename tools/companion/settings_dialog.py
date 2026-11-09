#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - Modern Settings Dialog (Tkinter)
Cửa sổ cấu hình thân thiện, hỗ trợ kiểm tra kết nối trạm ESP32 và tùy chọn khởi động cùng Windows.
"""

import os
import sys
import time
import threading
import requests
import tkinter as tk
from tkinter import ttk, messagebox

# Colors - Dark theme palette
BG_DARK = "#181825"
BG_CARD = "#1e1e2e"
BG_INPUT = "#313244"
TEXT_MAIN = "#cdd6f4"
TEXT_MUTED = "#a6adc8"
ACCENT_BLUE = "#89b4fa"
ACCENT_GREEN = "#a6e3a1"
ACCENT_RED = "#f38ba8"
BORDER_COLOR = "#45475a"

class SettingsDialog:
    def __init__(self, cfg_manager, on_save_callback=None):
        self.cfg_manager = cfg_manager
        self.on_save_callback = on_save_callback
        self.root = None

    def show(self):
        # Nếu dialog đang mở thì đưa lên trên cùng
        if self.root and self.root.winfo_exists():
            self.root.lift()
            self.root.focus_force()
            return

        cfg = self.cfg_manager.load_config()
        self.root = tk.Tk()
        self.root.title("Cài Đặt - Decor Space Companion")
        self.root.geometry("460x520")
        self.root.resizable(False, False)
        self.root.configure(bg=BG_DARK)

        # Đặt cửa sổ ở giữa màn hình
        self.root.update_idletasks()
        w = self.root.winfo_width()
        h = self.root.winfo_height()
        ws = self.root.winfo_screenwidth()
        hs = self.root.winfo_screenheight()
        x = (ws // 2) - (w // 2)
        y = (hs // 2) - (h // 2)
        self.root.geometry(f"{w}x{h}+{x}+{y}")

        # Header
        header_frame = tk.Frame(self.root, bg=BG_DARK)
        header_frame.pack(fill="x", padx=20, pady=(16, 10))

        title_lbl = tk.Label(
            header_frame,
            text="🛰️ Trạm Decor Vũ Trụ",
            font=("Segoe UI", 14, "bold"),
            bg=BG_DARK,
            fg=TEXT_MAIN
        )
        title_lbl.pack(anchor="w")

        sub_lbl = tk.Label(
            header_frame,
            text="Cấu hình kết nối phần cứng và dịch vụ chạy nền PC",
            font=("Segoe UI", 9),
            bg=BG_DARK,
            fg=TEXT_MUTED
        )
        sub_lbl.pack(anchor="w")

        # Container chính
        container = tk.Frame(self.root, bg=BG_DARK)
        container.pack(fill="both", expand=True, padx=20, pady=5)

        # --- Khối 1: Kết nối Trạm ESP32 ---
        esp_card = tk.LabelFrame(
            container,
            text=" 📡 Kết nối Trạm ESP32 ",
            font=("Segoe UI", 10, "bold"),
            bg=BG_CARD,
            fg=ACCENT_BLUE,
            padx=12,
            pady=10,
            relief="solid",
            bd=1
        )
        esp_card.pack(fill="x", pady=(0, 12))

        ip_row = tk.Frame(esp_card, bg=BG_CARD)
        ip_row.pack(fill="x", pady=4)

        tk.Label(ip_row, text="Địa chỉ IP:", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN).pack(side="left")
        self.ip_entry = tk.Entry(
            ip_row,
            font=("Segoe UI", 10),
            bg=BG_INPUT,
            fg=TEXT_MAIN,
            insertbackground=TEXT_MAIN,
            relief="flat",
            width=18
        )
        self.ip_entry.insert(0, cfg.get("esp32_ip", "192.168.1.233"))
        self.ip_entry.pack(side="left", padx=(8, 8))

        self.btn_ping = tk.Button(
            ip_row,
            text="Kiểm tra",
            font=("Segoe UI", 8, "bold"),
            bg=ACCENT_BLUE,
            fg="#11111b",
            relief="flat",
            padx=10,
            cursor="hand2",
            command=self._test_connection
        )
        self.btn_ping.pack(side="left")

        self.status_lbl = tk.Label(
            esp_card,
            text="Nhấn 'Kiểm tra' để thử kết nối tới trạm.",
            font=("Segoe UI", 8, "italic"),
            bg=BG_CARD,
            fg=TEXT_MUTED
        )
        self.status_lbl.pack(anchor="w", pady=(4, 0))

        # --- Khối 2: Tự động hóa & Khởi động ---
        auto_card = tk.LabelFrame(
            container,
            text=" ⚙️ Tùy Chọn & Tự Động Hóa ",
            font=("Segoe UI", 10, "bold"),
            bg=BG_CARD,
            fg=ACCENT_BLUE,
            padx=12,
            pady=10,
            relief="solid",
            bd=1
        )
        auto_card.pack(fill="x", pady=(0, 12))

        self.var_autostart = tk.BooleanVar(value=cfg.get("autostart", True))
        chk_autostart = tk.Checkbutton(
            auto_card,
            text="Khởi động cùng Windows (Auto-start on Boot)",
            variable=self.var_autostart,
            font=("Segoe UI", 9),
            bg=BG_CARD,
            fg=TEXT_MAIN,
            selectcolor=BG_INPUT,
            activebackground=BG_CARD,
            activeforeground=TEXT_MAIN
        )
        chk_autostart.pack(anchor="w", pady=2)

        self.var_monitor = tk.BooleanVar(value=cfg.get("monitor_enabled", True))
        chk_monitor = tk.Checkbutton(
            auto_card,
            text="Bật gửi số liệu PC lên màn hình Trạm (PC HUD)",
            variable=self.var_monitor,
            font=("Segoe UI", 9),
            bg=BG_CARD,
            fg=TEXT_MAIN,
            selectcolor=BG_INPUT,
            activebackground=BG_CARD,
            activeforeground=TEXT_MAIN
        )
        chk_monitor.pack(anchor="w", pady=2)

        self.var_transcoder = tk.BooleanVar(value=cfg.get("transcoder_enabled", True))
        chk_transcoder = tk.Checkbutton(
            auto_card,
            text="Bật máy chủ chuyển mã Video (Transcoder)",
            variable=self.var_transcoder,
            font=("Segoe UI", 9),
            bg=BG_CARD,
            fg=TEXT_MAIN,
            selectcolor=BG_INPUT,
            activebackground=BG_CARD,
            activeforeground=TEXT_MAIN
        )
        chk_transcoder.pack(anchor="w", pady=2)

        # --- Khối 3: Thông số nâng cao ---
        adv_card = tk.LabelFrame(
            container,
            text=" ⏱️ Thông Số Hoạt Động ",
            font=("Segoe UI", 10, "bold"),
            bg=BG_CARD,
            fg=ACCENT_BLUE,
            padx=12,
            pady=8,
            relief="solid",
            bd=1
        )
        adv_card.pack(fill="x", pady=(0, 12))

        row_int = tk.Frame(adv_card, bg=BG_CARD)
        row_int.pack(fill="x", pady=2)
        tk.Label(row_int, text="Chu kỳ gửi thông số (giây):", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN).pack(side="left")
        self.interval_entry = tk.Entry(
            row_int,
            font=("Segoe UI", 9),
            bg=BG_INPUT,
            fg=TEXT_MAIN,
            insertbackground=TEXT_MAIN,
            relief="flat",
            width=6
        )
        self.interval_entry.insert(0, str(cfg.get("monitor_interval", 1.2)))
        self.interval_entry.pack(side="right")

        row_port = tk.Frame(adv_card, bg=BG_CARD)
        row_port.pack(fill="x", pady=2)
        tk.Label(row_port, text="Cổng Web Transcoder (Port):", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN).pack(side="left")
        self.port_entry = tk.Entry(
            row_port,
            font=("Segoe UI", 9),
            bg=BG_INPUT,
            fg=TEXT_MAIN,
            insertbackground=TEXT_MAIN,
            relief="flat",
            width=6
        )
        self.port_entry.insert(0, str(cfg.get("transcoder_port", 5050)))
        self.port_entry.pack(side="right")

        # Nút Lưu và Hủy
        btn_frame = tk.Frame(self.root, bg=BG_DARK)
        btn_frame.pack(fill="x", padx=20, pady=(6, 16))

        btn_save = tk.Button(
            btn_frame,
            text="💾 Lưu & Áp Dụng",
            font=("Segoe UI", 9, "bold"),
            bg=ACCENT_GREEN,
            fg="#11111b",
            relief="flat",
            padx=16,
            pady=6,
            cursor="hand2",
            command=self._save_settings
        )
        btn_save.pack(side="right", padx=(8, 0))

        btn_cancel = tk.Button(
            btn_frame,
            text="Đóng",
            font=("Segoe UI", 9),
            bg=BG_INPUT,
            fg=TEXT_MAIN,
            relief="flat",
            padx=14,
            pady=6,
            cursor="hand2",
            command=self.root.destroy
        )
        btn_cancel.pack(side="right")

        self.root.mainloop()

    def _test_connection(self):
        ip = self.ip_entry.get().strip()
        if not ip:
            self.status_lbl.config(text="⚠️ Vui lòng nhập địa chỉ IP!", fg=ACCENT_RED)
            return

        self.status_lbl.config(text=f"Đang kiểm tra kết nối tới {ip}...", fg=TEXT_MUTED)
        self.btn_ping.config(state="disabled")

        def check_task():
            start_t = time.time()
            try:
                # Thử ping cổng 80 của ESP32 Web Server
                res = requests.get(f"http://{ip}/api/status", timeout=2.0)
                ms = int((time.time() - start_t) * 1000)
                if res.status_code == 200:
                    msg = f"🟢 Kết nối thành công! Phản hồi: {ms}ms"
                    color = ACCENT_GREEN
                else:
                    msg = f"🟡 Trạm phản hồi mã HTTP {res.status_code} ({ms}ms)"
                    color = "#f9e2af"
            except requests.exceptions.Timeout:
                msg = "🔴 Lỗi: Hết thời gian chờ phản hồi (Timeout 2s)"
                color = ACCENT_RED
            except requests.exceptions.ConnectionError:
                msg = "🔴 Không thể kết nối tới IP này (Hãy kiểm tra lại Wi-Fi)"
                color = ACCENT_RED
            except Exception as e:
                msg = f"🔴 Lỗi: {e}"
                color = ACCENT_RED

            def update_ui():
                if self.root and self.root.winfo_exists():
                    self.status_lbl.config(text=msg, fg=color)
                    self.btn_ping.config(state="normal")

            if self.root and self.root.winfo_exists():
                self.root.after(0, update_ui)

        threading.Thread(target=check_task, daemon=True).start()

    def _save_settings(self):
        new_ip = self.ip_entry.get().strip()
        try:
            new_interval = float(self.interval_entry.get().strip())
        except ValueError:
            new_interval = 1.2

        try:
            new_port = int(self.port_entry.get().strip())
        except ValueError:
            new_port = 5050

        cfg = self.cfg_manager.load_config()
        cfg["esp32_ip"] = new_ip
        cfg["monitor_enabled"] = self.var_monitor.get()
        cfg["monitor_interval"] = max(0.5, new_interval)
        cfg["transcoder_enabled"] = self.var_transcoder.get()
        cfg["transcoder_port"] = new_port
        cfg["autostart"] = self.var_autostart.get()

        # Lưu file cấu hình
        self.cfg_manager.save_config(cfg)

        # Cài đặt Windows Auto-start Registry
        self.cfg_manager.set_windows_autostart(cfg["autostart"])

        if self.on_save_callback:
            try:
                self.on_save_callback(cfg)
            except Exception:
                pass

        messagebox.showinfo("Thành công", "Đã lưu cài đặt và áp dụng thay đổi thành công!")
        if self.root:
            self.root.destroy()
