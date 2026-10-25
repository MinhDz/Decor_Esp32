#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Chuyển Mã Đa Phương Tiện (PC Transcoder) cho ESP32-S3 Trạm Decor Vũ Trụ
----------------------------------------------------------------------------
Chức năng:
  - Nhận link YouTube, TikTok, Facebook, Twitch hoặc video web / tệp cục bộ.
  - Tự động phân tích stream qua yt-dlp (hỗ trợ JS runtime Node.js).
  - Sử dụng FFmpeg chuyển mã tức thời (Realtime Transcode):
      + Video: MJPEG 240x240 hoặc 240x320 (Chất lượng 70, 15 FPS)
      + Audio: PCM 16-bit Mono 16,000 Hz
  - Stream mượt mà qua mạng cục bộ (LAN) xuống ESP32-S3:
      + Đẩy audio chunk ưu tiên vào Ring Buffer PSRAM (Core 0 I2S MAX98357A)
      + Đẩy video frame vào Core 1 (ST7789 TJpg_Decoder)
  - Cung cấp giao diện Web Control Dashboard hiện đại tại http://localhost:5050
"""

import os
import sys
import time
import json
import socket
import logging
import threading
import subprocess
from pathlib import Path
from flask import Flask, request, jsonify, Response, render_template_string

# Đảm bảo Windows console in tiếng Việt UTF-8 không bị lỗi charmap
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
    except Exception:
        pass

# Cấu hình logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger("Transcoder")

# Đường dẫn thư mục và cấu hình
SCRIPT_DIR = Path(__file__).resolve().parent
CONFIG_FILE = SCRIPT_DIR / "transcoder_config.json"

def load_config():
    defaults = {
        "esp_ip": "192.168.4.1",
        "fps": 15,
        "quality": 5,           # FFmpeg q:v (2-31, 5 tương đương ~75% quality)
        "resolution": "240x240",# 240x240 hoặc 240x320
        "volume": 100,          # 0 - 100%
        "last_url": ""
    }
    if CONFIG_FILE.exists():
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
                defaults.update(data)
        except Exception as e:
            logger.warning(f"Lỗi đọc config: {e}")
    return defaults

def save_config(cfg):
    try:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=2, ensure_ascii=False)
    except Exception as e:
        logger.warning(f"Lỗi ghi config: {e}")

# Tìm kiếm binary FFmpeg và Node.js
def find_ffmpeg():
    # 1. Thử từ imageio_ffmpeg
    try:
        import imageio_ffmpeg
        exe = imageio_ffmpeg.get_ffmpeg_exe()
        if os.path.exists(exe):
            return exe
    except Exception:
        pass

    # 2. Thử đường dẫn mặc định đã tìm thấy trên máy
    known_path = r"C:\Users\admin\AppData\Roaming\Python\Python314\site-packages\imageio_ffmpeg\binaries\ffmpeg-win-x86_64-v7.1.exe"
    if os.path.exists(known_path):
        return known_path

    # 3. Thử lệnh hệ thống
    import shutil
    sys_exe = shutil.which("ffmpeg")
    if sys_exe:
        return sys_exe

    return "ffmpeg"

def find_node():
    default_node = r"C:\Program Files\nodejs\node.exe"
    if os.path.exists(default_node):
        return default_node
    import shutil
    sys_node = shutil.which("node")
    if sys_node:
        return sys_node
    return None

def find_yt_dlp():
    default_ytdlp = r"C:\Users\admin\AppData\Roaming\Python\Python314\Scripts\yt-dlp.exe"
    if os.path.exists(default_ytdlp):
        return default_ytdlp
    import shutil
    sys_ytdlp = shutil.which("yt-dlp")
    if sys_ytdlp:
        return sys_ytdlp
    return sys.executable + " -m yt_dlp"

FFMPEG_EXE = find_ffmpeg()
NODE_EXE = find_node()
YT_DLP_EXE = find_yt_dlp()

logger.info(f"FFmpeg binary: {FFMPEG_EXE}")
logger.info(f"Node.js binary: {NODE_EXE}")
logger.info(f"yt-dlp binary: {YT_DLP_EXE}")

# ============================================================================
#               LỚP QUẢN LÝ TRANSCODER & STREAM ENGINE
# ============================================================================
class StreamSession:
    def __init__(self):
        self.lock = threading.RLock()
        self.is_running = False
        self.is_paused = False
        self.current_url = ""
        self.video_title = ""
        self.thumbnail_url = ""
        self.duration = 0
        self.esp_ip = "192.168.1.233"
        self.fps = 15
        self.resolution = "240x240"
        self.volume = 100

        # Tiến trình FFmpeg
        self.proc_video = None
        self.proc_audio = None

        # Tiểu trình stream
        self.thread_video = None
        self.thread_audio = None

        # Thống kê
        self.video_frames_sent = 0
        self.audio_chunks_sent = 0
        self.audio_bytes_sent = 0
        self.audio_pts = 0.0
        self.video_frames_decoded = 0
        self.video_frames_dropped = 0
        self.start_time = 0
        self.last_frame_jpeg = None
        self.current_live_fps = 0.0
        self.frame_cond = threading.Condition()

    def parse_media_url(self, url):
        """Phân tích link YouTube hoặc web link để lấy stream direct URLs"""
        url = url.strip()
        if not url:
            raise ValueError("URL rỗng")

        # 1. Nếu là tệp cục bộ có thật trên máy tính
        if os.path.isfile(url):
            return {
                "title": os.path.basename(url),
                "is_local": True,
                "video_url": url,
                "audio_url": url,
                "thumbnail": "",
                "duration": 0
            }

        # 2. Sử dụng yt-dlp để trích xuất đồng thời Metadata và Direct Stream URLs trong 1 lượt gọi
        title = "Video Stream"
        thumbnail = ""
        duration = 0

        cmd_stream = [
            YT_DLP_EXE,
            "--no-playlist",
            "--no-check-certificates",
            "-f", "bestvideo[height<=480]+bestaudio/best[height<=480]/best",
            "--print", "%(title)s",
            "--print", "%(thumbnail)s",
            "--print", "%(duration)s",
            "-g",
            url
        ]
        if NODE_EXE:
            cmd_stream[1:1] = ["--js-runtimes", f"node:{NODE_EXE}"]

        logger.info(f"Đang phân tích & trích xuất direct media stream qua yt-dlp: {url}")
        res_stream = subprocess.run(cmd_stream, capture_output=True, text=True, timeout=25)
        if res_stream.returncode != 0:
            # Fallback nếu video không có DASH phân tách
            cmd_fallback = [YT_DLP_EXE, "--no-playlist", "-f", "best", "--print", "%(title)s", "-g", url]
            res_stream = subprocess.run(cmd_fallback, capture_output=True, text=True, timeout=20)
            if res_stream.returncode != 0:
                raise RuntimeError(f"yt-dlp không thể trích xuất stream: {res_stream.stderr[:200]}")

        lines = [l.strip() for l in res_stream.stdout.splitlines() if l.strip()]
        http_urls = [l for l in lines if l.startswith("http")]
        non_http = [l for l in lines if not l.startswith("http")]

        if len(non_http) >= 1:
            title = non_http[0]
        if len(non_http) >= 2 and non_http[1].isdigit():
            duration = int(non_http[1])

        media_urls = []
        for u in http_urls:
            if any(ext in u.lower() for ext in [".jpg", ".jpeg", ".webp", ".png"]):
                thumbnail = u
            else:
                media_urls.append(u)

        if not media_urls:
            media_urls = http_urls

        if len(media_urls) >= 2:
            v_url = media_urls[0]
            a_url = media_urls[1]
        elif len(media_urls) >= 1:
            v_url = media_urls[0]
            a_url = media_urls[0]
        else:
            raise RuntimeError("yt-dlp không tìm thấy direct media stream nào")

        logger.info(f"Đã trích xuất thành công: Video URL ({len(v_url)} ký tự), Audio URL ({len(a_url)} ký tự)")
        return {
            "title": title,
            "is_local": False,
            "video_url": v_url,
            "audio_url": a_url,
            "thumbnail": thumbnail,
            "duration": duration
        }

    def start(self, url, esp_ip, fps=15, resolution="240x240", volume=100):
        with self.lock:
            self.stop_locked()

            self.esp_ip = esp_ip
            self.fps = fps
            self.resolution = resolution
            self.volume = max(0, min(100, volume))
            self.current_url = url

            # 1. Phân tích link
            media_info = self.parse_media_url(url)
            self.video_title = media_info["title"]
            self.thumbnail_url = media_info["thumbnail"]
            self.duration = media_info["duration"]

            v_url = media_info["video_url"]
            a_url = media_info["audio_url"]

            logger.info(f"Bắt đầu Transcode & Stream: \"{self.video_title}\" xuống ESP32 ({self.esp_ip})")

            # 2. Xây dựng Video Filter cho FFmpeg
            if resolution == "240x320":
                # Scale vừa chiều ngang 240, chiều dọc tối đa 320, lót đen phần thừa
                vf = "scale=240:320:force_original_aspect_ratio=decrease,pad=240:320:(ow-iw)/2:(oh-ih)/2:black,format=yuvj420p"
            elif resolution == "crop240":
                # Crop tràn khung 240x240
                vf = "scale=240:240:force_original_aspect_ratio=increase,crop=240:240,format=yuvj420p"
            else: # Mặc định 240x240 chuẩn vuông
                vf = "scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:black,format=yuvj420p"

            # 3. Khởi tạo tiến trình FFmpeg Video (MJPEG @ target FPS, kèm reconnect và User-Agent tránh bị YouTube ngắt)
            v_cmd = [
                FFMPEG_EXE, "-hide_banner", "-loglevel", "warning",
                "-reconnect", "1", "-reconnect_at_eof", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "5",
                "-user_agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
                "-re", "-i", v_url,
                "-an",
                "-vf", vf,
                "-r", str(fps),
                "-q:v", "5",
                "-f", "image2pipe", "-vcodec", "mjpeg", "pipe:1"
            ]

            # 4. Khởi tạo tiến trình FFmpeg Audio (PCM 16-bit Mono 16000Hz)
            a_cmd = [
                FFMPEG_EXE, "-hide_banner", "-loglevel", "warning",
                "-reconnect", "1", "-reconnect_at_eof", "1", "-reconnect_streamed", "1", "-reconnect_delay_max", "5",
                "-user_agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
                "-re", "-i", a_url,
                "-vn",
                "-acodec", "pcm_s16le",
                "-ar", "16000",
                "-ac", "1",
                "-f", "s16le", "pipe:1"
            ]

            self.proc_video = subprocess.Popen(v_cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=65536)
            self.proc_audio = subprocess.Popen(a_cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=32768)

            self.is_running = True
            self.is_paused = False
            self.video_frames_sent = 0
            self.audio_chunks_sent = 0
            self.audio_bytes_sent = 0
            self.audio_pts = 0.0
            self.video_frames_decoded = 0
            self.video_frames_dropped = 0
            self.start_time = time.time()

            # 5. Kích hoạt chế độ Stream trên ESP32
            self._notify_esp_stream_mode()

            # 6. Khởi động 2 Worker Threads
            self.thread_audio = threading.Thread(target=self._audio_worker, daemon=True)
            self.thread_video = threading.Thread(target=self._video_worker, daemon=True)
            self.thread_audio.start()
            self.thread_video.start()

            return {
                "status": "ok",
                "title": self.video_title,
                "fps": self.fps,
                "resolution": self.resolution,
                "esp_ip": self.esp_ip
            }

    def _notify_esp_stream_mode(self):
        """Báo cho ESP32 chuyển màn hình sang chế độ Stream Mode (Mode 17)"""
        try:
            import requests
            requests.post(
                f"http://{self.esp_ip}/api/screen/state",
                json={"mode": "stream"},
                timeout=1.0
            )
        except Exception:
            pass

    def _audio_worker(self):
        """Đọc raw PCM 16kHz từ FFmpeg và đẩy trực tiếp vào Ring Buffer PSRAM của ESP32 qua UDP Port 12345"""
        import socket
        
        udp_port = 12345
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 32768)
        except Exception:
            pass

        # Kích thước chunk: 512 mẫu 16-bit = 1024 bytes (~32ms âm thanh @ 16kHz)
        CHUNK_BYTES = 1024
        start_time = self.start_time

        logger.info(f"Khởi chạy luồng Audio Worker UDP (PCM 16kHz Mono -> {self.esp_ip}:{udp_port})...")
        while self.is_running:
            if self.is_paused:
                time.sleep(0.05)
                continue

            try:
                raw_chunk = self.proc_audio.stdout.read(CHUNK_BYTES)
                if not raw_chunk:
                    logger.info("Luồng Audio hoàn tất (EOF)")
                    break

                # Áp dụng âm lượng phần mềm nếu != 100%
                if self.volume != 100:
                    import array
                    samples = array.array('h', raw_chunk)
                    vol_scale = self.volume / 100.0
                    for i in range(len(samples)):
                        samples[i] = int(samples[i] * vol_scale)
                    raw_chunk = samples.tobytes()

                # Bắn trực tiếp qua UDP Port 12345 (latency < 0.2ms)
                udp_sock.sendto(raw_chunk, (self.esp_ip, udp_port))
                self.audio_chunks_sent += 1
                self.audio_bytes_sent += len(raw_chunk)

                # Cập nhật Audio PTS (Master Clock - Thời điểm âm thanh thực tế phát trên loa)
                self.audio_pts = self.audio_bytes_sent / 32000.0

                # Giữ nhịp âm thanh khớp chuẩn xác thời gian thực (32ms/chunk)
                now = time.time()
                elapsed = now - start_time
                if self.audio_pts > elapsed:
                    time.sleep(self.audio_pts - elapsed)

            except Exception as e:
                logger.error(f"Lỗi trong Audio Worker UDP: {e}")
                break

        try:
            udp_sock.close()
        except Exception:
            pass
        logger.info("Kết thúc luồng Audio Worker UDP")

    def _video_worker(self):
        """Bóc tách từng frame JPEG từ FFmpeg pipe và đẩy lên màn hình ST7789 của ESP32 với Audio Master-Clock Sync"""
        import requests
        import struct
        import socket

        session = requests.Session()
        target_url = f"http://{self.esp_ip}/api/stream/video_frame"

        # Thử mở kết nối TCP Video tốc độ cao tới ESP32 (Port 12346)
        tcp_video_sock = None
        try:
            tcp_video_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tcp_video_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            tcp_video_sock.settimeout(2.0)
            tcp_video_sock.connect((self.esp_ip, 12346))
            logger.info(f"⚡ Đã kết nối luồng Video TCP tốc độ cao (Port 12346): {self.esp_ip}")
        except Exception as e:
            logger.warning(f"Chưa kết nối được TCP 12346 ({e}), dùng HTTP POST fallback.")
            tcp_video_sock = None

        raw_stream = bytearray()
        frame_idx = 0
        target_fps = float(self.fps)
        start_time = self.start_time
        fps_frame_count = 0
        fps_timer = time.time()

        logger.info(f"Khởi chạy luồng Video Worker (MJPEG {self.resolution} @ {self.fps} FPS, Master-Clock Synced)...")

        while self.is_running:
            if self.is_paused:
                time.sleep(0.05)
                continue

            try:
                chunk = self.proc_video.stdout.read(8192)
                if not chunk:
                    logger.info("Luồng Video hoàn tất (EOF)")
                    break
                raw_stream.extend(chunk)

                # Bóc tách khung hình JPEG hoàn chỉnh (bắt đầu bằng \xff\xd8, kết thúc \xff\xd9)
                while True:
                    soi = raw_stream.find(b'\xff\xd8')
                    if soi == -1:
                        if len(raw_stream) > 65536:
                            raw_stream.clear()
                        break
                    eoi = raw_stream.find(b'\xff\xd9', soi + 2)
                    if eoi == -1:
                        break

                    frame_bytes = bytes(raw_stream[soi : eoi + 2])
                    del raw_stream[: eoi + 2]

                    video_pts = frame_idx / target_fps
                    frame_idx += 1

                    with self.frame_cond:
                        self.last_frame_jpeg = frame_bytes
                        self.frame_cond.notify_all()

                    # ---------------- ACTIVE AUDIO-MASTER CLOCK SYNC ----------------
                    now = time.time()
                    master_clock = self.audio_pts if self.audio_pts > 0 else (now - start_time)
                    drift = video_pts - master_clock

                    # Trường hợp 1: Video bị trễ hơn âm thanh > 50ms (Frame cũ)
                    # Bỏ qua ngay lập tức trong 0.01ms để vòng lặp đọc nhanh đuổi kịp âm thanh!
                    if drift < -0.05:
                        self.video_frames_dropped += 1
                        continue

                    # Trường hợp 2: Video chạy nhanh hơn âm thanh > 20ms -> Chờ nhịp âm thanh
                    if drift > 0.02:
                        time.sleep(drift)

                    # Trường hợp 3: Khớp nhịp chuẩn -> Gửi frame xuống ESP32
                    sent_ok = False
                    if tcp_video_sock:
                        try:
                            payload = struct.pack("!I", len(frame_bytes)) + frame_bytes
                            tcp_video_sock.sendall(payload)
                            sent_ok = True
                        except Exception as err:
                            logger.warning(f"Lỗi socket TCP Video ({err}), chuyển sang HTTP POST")
                            try:
                                tcp_video_sock.close()
                            except Exception:
                                pass
                            tcp_video_sock = None

                    if not sent_ok:
                        try:
                            files = {'f': ('frame.jpg', frame_bytes, 'image/jpeg')}
                            resp = session.post(target_url, files=files, timeout=1.0)
                            if resp.status_code == 200:
                                sent_ok = True
                        except Exception:
                            pass

                    if sent_ok:
                        self.video_frames_sent += 1
                        fps_frame_count += 1

                    # Tính toán FPS thực tế hiển thị
                    if time.time() - fps_timer >= 1.0:
                        self.current_live_fps = round(fps_frame_count / (time.time() - fps_timer), 1)
                        fps_frame_count = 0
                        fps_timer = time.time()

            except Exception as e:
                logger.error(f"Lỗi trong Video Worker: {e}")
                break

        if tcp_video_sock:
            try:
                tcp_video_sock.close()
            except Exception:
                pass

        logger.info("Kết thúc luồng Video Worker")
        if self.is_running:
            self.stop()

    def pause(self):
        with self.lock:
            self.is_paused = not self.is_paused
            return {"status": "ok", "paused": self.is_paused}

    def set_volume(self, vol):
        self.volume = max(0, min(100, int(vol)))
        return {"status": "ok", "volume": self.volume}

    def stop_locked(self):
        self.is_running = False
        self.is_paused = False
        with self.frame_cond:
            self.last_frame_jpeg = None
            self.frame_cond.notify_all()

        if self.proc_video:
            try:
                self.proc_video.kill()
            except Exception:
                pass
            self.proc_video = None

        if self.proc_audio:
            try:
                self.proc_audio.kill()
            except Exception:
                pass
            self.proc_audio = None

        # Chờ các worker kết thúc gửi gói mạng đang dở trước khi gửi tín hiệu STOP
        time.sleep(0.08)

        # Báo cho ESP32 dừng stream
        try:
            import requests
            target = self.esp_ip if (self.esp_ip and self.esp_ip != "127.0.0.1") else "192.168.1.233"
            requests.post(f"http://{target}/api/stream/stop", timeout=1.0)
        except Exception:
            pass

    def stop(self):
        with self.lock:
            self.stop_locked()
            logger.info("Đã dừng toàn bộ luồng Transcoder.")
            return {"status": "ok", "stopped": True}

    def get_status(self):
        elapsed = int(time.time() - self.start_time) if (self.is_running and self.start_time > 0) else 0
        mins = elapsed // 60
        secs = elapsed % 60
        return {
            "running": self.is_running,
            "paused": self.is_paused,
            "title": self.video_title,
            "url": self.current_url,
            "esp_ip": self.esp_ip,
            "fps": self.fps,
            "live_fps": self.current_live_fps,
            "resolution": self.resolution,
            "volume": self.volume,
            "frames_sent": self.video_frames_sent,
            "video_dropped": self.video_frames_dropped,
            "audio_pts": round(self.audio_pts, 2),
            "video_pts": round(self.video_frames_sent / float(self.fps), 2) if self.fps > 0 else 0,
            "audio_kb_sent": round(self.audio_bytes_sent / 1024, 1),
            "elapsed_str": f"{mins:02d}:{secs:02d}",
            "has_preview": self.last_frame_jpeg is not None
        }

# Khởi tạo phiên làm việc duy nhất
session = StreamSession()

# ============================================================================
#                      GIAO DIỆN WEB CONTROL DASHBOARD
# ============================================================================
app = Flask(__name__)

HTML_DASHBOARD = """
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Trạm Chuyển Mã Transcoder // ESP32-S3 Decor</title>
  <style>
    :root {
      --bg: #090d16;
      --card: #131b2e;
      --card-border: #1e293b;
      --primary: #06b6d4;
      --accent: #10b981;
      --pink: #ec4899;
      --amber: #f59e0b;
      --text: #f8fafc;
      --subtext: #94a3b8;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }
    body { background-color: var(--bg); color: var(--text); padding: 20px; display: flex; justify-content: center; }
    .container { width: 100%; max-width: 860px; }
    header { display: flex; justify-content: space-between; align-items: center; padding-bottom: 16px; border-bottom: 1px solid var(--card-border); margin-bottom: 20px; }
    h1 { font-size: 1.35rem; color: var(--primary); display: flex; align-items: center; gap: 8px; }
    .badge { background: rgba(6,182,212,0.15); border: 1px solid var(--primary); color: var(--primary); font-size: 0.75rem; padding: 3px 8px; border-radius: 12px; }
    .card { background: var(--card); border: 1px solid var(--card-border); border-radius: 12px; padding: 18px; margin-bottom: 18px; box-shadow: 0 4px 20px rgba(0,0,0,0.3); }
    .card-title { font-size: 0.95rem; font-weight: 600; color: var(--primary); margin-bottom: 14px; display: flex; align-items: center; gap: 6px; }
    .form-group { margin-bottom: 14px; }
    label { display: block; font-size: 0.8rem; color: var(--subtext); margin-bottom: 6px; }
    input[type="text"], select { width: 100%; padding: 10px 12px; background: #0b1120; border: 1px solid var(--card-border); border-radius: 8px; color: #fff; font-size: 0.9rem; outline: none; transition: border-color 0.2s; }
    input[type="text"]:focus, select:focus { border-color: var(--primary); }
    .row { display: flex; gap: 12px; }
    .col { flex: 1; }
    .btn { background: var(--primary); color: #000; border: none; font-weight: 600; padding: 10px 16px; border-radius: 8px; cursor: pointer; display: inline-flex; align-items: center; justify-content: center; gap: 6px; transition: 0.2s; font-size: 0.88rem; }
    .btn:hover { filter: brightness(1.15); transform: translateY(-1px); }
    .btn-danger { background: #ef4444; color: #fff; }
    .btn-warning { background: var(--amber); color: #000; }
    .btn-outline { background: transparent; border: 1px solid var(--card-border); color: var(--text); }
    .btn-outline:hover { border-color: var(--primary); color: var(--primary); }
    .preview-box { display: flex; gap: 20px; align-items: center; }
    .screen-mock { width: 240px; height: 240px; background: #000; border: 3px solid #334155; border-radius: 10px; overflow: hidden; display: flex; align-items: center; justify-content: center; box-shadow: 0 0 15px rgba(6,182,212,0.2); position: relative; }
    .screen-mock img { width: 100%; height: 100%; object-fit: contain; }
    .screen-overlay { position: absolute; bottom: 4px; left: 4px; background: rgba(0,0,0,0.7); font-size: 0.68rem; padding: 2px 6px; border-radius: 4px; color: var(--primary); }
    .stats-grid { flex: 1; display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }
    .stat-card { background: #0b1120; padding: 10px 12px; border-radius: 8px; border: 1px solid rgba(255,255,255,0.04); }
    .stat-label { font-size: 0.7rem; color: var(--subtext); }
    .stat-val { font-size: 1.1rem; font-weight: 600; color: #fff; margin-top: 2px; }
    .quick-chips { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 8px; }
    .chip { background: #0b1120; border: 1px solid var(--card-border); padding: 5px 10px; border-radius: 6px; font-size: 0.75rem; color: var(--subtext); cursor: pointer; transition: 0.15s; }
    .chip:hover { border-color: var(--primary); color: #fff; }
    .live-dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; background: #64748b; margin-right: 6px; }
    .live-dot.active { background: var(--accent); box-shadow: 0 0 8px var(--accent); }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>📡 Trạm Chuyển Mã Transcoder <span class="badge">ESP32-S3 Decor</span></h1>
      <div id="live-indicator"><span class="live-dot" id="dot"></span><span id="dot-text">IDLE</span></div>
    </header>

    <!-- Thẻ nhập URL & Kết nối -->
    <div class="card">
      <div class="card-title">🔗 Nạp Link YouTube / Video Web</div>
      <div class="form-group">
        <label>Đường link Video (YouTube, TikTok, Facebook, MP4, HLS, hoặc đường dẫn tệp trên PC):</label>
        <input type="text" id="url" placeholder="https://www.youtube.com/watch?v=... hoặc C:/video.mp4">
        <div class="quick-chips">
          <span class="chip" onclick="setUrl('https://www.youtube.com/watch?v=jfKfPfyJRdk')">🎵 Lofi Girl Livestream</span>
          <span class="chip" onclick="setUrl('https://www.youtube.com/watch?v=5qap5aO4i9A')">🎶 Lofi Hip Hop Beats</span>
          <span class="chip" onclick="setUrl('https://www.youtube.com/watch?v=dQw4w9WgXcQ')">🕺 Never Gonna Give You Up</span>
          <span class="chip" onclick="setUrl('https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4')">🐰 Big Buck Bunny (MP4)</span>
        </div>
      </div>

      <div class="row">
        <div class="col">
          <label>Địa chỉ IP ESP32:</label>
          <input type="text" id="esp_ip" value="{{ cfg.esp_ip or '192.168.1.233' }}" placeholder="192.168.1.xxx hoặc 192.168.4.1">
        </div>
        <div class="col">
          <label>Độ phân giải & Tỉ lệ:</label>
          <select id="resolution">
            <option value="240x240" selected>240x240 (Vuông - Tối ưu nhất)</option>
            <option value="240x320">240x320 (Toàn màn hình dọc ST7789)</option>
            <option value="crop240">240x240 (Crop tâm tràn viền)</option>
          </select>
        </div>
        <div class="col">
          <label>Tốc độ khung hình (FPS):</label>
          <select id="fps">
            <option value="12">12 FPS (Mạng yếu)</option>
            <option value="15" selected>15 FPS (Chuẩn mượt mà & ổn định)</option>
            <option value="20">20 FPS (Tối đa)</option>
          </select>
        </div>
      </div>

      <div class="row" style="margin-top: 14px;">
        <button class="btn" style="flex:2" onclick="startStream()">▶ BẮT ĐẦU CHUYỂN MÃ & STREAM (FFmpeg)</button>
        <button class="btn btn-warning" onclick="togglePause()">⏸ TẠM DỪNG</button>
        <button class="btn btn-danger" onclick="stopStream()">⏹ DỪNG HẲN</button>
      </div>

      <!-- Phương thức 2: Bắt Luồng Tab YouTube Trực Tiếp Trên Trình Duyệt Localhost -->
      <div style="background: rgba(6,182,212,0.06); border: 1px dashed rgba(6,182,212,0.4); border-radius: 8px; padding: 12px; margin-top: 14px;">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom: 6px;">
          <span style="font-size: 0.84rem; font-weight: 600; color: var(--primary);">🖥️ Phát Bằng Chia Sẻ Tab Trực Tiếp (Không Bị Chặn HTTP)</span>
          <span style="font-size: 0.72rem; color: var(--accent); background: rgba(16,185,129,0.15); padding: 2px 6px; border-radius: 4px;">Localhost Secure Context</span>
        </div>
        <p style="font-size: 0.75rem; color: var(--subtext); margin-bottom: 8px; line-height: 1.4;">
          Vì trang này chạy trên <strong>localhost:5050</strong> nên Cốc Cốc / Chrome cho phép bắt toàn bộ video và âm thanh của Tab YouTube mà không cần băm file. Bấm nút dưới và chọn Tab YouTube đang phát để truyền ngay xuống ESP32:
        </p>
        <div class="row">
          <button class="btn" style="flex:2; background: linear-gradient(135deg, #0284c7, #06b6d4); color: #fff;" onclick="startBrowserTabShare()">
            ▶ CHỌN TAB YOUTUBE ĐỂ PHÁT XUỐNG ESP32
          </button>
          <button class="btn btn-danger" onclick="stopBrowserTabShare()">
            ⏹ DỪNG CHIA SẺ TAB
          </button>
        </div>
      </div>
    </div>

    <!-- Thẻ Giám sát thời gian thực (Live Monitor) -->
    <div class="card">
      <div class="card-title">🖥️ Màn Hình ST7789 Trực Tiếp & Thống Kê</div>
      <div class="preview-box">
        <div class="screen-mock">
          <img id="preview-img" src="/api/preview.jpg" alt="Live Screen">
          <div class="screen-overlay" id="overlay-res">240x240 // 15 FPS</div>
        </div>
        <div class="stats-grid">
          <div class="stat-card">
            <div class="stat-label">TIÊU ĐỀ VIDEO</div>
            <div class="stat-val" style="font-size: 0.85rem; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;" id="stat-title">--</div>
          </div>
          <div class="stat-card">
            <div class="stat-label">THỜI GIAN STREAM</div>
            <div class="stat-val" id="stat-time">00:00</div>
          </div>
          <div class="stat-card">
            <div class="stat-label">FPS THỰC TẾ</div>
            <div class="stat-val" style="color: var(--accent)" id="stat-fps">0.0 FPS</div>
          </div>
          <div class="stat-card">
            <div class="stat-label">KHUNG HÌNH ĐÃ GỬI</div>
            <div class="stat-val" id="stat-frames">0 frames</div>
          </div>
          <div class="stat-card">
            <div class="stat-label">ÂM THANH PCM (UDP Port 12345)</div>
            <div class="stat-val" style="color: var(--primary)" id="stat-audio">0 KB</div>
          </div>
          <div class="stat-card">
            <div class="stat-label">ĐÍCH ĐẾN</div>
            <div class="stat-val" style="font-size: 0.88rem" id="stat-ip">--</div>
          </div>
        </div>
      </div>

      <!-- Thanh trượt âm lượng -->
      <div style="margin-top: 16px;">
        <div style="display: flex; justify-content: space-between; font-size: 0.8rem; margin-bottom: 4px;">
          <span style="color: var(--subtext)">Âm lượng Stream xuống Loa MAX98357A</span>
          <span id="vol-val" style="font-weight: 600; color: var(--primary)">100%</span>
        </div>
        <input type="range" min="0" max="100" value="100" id="vol" style="width: 100%" oninput="changeVolume(this.value)">
      </div>
    </div>
  </div>

  <script>
    function setUrl(u) {
      document.getElementById('url').value = u;
    }

    async function startStream() {
      const url = document.getElementById('url').value.trim();
      const esp_ip = document.getElementById('esp_ip').value.trim();
      const resolution = document.getElementById('resolution').value;
      const fps = parseInt(document.getElementById('fps').value);
      const volume = parseInt(document.getElementById('vol').value);

      if (!url) return alert("Vui lòng nhập link YouTube hoặc link video!");
      if (!esp_ip) return alert("Vui lòng nhập địa chỉ IP của ESP32!");

      document.getElementById('stat-title').innerText = "Đang kết nối & tải stream...";

      try {
        const res = await fetch('/api/start', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ url, esp_ip, resolution, fps, volume })
        });
        const data = await res.json();
        if (data.status === 'ok') {
          document.getElementById('stat-title').innerText = data.title;
        } else {
          alert("Lỗi: " + (data.error || "Không thể khởi động"));
        }
      } catch (err) {
        alert("Lỗi kết nối tới Trạm Transcoder: " + err);
      }
    }

    async function togglePause() {
      await fetch('/api/pause', { method: 'POST' });
    }

    async function stopStream() {
      try {
        await fetch('/api/stop', { method: 'POST' });
      } catch(e) {}
      const esp_ip = (document.getElementById('esp_ip') ? document.getElementById('esp_ip').value.trim() : '') || '192.168.1.233';
      try {
        await fetch('http://' + esp_ip + '/api/stream/stop', { method: 'POST', mode: 'no-cors' });
      } catch(e) {}
    }

    async function changeVolume(val) {
      document.getElementById('vol-val').innerText = val + '%';
      await fetch('/api/volume', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ volume: parseInt(val) })
      });
    }

    // Vòng lặp cập nhật trạng thái & Live Preview mỗi 500ms
    setInterval(async () => {
      try {
        const res = await fetch('/api/status');
        const st = await res.json();

        const dot = document.getElementById('dot');
        const dotText = document.getElementById('dot-text');

        if (st.running) {
          dot.classList.add('active');
          dotText.innerText = st.paused ? 'PAUSED' : 'STREAMING';
          dotText.style.color = '#10b981';
          document.getElementById('stat-title').innerText = st.title || 'Đang stream...';
          document.getElementById('stat-time').innerText = st.elapsed_str;
          document.getElementById('stat-fps').innerText = st.live_fps + ' FPS';
          document.getElementById('stat-frames').innerText = st.frames_sent + ' frames';
          document.getElementById('stat-audio').innerText = st.audio_kb_sent + ' KB (UDP)';
          document.getElementById('stat-ip').innerText = st.esp_ip;

          // Cập nhật live preview mượt mà 15 FPS qua MJPEG stream
          const pImg = document.getElementById('preview-img');
          if (pImg && pImg.dataset.streaming !== 'true') {
            pImg.src = '/api/stream.mjpg';
            pImg.dataset.streaming = 'true';
          }
        } else {
          dot.classList.remove('active');
          dotText.innerText = 'IDLE';
          dotText.style.color = '#64748b';

          const pImg = document.getElementById('preview-img');
          if (pImg && pImg.dataset.streaming === 'true') {
            pImg.src = '/api/preview.jpg';
            pImg.dataset.streaming = 'false';
          }
        }
      } catch (e) {}
    }, 500);

    let s_tabStream = null;
    let s_tabInterval = null;
    let s_tabAudioCtx = null;
    let s_tabCanvas = null;
    let s_tabVideo = null;

    async function startBrowserTabShare() {
      const esp_ip = document.getElementById('esp_ip').value.trim() || '192.168.1.233';
      try {
        s_tabStream = await navigator.mediaDevices.getDisplayMedia({
          video: {
            displaySurface: "browser",
            frameRate: { ideal: 15, max: 15 },
            width: { ideal: 240, max: 480 },
            height: { ideal: 240, max: 480 }
          },
          audio: true
        });
      } catch (err) {
        return alert("Chưa chọn tab chia sẻ: " + err.message);
      }

      // Thông báo ESP32 chuyển sang chế độ Stream
      fetch('http://' + esp_ip + '/api/screen/state', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode: 'stream' })
      }).catch(()=>{});

      if (!s_tabCanvas) {
        s_tabCanvas = document.createElement('canvas');
        s_tabCanvas.width = 240;
        s_tabCanvas.height = 240;
      }
      const ctx = s_tabCanvas.getContext('2d');

      if (!s_tabVideo) {
        s_tabVideo = document.createElement('video');
        s_tabVideo.playsInline = true;
        s_tabVideo.autoplay = true;
        s_tabVideo.muted = true;
      }
      s_tabVideo.srcObject = s_tabStream;
      await s_tabVideo.play();

      document.getElementById('stat-title').innerText = "Đang chia sẻ Tab YouTube trực tiếp...";
      const dot = document.getElementById('dot');
      const dotText = document.getElementById('dot-text');
      dot.classList.add('active');
      dotText.innerText = 'TAB STREAM';
      dotText.style.color = '#00e5ff';

      // Video Pump (15 FPS)
      const targetInterval = 1000 / 15;
      let isSendingFrame = false;
      let sentFrames = 0;

      s_tabInterval = setInterval(() => {
        if (isSendingFrame || !s_tabVideo.videoWidth) return;
        ctx.drawImage(s_tabVideo, 0, 0, 240, 240);
        s_tabCanvas.toBlob(blob => {
          if (!blob) return;
          isSendingFrame = true;
          const fd = new FormData();
          fd.append('f', blob, 'frame.jpg');
          fetch('http://' + esp_ip + '/api/stream/video_frame', { method: 'POST', body: fd })
            .then(res => {
              if (res.ok) {
                sentFrames++;
                document.getElementById('stat-frames').innerText = sentFrames + ' frames';
              }
            })
            .catch(()=>{})
            .finally(() => { isSendingFrame = false; });
        }, 'image/jpeg', 0.5);
      }, targetInterval);

      // Audio Capture từ Display Media Stream
      const audioTracks = s_tabStream.getAudioTracks();
      if (audioTracks && audioTracks.length > 0) {
        s_tabAudioCtx = new (window.AudioContext || window.webkitAudioContext)();
        const srcNode = s_tabAudioCtx.createMediaStreamSource(s_tabStream);
        const procNode = s_tabAudioCtx.createScriptProcessor(2048, 1, 1);
        srcNode.connect(procNode);
        procNode.connect(s_tabAudioCtx.destination);

        let audioBuf = [];
        const ratio = 16000 / s_tabAudioCtx.sampleRate;
        let isSendingAudio = false;
        let audioBytesSent = 0;

        procNode.onaudioprocess = (e) => {
          const chData = e.inputBuffer.getChannelData(0);
          for (let i = 0; i < chData.length; i += (1 / ratio)) {
            const idx = Math.floor(i);
            if (idx < chData.length) {
              let s = Math.max(-1, Math.min(1, chData[idx]));
              audioBuf.push(Math.round(s < 0 ? s * 32768 : s * 32767));
            }
          }
          if (!isSendingAudio && audioBuf.length >= 1024) {
            isSendingAudio = true;
            const toSend = audioBuf.splice(0, 1024);
            const int16 = new Int16Array(toSend);
            // Gửi qua API Relay UDP của Transcoder trên localhost để chuyển tiếp tức thì qua UDP port 12345 tới ESP32
            fetch('/api/relay_audio?esp_ip=' + esp_ip, { method: 'POST', body: int16.buffer })
              .then(res => {
                if (res.ok) {
                  audioBytesSent += int16.byteLength;
                  document.getElementById('stat-audio').innerText = Math.round(audioBytesSent / 1024) + ' KB (UDP)';
                } else {
                  throw new Error('Relay HTTP ' + res.status);
                }
              })
              .catch(() => {
                // Fallback nếu chạy trực tiếp không qua Flask: gửi HTTP POST tới ESP32
                const blob = new Blob([int16.buffer], { type: 'application/octet-stream' });
                const fd = new FormData();
                fd.append('a', blob, 'audio.pcm');
                fetch('http://' + esp_ip + '/api/stream/audio_pcm', { method: 'POST', body: fd })
                  .then(r => {
                    if (r.ok) {
                      audioBytesSent += int16.byteLength;
                      document.getElementById('stat-audio').innerText = Math.round(audioBytesSent / 1024) + ' KB (HTTP)';
                    }
                  })
                  .catch(() => {});
              })
              .finally(() => { isSendingAudio = false; });
          }
        };
      }

      s_tabStream.getVideoTracks()[0].onended = () => {
        stopBrowserTabShare();
      };
    }

    function stopBrowserTabShare() {
      if (s_tabInterval) { clearInterval(s_tabInterval); s_tabInterval = null; }
      if (s_tabStream) { s_tabStream.getTracks().forEach(t => t.stop()); s_tabStream = null; }
      if (s_tabAudioCtx) { s_tabAudioCtx.close(); s_tabAudioCtx = null; }
      const esp_ip = document.getElementById('esp_ip').value.trim() || '192.168.1.233';
      fetch('http://' + esp_ip + '/api/stream/stop', { method: 'POST' }).catch(()=>{});
      const dot = document.getElementById('dot');
      const dotText = document.getElementById('dot-text');
      dot.classList.remove('active');
      dotText.innerText = 'IDLE';
      dotText.style.color = '#64748b';
    }
  </script>
</body>
</html>
"""

def _make_cors_headers():
    origin = request.headers.get("Origin") or "*"
    req_hdrs = request.headers.get("Access-Control-Request-Headers", "Content-Type, Authorization, *")
    return {
        "Access-Control-Allow-Origin": origin,
        "Access-Control-Allow-Methods": "GET, POST, OPTIONS, PUT, DELETE",
        "Access-Control-Allow-Headers": req_hdrs,
        "Access-Control-Allow-Private-Network": "true",
        "Access-Control-Allow-Credentials": "true",
        "Access-Control-Max-Age": "86400"
    }

@app.after_request
def add_cors_headers(response):
    for k, v in _make_cors_headers().items():
        response.headers[k] = v
    return response

@app.route("/<path:dummy>", methods=["OPTIONS"])
@app.route("/", methods=["OPTIONS"])
def api_options(dummy=""):
    resp = Response("", status=204)
    for k, v in _make_cors_headers().items():
        resp.headers[k] = v
    return resp

@app.route("/")
def index():
    cfg = load_config()
    return render_template_string(HTML_DASHBOARD, cfg=cfg)

@app.route("/api/status", methods=["GET"])
def api_status():
    return jsonify(session.get_status())

@app.route("/api/preview.jpg", methods=["GET"])
def api_preview():
    if session.last_frame_jpeg:
        return Response(session.last_frame_jpeg, mimetype="image/jpeg")
    # Trả về 1 ảnh đen rỗng nếu chưa có frame
    empty_jpeg = b'\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x01\x00H\x00H\x00\x00\xff\xdb\x00C\x00\x08\x06\x06\x07\x06\x05\x08\x07\x07\x07\t\t\x08\n\x0c\x14\r\x0c\x0b\x0b\x0c\x19\x12\x13\x0f\x14\x1d\x1a\x1f\x1e\x1d\x1a\x1c\x1c $.\' \",#\x1c\x1c(7),01444\x1f\'9=82<.342\xff\xc0\x00\x0b\x08\x00\x10\x00\x10\x01\x01\x11\x00\xff\xc4\x00\x1f\x00\x00\x01\x05\x01\x01\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\xff\xda\x00\x08\x01\x01\x00\x00?\x00\xbf\x00\xff\xd9'
    return Response(empty_jpeg, mimetype="image/jpeg")

@app.route("/api/stream.mjpg", methods=["GET"])
def api_stream_mjpg():
    """Live MJPEG preview stream thời gian thực (15 FPS) cho Web Dashboard"""
    def generate():
        last_sent = None
        while session.is_running:
            with session.frame_cond:
                if session.last_frame_jpeg is last_sent and session.is_running:
                    session.frame_cond.wait(timeout=0.25)
                frame = session.last_frame_jpeg
            if frame and frame is not last_sent:
                last_sent = frame
                yield (b'--frame\r\n'
                       b'Content-Type: image/jpeg\r\n'
                       b'Content-Length: ' + str(len(frame)).encode('ascii') + b'\r\n\r\n' +
                       frame + b'\r\n')
            elif not session.is_running:
                break
    return Response(generate(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route("/api/start", methods=["POST", "OPTIONS"])
def api_start():
    if request.method == "OPTIONS":
        return "", 204
    data = request.get_json() or {}
    url = data.get("url", "").strip()
    esp_ip = data.get("esp_ip", "192.168.4.1").strip()
    fps = int(data.get("fps", 15))
    resolution = data.get("resolution", "240x240")
    volume = int(data.get("volume", 100))

    if not url:
        return jsonify({"status": "error", "error": "Thiếu URL video"}), 400

    cfg = load_config()
    cfg["esp_ip"] = esp_ip
    cfg["fps"] = fps
    cfg["resolution"] = resolution
    cfg["last_url"] = url
    cfg["volume"] = volume
    save_config(cfg)

    try:
        res = session.start(url, esp_ip, fps, resolution, volume)
        return jsonify(res)
    except Exception as e:
        logger.error(f"Lỗi khởi động stream: {e}", exc_info=True)
        return jsonify({"status": "error", "error": str(e)}), 500

@app.route("/api/pause", methods=["POST", "OPTIONS"])
def api_pause():
    if request.method == "OPTIONS":
        return "", 204
    return jsonify(session.pause())

@app.route("/api/stop", methods=["POST", "OPTIONS"])
def api_stop():
    if request.method == "OPTIONS":
        return "", 204
    res = session.stop()
    try:
        import requests
        cfg = load_config()
        esp_ip = cfg.get("esp_ip", session.esp_ip)
        requests.post(f"http://{esp_ip}/api/stream/stop", timeout=0.8)
    except Exception:
        pass
    return jsonify(res)

@app.route("/api/volume", methods=["POST", "OPTIONS"])
def api_volume():
    if request.method == "OPTIONS":
        return "", 204
    data = request.get_json() or {}
    vol = data.get("volume", 100)
    return jsonify(session.set_volume(vol))

@app.route("/api/relay_audio", methods=["POST", "OPTIONS"])
def api_relay_audio():
    if request.method == "OPTIONS":
        return "", 204
    esp_ip = request.args.get("esp_ip", session.esp_ip)
    if not esp_ip:
        return jsonify({"status": "error", "error": "Thiếu IP ESP32"}), 400
    data = request.get_data()
    if data:
        try:
            import socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.sendto(data, (esp_ip, 12345))
            sock.close()
            return jsonify({"status": "ok", "bytes": len(data)})
        except Exception as e:
            return jsonify({"status": "error", "error": str(e)}), 500
    return jsonify({"status": "empty"})

# ============================================================================
#                      HÀM KHỞI CHẠY CHÍNH (CLI & SERVER)
# ============================================================================
def main():
    import argparse
    parser = argparse.ArgumentParser(description="Trạm Chuyển Mã Transcoder cho ESP32-S3")
    parser.add_argument("url", nargs="?", default="", help="Đường link YouTube / Video cần stream ngay lập tức")
    parser.add_argument("--esp", default="", help="Địa chỉ IP của ESP32 (ví dụ: 192.168.1.15 hoặc 192.168.4.1)")
    parser.add_argument("--port", type=int, default=5050, help="Cổng Web Dashboard (mặc định: 5050)")
    parser.add_argument("--fps", type=int, default=15, help="Tốc độ khung hình (10-20 FPS)")
    parser.add_argument("--res", default="240x240", choices=["240x240", "240x320", "crop240"], help="Độ phân giải")
    args = parser.parse_args()

    cfg = load_config()
    esp_ip = args.esp if args.esp else cfg.get("esp_ip", "192.168.4.1")

    # Nếu truyền URL trực tiếp qua CLI
    if args.url:
        logger.info(f"Khởi động stream trực tiếp từ tham số CLI: {args.url}")
        session.start(args.url, esp_ip, args.fps, args.res, cfg.get("volume", 100))

    logger.info("="*65)
    logger.info("  🚀 TRẠM CHUYỂN MÃ (TRANSCODER) ESP32-S3 ĐÃ SẴN SÀNG")
    logger.info(f"  🌐 Mở Bảng Điều Khiển Web tại: http://localhost:{args.port}")
    logger.info(f"  📡 Đích đến ESP32 mặc định    : http://{esp_ip}")
    logger.info("="*65)

    # Chạy Web Server Flask (host='0.0.0.0' để điện thoại cùng Wi-Fi cũng truy cập được)
    app.run(host="0.0.0.0", port=args.port, debug=False, threaded=True)

if __name__ == "__main__":
    main()
