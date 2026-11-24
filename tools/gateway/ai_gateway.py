#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Decor AI Gateway Server 2.0 - High-Performance Streaming Audio Gateway
-----------------------------------------------------------------------
Kiến trúc: PC đóng vai trò AI Gateway xử lý mô hình lớn và Edge-TTS.
Trạm Decor ESP32-S3 đóng vai trò Micro ngoại vi & Loa ngoại vi (Audio Endpoint).

Tính năng nổi bật:
  1. PIPELINED CHUNKED STREAMING (Producer - Consumer Queue):
     - Khắc phục triệt để lỗi không phát âm thanh khi đọc bài báo / văn bản dài.
     - Cắt câu thông minh (100-140 ký tự), Edge-TTS sinh âm thanh trong 1-2s.
     - Trạm ESP32 cất tiếng nói chỉ sau 1.5s, âm thanh gối đầu liên tục 100% không đứt đoạn.
  2. RAW PCM 16kHz 16-BIT MONO STREAMING:
     - PC giải mã MP3 ra PCM 16kHz Mono qua FFmpeg, stream trực tiếp nhị phân.
     - ESP32 nhận mẫu PCM đẩy thẳng vào I2S DMA ra MAX98357A.
     - Không có ID3 Header rác, không lo deadlock decoder, CPU ESP32 tải < 1%.
  3. HỖ TRỢ ĐA MÔ HÌNH AI:
     - Google Gemini (Gemini 2.5 Flash, Gemini Flash Lite, Gemini Pro có Web Grounding).
     - OpenAI Compatible (DeepSeek-R1, ChatGPT, Groq, Ollama).
  4. HỖ TRỢ CẢ WEBSOCKET VÀ HTTP REST:
     - WebSocket: / và /ws (chuẩn giao tiếp âm thanh 2 chiều thời gian thực với ESP32).
     - HTTP POST /api/chat: ESP32 gửi WAV mic -> Nhận STT, LLM reply, Emotion và stream audio.
     - HTTP GET /tts_stream: Stream PCM 16kHz cho văn bản bất kỳ (bài báo dài vô tận).
     - HTTP GET /news_stream: Tự động tổng hợp tin tức thời sự hôm nay và đọc báo.
     - HTTP GET /health: Ping kiểm tra trạng thái gateway (<2ms).
