#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - Modern Settings Dialog (Tkinter)
Cửa sổ cấu hình thân thiện, hỗ trợ kiểm tra kết nối trạm ESP32,
quản lý dịch vụ AI Gateway 2.0 (Port 8765), Transcoder (Port 5050), PC HUD và tự khởi động cùng Windows.
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
ACCENT_YELLOW = "#f9e2af"
BORDER_COLOR = "#45475a"

VOICE_OPTIONS = [
    ("vi-VN-HoaiMyNeural (Nữ - Truyền cảm)", "vi-VN-HoaiMyNeural"),
    ("vi-VN-NamMinhNeural (Nam - Ấm áp)", "vi-VN-NamMinhNeural"),
    ("vi-google (Google Translate TTS)", "vi-google")
]

class SettingsDialog:
    def __init__(self, cfg_manager, on_save_callback=None):
        self.cfg_manager = cfg_manager
        self.on_save_callback = on_save_callback
        self.root = None
        self._build_ui()

    def _close_dialog(self):
        """Ẩn cửa sổ cài đặt về chế độ nền mà không hủy Tcl root."""
        if self.root:
            try:
                self.root.withdraw()
            except Exception:
                pass

    def close_completely(self):
        """Hủy hoàn toàn Tkinter root khi người dùng chọn Thoát ứng dụng từ khay hệ thống."""
        if self.root:
            try:
                self.root.after(0, lambda: (self.root.quit(), self.root.destroy()))
            except Exception:
                pass

    def show(self):
        """Mở lại bảng cài đặt từ bất kỳ thread nào bằng Tkinter after."""
        if self.root:
            self.root.after(0, self._show_ui)

    def _show_ui(self):
        if not self.root:
            return
        try:
            cfg = self.cfg_manager.load_config()
            self.ip_entry.delete(0, "end")
            self.ip_entry.insert(0, cfg.get("esp32_ip", "192.168.1.82"))
            self.interval_entry.delete(0, "end")
            self.interval_entry.insert(0, str(cfg.get("monitor_interval", 1.2)))
            self.port_entry.delete(0, "end")
            self.port_entry.insert(0, str(cfg.get("transcoder_port", 5050)))
            self.ai_port_entry.delete(0, "end")
            self.ai_port_entry.insert(0, str(cfg.get("ai_gateway_port", 8765)))
            self.gemini_entry.delete(0, "end")
            self.gemini_entry.insert(0, cfg.get("gemini_api_key", ""))
            if hasattr(self, "gemini_model_combo"):
                self.gemini_model_combo.set(cfg.get("gemini_model") or cfg.get("openai_model") or "gemini-flash-lite-latest")
            self.deepseek_entry.delete(0, "end")
            self.deepseek_entry.insert(0, cfg.get("openai_api_key", ""))

            self.var_monitor.set(cfg.get("monitor_enabled", True))
            self.var_transcoder.set(cfg.get("transcoder_enabled", True))
            self.var_ai_enabled.set(cfg.get("ai_gateway_enabled", True))
            self.var_autostart.set(cfg.get("autostart", True))

            current_voice = cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
            default_label = self.voice_map.get(current_voice, VOICE_OPTIONS[0][0])
            self.voice_combo.set(default_label)

            self.root.deiconify()
            self.root.lift()
            self.root.focus_force()
        except Exception as e:
            print(f"Error displaying settings: {e}")

    def run(self):
        """Chạy event loop chính của Tkinter trên Main Thread."""
        if self.root:
            self.root.mainloop()

    def _build_ui(self):
        cfg = self.cfg_manager.load_config()
        self.root = tk.Tk()
        self.root.title("Cài Đặt - Decor Space Companion")
        self.root.geometry("520x700")
        self.root.resizable(False, True)
        self.root.configure(bg=BG_DARK)

        # Đặt cửa sổ ở giữa màn hình
        self.root.update_idletasks()
        w = self.root.winfo_width()
        h = self.root.winfo_height()
        ws = self.root.winfo_screenwidth()
        hs = self.root.winfo_screenheight()
        x = (ws // 2) - (w // 2)
        y = max(20, (hs // 2) - (h // 2) - 30)
        self.root.geometry(f"{w}x{h}+{x}+{y}")

        # Header
        header_frame = tk.Frame(self.root, bg=BG_DARK)
        header_frame.pack(fill="x", padx=20, pady=(16, 8))

        title_lbl = tk.Label(
            header_frame,
            text="🛰️ Trạm Decor Vũ Trụ",
            font=("Segoe UI", 15, "bold"),
            bg=BG_DARK,
            fg=TEXT_MAIN
        )
        title_lbl.pack(anchor="w")

        sub_lbl = tk.Label(
            header_frame,
            text="Cấu hình Trạm ESP32, AI Gateway Stream, Video Transcoder & PC HUD",
            font=("Segoe UI", 9),
            bg=BG_DARK,
            fg=TEXT_MUTED
        )
        sub_lbl.pack(anchor="w")

        # Khung cuộn Canvas để chứa toàn bộ cards
        main_canvas_frame = tk.Frame(self.root, bg=BG_DARK)
        main_canvas_frame.pack(fill="both", expand=True, padx=10, pady=5)

        canvas = tk.Canvas(main_canvas_frame, bg=BG_DARK, highlightthickness=0)
        scrollbar = ttk.Scrollbar(main_canvas_frame, orient="vertical", command=canvas.yview)
        scrollable_frame = tk.Frame(canvas, bg=BG_DARK)

        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )

        canvas_window = canvas.create_window((0, 0), window=scrollable_frame, anchor="nw", width=480)
        canvas.configure(yscrollcommand=scrollbar.set)

        def _on_mousewheel(event):
            canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

        canvas.bind_all("<MouseWheel>", _on_mousewheel)

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        container = scrollable_frame

        # =========================================================================
        # --- Khối 1: Kết nối Trạm ESP32 ---
        # =========================================================================
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
        esp_card.pack(fill="x", padx=10, pady=(0, 10))

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
        self.ip_entry.insert(0, cfg.get("esp32_ip", "192.168.1.82"))
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
            text="Nhấn 'Kiểm tra' để thử kết nối tới trạm ESP32.",
            font=("Segoe UI", 8, "italic"),
            bg=BG_CARD,
            fg=TEXT_MUTED
        )
        self.status_lbl.pack(anchor="w", pady=(4, 0))

        # =========================================================================
        # --- Khối 2: AI Gateway & Giọng Nói Trợ Lý (Mới 2.0) ---
        # =========================================================================
        ai_card = tk.LabelFrame(
            container,
            text=" 🎙️ Trợ Lý AI & Audio Streaming (Port 8765) ",
            font=("Segoe UI", 10, "bold"),
            bg=BG_CARD,
            fg=ACCENT_BLUE,
            padx=12,
            pady=10,
            relief="solid",
            bd=1
        )
        ai_card.pack(fill="x", padx=10, pady=(0, 10))

        self.var_ai_enabled = tk.BooleanVar(value=cfg.get("ai_gateway_enabled", True))
        chk_ai = tk.Checkbutton(
            ai_card,
            text="Bật Máy Chủ AI Gateway (Tự động nhận mic & phát loa)",
            variable=self.var_ai_enabled,
            font=("Segoe UI", 9, "bold"),
            bg=BG_CARD,
            fg=TEXT_MAIN,
            selectcolor=BG_INPUT,
            activebackground=BG_CARD,
            activeforeground=TEXT_MAIN
        )
        chk_ai.pack(anchor="w", pady=2)

        # Cổng AI Gateway & Giọng đọc
        row_voice = tk.Frame(ai_card, bg=BG_CARD)
        row_voice.pack(fill="x", pady=4)
        tk.Label(row_voice, text="Giọng đọc TTS:", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN).pack(side="left")

        cur_voice = cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
        self.voice_var = tk.StringVar(value=cur_voice)
        voice_labels = [opt[0] for opt in VOICE_OPTIONS]
        self.voice_map = {opt[1]: opt[0] for opt in VOICE_OPTIONS}
        rev_voice_map = {opt[0]: opt[1] for opt in VOICE_OPTIONS}
        self.rev_voice_map = rev_voice_map

        init_label = self.voice_map.get(cur_voice, voice_labels[0])
        self.voice_combo = ttk.Combobox(row_voice, values=voice_labels, state="readonly", width=26)
        self.voice_combo.set(init_label)
        self.voice_combo.pack(side="left", padx=(8, 0))

        # Test phát âm thanh
        row_test_ai = tk.Frame(ai_card, bg=BG_CARD)
        row_test_ai.pack(fill="x", pady=4)
        self.btn_test_speak = tk.Button(
            row_test_ai,
            text="🔊 Thử Giọng Đọc Qua Loa Trạm",
            font=("Segoe UI", 8, "bold"),
            bg=ACCENT_BLUE,
            fg="#11111b",
            relief="flat",
            padx=10,
            pady=3,
            cursor="hand2",
            command=self._test_speak
        )
        self.btn_test_speak.pack(side="left")

        self.lbl_speak_status = tk.Label(
            row_test_ai,
            text="",
            font=("Segoe UI", 8, "italic"),
            bg=BG_CARD,
            fg=TEXT_MUTED
        )
        self.lbl_speak_status.pack(side="left", padx=8)

        # API Keys
        lbl_ai_note = tk.Label(
            ai_card,
            text="Khóa API Trí Tuệ Nhân Tạo (Tùy chọn cho hỏi đáp thông minh):",
            font=("Segoe UI", 8, "bold"),
            bg=BG_CARD,
            fg=ACCENT_YELLOW
        )
        lbl_ai_note.pack(anchor="w", pady=(6, 2))

        # Gemini API Key
        row_gemini = tk.Frame(ai_card, bg=BG_CARD)
        row_gemini.pack(fill="x", pady=2)
        tk.Label(row_gemini, text="Gemini API Key:", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN, width=15, anchor="w").pack(side="left")
        self.gemini_entry = tk.Entry(
            row_gemini,
            font=("Segoe UI", 9),
            bg=BG_INPUT,
            fg=TEXT_MAIN,
            insertbackground=TEXT_MAIN,
            relief="flat",
            show="*",
            width=28
        )
        self.gemini_entry.insert(0, cfg.get("gemini_api_key", ""))
        self.gemini_entry.pack(side="left", fill="x", expand=True)

        self.btn_check_gemini = tk.Button(
            row_gemini,
            text="🔍 Kiểm Tra",
            font=("Segoe UI", 8, "bold"),
            bg="#0284c7",
            fg="#ffffff",
            activebackground="#0369a1",
            activeforeground="#ffffff",
            relief="flat",
            padx=8,
            pady=2,
            cursor="hand2",
            command=self.check_gemini_models
        )
        self.btn_check_gemini.pack(side="left", padx=(6, 0))

        # Model Gemini Dropdown
        row_gemini_model = tk.Frame(ai_card, bg=BG_CARD)
        row_gemini_model.pack(fill="x", pady=2)
        tk.Label(row_gemini_model, text="Mô hình AI:", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN, width=15, anchor="w").pack(side="left")
        self.gemini_model_combo = ttk.Combobox(
            row_gemini_model,
            values=[
                "gemini-flash-lite-latest",
                "gemini-3-flash-preview",
                "gemini-flash-latest",
                "gemini-pro-latest"
            ],
            state="normal",
            font=("Segoe UI", 9)
        )
        self.gemini_model_combo.set(cfg.get("gemini_model") or cfg.get("openai_model") or "gemini-flash-lite-latest")
        self.gemini_model_combo.pack(side="left", fill="x", expand=True)

        # DeepSeek API Key
        row_deepseek = tk.Frame(ai_card, bg=BG_CARD)
        row_deepseek.pack(fill="x", pady=2)
        tk.Label(row_deepseek, text="DeepSeek Key:", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN, width=15, anchor="w").pack(side="left")
        self.deepseek_entry = tk.Entry(
            row_deepseek,
            font=("Segoe UI", 9),
            bg=BG_INPUT,
            fg=TEXT_MAIN,
            insertbackground=TEXT_MAIN,
            relief="flat",
            show="*",
            width=28
        )
        self.deepseek_entry.insert(0, cfg.get("openai_api_key", ""))
        self.deepseek_entry.pack(side="left", fill="x", expand=True)

        # =========================================================================
        # --- Khối 3: Tùy Chọn & Tự Động Hóa ---
        # =========================================================================
        auto_card = tk.LabelFrame(
            container,
            text=" ⚙️ Dịch Vụ Khác & Khởi Động ",
            font=("Segoe UI", 10, "bold"),
            bg=BG_CARD,
            fg=ACCENT_BLUE,
            padx=12,
            pady=10,
            relief="solid",
            bd=1
        )
        auto_card.pack(fill="x", padx=10, pady=(0, 10))

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
            text="Bật máy chủ chuyển mã Video (Transcoder Port 5050)",
            variable=self.var_transcoder,
            font=("Segoe UI", 9),
            bg=BG_CARD,
            fg=TEXT_MAIN,
            selectcolor=BG_INPUT,
            activebackground=BG_CARD,
            activeforeground=TEXT_MAIN
        )
        chk_transcoder.pack(anchor="w", pady=2)

        # =========================================================================
        # --- Khối 4: Cổng & Tần Số Nâng Cao ---
        # =========================================================================
        adv_card = tk.LabelFrame(
            container,
            text=" ⏱️ Cổng Mạng Nâng Cao ",
            font=("Segoe UI", 10, "bold"),
            bg=BG_CARD,
            fg=ACCENT_BLUE,
            padx=12,
            pady=8,
            relief="solid",
            bd=1
        )
        adv_card.pack(fill="x", padx=10, pady=(0, 10))

        row_int = tk.Frame(adv_card, bg=BG_CARD)
        row_int.pack(fill="x", pady=2)
        tk.Label(row_int, text="Chu kỳ gửi HUD (giây):", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN).pack(side="left")
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

        row_ai_port = tk.Frame(adv_card, bg=BG_CARD)
        row_ai_port.pack(fill="x", pady=2)
        tk.Label(row_ai_port, text="Cổng AI Gateway (Mặc định 8765):", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN).pack(side="left")
        self.ai_port_entry = tk.Entry(
            row_ai_port,
            font=("Segoe UI", 9),
            bg=BG_INPUT,
            fg=TEXT_MAIN,
            insertbackground=TEXT_MAIN,
            relief="flat",
            width=6
        )
        self.ai_port_entry.insert(0, str(cfg.get("ai_gateway_port", 8765)))
        self.ai_port_entry.pack(side="right")

        row_port = tk.Frame(adv_card, bg=BG_CARD)
        row_port.pack(fill="x", pady=2)
        tk.Label(row_port, text="Cổng Web Transcoder (Mặc định 5050):", font=("Segoe UI", 9), bg=BG_CARD, fg=TEXT_MAIN).pack(side="left")
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

        # Nút Lưu và Hủy ở đáy
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
            command=self._close_dialog
        )
        btn_cancel.pack(side="right")

        self.root.protocol("WM_DELETE_WINDOW", self._close_dialog)
        self.root.withdraw()

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
                res = requests.get(f"http://{ip}/api/status", timeout=2.0)
                ms = int((time.time() - start_t) * 1000)
                if res.status_code == 200:
                    msg = f"🟢 Kết nối thành công! Phản hồi: {ms}ms"
                    color = ACCENT_GREEN
                else:
                    msg = f"🟡 Trạm phản hồi mã HTTP {res.status_code} ({ms}ms)"
                    color = ACCENT_YELLOW
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

    def _test_speak(self):
        port = self.ai_port_entry.get().strip() or "8765"
        selected_label = self.voice_combo.get()
        voice_code = self.rev_voice_map.get(selected_label, "vi-VN-HoaiMyNeural")
        test_text = "Xin chào! Trợ lý ảo Decor Space Companion đã sẵn sàng phục vụ."

        self.lbl_speak_status.config(text="Đang gửi lệnh...", fg=TEXT_MUTED)
        self.btn_test_speak.config(state="disabled")

        def speak_task():
            try:
                url = f"http://127.0.0.1:{port}/api/speak"
                res = requests.post(
                    url,
                    json={"text": test_text, "voice": voice_code},
                    timeout=5.0
                )
                if res.status_code == 200:
                    res_json = res.json()
                    c_count = res_json.get("active_clients", 0)
                    if c_count > 0:
                        msg = f"🟢 Đang phát loa ({c_count} trạm kết nối)!"
                        color = ACCENT_GREEN
                    else:
                        msg = "🟡 AI đã tổng hợp, nhưng chưa có Trạm kết nối WebSocket."
                        color = ACCENT_YELLOW
                else:
                    msg = f"🔴 Gateway phản hồi mã: {res.status_code}"
                    color = ACCENT_RED
            except Exception as e:
                msg = f"🔴 AI Gateway chưa chạy: {e}"
                color = ACCENT_RED

            def update_ui():
                if self.root and self.root.winfo_exists():
                    self.lbl_speak_status.config(text=msg, fg=color)
                    self.btn_test_speak.config(state="normal")

            if self.root and self.root.winfo_exists():
                self.root.after(0, update_ui)

        threading.Thread(target=speak_task, daemon=True).start()

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

        try:
            new_ai_port = int(self.ai_port_entry.get().strip())
        except ValueError:
            new_ai_port = 8765

        selected_label = self.voice_combo.get()
        voice_code = self.rev_voice_map.get(selected_label, "vi-VN-HoaiMyNeural")

        cfg = self.cfg_manager.load_config()
        cfg["esp32_ip"] = new_ip
        cfg["monitor_enabled"] = self.var_monitor.get()
        cfg["monitor_interval"] = max(0.5, new_interval)
        cfg["transcoder_enabled"] = self.var_transcoder.get()
        cfg["transcoder_port"] = new_port
        cfg["ai_gateway_enabled"] = self.var_ai_enabled.get()
        cfg["ai_gateway_port"] = new_ai_port
        cfg["ai_gateway_voice"] = voice_code
        cfg["gemini_api_key"] = self.gemini_entry.get().strip()
        if hasattr(self, "gemini_model_combo"):
            sel_model = self.gemini_model_combo.get().strip() or "gemini-flash-lite-latest"
            cfg["gemini_model"] = sel_model
            cfg["openai_model"] = sel_model
        cfg["openai_api_key"] = self.deepseek_entry.get().strip()
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
        self._close_dialog()

    def check_gemini_models(self):
        """Kiểm tra API Key và lấy danh sách model Gemini khả dụng."""
        key = self.gemini_entry.get().strip()
        if not key:
            messagebox.showwarning("Thông báo", "Vui lòng nhập hoặc dán Gemini API Key trước!")
            return

        self.btn_check_gemini.config(text="⏳ Đang test...", state="disabled")

        def _worker():
            try:
                from tools.companion.ai_gateway_worker import fetch_and_test_gemini_models
            except ImportError:
                from ai_gateway_worker import fetch_and_test_gemini_models

            res = fetch_and_test_gemini_models(key)

            def _update_ui():
                self.btn_check_gemini.config(text="🔍 Kiểm Tra", state="normal")
                if res.get("status") == "ok":
                    working = res.get("working_models", [])
                    all_m = res.get("all_models", [])
                    rec = res.get("recommended", "gemini-flash-lite-latest")
                    combo_list = list(working)
                    for m in all_m:
                        if m not in combo_list:
                            combo_list.append(m)
                    self.gemini_model_combo["values"] = combo_list
                    self.gemini_model_combo.set(rec)
                    messagebox.showinfo(
                        "Kiểm Tra Thành Công",
                        f"Đã xác thực API Key thành công!\n\n"
                        f"• Model đang hoạt động tốt: {', '.join(working) if working else 'Đang kiểm tra'}\n"
                        f"• Đã tự động chọn model tối ưu: {rec}\n"
                        f"• Tổng số model được cấp phép: {len(all_m)}"
                    )
                else:
                    messagebox.showerror("Lỗi API Key", f"Không thể lấy danh sách model:\n{res.get('message', 'Lỗi không xác định')}")

            if self.root:
                self.root.after(0, _update_ui)

        threading.Thread(target=_worker, daemon=True).start()
