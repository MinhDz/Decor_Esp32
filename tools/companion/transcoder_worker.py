#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - Transcoder Background Worker
Chạy máy chủ Web Transcoder (Dashboard & Realtime Video Streamer) trên nền tảng Werkzeug/Flask.
"""

import os
import sys
import time
import logging
import threading
from werkzeug.serving import make_server

logger = logging.getLogger("TranscoderWorker")

# Hỗ trợ tìm kiếm module transcoder khi chạy script hoặc chạy từ PyInstaller bundle
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
candidate_dirs = [
    os.path.abspath(os.path.join(SCRIPT_DIR, "..", "transcoder")),
    SCRIPT_DIR
]

if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
    candidate_dirs.insert(0, os.path.join(sys._MEIPASS, "transcoder"))
    candidate_dirs.insert(0, sys._MEIPASS)

for p in candidate_dirs:
    if os.path.exists(p) and p not in sys.path:
        sys.path.insert(0, p)

try:
    import transcoder
    HAS_TRANSCODER = True
except Exception as e:
    logger.error(f"Không thể import module transcoder: {e}")
    HAS_TRANSCODER = False

class TranscoderWorker:
    def __init__(self, port=5050, esp_ip="192.168.1.233", enabled=True):
        self.port = int(port)
        self.esp_ip = str(esp_ip).strip()
        self.enabled = enabled
        self.server = None
        self.thread = None
        self._is_running = False
        self._lock = threading.Lock()

    def set_esp_ip(self, ip):
        with self._lock:
            self.esp_ip = str(ip).strip()
            if HAS_TRANSCODER and hasattr(transcoder, 'session'):
                transcoder.session.esp_ip = self.esp_ip
                logger.info(f"Đã cập nhật IP ESP32 trong Transcoder: {self.esp_ip}")

    def is_running(self):
        return self._is_running

    def start(self):
        with self._lock:
            if not HAS_TRANSCODER:
                logger.warning("Không có module transcoder, bỏ qua khởi động.")
                return False
            if self._is_running:
                return True

            try:
                # Cập nhật cấu hình ban đầu cho transcoder session
                if hasattr(transcoder, 'session'):
                    transcoder.session.esp_ip = self.esp_ip

                # Khởi tạo werkzeug make_server (hỗ trợ graceful shutdown)
                self.server = make_server("0.0.0.0", self.port, transcoder.app, threaded=True)
                self._is_running = True

                def run_loop():
                    logger.info(f"Máy chủ Transcoder đang lắng nghe tại http://0.0.0.0:{self.port}")
                    try:
                        self.server.serve_forever()
                    except Exception as err:
                        logger.error(f"Transcoder server exception: {err}")
                    finally:
                        self._is_running = False
                        logger.info("Máy chủ Transcoder đã dừng.")

                self.thread = threading.Thread(target=run_loop, name="TranscoderServerThread", daemon=True)
                self.thread.start()
                return True
            except Exception as e:
                logger.error(f"Lỗi khởi động máy chủ Transcoder tại port {self.port}: {e}")
                self._is_running = False
                return False

    def stop(self):
        with self._lock:
            if not self._is_running:
                return
            logger.info("Đang dừng máy chủ Transcoder...")
            try:
                if HAS_TRANSCODER and hasattr(transcoder, 'session'):
                    transcoder.session.stop()
            except Exception:
                pass

            if self.server:
                try:
                    self.server.shutdown()
                except Exception as e:
                    logger.warning(f"Lỗi shutdown server: {e}")
                self.server = None

            self._is_running = False

    def restart(self, port=None, esp_ip=None):
        self.stop()
        time.sleep(0.5)
        if port:
            self.port = int(port)
        if esp_ip:
            self.esp_ip = str(esp_ip).strip()
        return self.start()
