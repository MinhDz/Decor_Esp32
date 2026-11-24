#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - Máy Chủ WebSocket Lắng Nghe & Phát Bản Thu Mic (Cổng 8766)
-----------------------------------------------------------------------------
Tính năng:
  1. Lắng nghe WebSocket tại ws://0.0.0.0:8766/ws
  2. Khi trạm ESP32 hoàn thành thu âm (nhả OK hoặc nhấc tay cảm ứng),
     trạm sẽ tự động gửi toàn bộ bản thu âm lên cổng WebSocket này.
  3. Tự động lưu tệp .wav vào thư mục 'recordings/' với tên rec_<giờ_ngày>.wav
  4. Tự động phát âm thanh ra LOA MÁY TÍNH ngay lập tức (winsound).
  5. Cung cấp giao diện Web tại http://localhost:8766 để nghe lại và tải về.
"""

import os
import sys
import io
import json
import wave
import time
import asyncio
import logging
from pathlib import Path
from aiohttp import web
import aiohttp

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

try:
    import winsound
    HAS_WINSOUND = True
except ImportError:
    HAS_WINSOUND = False

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S"
)
logger = logging.getLogger("MicListener")

BASE_DIR = Path(__file__).resolve().parent
REC_DIR = BASE_DIR / "recordings"
REC_DIR.mkdir(parents=True, exist_ok=True)

LATEST_WAV_PATH = REC_DIR / "rec_latest.wav"
LAST_RECORD_INFO = {
    "filename": "rec_latest.wav",
    "samples": 0,
    "duration_sec": 0.0,
    "received_at": ""
}

def pcm_to_wav_bytes(pcm_bytes: bytes, sample_rate: int = 16000) -> bytes:
    """Đóng gói PCM 16-bit Mono thành tệp WAV chuẩn."""
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_bytes)
    return buf.getvalue()

def play_audio_on_pc(wav_file_path: Path):
    """Phát âm thanh bản thu trực tiếp ra Loa Máy tính."""
    if HAS_WINSOUND and sys.platform == "win32":
        try:
            logger.info(f"🔊 [PC SPEAKER] Đang tự động phát: {wav_file_path.name} ra loa máy tính...")
            winsound.PlaySound(str(wav_file_path), winsound.SND_FILENAME | winsound.SND_ASYNC)
            return True
        except Exception as e:
            logger.warning(f"Không thể phát winsound: {e}")
    return False

async def handle_ws(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse(max_msg_size=16 * 1024 * 1024)
    await ws.prepare(request)

    peer = request.remote
    logger.info(f"🔗 Trạm ESP32 đã kết nối tới Cổng Mic Test từ {peer}")

    pcm_buf = bytearray()
    meta = {
        "filename": f"rec_{time.strftime('%Hh%Mm%Ss_%d_%m')}.wav",
        "sample_rate": 16000,
        "samples": 0,
        "duration_sec": 0.0
    }

    try:
        async for msg in ws:
            if msg.type == aiohttp.WSMsgType.BINARY:
                pcm_buf.extend(msg.data)
            elif msg.type == aiohttp.WSMsgType.TEXT:
                try:
                    data = json.loads(msg.data)
                except Exception:
                    data = {}

                msg_type = data.get("type", "")
                if msg_type in ("mic_record", "start", "meta"):
                    pcm_buf.clear()
                    fn = data.get("filename", "")
                    if fn:
                        # Chỉ lấy tên file, bỏ đường dẫn thư mục nếu có
                        fn_clean = Path(fn).name
                        if fn_clean:
                            meta["filename"] = fn_clean
                    meta["sample_rate"] = int(data.get("sample_rate", 16000))
                    meta["samples"] = int(data.get("samples", 0))
                    meta["duration_sec"] = float(data.get("duration_sec", 0.0))
                    logger.info(f"🎙️ Bắt đầu nhận bản thu mới: {meta['filename']} (Dự kiến {meta['samples']} mẫu, {meta['duration_sec']:.1f}s)")

                elif msg_type == "end":
                    logger.info(f"📦 Đã nhận đủ {len(pcm_buf)} bytes PCM từ Mic ESP32. Đang tạo file WAV...")
                    sample_rate = meta.get("sample_rate", 16000)
                    wav_data = pcm_to_wav_bytes(bytes(pcm_buf), sample_rate)

                    # 1. Lưu file theo tên timestamp
                    save_name = meta.get("filename") or f"rec_{time.strftime('%Hh%Mm%Ss_%d_%m')}.wav"
                    if not save_name.lower().endswith(".wav"):
                        save_name += ".wav"
                    target_file = REC_DIR / save_name
                    target_file.write_bytes(wav_data)

                    # 2. Lưu file latest để tiện nghe lại
                    LATEST_WAV_PATH.write_bytes(wav_data)

                    dur = len(pcm_buf) / (sample_rate * 2)
                    logger.info("=" * 68)
                    logger.info("  🎉 [THU THÀNH CÔNG] ĐÃ LƯU BẢN THU ÂM VÀO MÁY TÍNH!")
                    logger.info(f"  📁 Tệp đã lưu : {target_file}")
                    logger.info(f"  ⏱️ Thời lượng : {dur:.2f} giây ({len(pcm_buf) // 2:,} mẫu @ {sample_rate}Hz)")
                    logger.info("=" * 68)

                    LAST_RECORD_INFO["filename"] = save_name
                    LAST_RECORD_INFO["samples"] = len(pcm_buf) // 2
                    LAST_RECORD_INFO["duration_sec"] = round(dur, 2)
                    LAST_RECORD_INFO["received_at"] = time.strftime("%H:%M:%S %d/%m/%Y")

                    # 3. Tự động phát ra Loa Máy Tính
                    play_audio_on_pc(target_file)

                    await ws.send_str(json.dumps({
                        "type": "ack",
                        "status": "ok",
                        "filename": save_name,
                        "bytes": len(wav_data),
                        "duration_sec": dur
                    }))

    except Exception as e:
        logger.exception(f"Lỗi phiên WebSocket Mic: {e}")
    finally:
        logger.info(f"🔌 Trạm ESP32 đã ngắt kết nối WebSocket ({peer})")

    return ws

async def handle_index(request: web.Request) -> web.Response:
    """Giao diện Web xem và nghe lại bản thu âm trên máy tính."""
    html = f"""<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Trạm Decor - Mic Audio Test Gateway</title>
  <style>
    body {{
      background: #0d1117;
      color: #c9d1d9;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      margin: 0;
      padding: 30px;
      display: flex;
      justify-content: center;
    }}
    .container {{
      max-width: 600px;
      width: 100%;
      background: #161b22;
      border: 1px solid #30363d;
      border-radius: 12px;
      padding: 24px;
      box-shadow: 0 8px 24px rgba(0,0,0,0.5);
    }}
    h1 {{
      font-size: 20px;
      color: #58a6ff;
      margin-top: 0;
      display: flex;
      align-items: center;
      gap: 10px;
    }}
    .status-card {{
      background: #0d1117;
      border: 1px solid #238636;
      border-radius: 8px;
      padding: 16px;
      margin: 16px 0;
    }}
    .card-title {{
      font-size: 13px;
      color: #3fb950;
      font-weight: bold;
      text-transform: uppercase;
      margin-bottom: 8px;
    }}
    .stat-row {{
      display: flex;
      justify-content: space-between;
      padding: 4px 0;
      font-size: 14px;
    }}
    .stat-val {{
      font-weight: bold;
      color: #f0f6fc;
    }}
    audio {{
      width: 100%;
      margin-top: 14px;
      border-radius: 6px;
    }}
    .btn {{
      display: inline-block;
      background: #238636;
      color: #fff;
      padding: 10px 18px;
      border-radius: 6px;
      text-decoration: none;
      font-weight: 600;
      font-size: 14px;
      margin-top: 12px;
      text-align: center;
    }}
    .btn:hover {{
      background: #2ea043;
    }}
    .tip {{
      font-size: 12px;
      color: #8b949e;
      margin-top: 16px;
      line-height: 1.5;
    }}
  </style>