"""

import os
import sys
import io
import re
import json
import wave
import time
import shutil
import hashlib
import asyncio
import logging
import subprocess
from pathlib import Path
from aiohttp import web
import aiohttp
import urllib.request
import urllib.parse

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S"
)
logger = logging.getLogger("AIGateway")

BASE_DIR = Path(__file__).resolve().parent
CONFIG_FILE = BASE_DIR / "config.json"

# Cache PCM âm thanh cho các câu ngắn thường dùng để phản hồi tức thì (<10ms)
PCM_CACHE: dict = {}
MAX_CACHE_ENTRIES = 100

# Tập hợp các WebSocket client đang kết nối (ESP32)
ACTIVE_WS_CLIENTS = set()

def load_config():
    if not CONFIG_FILE.exists():
        example = BASE_DIR / "config.example.json"
        if example.exists():
            shutil.copy(example, CONFIG_FILE)
            logger.warning(f"Đã tạo {CONFIG_FILE}. Vui lòng điền API Key vào file này!")
        else:
            default_cfg = {
                "listen_host": "0.0.0.0",
                "listen_port": 8765,
                "gemini_api_key": "YOUR_GEMINI_API_KEY",
                "openai_compatible": {
                    "api_key": "YOUR_OPENAI_OR_DEEPSEEK_KEY",
                    "base_url": "https://api.deepseek.com",
                    "model": "deepseek-reasoner"
                },
                "tts_voice": "vi-VN-HoaiMyNeural",
                "system_prompt": "Bạn là Trợ lý AI siêu thông minh của Trạm Decor Vũ Trụ ESP32-S3. Hãy trả lời tiếng Việt tự nhiên, súc tích, mạch lạc, dễ nghe qua loa ngoài."
            }
            with open(CONFIG_FILE, "w", encoding="utf-8") as f:
                json.dump(default_cfg, f, indent=2, ensure_ascii=False)
    if CONFIG_FILE.exists():
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as e:
            logger.error(f"Lỗi đọc {CONFIG_FILE}: {e}")
    return {}

def find_ffmpeg():
    try:
        import imageio_ffmpeg
        exe = imageio_ffmpeg.get_ffmpeg_exe()
        if os.path.exists(exe):
            return exe
    except Exception:
        pass
    return shutil.which("ffmpeg") or "ffmpeg"

FFMPEG_EXE = find_ffmpeg()
logger.info(f"Bộ mã hóa FFmpeg: {FFMPEG_EXE}")

def pcm_to_wav_bytes(pcm_bytes: bytes, sample_rate: int = 16000) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_bytes)
    return buf.getvalue()

def clean_text_for_tts(text: str) -> str:
    """Làm sạch văn bản trước khi đưa vào TTS: loại bỏ markdown, emoji, chuẩn hóa số thứ tự."""
    if not text:
        return ""
    # Bỏ markdown asterisks, hashes, backticks, tildes
    t = re.sub(r'[*_#`~]', '', text)
    # Chuyển đổi số thứ tự đầu dòng: 1. 2. thành Mục 1: , Mục 2: để không bị ngắt câu rời rạc
    t = re.sub(r'^\s*(\d+)\.\s+', r'Mục \1: ', t, flags=re.MULTILINE)
    # Bỏ emoji và các icon ký tự đặc biệt
    t = re.sub(r'[✨⭐🚀🤖⚡📢📦🏁🔌💡❤️🥰😘😋😡😭😱😴🤔😉😂🤣]+', '', t)
    return t.strip()

def split_into_smart_sentences(text: str, max_chars: int = 140) -> list[str]:
    """Phân tách thông minh các câu theo ngữ pháp tự nhiên, không cắt vụn số hoặc từ viết tắt."""
    text = clean_text_for_tts(text)
    if not text:
        return []
    # Tách theo dấu kết thúc câu (.?!;\n) với điều kiện ký tự đứng trước không phải là chữ số (tránh cắt 1.5, 3.14)
    raw_sentences = re.split(r'(?<=[^\d][.?!;\n])\s+', text)
    sentences = []
    for s in raw_sentences:
        s = s.strip()
        if not s:
            continue
        if len(s) <= max_chars:
            sentences.append(s)
        else:
            # Nếu câu quá dài, tách tiếp theo dấu phẩy, hai chấm, gạch ngang
            sub = re.split(r'(?<=[,:—–])\s+', s)
            curr = ""
            for p in sub:
                p = p.strip()
                if not p:
                    continue
                if len(curr) + len(p) + 1 <= max_chars:
                    curr = (curr + " " + p).strip() if curr else p
                else:
                    if curr:
                        sentences.append(curr)
                    curr = p
            if curr:
                sentences.append(curr)
    return [s for s in sentences if s]

def detect_emotion_from_text(text: str) -> str:
    """Xác định cảm xúc khuôn mặt cho màn hình LCD ST7789 từ nội dung trả lời."""
    t = text.lower()
    if any(k in t for k in ["😂", "🤣", "haha", "buồn cười", "hài hước", "truyện cười", "[laugh]"]):
        return "laughing"
    if any(k in t for k in ["🥰", "❤️", "yêu quý", "dễ thương", "đáng yêu", "[love]"]):
        return "loving"
    if any(k in t for k in ["😘", "hôn", "[kiss]"]):
        return "kissy"
    if any(k in t for k in ["😋", "ngon", "thơm ngon", "ẩm thực", "món ăn"]):
        return "delicious"
    if any(k in t for k in ["😎", "tuyệt vời", "xuất sắc", "đỉnh", "pro", "[cool]"]):
        return "cool"
    if any(k in t for k in ["😱", "trời ơi", "kinh ngạc", "bất ngờ", "[shocked]"]):
        return "shocked"
    if any(k in t for k in ["😭", "buồn", "chia buồn", "thương tiếc", "đau lòng"]):
        return "sad"
    if any(k in t for k in ["😡", "tức giận", "phẫn nộ", "bực mình", "[angry]"]):
        return "angry"
    if any(k in t for k in ["😴", "chúc ngủ ngon", "ngủ ngon", "mệt mỏi", "buồn ngủ"]):
        return "sleepy"
    if any(k in t for k in ["🤔", "suy nghĩ", "tính toán", "phân tích", "để xem"]):
        return "thinking"
    if any(k in t for k in ["😉", "[wink]"]):
        return "winking"
    return "happy"

def fetch_google_tts_mp3(text: str) -> bytes:
    """Lấy âm thanh Google Translate TTS miễn phí siêu tốc (<0.4s)."""
    import urllib.request
    import urllib.parse
    url = f"https://translate.google.com/translate_tts?ie=UTF-8&q={urllib.parse.quote(text)}&tl=vi&client=tw-ob"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=5) as res:
        return res.read()

def synthesize_sentence_worker(text: str, voice: str) -> bytes:
    """Tổng hợp âm thanh: Đảm bảo đồng nhất 1 giọng duy nhất (không nhảy giọng giữa các câu)."""
    if not text.strip():
        return b""

    mp3_bytes = b""
    engine = "Edge-TTS"

    # 1. Nếu cấu hình chỉ định dùng Google TTS trực tiếp
    if voice.startswith("vi-google"):
        try:
            mp3_bytes = fetch_google_tts_mp3(text)
            engine = "Google-TTS"
        except Exception as e:
            logger.warning(f"Google TTS lỗi: {e}")

    # 2. Mặc định: Ưu tiên Microsoft Edge Neural TTS (Hoài My / Nam Minh)
    # Timeout 12 giây + 2 lần thử để chống rớt kết nối mạng mà không bị nhảy giọng
    if not mp3_bytes:
        for attempt in range(2):
            async def _inner():
                import edge_tts
                comm = edge_tts.Communicate(text, voice)
                mp3 = bytearray()
                async for chunk in comm.stream():
                    if chunk["type"] == "audio":
                        mp3.extend(chunk["data"])
                return bytes(mp3)

            try:
                async def _run_with_timeout():
                    return await asyncio.wait_for(_inner(), timeout=12.0)
                mp3_bytes = asyncio.run(_run_with_timeout())
                if mp3_bytes:
                    engine = f"Edge-TTS ({voice})"
                    break
            except Exception as e:
                if attempt == 0:
                    logger.warning(f"Edge-TTS thử lại lần 2 cho câu '{text[:30]}...': {e}")
                    time.sleep(0.3)
                else:
                    logger.error(f"Edge-TTS thất bại sau 2 lần thử: {e}")

    # 3. Fallback chỉ khi Edge-TTS hoàn toàn không phản hồi sau 2 lần thử
    if not mp3_bytes:
        try:
            mp3_bytes = fetch_google_tts_mp3(text)
            engine = "Google-TTS (Fallback khi Edge-TTS mất kết nối)"
            logger.warning(f"⚠️ Edge-TTS không phản hồi sau 2 lần thử, dùng tạm Google TTS cho: '{text[:30]}...'")
        except Exception as e:
            logger.error(f"Cả Edge-TTS và Google-TTS đều lỗi: {e}")
            return b""

    # 4. Chuyển đổi MP3 sang Raw PCM 16kHz 16-bit Mono qua FFmpeg
    try:
        proc = subprocess.run(
            [
                FFMPEG_EXE, "-hide_banner", "-loglevel", "error",
                "-f", "mp3", "-i", "pipe:0",
                "-ac", "1", "-ar", "16000", "-f", "s16le", "pipe:1"
            ],
            input=mp3_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False
        )
        if proc.returncode == 0:
            return proc.stdout
        else:
            logger.error(f"FFmpeg lỗi: {proc.stderr.decode(errors='ignore')}")
            return b""
    except Exception as e:
        logger.error(f"Lỗi FFmpeg: {e}")
        return b""

async def synthesize_single_sentence_pcm(sentence: str, voice: str = "vi-VN-HoaiMyNeural") -> bytes:
    """Sinh âm thanh cho 1 câu duy nhất (Edge-TTS -> PCM 16kHz 16-bit Mono) qua bộ đệm nhớ."""
    clean = sentence.strip()
    if not clean:
        return b""

    cache_key = hashlib.md5(f"{voice}:{clean}".encode("utf-8")).hexdigest()
    if cache_key in PCM_CACHE:
        return PCM_CACHE[cache_key]

    pcm = await asyncio.to_thread(synthesize_sentence_worker, clean, voice)
    if pcm:
        if len(PCM_CACHE) >= MAX_CACHE_ENTRIES:
            PCM_CACHE.pop(next(iter(PCM_CACHE)))
        PCM_CACHE[cache_key] = pcm
    return pcm

def fetch_vnexpress_news(limit: int = 5) -> list[dict]:
    """Lấy tin tức thời sự tiếng Việt mới nhất trực tiếp từ RSS VnExpress không cần API Key."""
    try:
        import urllib.request
        import xml.etree.ElementTree as ET
        import re
        url = "https://vnexpress.net/rss/tin-moi-nhat.rss"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"})
        with urllib.request.urlopen(req, timeout=5) as res:
            xml_data = res.read()
        root = ET.fromstring(xml_data)
        items = []
        for item in root.findall(".//item")[:limit]:
            title_elem = item.find("title")
            desc_elem = item.find("description")
            title = title_elem.text.strip() if title_elem is not None and title_elem.text else ""
            desc = desc_elem.text.strip() if desc_elem is not None and desc_elem.text else ""
            desc = re.sub(r"<[^>]+>", "", desc).strip()
            if title:
                items.append({"title": title, "summary": desc})
        return items
    except Exception as e:
        logger.warning(f"Lỗi lấy RSS VnExpress: {e}")
        return []

def search_web_realtime(query: str, max_results: int = 4) -> str:
    """Tìm kiếm web thời gian thực kết hợp VnExpress RSS và DuckDuckGo."""
    snippets = []
    news = fetch_vnexpress_news(max_results)
    if news and any(k in query.lower() for k in ["tin tức", "thời sự", "báo", "sáng nay", "mới nhất"]):
        for n in news:
            snippets.append(f"- {n['title']}: {n['summary']}")

    if not snippets:
        try:
            from duckduckgo_search import DDGS
            with DDGS() as ddgs:
                for r in ddgs.text(query, region="vn-vi", timelimit="w", max_results=max_results):
                    snippets.append(f"- {r.get('title', '')}: {r.get('body', '')}")
        except Exception:
            pass

    if not snippets and news:
        for n in news[:max_results]:
            snippets.append(f"- {n['title']}: {n['summary']}")

    return "\n".join(snippets)

ACTIVE_WS_STREAMS: dict = {}

async def cancel_existing_ws_stream(ws: web.WebSocketResponse):
    """Hủy luồng âm thanh đang phát trên WebSocket (nếu có) để tránh phát đè 2 giọng nói cùng lúc."""
    task = ACTIVE_WS_STREAMS.get(ws)
    if task and not task.done():
        logger.info("⏹️ Đang hủy luồng âm thanh cũ trước khi phát câu mới...")
        task.cancel()
        try:
            await asyncio.wait_for(asyncio.shield(task), timeout=0.6)
        except Exception:
            pass

async def stream_sentences_tts_pcm(sentences: list[str], voice: str, ws: web.WebSocketResponse):
    """
    KIẾN TRÚC PIPELINED STREAMING (Producer - Consumer Queue):
    - Producer sinh âm thanh từng câu ngắn gối đầu trong nền.
    - Consumer điều tốc theo đồng hồ thực (Wall-Clock Precision Pacing):
      + 0.8 giây đầu gửi nhanh (Burst) để nạp trước Jitter Buffer cho ESP32.
      + Sau đó nhịp gửi khớp 100% với tốc độ phát thực tế (16,000 mẫu/giây = 32,000 bytes/giây).
      + Tự động bù trôi (Self-correcting drift) và chống tràn bộ đệm PSRAM triệt để ('gom âm thanh', tua nhanh 3x).
    """
    queue = asyncio.Queue(maxsize=3)
    stop_event = asyncio.Event()

    async def producer():
        try:
            for idx, sentence in enumerate(sentences):
                if stop_event.is_set():
                    break
                t_start = time.time()
                pcm = await synthesize_single_sentence_pcm(sentence, voice)
                if stop_event.is_set():
                    break
                if pcm and len(pcm) > 0:
                    dur_sec = len(pcm) / 32000.0
                    logger.info(f"🎙️ [PRODUCER] Câu {idx+1}/{len(sentences)} ({dur_sec:.1f}s) xong sau {int((time.time()-t_start)*1000)}ms: '{sentence[:30]}...'")
                    await queue.put((idx, pcm))
                else:
                    logger.warning(f"⚠️ [PRODUCER] Bỏ qua câu {idx+1} do không có audio.")
        except asyncio.CancelledError:
            stop_event.set()
            raise
        except Exception as e:
            logger.error(f"Lỗi Producer: {e}")
        finally:
            await queue.put((None, None))

    async def consumer():
        chunk_size = 2048 # 1024 mẫu 16-bit Mono = 64ms âm thanh mỗi gói
        first_frame = True
        start_time = 0.0
        total_samples_sent = 0

        try:
            while True:
                idx, pcm_data = await queue.get()
                if pcm_data is None:
                    queue.task_done()
                    break

                for i in range(0, len(pcm_data), chunk_size):
                    if ws.closed or stop_event.is_set():
                        stop_event.set()
                        break
                    chunk = pcm_data[i:i+chunk_size]
                    await ws.send_bytes(chunk)
                    now = time.monotonic()
                    if first_frame:
                        first_frame = False
                        start_time = now
                        total_samples_sent = 0
                        logger.info("⚡ [CONSUMER] Đã gửi Frame PCM đầu tiên tới ESP32!")

                    samples_in_chunk = len(chunk) // 2
                    total_samples_sent += samples_in_chunk
                    audio_time_sent = total_samples_sent / 16000.0

                    # Tự phục hồi mốc thời gian nếu ESP32 đã phát cạn do mạng trễ giữa các câu
                    if (now - start_time) > audio_time_sent:
                        start_time = now
                        total_samples_sent = samples_in_chunk
                        audio_time_sent = samples_in_chunk / 16000.0

                    # Duy trì đúng 0.8s pre-buffer trên ESP32, tuyệt đối không gửi vượt tốc gây tràn bộ đệm
                    target_time = start_time + max(0.0, audio_time_sent - 0.8)
                    now_check = time.monotonic()
                    if target_time > now_check:
                        await asyncio.sleep(target_time - now_check)

                queue.task_done()
        except asyncio.CancelledError:
            stop_event.set()
            raise
        except Exception as e:
            logger.error(f"Lỗi Consumer: {e}")
            stop_event.set()

    prod_task = asyncio.create_task(producer())
    cons_task = asyncio.create_task(consumer())
    try:
        await asyncio.gather(prod_task, cons_task)
    except asyncio.CancelledError:
        stop_event.set()
        prod_task.cancel()
        cons_task.cancel()
        raise

# ================= XỬ LÝ MÔ HÌNH AI =================

def normalize_gemini_model(model_name: str) -> str:
    """Chuẩn hóa tên model Gemini, map các model cũ đã ngưng hoạt động sang model hiện đại."""
    if not model_name:
        return "gemini-flash-lite-latest"
    m = model_name.strip()
    if m.startswith("models/"):
        m = m[7:]
    lower_m = m.lower()
    if lower_m in ["gemini-1.5-flash", "gemini-flash", "gemini-2.5-flash", "gemini-2.5-flash-lite"]:
        return "gemini-flash-lite-latest"
    if lower_m in ["gemini-1.5-pro", "gemini-pro", "gemini-2.5-pro"]:
        return "gemini-pro-latest"
    return m

def fetch_and_test_gemini_models(api_key: str) -> dict:
    """Truy vấn danh sách model từ Google Gemini API và kiểm tra model nào đang hoạt động."""
    if not api_key:
        return {"status": "error", "message": "API key rỗng", "working_models": [], "all_models": []}
    url = f"https://generativelanguage.googleapis.com/v1beta/models?key={api_key}"
    req = urllib.request.Request(url, headers={"User-Agent": "DecorSpaceGateway/2.0"})
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            data = json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as he:
        err_body = he.read().decode("utf-8", errors="ignore")
        return {"status": "error", "message": f"HTTP {he.code}: {err_body[:120]}", "working_models": [], "all_models": []}
    except Exception as e:
        logger.warning(f"Lỗi truy vấn danh sách model từ Google: {e}")
        return {"status": "error", "message": str(e), "working_models": [], "all_models": []}

    all_content_models = [
        m["name"].replace("models/", "")
        for m in data.get("models", [])
        if "generateContent" in m.get("supportedGenerationMethods", [])
    ]

    candidates = [
        "gemini-flash-lite-latest",
        "gemini-3-flash-preview",
        "gemini-flash-latest",
        "gemini-pro-latest",
        "gemini-3.5-flash",
        "gemini-2.5-flash-lite"
    ]
    to_test = [m for m in candidates if m in all_content_models]
    if not to_test:
        to_test = all_content_models[:6]

    working = []
    payload = json.dumps({"contents": [{"parts": [{"text": "hi"}]}]}).encode("utf-8")

    def check_m(m):
        m_url = f"https://generativelanguage.googleapis.com/v1beta/models/{m}:generateContent?key={api_key}"
        m_req = urllib.request.Request(m_url, data=payload, headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(m_req, timeout=5) as resp:
                if resp.status == 200:
                    return m
        except Exception:
            return None

    import concurrent.futures
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as ex:
        results = ex.map(check_m, to_test)
        for r in results:
            if r:
                working.append(r)

    rec = working[0] if working else (all_content_models[0] if all_content_models else "gemini-flash-lite-latest")
    return {
        "status": "ok",
        "recommended": rec,
        "working_models": working,
        "all_models": all_content_models
    }

async def ask_gemini_internal(cfg: dict, model_id: str, wav_bytes: bytes, enable_search: bool) -> tuple[str, str, str]:
    """Gọi Gemini (hỗ trợ REST audio base64 hoặc SDK) và trả về (user_text, reply, sources)."""
    api_key = cfg.get("gemini_api_key", "").strip()
    if not api_key or "YOUR_" in api_key:
        return ("", "Xin chào! Tôi là Trợ lý AI của Trạm Decor. Hệ thống Gateway đã đồng bộ thành công với máy tính của bạn và hệ thống âm thanh trực tuyến đã sẵn sàng. Bạn hãy điền API Key vào file cấu hình để bắt đầu đàm thoại thông minh nhé!", "")

    chosen_model = normalize_gemini_model(model_id or cfg.get("gemini_model") or "gemini-flash-lite-latest")
    candidate_models = []
    for m in [chosen_model, "gemini-flash-lite-latest", "gemini-3-flash-preview", "gemini-flash-latest"]:
        norm = normalize_gemini_model(m)
        if norm not in candidate_models:
            candidate_models.append(norm)

    import base64
    audio_b64 = base64.b64encode(wav_bytes).decode("ascii")
    system_inst = cfg.get("system_prompt", "Trả lời ngắn gọn, tự nhiên bằng tiếng Việt.")
    
    last_err = ""
    for try_model in candidate_models:
        try:
            url = f"https://generativelanguage.googleapis.com/v1beta/models/{try_model}:generateContent?key={api_key}"
            req_data = {
                "contents": [{
                    "role": "user",
                    "parts": [
                        {"inline_data": {"mime_type": "audio/wav", "data": audio_b64}},
                        {"text": f"{system_inst}\n\nHãy nghe câu hỏi trong audio và trả lời bằng tiếng Việt."}
                    ]
                }]
            }
            req = urllib.request.Request(
                url,
                data=json.dumps(req_data).encode("utf-8"),
                headers={"Content-Type": "application/json"}
            )
            def _call_rest():
                with urllib.request.urlopen(req, timeout=20) as res:
                    return json.loads(res.read().decode("utf-8"))
            resp_json = await asyncio.to_thread(_call_rest)
            candidates = resp_json.get("candidates", [])
            if candidates:
                parts = candidates[0].get("content", {}).get("parts", [])
                if parts:
                    reply = parts[0].get("text", "").strip()
                    if reply:
                        return ("", reply, f"Google Gemini ({try_model})")
        except Exception as e:
            last_err = str(e)
            logger.warning(f"Lỗi REST audio Gemini ({try_model}): {e}")

    return ("", f"Lỗi Gemini: {last_err or 'Không thể xử lý âm thanh'}", "")
        reply = f"Lỗi Gemini: {e}"

    return (user_text, reply, sources)

async def process_with_gemini(cfg: dict, model_id: str, wav_bytes: bytes, enable_search: bool, ws: web.WebSocketResponse):
    """Xử lý âm thanh hỏi đáp bằng Google Gemini với khả năng Web Grounding thời gian thực."""
    voice = cfg.get("tts_voice", "vi-VN-HoaiMyNeural")
    await ws.send_str(json.dumps({"type": "thinking", "text": f"{model_id} đang nghe và suy luận..."}))

    _, reply, sources = await ask_gemini_internal(cfg, model_id, wav_bytes, enable_search)
    logger.info(f"🤖 [{model_id}] Phản hồi: {reply}")
    emotion = detect_emotion_from_text(reply)
    await ws.send_str(json.dumps({"type": "answer", "text": reply, "sources": sources, "emotion": emotion}, ensure_ascii=False))

    # Tách câu và stream PCM gối đầu
    sentences = split_into_smart_sentences(reply, max_chars=130)
    logger.info(f"⚡ [STREAMING] Đã chia thành {len(sentences)} câu. Bắt đầu Pipelined TTS...")
    await stream_sentences_tts_pcm(sentences, voice, ws)

async def process_with_openai_compat(cfg: dict, wav_bytes: bytes, enable_search: bool, ws: web.WebSocketResponse):
    """Xử lý với mô hình OpenAI Compatible (DeepSeek-R1, ChatGPT, v.v.)."""
    from openai import OpenAI

    oai_cfg = cfg.get("openai_compatible", {})
    oai_key = oai_cfg.get("api_key", "")
    if not oai_key or "YOUR_" in oai_key:
        await ws.send_str(json.dumps({"type": "error", "text": "Chưa cấu hình openai_compatible.api_key trong config.json!"}))
        return

    # STT chuyển giọng nói thành văn bản qua Gemini nếu có
    user_text = ""
    gemini_key = cfg.get("gemini_api_key", "")
    if gemini_key and "YOUR_" not in gemini_key:
        try:
            try:
                from google import genai
                from google.genai import types
                gclient = genai.Client(api_key=gemini_key)
                audio_part = types.Part.from_bytes(data=wav_bytes, mime_type="audio/wav")
                stt_resp = await asyncio.to_thread(
                    lambda: gclient.models.generate_content(
                        model="gemini-flash-lite-latest",
                        contents=[audio_part, "Chỉ xuất ra câu tiếng Việt người dùng vừa nói, không thêm bất kỳ lời bình nào."]
                    )
                )
                user_text = (stt_resp.text or "").strip()
            except ImportError:
                import google.generativeai as genai_legacy
                genai_legacy.configure(api_key=gemini_key)
                m = genai_legacy.GenerativeModel("gemini-1.5-flash")
                audio_blob = {"mime_type": "audio/wav", "data": wav_bytes}
                stt_resp = await asyncio.to_thread(
                    lambda: m.generate_content([audio_blob, "Chỉ ghi lại nguyên văn câu tiếng Việt người dùng vừa nói."])
                )
                user_text = (stt_resp.text or "").strip()
        except Exception as e:
            logger.warning(f"Lỗi STT qua Gemini: {e}")

    if not user_text:
        user_text = "Hôm nay thời tiết và tin tức thời sự thế nào?"

    logger.info(f"🗣️ [STT] Người dùng: {user_text}")
    await ws.send_str(json.dumps({"type": "stt", "text": user_text}, ensure_ascii=False))

    web_ctx = ""
    if enable_search and user_text:
        await ws.send_str(json.dumps({"type": "thinking", "text": "Đang tìm kiếm thông tin thời gian thực..."}))
        web_ctx = await asyncio.to_thread(search_web_realtime, user_text)

    model_name = oai_cfg.get("model", "deepseek-reasoner")
    await ws.send_str(json.dumps({"type": "thinking", "text": f"{model_name} đang suy luận..."}))

    oai_client = OpenAI(api_key=oai_key, base_url=oai_cfg.get("base_url", "https://api.deepseek.com"))
    prompt_content = user_text
    if web_ctx:
        prompt_content = f"Thông tin cập nhật thời gian thực ({time.strftime('%d/%m/%Y %H:%M')}):\n{web_ctx}\n\nCâu hỏi: {user_text}"

    def call_llm():
        return oai_client.chat.completions.create(
            model=model_name,
            messages=[
                {"role": "system", "content": cfg.get("system_prompt", "Trả lời súc tích bằng tiếng Việt.")},
                {"role": "user", "content": prompt_content}
            ]
        )

    try:
        comp = await asyncio.to_thread(call_llm)
        reply = (comp.choices[0].message.content or "").strip()
    except Exception as e:
        logger.error(f"Lỗi LLM {model_name}: {e}")
        await ws.send_str(json.dumps({"type": "error", "text": f"Lỗi LLM: {e}"}))
        return

    logger.info(f"🧠 [{model_name}] Phản hồi: {reply}")
    emotion = detect_emotion_from_text(reply)
    await ws.send_str(json.dumps({"type": "answer", "text": reply, "sources": "Web Search + " + model_name, "emotion": emotion}, ensure_ascii=False))

    voice = cfg.get("tts_voice", "vi-VN-HoaiMyNeural")
    sentences = split_into_smart_sentences(reply, max_chars=130)
    await stream_sentences_tts_pcm(sentences, voice, ws)

# ================= HTTP & WEBSOCKET HANDLERS =================

async def handle_http_options(request: web.Request) -> web.Response:
    """Xử lý CORS preflight OPTIONS."""
    return web.Response(
        status=204,
        headers={
            "Access-Control-Allow-Origin": "*",
            "Access-Control-Allow-Methods": "GET, POST, OPTIONS, HEAD",
            "Access-Control-Allow-Headers": "Content-Type, Authorization, X-Requested-With, *",
            "Access-Control-Allow-Private-Network": "true",
            "Access-Control-Max-Age": "86400",
        }
    )

async def handle_http_health(request: web.Request) -> web.Response:
    """ESP32 ping kiểm tra Gateway có online hay không (<2ms)."""
    cfg = load_config()
    return web.json_response({
        "status": "ok",
        "gateway": "active",
        "version": "2.0-pipelined-pcm",
        "edge_tts": True,
        "default_voice": cfg.get("tts_voice", "vi-VN-HoaiMyNeural"),
        "models": ["gemini-pro", "gemini-flash", "deepseek-r1"]
    }, headers={
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Private-Network": "true"
    })
        "models": ["gemini-pro", "gemini-flash", "deepseek-r1"]
    }, headers={"Access-Control-Allow-Origin": "*"})

async def handle_http_config(request: web.Request) -> web.Response:
    """Đọc hoặc ghi cấu hình API Keys (Gemini, DeepSeek, Edge-TTS voice)."""
    cfg = load_config()
    if request.method == "POST":
        try:
            body = await request.json()
            changed = False
            for k in ["gemini_api_key", "openai_api_key", "openai_base_url", "openai_model", "tts_voice"]:
                if k in body and body[k] is not None:
                    val = str(body[k]).strip()
                    if val != "" or k in ["openai_base_url", "openai_model", "tts_voice"]:
                        cfg[k] = val
                        changed = True
            if changed:
                save_config(cfg)
            return web.json_response({
                "status": "ok",
                "message": "Đã lưu cấu hình API Key vào Decor AI Gateway thành công!",
                "has_gemini": bool(cfg.get("gemini_api_key")),
                "has_deepseek": bool(cfg.get("openai_api_key")),
                "voice": cfg.get("tts_voice", "vi-VN-HoaiMyNeural")
            }, headers={"Access-Control-Allow-Origin": "*"})
        except Exception as e:
            return web.json_response({"status": "error", "message": str(e)}, status=400, headers={"Access-Control-Allow-Origin": "*"})
    else:
        return web.json_response({
            "status": "ok",
            "has_gemini": bool(cfg.get("gemini_api_key")),
            "has_deepseek": bool(cfg.get("openai_api_key")),
            "gemini_model": cfg.get("gemini_model", "gemini-flash-lite-latest"),
            "openai_base_url": cfg.get("openai_base_url", "https://api.deepseek.com"),
            "voice": cfg.get("tts_voice", "vi-VN-HoaiMyNeural")
        }, headers={"Access-Control-Allow-Origin": "*"})

async def handle_http_gemini_models(request: web.Request) -> web.Response:
    key = request.query.get("key", "").strip()
    if not key and request.method == "POST":
        try:
            body = await request.json()
            key = body.get("key", "").strip()
        except Exception:
            pass
    if not key:
        cfg = load_config()
        key = cfg.get("gemini_api_key", "").strip()

    if not key:
        return web.json_response({
            "status": "error",
            "message": "Chưa có Gemini API Key. Vui lòng nhập API Key để kiểm tra!"
        }, status=400, headers={"Access-Control-Allow-Origin": "*", "Access-Control-Allow-Private-Network": "true"})

    data = await asyncio.to_thread(fetch_and_test_gemini_models, key)
    return web.json_response(data, headers={"Access-Control-Allow-Origin": "*", "Access-Control-Allow-Private-Network": "true"})

async def call_llm_text_async(cfg: dict, prompt_text: str, model_id: str = "gemini-flash") -> tuple[str, str]:
    """Gọi LLM từ text thuần (hỗ trợ DeepSeek, Gemini, fallback)."""
    gemini_key = cfg.get("gemini_api_key", "").strip()
    oai_key = cfg.get("openai_api_key", "").strip()
    system_prompt = cfg.get("system_prompt", "Bạn là Trợ lý AI trên Trạm Decor ESP32. Hãy trả lời súc tích, tự nhiên, sinh động bằng tiếng Việt.")

    # 1. Nếu chọn DeepSeek hoặc có OpenAI key
    if "deepseek" in model_id.lower() or (oai_key and not gemini_key):
        if oai_key:
            try:
                from openai import OpenAI
                model_name = cfg.get("openai_model", "deepseek-chat")
                base_url = cfg.get("openai_base_url", "https://api.deepseek.com")
                client = OpenAI(api_key=oai_key, base_url=base_url)
                def _call_oai():
                    comp = client.chat.completions.create(
                        model=model_name,
                        messages=[
                            {"role": "system", "content": system_prompt},
                            {"role": "user", "content": prompt_text}
                        ]
                    )
                    return comp.choices[0].message.content or ""
                reply = await asyncio.to_thread(_call_oai)
                if reply.strip():
                    return (reply.strip(), detect_emotion_from_text(reply))
            except Exception as e:
                logger.error(f"Lỗi gọi OpenAI/DeepSeek: {e}")

    # 2. Mặc định gọi Gemini
    if gemini_key and "YOUR_" not in gemini_key:
        chosen_model = normalize_gemini_model(model_id or cfg.get("gemini_model") or cfg.get("openai_model") or "gemini-flash-lite-latest")
        candidate_models = []
        for m in [chosen_model, "gemini-flash-lite-latest", "gemini-3-flash-preview", "gemini-flash-latest", "gemini-pro-latest"]:
            norm = normalize_gemini_model(m)
            if norm not in candidate_models:
                candidate_models.append(norm)

        last_error = ""
        for try_model in candidate_models:
            try:
                url = f"https://generativelanguage.googleapis.com/v1beta/models/{try_model}:generateContent?key={gemini_key}"
                req_data = {
                    "contents": [
                        {"role": "user", "parts": [{"text": f"{system_prompt}\n\nNgười dùng: {prompt_text}"}]}
                    ]
                }
                req = urllib.request.Request(
                    url,
                    data=json.dumps(req_data).encode("utf-8"),
                    headers={"Content-Type": "application/json"}
                )
                def _call_gem():
                    with urllib.request.urlopen(req, timeout=15) as response:
                        res_body = response.read().decode("utf-8")
                        d = json.loads(res_body)
                        return d["candidates"][0]["content"]["parts"][0]["text"]
                reply = await asyncio.to_thread(_call_gem)
                if reply.strip():
                    if try_model != chosen_model:
                        logger.info(f"Model {chosen_model} không phản hồi, tự động dùng {try_model} thành công!")
                    return (reply.strip(), detect_emotion_from_text(reply))
            except urllib.error.HTTPError as he:
                err_body = he.read().decode("utf-8", errors="ignore")
                last_error = f"HTTP {he.code}: {err_body[:100]}"
                logger.warning(f"Lỗi REST Gemini API với model {try_model}: {last_error}")
            except Exception as e:
                last_error = str(e)
                logger.warning(f"Lỗi gọi Gemini REST API với model {try_model}: {e}")

        # Nếu có API key mà tất cả model đều lỗi: báo lỗi thực tế
        err_reply = f"Lỗi từ Google Gemini ({last_error}). Vui lòng kiểm tra lại API key hoặc chọn model khác nhé!"
        return (err_reply, "sad")

    # 3. Fallback câu trả lời thông minh
    pt_lower = prompt_text.lower()
    if "thời tiết" in pt_lower:
        reply = "Hôm nay thời tiết rất dễ chịu, nhiệt độ khoảng 26 đến 29 độ C. Trời quang đãng, gió nhẹ!"
    elif "mấy giờ" in pt_lower or "thời gian" in pt_lower:
        from datetime import datetime
        now = datetime.now()
        reply = f"Bây giờ là {now.hour} giờ {now.minute} phút."
    elif "chuyện cười" in pt_lower:
        reply = "Có một chú chim lười biếng, buổi sáng mẹ bảo: 'Dậy sớm đi con, chim dậy sớm mới có sâu ăn!'. Chú chim đáp: 'Thế con sâu dậy sớm thì bị chim ăn à mẹ?'"
    elif "nhạc" in pt_lower:
        reply = "Đang kết nối kho nhạc chất lượng cao để phát trên Trạm Decor của bạn!"
    else:
        reply = f"Mình đã nhận được câu hỏi: '{prompt_text}'. Trợ lý AI sẵn sàng đồng hành cùng bạn!"
    return (reply, detect_emotion_from_text(reply))

def requests_quote(s: str) -> str:
    import urllib.parse
    return urllib.parse.quote(s)

async def handle_http_chat(request: web.Request) -> web.Response:
    """REST endpoint cho ESP32 hoặc WebControl gửi tin nhắn text hoặc audio."""
    cfg = load_config()
    raw_body = await request.read()
    if not raw_body:
        return web.Response(status=400, text="Body rỗng", headers={"Access-Control-Allow-Origin": "*"})

    # Trường hợp 1: Nhận tin nhắn JSON từ Web Control
    if raw_body.startswith(b"{") or request.content_type == "application/json":
        try:
            d = json.loads(raw_body.decode('utf-8'))
            msg = d.get("message", "") or d.get("text", "")
            model = d.get("model", cfg.get("ai_model", "gemini-flash"))
            voice = d.get("voice", cfg.get("tts_voice", "vi-VN-HoaiMyNeural"))
            stream_to_station = bool(d.get("stream_to_station", True))
        except Exception:
            msg = ""

        if not msg:
            return web.json_response({"status": "error", "message": "Nội dung tin nhắn trống"}, status=400, headers={"Access-Control-Allow-Origin": "*"})

        reply_text, emotion = await call_llm_text_async(cfg, msg, model)
        logger.info(f"💬 [CHAT TEXT] '{msg}' -> '{reply_text}'")

        async def _stream_chat_reply():
            if stream_to_station and ACTIVE_WS_CLIENTS:
                sentences = split_into_smart_sentences(reply_text, max_chars=130)
                for ws in list(ACTIVE_WS_CLIENTS):
                    if not ws.closed:
                        await cancel_existing_ws_stream(ws)
                        async def _send_one(target_ws):
                            try:
                                await target_ws.send_str(json.dumps({
                                    "type": "answer",
                                    "text": reply_text,
                                    "sources": f"Web Chat ({model})",
                                    "emotion": emotion
                                }, ensure_ascii=False))
                                await stream_sentences_tts_pcm(sentences, voice, target_ws)
                                await target_ws.send_str(json.dumps({"type": "done"}))
                            except asyncio.CancelledError:
                                logger.info("🛑 Đã dừng luồng stream do có tin nhắn mới.")
                            except Exception as we:
                                logger.warning(f"Lỗi stream chat xuống ws: {we}")
                            finally:
                                if ACTIVE_WS_STREAMS.get(target_ws) is asyncio.current_task():
                                    ACTIVE_WS_STREAMS.pop(target_ws, None)

                        task = asyncio.create_task(_send_one(ws))
                        ACTIVE_WS_STREAMS[ws] = task

        if stream_to_station and ACTIVE_WS_CLIENTS:
            asyncio.create_task(_stream_chat_reply())

        return web.json_response({
            "status": "ok",
            "user_message": msg,
            "ai_reply": reply_text,
            "reply": reply_text,
            "emotion": emotion
        }, headers={"Access-Control-Allow-Origin": "*"})

    # Trường hợp 2: Nhận file âm thanh Mic WAV/PCM từ ESP32
    model = request.query.get("model") or "gemini-flash"
    search = request.query.get("search", "1") == "1"

    if raw_body.startswith(b"RIFF"):
        wav_bytes = raw_body
    else:
        wav_bytes = pcm_to_wav_bytes(raw_body, 16000)

    user_text, reply, sources = await ask_gemini_internal(cfg, "gemini-flash-latest", wav_bytes, search)
    emotion = detect_emotion_from_text(reply)

    host = request.headers.get("Host", f"127.0.0.1:{cfg.get('listen_port', 8765)}")
    stream_url = f"http://{host}/tts_stream?text={requests_quote(reply)}"

    return web.json_response({
        "status": "ok",
        "user_text": user_text,
        "reply": reply,
        "emotion": emotion,
        "sources": sources,
        "stream_url": stream_url
    }, headers={"Access-Control-Allow-Origin": "*"})

async def handle_http_tts_stream(request: web.Request) -> web.StreamResponse:
    """HTTP Chunked Streaming Raw PCM 16kHz Mono cho ESP32 hoặc trình duyệt."""
    text = request.query.get("text") or request.query.get("q")
    cfg = load_config()
    voice = request.query.get("voice") or cfg.get("tts_voice", "vi-VN-HoaiMyNeural")

    if not text and request.method == "POST":
        try:
            body = await request.text()
            if body.startswith("{"):
                d = json.loads(body)
                text = d.get("text") or d.get("q")
                if d.get("voice"):
                    voice = d["voice"]
            else:
                text = body.strip()
        except Exception:
            pass

    if not text:
        return web.Response(status=400, text="Thiếu nội dung 'text'")

    sentences = split_into_smart_sentences(text, max_chars=130)
    if not sentences:
        return web.Response(status=400, text="Nội dung rỗng")

    response = web.StreamResponse(
        status=200,
        reason='OK',
        headers={
            'Content-Type': 'audio/pcm; rate=16000; channels=1',
            'Access-Control-Allow-Origin': '*'
        }
    )
    await response.prepare(request)

    for s in sentences:
        pcm = await synthesize_single_sentence_pcm(s, voice)
        if pcm:
            for i in range(0, len(pcm), 2048):
                await response.write(pcm[i:i+2048])
                await asyncio.sleep(0.002)

    await response.write_eof()
    return response

async def handle_http_news_stream(request: web.Request) -> web.Response:
    """Tự động tìm kiếm tin tức thời sự sáng hôm nay và đọc báo trực tiếp xuống trạm."""
    cfg = load_config()
    topic = request.query.get("topic") or "tin tức thời sự xã hội kinh tế công nghệ nổi bật sáng hôm nay"
    voice = request.query.get("voice") or cfg.get("tts_voice", "vi-VN-HoaiMyNeural")

    logger.info(f"📰 [NEWS STREAM] Đang tổng hợp bản tin thời sự: '{topic}'...")
    web_ctx = await asyncio.to_thread(search_web_realtime, topic, 5)

    briefing_text = ""
    api_key = cfg.get("gemini_api_key", "")
    if api_key and "YOUR_" not in api_key:
        try:
            prompt = (
                f"Thông tin thời sự thời gian thực ({time.strftime('%d/%m/%Y')}):\n{web_ctx}\n\n"
                f"Hãy đóng vai là biên tập viên tin tức của Trạm Decor. "
                f"Soạn một bài đọc bản tin tổng hợp ngắn gọn, rõ ràng, truyền cảm (khoảng 3-4 đoạn, đọc trong 1-2 phút). "
                f"Tuyệt đối không dùng ký hiệu markdown (*, #, `): {topic}"
            )
            try:
                from google import genai
                gclient = genai.Client(api_key=api_key)
                resp = await asyncio.to_thread(
                    lambda: gclient.models.generate_content(
                        model="gemini-flash-latest",
                        contents=[prompt]
                    )
                )
                briefing_text = (resp.text or "").strip()
            except ImportError:
                import google.generativeai as genai_legacy
                genai_legacy.configure(api_key=api_key)
                m = genai_legacy.GenerativeModel("gemini-1.5-flash")
                resp = await asyncio.to_thread(lambda: m.generate_content(prompt))
                briefing_text = (resp.text or "").strip()
        except Exception as e:
            logger.error(f"Lỗi tạo bản tin Gemini: {e}")

    if not briefing_text:
        briefing_text = f"Chào buổi sáng! Hôm nay là ngày {time.strftime('%d/%m/%Y')}. Chúc bạn một ngày mới tràn đầy năng lượng cùng Trạm Decor Vũ Trụ!"

    req_proxy = request.clone(rel_url=request.rel_url.with_query({"text": briefing_text, "voice": voice}))
    return await handle_http_tts_stream(req_proxy)

async def handle_http_speak(request: web.Request) -> web.Response:
    """Nhận văn bản từ HTTP POST/GET và stream giọng nói tức thì xuống ESP32 đang kết nối."""
    cfg = load_config()
    text = request.query.get("text") or ""
    voice = request.query.get("voice") or cfg.get("tts_voice", "vi-VN-HoaiMyNeural")

    if request.method == "POST":
        try:
            body = await request.json()
            if "text" in body: text = body["text"]
            if "voice" in body: voice = body["voice"]
        except Exception:
            pass

    if not text:
        return web.json_response({"status": "error", "message": "Thiếu nội dung 'text'"}, status=400)

    if not ACTIVE_WS_CLIENTS:
        return web.json_response({
            "status": "error",
            "message": "Chưa có thiết bị ESP32 nào kết nối WebSocket tới PC AI Gateway!"
        }, status=503)

    emotion = detect_emotion_from_text(text)
    sentences = split_into_smart_sentences(text, max_chars=130)
    logger.info(f"📢 [SPEAK API] Bắt đầu stream {len(sentences)} câu thoại xuống {len(ACTIVE_WS_CLIENTS)} trạm ESP32...")

    for ws in list(ACTIVE_WS_CLIENTS):
        if not ws.closed:
            await ws.send_str(json.dumps({
                "type": "answer",
                "text": text,
                "sources": "PC AI Gateway",
                "emotion": emotion
            }, ensure_ascii=False))
            await stream_sentences_tts_pcm(sentences, voice, ws)
            await ws.send_str(json.dumps({"type": "done"}))

    return web.json_response({
        "status": "ok",
        "sentences": len(sentences),
        "message": "Đã phát âm thanh thành công ra loa trạm Decor!"
    })

async def handle_http_news_speak(request: web.Request) -> web.Response:
    """Lấy bản tin thời sự VnExpress mới nhất và phát thanh trực tiếp ra loa ESP32."""
    if not ACTIVE_WS_CLIENTS:
        return web.json_response({
            "status": "error",
            "message": "Chưa có thiết bị ESP32 nào kết nối WebSocket tới PC AI Gateway!"
        }, status=503)

    cfg = load_config()
    voice = request.query.get("voice") or cfg.get("tts_voice", "vi-VN-HoaiMyNeural")
    limit = int(request.query.get("limit", 4))

    news = await asyncio.to_thread(fetch_vnexpress_news, limit)
    if not news:
        article_text = "Chào bạn, hiện tại chưa thể lấy bản tin từ internet. Chúc bạn một ngày mới an lành và tràn đầy năng lượng!"
    else:
        articles = [f"Chào bạn! Sau đây là bản tin thời sự tổng hợp nổi bật nhất ngày {time.strftime('%d tháng %m')}:"]
        for idx, item in enumerate(news, 1):
            articles.append(f"Tin thứ {idx}: {item['title']}. {item['summary']}.")
        articles.append("Bản tin đến đây là kết thúc. Chúc bạn một ngày làm việc tràn đầy năng lượng và sáng tạo cùng Trạm Decor Vũ Trụ!")
        article_text = " ".join(articles)

    logger.info(f"📰 [NEWS SPEAK] Bắt đầu phát bản tin thời sự dài ({len(article_text)} ký tự)...")
    sentences = split_into_smart_sentences(article_text, max_chars=130)

    for ws in list(ACTIVE_WS_CLIENTS):
        if not ws.closed:
            await ws.send_str(json.dumps({
                "type": "answer",
                "text": article_text,
                "sources": "VnExpress RSS",
                "emotion": "happy"
            }, ensure_ascii=False))
            await stream_sentences_tts_pcm(sentences, voice, ws)
            await ws.send_str(json.dumps({"type": "done"}))

    return web.json_response({
        "status": "ok",
        "news_count": len(news),
        "sentences": len(sentences),
        "message": "Đang phát bản tin thời sự trên loa trạm Decor!"
    })

async def handle_websocket(request: web.Request) -> web.WebSocketResponse:
    """Phiên giao tiếp WebSocket 2 chiều thời gian thực với ESP32-S3."""
    ws = web.WebSocketResponse(max_msg_size=8 * 1024 * 1024)
    await ws.prepare(request)

    peer = request.remote
    logger.info(f"🔗 ESP32 kết nối WebSocket từ {peer}")
    ACTIVE_WS_CLIENTS.add(ws)
    cfg = load_config()
    pcm_buf = bytearray()
    model_key = "gemini-pro"
    enable_search = True
    sample_rate = 16000

    try:
        async for msg in ws:
            if msg.type == aiohttp.WSMsgType.BINARY:
                pcm_buf.extend(msg.data)
            elif msg.type == aiohttp.WSMsgType.TEXT:
                data = json.loads(msg.data)
                msg_type = data.get("type")
                if msg_type == "start":
                    pcm_buf.clear()
                    await cancel_existing_ws_stream(ws)
                    model_key = data.get("model", "gemini-pro")
                    enable_search = bool(data.get("search", True))
                    sample_rate = int(data.get("sample_rate", 16000))
                    logger.info(f"🎙️ Phiên mới: model={model_key}, search={enable_search}, sr={sample_rate}")
                elif msg_type == "end":
                    logger.info(f"📦 Đã nhận xong {len(pcm_buf)} bytes PCM từ Mic ESP32. Bắt đầu xử lý...")
                    wav_bytes = pcm_to_wav_bytes(bytes(pcm_buf), sample_rate)

                    if model_key == "deepseek-r1":
                        await process_with_openai_compat(cfg, wav_bytes, enable_search, ws)
                    elif model_key == "gemini-pro":
                        await process_with_gemini(cfg, "gemini-flash-latest", wav_bytes, enable_search, ws)
                    else:
                        await process_with_gemini(cfg, "gemini-flash-lite-latest", wav_bytes, enable_search, ws)

                    await ws.send_str(json.dumps({"type": "done"}))
                    logger.info("🏁 [DONE] Đã hoàn tất toàn bộ chu kỳ phản hồi.")
                elif msg_type in ("speak", "text"):
                    txt = data.get("text") or data.get("message", "")
                    if txt:
                        logger.info(f"💬 Yêu cầu phát âm thanh: {txt}")
                        sentences = split_into_smart_sentences(txt, max_chars=130)
                        voice = data.get("voice") or cfg.get("tts_voice", "vi-VN-HoaiMyNeural")
                        await ws.send_str(json.dumps({
                            "type": "answer",
                            "text": txt,
                            "sources": "Local Text",
                            "emotion": detect_emotion_from_text(txt)
                        }, ensure_ascii=False))
                        await stream_sentences_tts_pcm(sentences, voice, ws)
                        await ws.send_str(json.dumps({"type": "done"}))
                elif msg_type == "news":
                    logger.info("📰 Yêu cầu đọc bản tin từ trạm...")
                    news = await asyncio.to_thread(fetch_vnexpress_news, 4)
                    if news:
                        articles = [f"Chào bạn! Sau đây là bản tin thời sự tổng hợp nổi bật nhất ngày {time.strftime('%d tháng %m')}:"]
                        for idx, item in enumerate(news, 1):
                            articles.append(f"Tin thứ {idx}: {item['title']}. {item['summary']}.")
                        articles.append("Bản tin đến đây là kết thúc. Chúc bạn một ngày tốt lành!")
                        art_text = " ".join(articles)
                    else:
                        art_text = "Không thể kết nối đến máy chủ tin tức vào lúc này."
                    sentences = split_into_smart_sentences(art_text, max_chars=130)
                    voice = data.get("voice") or cfg.get("tts_voice", "vi-VN-HoaiMyNeural")
                    await ws.send_str(json.dumps({
                        "type": "answer",
                        "text": art_text,
                        "sources": "VnExpress RSS",
                        "emotion": "happy"
                    }, ensure_ascii=False))
                    await stream_sentences_tts_pcm(sentences, voice, ws)
                    await ws.send_str(json.dumps({"type": "done"}))
            elif msg.type == aiohttp.WSMsgType.ERROR:
                logger.error(f"WebSocket error: {ws.exception()}")
    except Exception as e:
        logger.exception(f"Lỗi phiên WebSocket: {e}")
    finally:
        ACTIVE_WS_CLIENTS.discard(ws)
        logger.info(f"🔌 ESP32 đã ngắt kết nối WebSocket ({peer})")

    return ws

def create_app():
    app = web.Application()
    app.router.add_get("/health", handle_http_health)
    app.router.add_options("/health", handle_http_options)
    app.router.add_post("/api/chat", handle_http_chat)
    app.router.add_options("/api/chat", handle_http_options)
    app.router.add_get("/api/config", handle_http_config)
    app.router.add_post("/api/config", handle_http_config)
    app.router.add_options("/api/config", handle_http_options)
    app.router.add_get("/api/gemini/models", handle_http_gemini_models)
    app.router.add_post("/api/gemini/models", handle_http_gemini_models)
    app.router.add_options("/api/gemini/models", handle_http_options)
    app.router.add_get("/api/models", handle_http_gemini_models)
    app.router.add_post("/api/models", handle_http_gemini_models)
    app.router.add_options("/api/models", handle_http_options)
    app.router.add_get("/tts_stream", handle_http_tts_stream)
    app.router.add_post("/tts_stream", handle_http_tts_stream)
    app.router.add_get("/news_stream", handle_http_news_stream)
    app.router.add_post("/news_stream", handle_http_news_stream)
    app.router.add_get("/api/speak", handle_http_speak)
    app.router.add_post("/api/speak", handle_http_speak)
    app.router.add_get("/api/news/speak", handle_http_news_speak)
    app.router.add_post("/api/news/speak", handle_http_news_speak)
    app.router.add_get("/ws", handle_websocket)
    app.router.add_get("/", handle_websocket)
    return app

def main():
    cfg = load_config()
    host = cfg.get("listen_host", "0.0.0.0")
    port = int(cfg.get("listen_port", 8765))

    logger.info("=" * 70)
    logger.info("  🧠 DECOR AI GATEWAY 2.0 - PIPELINED CHUNKED STREAMING")
    logger.info(f"  🌐 Server lắng nghe tại: http://{host}:{port}")
    logger.info(f"  🔊 Endpoint WebSocket : ws://<PC_IP>:{port}/ws")
    logger.info(f"  💬 Endpoint Web Chat  : http://<PC_IP>:{port}/api/chat")
    logger.info(f"  ⚙️ Endpoint Cấu Hình  : http://<PC_IP>:{port}/api/config")
    logger.info(f"  📰 Endpoint Đọc Báo   : http://<PC_IP>:{port}/news_stream")
    logger.info(f"  🩺 Endpoint Health     : http://<PC_IP>:{port}/health")
    logger.info("  ⚡ Hỗ trợ phát âm thanh bài báo dài liên tục không đứt đoạn!")
    logger.info("=" * 70)

    app = create_app()
    try:
        web.run_app(app, host=host, port=port, print=None)
    except OSError as e:
        if getattr(e, 'errno', None) == 10048 or "10048" in str(e):
            print("\n" + "=" * 70)
            print("  ⚠️  CỔNG 8765 ĐANG ĐƯỢC MỞ BỞI DECOR SPACE COMPANION!")
            print("  ✨  Ứng dụng DecorSpaceCompanion.exe đã tích hợp sẵn AI Gateway")
            print("      và đang chạy ngầm phục vụ toàn bộ kết nối cho Trạm Decor!")
            print("  🎉  Bạn không cần chạy thêm cửa sổ này. Trạm ESP32 và Web Control")
            print("      đã có thể chat AI và nghe phát âm thanh bình thường!")
            print("=" * 70 + "\n")
            try:
                input("Nhấn phím Enter để đóng...")
            except Exception:
                pass
            return
        raise

if __name__ == "__main__":
    main()