</head>
<body>
  <div class="container">
    <h1>🎙️ Trạm Decor - Mic Audio Test Gateway</h1>
    <p style="font-size: 14px; color: #8b949e;">Cổng WebSocket lắng nghe bản thu âm từ Mic INMP441 (Cổng 8766).</p>

    <div class="status-card">
      <div class="card-title">Bản thu âm gần nhất</div>
      <div class="stat-row">
        <span>Tên file:</span>
        <span class="stat-val">{LAST_RECORD_INFO['filename']}</span>
      </div>
      <div class="stat-row">
        <span>Thời lượng:</span>
        <span class="stat-val">{LAST_RECORD_INFO['duration_sec']} giây</span>
      </div>
      <div class="stat-row">
        <span>Thời gian nhận:</span>
        <span class="stat-val">{LAST_RECORD_INFO['received_at'] or 'Chưa có bản thu nào'}</span>
      </div>

      <audio controls autoplay src="/audio/latest.wav?t={int(time.time())}"></audio>
      <a class="btn" href="/audio/latest.wav" download="{LAST_RECORD_INFO['filename']}">⬇️ Tải tệp WAV về máy</a>
    </div>

    <div class="tip">
      💡 <b>Hướng dẫn thử nghiệm:</b><br>
      1. Trên Trạm ESP32 (Chế độ Sóng Âm - Mode 3), <b>Nhấn giữ nút [OK]</b> hoặc <b>Chạm giữ màn hình</b> để bắt đầu thu âm.<br>
      2. Hãy nói to vào Mic INMP441 (tùy ý thời lượng, không giới hạn giây).<br>
      3. <b>Nhả tay ra</b>: Trạm sẽ tự động lưu vào Thẻ nhớ SD, phát qua Loa trạm và <b>bắn ngay lên cổng 8766 này</b>!<br>
      4. Loa máy tính sẽ tự động phát âm thanh vừa thu để bạn kiểm tra chất lượng.
    </div>
  </div>
</body>
</html>"""
    return web.Response(text=html, content_type="text/html")

async def handle_latest_audio(request: web.Request) -> web.Response:
    if not LATEST_WAV_PATH.exists():
        return web.Response(status=404, text="Chưa có bản thu âm nào!")
    data = LATEST_WAV_PATH.read_bytes()
    return web.Response(body=data, content_type="audio/wav", headers={
        "Content-Disposition": f'inline; filename="{LAST_RECORD_INFO["filename"]}"',
        "Cache-Control": "no-cache"
    })

def create_app():
    app = web.Application()
    app.router.add_get("/", handle_index)
    app.router.add_get("/ws", handle_ws)
    app.router.add_get("/audio/latest.wav", handle_latest_audio)
    return app

def main():
    host = "0.0.0.0"
    port = 8766

    print("\n" + "=" * 70)
    print("  🎙️ DECOR ESP32 - MIC TEST WEBSOCKET SERVER (PORT 8766)")
    print(f"  🌐 WebSocket Endpoint : ws://<PC_IP>:{port}/ws")
    print(f"  🖥️ Web Browser UI    : http://localhost:{port}/")
    print("  🔊 Tự động phát ra LOA MÁY TÍNH ngay khi trạm nhả nút OK!")
    print("=" * 70 + "\n")

    app = create_app()
    web.run_app(app, host=host, port=port, print=None)

if __name__ == "__main__":
    main()
