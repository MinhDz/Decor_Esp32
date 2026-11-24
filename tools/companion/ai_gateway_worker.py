#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - AI Gateway & Audio Streaming Worker
Tích hợp toàn diện mô hình AI (Gemini, DeepSeek), Microsoft Edge Neural TTS
và kiến trúc Pipelined Chunked Streaming vào Decor Space Companion.
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
import threading
import subprocess
import urllib.request
import urllib.parse
import xml.etree.ElementTree as ET
from pathlib import Path

from aiohttp import web
import aiohttp

logger = logging.getLogger("AiGatewayWorker")

# Tìm đường dẫn FFmpeg thực thi
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

# Bộ nhớ đệm PCM cho các câu phổ biến
PCM_CACHE: dict = {}
MAX_CACHE_ENTRIES = 120

# Danh sách kết nối WebSocket thời gian thực từ ESP32
ACTIVE_WS_CLIENTS = set()

def pcm_to_wav_bytes(pcm_bytes: bytes, sample_rate: int = 16000) -> bytes:
    """Đóng gói dữ liệu PCM thô 16-bit Mono thành tệp WAV hợp lệ."""
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
    """Xác định cảm xúc khuôn mặt hiển thị trên trạm."""
    t = text.lower()
    if any(k in t for k in ["😂", "🤣", "haha", "buồn cười", "hài hước", "truyện cười", "[laugh]"]):
        return "laughing"
    if any(k in t for k in ["🥰", "❤️", "yêu quý", "dễ thương", "đáng yêu", "[love]"]):
        return "loving"
    if any(k in t for k in ["😘", "hôn", "[kiss]"]):
        return "kissy"
    if any(k in t for k in ["😋", "ngon", "thơm ngon", "ẩm thực", "món ăn"]):
        return "delicious"
    if any(k in t for k in ["😡", "tức giận", "bực mình", "khó chịu", "bất mãn", "[angry]"]):
        return "angry"
    if any(k in t for k in ["😭", "buồn", "tiếc nuối", "đau lòng", "thất vọng", "[cry]", "[sad]"]):
        return "sad"
    if any(k in t for k in ["😱", "bất ngờ", "ngạc nhiên", "kinh ngạc", "thật sao", "ồ"]):
        return "surprised"
    if any(k in t for k in ["😴", "chúc ngủ ngon", "ngủ ngon", "mệt mỏi", "buồn ngủ"]):
        return "sleepy"
    if any(k in t for k in ["🤔", "suy nghĩ", "tính toán", "phân tích", "để xem"]):
        return "thinking"
    if any(k in t for k in ["😉", "[wink]"]):
        return "winking"
    return "happy"

def fetch_google_tts_mp3(text: str) -> bytes:
    """Lấy âm thanh Google Translate TTS miễn phí siêu tốc (<0.4s)."""
    url = f"https://translate.google.com/translate_tts?ie=UTF-8&q={urllib.parse.quote(text)}&tl=vi&client=tw-ob"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"})
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
    """Sinh âm thanh cho 1 câu duy nhất với bộ nhớ đệm RAM."""
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
    req = urllib.request.Request(url, headers={"User-Agent": "DecorSpaceCompanion/2.0"})
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
        return ("", "Xin chào! Tôi là Trợ lý AI Trạm Decor chạy trên Companion. Hệ thống âm thanh đã đồng bộ sẵn sàng! Để đàm thoại chuyên sâu hơn, bạn chỉ cần điền Gemini API Key trong Cài đặt nhé!", "")

    chosen_model = normalize_gemini_model(model_id or cfg.get("gemini_model") or "gemini-flash-lite-latest")
    candidate_models = []
    for m in [chosen_model, "gemini-flash-lite-latest", "gemini-3-flash-preview", "gemini-flash-latest"]:
        norm = normalize_gemini_model(m)
        if norm not in candidate_models:
            candidate_models.append(norm)

    import base64
    audio_b64 = base64.b64encode(wav_bytes).decode("ascii")
    system_inst = cfg.get("system_prompt", "Bạn là Trợ lý AI Trạm Decor Vũ Trụ. Hãy nghe âm thanh câu hỏi và trả lời chi tiết, chính xác bằng tiếng Việt.")
    
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

async def process_with_gemini(cfg: dict, model_id: str, wav_bytes: bytes, enable_search: bool, ws: web.WebSocketResponse):
    """Xử lý âm thanh hỏi đáp bằng Google Gemini."""
    voice = cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
    await ws.send_str(json.dumps({"type": "thinking", "text": f"{model_id} đang nghe và suy luận..."}))

    _, reply, sources = await ask_gemini_internal(cfg, model_id, wav_bytes, enable_search)
    logger.info(f"🤖 [{model_id}] Phản hồi: {reply}")
    emotion = detect_emotion_from_text(reply)
    await ws.send_str(json.dumps({"type": "answer", "text": reply, "sources": sources, "emotion": emotion}, ensure_ascii=False))

    sentences = split_into_smart_sentences(reply, max_chars=130)
    logger.info(f"⚡ [STREAMING] Đã chia thành {len(sentences)} câu. Bắt đầu Pipelined TTS...")
    await stream_sentences_tts_pcm(sentences, voice, ws)

async def process_with_openai_compat(cfg: dict, wav_bytes: bytes, enable_search: bool, ws: web.WebSocketResponse):
    """Xử lý với mô hình OpenAI Compatible (DeepSeek-R1, v.v.)."""
    from openai import OpenAI

    oai_key = cfg.get("openai_api_key", "")
    if not oai_key or "YOUR_" in oai_key:
        reply = "Bạn đang chọn mô hình DeepSeek R1 nhưng chưa điền API Key trong Cài đặt của Companion."
        sentences = split_into_smart_sentences(reply)
        voice = cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
        await ws.send_str(json.dumps({"type": "answer", "text": reply, "sources": "Local", "emotion": "confused"}, ensure_ascii=False))
        await stream_sentences_tts_pcm(sentences, voice, ws)
        return

    user_text = ""
    gemini_key = cfg.get("gemini_api_key", "")
    if gemini_key and "YOUR_" not in gemini_key:
        try:
            from google import genai
            from google.genai import types
            gclient = genai.Client(api_key=gemini_key)
            audio_part = types.Part.from_bytes(data=wav_bytes, mime_type="audio/wav")
            stt_resp = await asyncio.to_thread(
                lambda: gclient.models.generate_content(
                    model="gemini-flash-lite-latest",
                    contents=[audio_part, "Chỉ xuất ra câu tiếng Việt người dùng vừa nói, không thêm lời bình."]
                )
            )
            user_text = (stt_resp.text or "").strip()
        except Exception:
            pass

    if not user_text:
        user_text = "Hôm nay thời tiết và tin tức thời sự thế nào?"

    logger.info(f"🗣️ [STT] Người dùng: {user_text}")
    await ws.send_str(json.dumps({"type": "stt", "text": user_text}, ensure_ascii=False))

    web_ctx = ""
    if enable_search and user_text:
        await ws.send_str(json.dumps({"type": "thinking", "text": "Đang tìm kiếm thông tin thời gian thực..."}))
        web_ctx = await asyncio.to_thread(search_web_realtime, user_text)

    model_name = cfg.get("openai_model", "deepseek-reasoner")
    base_url = cfg.get("openai_base_url", "https://api.deepseek.com")
    await ws.send_str(json.dumps({"type": "thinking", "text": f"{model_name} đang suy luận..."}))

    oai_client = OpenAI(api_key=oai_key, base_url=base_url)
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

    voice = cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
    sentences = split_into_smart_sentences(reply, max_chars=130)
    await stream_sentences_tts_pcm(sentences, voice, ws)

async def call_llm_text_async(cfg: dict, prompt_text: str, model_id: str = "gemini-pro") -> tuple[str, str]:
    """
    Gọi mô hình LLM từ text thuần (Chat WebControl / Companion Chat).
    Trả về: (reply_text, emotion)
    """
    gemini_key = cfg.get("gemini_api_key", "").strip()
    oai_key = cfg.get("openai_api_key", "").strip()
    system_prompt = cfg.get("system_prompt", "Bạn là Trợ lý AI trên Trạm Decor ESP32. Hãy trả lời súc tích, tự nhiên, sinh động bằng tiếng Việt.")

    # 1. Nếu chọn DeepSeek hoặc có OpenAI key
    if model_id == "deepseek-r1" or (oai_key and not gemini_key):
        if oai_key:
            try:
                from openai import OpenAI
                model_name = cfg.get("openai_model", "deepseek-reasoner")
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
                def _call_gemini_rest():
                    with urllib.request.urlopen(req, timeout=18) as res:
                        return json.loads(res.read().decode("utf-8"))
                resp_json = await asyncio.to_thread(_call_gemini_rest)
                candidates = resp_json.get("candidates", [])
                if candidates:
                    parts = candidates[0].get("content", {}).get("parts", [])
                    if parts:
                        reply = parts[0].get("text", "").strip()
                        if reply:
                            if try_model != chosen_model:
                                logger.info(f"Model {chosen_model} không phản hồi, tự động dùng {try_model} thành công!")
                            return (reply, detect_emotion_from_text(reply))
            except urllib.error.HTTPError as he:
                err_body = he.read().decode("utf-8", errors="ignore")
                last_error = f"HTTP {he.code}: {err_body[:100]}"
                logger.warning(f"Lỗi REST Gemini API với model {try_model}: {last_error}")
            except Exception as e:
                last_error = str(e)
                logger.warning(f"Lỗi REST Gemini API với model {try_model}: {e}")

        # Thử fallback bằng SDK nếu có
        try:
            from google import genai
            gclient = genai.Client(api_key=gemini_key)
            gresp = await asyncio.to_thread(
                lambda: gclient.models.generate_content(
                    model=candidate_models[0],
                    contents=[f"{system_prompt}\n\nNgười dùng: {prompt_text}"]
                )
            )
            reply = (gresp.text or "").strip()
            if reply:
                return (reply, detect_emotion_from_text(reply))
        except Exception as e2:
            logger.error(f"Lỗi google.genai SDK: {e2}")

        # Nếu có API key mà tất cả model đều lỗi: báo lỗi thực tế
        err_reply = f"Lỗi từ Google Gemini ({last_error}). Vui lòng kiểm tra lại API key hoặc chọn model khác nhé!"
        return (err_reply, "sad")

    # 3. Fallback câu trả lời thông minh nếu chưa có key
    fallback_reply = (
        f"Xin chào! Tôi đã nhận được tin nhắn: '{prompt_text}'. "
        "Tôi là Trợ lý AI trên Trạm Decor ESP32. "
        "Để tôi có thể trả lời thông minh mọi câu hỏi của bạn, hãy nhập Gemini hoặc DeepSeek API Key trong menu Cài đặt Decor Space Companion nhé!"
    )
    return (fallback_reply, "happy")

# ================= HTTP & WEBSOCKET HANDLERS =================

class AiGatewayServer:
    def __init__(self, cfg_getter, cfg_setter=None):
        self.cfg_getter = cfg_getter
        self.cfg_setter = cfg_setter

    def get_cfg(self):
        return self.cfg_getter()

    async def handle_http_config(self, request: web.Request) -> web.Response:
        cfg = self.get_cfg()
        if request.method == "POST":
            try:
                body = await request.json()
                changed = False
                for k in ["gemini_api_key", "openai_api_key", "openai_base_url", "openai_model", "gemini_model", "ai_gateway_voice"]:
                    if k in body and body[k] is not None:
                        val = str(body[k]).strip()
                        if val != "" or k in ["openai_base_url", "openai_model", "gemini_model", "ai_gateway_voice"]:
                            cfg[k] = val
                            changed = True
                if changed and self.cfg_setter and callable(self.cfg_setter):
                    self.cfg_setter(cfg)
                return web.json_response({
                    "status": "ok",
                    "message": "Đã lưu cấu hình API Key vào Decor Space Companion thành công!",
                    "has_gemini": bool(cfg.get("gemini_api_key")),
                    "gemini_model": cfg.get("gemini_model", "gemini-flash-lite-latest"),
                    "has_deepseek": bool(cfg.get("openai_api_key")),
                    "voice": cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
                }, headers={"Access-Control-Allow-Origin": "*"})
            except Exception as e:
                return web.json_response({"status": "error", "message": str(e)}, status=400, headers={"Access-Control-Allow-Origin": "*"})
        
        g_key = cfg.get("gemini_api_key", "")
        o_key = cfg.get("openai_api_key", "")
        return web.json_response({
            "status": "ok",
            "has_gemini": bool(g_key),
            "gemini_masked": (g_key[:4] + "..." + g_key[-4:]) if len(g_key) > 8 else ("***" if g_key else ""),
            "gemini_model": cfg.get("gemini_model", "gemini-flash-lite-latest"),
            "has_deepseek": bool(o_key),
            "deepseek_masked": (o_key[:4] + "..." + o_key[-4:]) if len(o_key) > 8 else ("***" if o_key else ""),
            "openai_base_url": cfg.get("openai_base_url", "https://api.deepseek.com"),
            "openai_model": cfg.get("openai_model", "gemini-flash-lite-latest"),
            "voice": cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
        }, headers={"Access-Control-Allow-Origin": "*"})

    async def handle_http_gemini_models(self, request: web.Request) -> web.Response:
        key = request.query.get("key", "").strip()
        if not key and request.method == "POST":
            try:
                body = await request.json()
                key = body.get("key", "").strip()
            except Exception:
                pass
        if not key:
            cfg = self.get_cfg()
            key = cfg.get("gemini_api_key", "").strip()

        if not key:
            return web.json_response({
                "status": "error",
                "message": "Chưa có Gemini API Key. Vui lòng nhập API Key để kiểm tra!"
            }, status=400, headers={"Access-Control-Allow-Origin": "*", "Access-Control-Allow-Private-Network": "true"})

        data = await asyncio.to_thread(fetch_and_test_gemini_models, key)
        return web.json_response(data, headers={"Access-Control-Allow-Origin": "*", "Access-Control-Allow-Private-Network": "true"})

    async def handle_http_health(self, request: web.Request) -> web.Response:
        cfg = self.get_cfg()
        return web.json_response({
            "status": "ok",
            "gateway": "active",
            "service": "DecorSpaceCompanion",
            "version": "2.0-unified",
            "edge_tts": True,
            "default_voice": cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural"),
            "models": ["gemini-pro", "gemini-flash", "deepseek-r1"]
        }, headers={
            "Access-Control-Allow-Origin": "*",
            "Access-Control-Allow-Private-Network": "true"
        })

    async def handle_http_speak(self, request: web.Request) -> web.Response:
        cfg = self.get_cfg()
        text = request.query.get("text") or ""
        voice = request.query.get("voice") or cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")

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
                "message": "Chưa có thiết bị ESP32 nào kết nối WebSocket tới AI Gateway!"
            }, status=503)

        emotion = detect_emotion_from_text(text)
        sentences = split_into_smart_sentences(text, max_chars=130)
        logger.info(f"📢 [SPEAK API] Bắt đầu stream {len(sentences)} câu thoại xuống {len(ACTIVE_WS_CLIENTS)} trạm ESP32...")

        for ws in list(ACTIVE_WS_CLIENTS):
            if not ws.closed:
                await ws.send_str(json.dumps({
                    "type": "answer",
                    "text": text,
                    "sources": "PC Companion Gateway",
                    "emotion": emotion
                }, ensure_ascii=False))
                await stream_sentences_tts_pcm(sentences, voice, ws)
                await ws.send_str(json.dumps({"type": "done"}))

        return web.json_response({
            "status": "ok",
            "sentences": len(sentences),
            "message": "Đã phát âm thanh thành công ra loa trạm Decor!"
        })

    async def handle_http_news_speak(self, request: web.Request) -> web.Response:
        if not ACTIVE_WS_CLIENTS:
            return web.json_response({
                "status": "error",
                "message": "Chưa có thiết bị ESP32 nào kết nối WebSocket tới AI Gateway!"
            }, status=503)

        cfg = self.get_cfg()
        voice = request.query.get("voice") or cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
        limit = int(request.query.get("limit", 4))

        news = await asyncio.to_thread(fetch_vnexpress_news, limit)
        if not news:
            article_text = "Chào bạn, hiện tại chưa thể lấy bản tin từ internet. Chúc bạn một ngày mới tràn đầy niềm vui!"
        else:
            articles = [f"Chào bạn! Sau đây là bản tin thời sự tổng hợp nổi bật nhất ngày {time.strftime('%d tháng %m')}:"]
            for idx, item in enumerate(news, 1):
                articles.append(f"Tin thứ {idx}: {item['title']}. {item['summary']}.")
            articles.append("Bản tin đến đây là kết thúc. Chúc bạn một ngày làm việc tràn đầy năng lượng cùng Trạm Decor Vũ Trụ!")
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

    async def handle_http_options(self, request: web.Request) -> web.Response:
        return web.Response(status=204, headers={
            "Access-Control-Allow-Origin": "*",
            "Access-Control-Allow-Methods": "POST, GET, OPTIONS, HEAD",
            "Access-Control-Allow-Headers": "Content-Type, Authorization, X-Requested-With, *",
            "Access-Control-Allow-Private-Network": "true"
        })

    async def handle_http_chat(self, request: web.Request) -> web.Response:
        cfg = self.get_cfg()
        message = ""
        model = cfg.get("openai_model", "gemini-pro")
        voice = cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
        stream_to_station = True

        if request.method == "POST":
            try:
                body = await request.json()
                message = body.get("message", "") or body.get("text", "")
                if "model" in body: model = body["model"]
                if "voice" in body: voice = body["voice"]
                if "stream_to_station" in body: stream_to_station = bool(body["stream_to_station"])
            except Exception:
                pass
        else:
            message = request.query.get("message") or request.query.get("text") or ""

        if not message.strip():
            return web.json_response({
                "status": "error",
                "message": "Vui lòng nhập nội dung 'message'!"
            }, status=400, headers={"Access-Control-Allow-Origin": "*"})

        logger.info(f"💬 [WEB CHAT] Nhận tin nhắn: {message}")
        reply_text, emotion = await call_llm_text_async(cfg, message, model)
        logger.info(f"🤖 [WEB CHAT] Phản hồi: {reply_text} (Emotion: {emotion})")

        streamed = False
        if stream_to_station and ACTIVE_WS_CLIENTS:
            sentences = split_into_smart_sentences(reply_text, max_chars=130)
            logger.info(f"📢 [WEB CHAT] Bắt đầu stream câu trả lời ({len(sentences)} câu) xuống {len(ACTIVE_WS_CLIENTS)} trạm ESP32...")

            async def _bg_stream_tts(ws_list, s_list, v_code, ans_text, emo):
                for ws in ws_list:
                    if not ws.closed:
                        await cancel_existing_ws_stream(ws)
                        async def _send_one(target_ws):
                            try:
                                await target_ws.send_str(json.dumps({
                                    "type": "answer",
                                    "text": ans_text,
                                    "sources": "Web Chat (" + model + ")",
                                    "emotion": emo
                                }, ensure_ascii=False))
                                await stream_sentences_tts_pcm(s_list, v_code, target_ws)
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

            asyncio.create_task(_bg_stream_tts(list(ACTIVE_WS_CLIENTS), sentences, voice, reply_text, emotion))
            streamed = True

        return web.json_response({
            "status": "ok",
            "user_message": message,
            "ai_reply": reply_text,
            "emotion": emotion,
            "streamed_to_station": streamed,
            "active_clients": len(ACTIVE_WS_CLIENTS)
        }, headers={"Access-Control-Allow-Origin": "*"})

    async def handle_websocket(self, request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse(max_msg_size=8 * 1024 * 1024)
        await ws.prepare(request)

        peer = request.remote
        logger.info(f"🔗 ESP32 kết nối WebSocket từ {peer}")
        ACTIVE_WS_CLIENTS.add(ws)
        cfg = self.get_cfg()
        pcm_buf = bytearray()
        model_key = "gemini-pro"
        enable_search = True
        sample_rate = 16000
        is_mic_test = False
        rec_filename = ""

        try:
            async for msg in ws:
                if msg.type == aiohttp.WSMsgType.BINARY:
                    pcm_buf.extend(msg.data)
                elif msg.type == aiohttp.WSMsgType.TEXT:
                    data = json.loads(msg.data)
                    msg_type = data.get("type")
                    if msg_type in ("start", "mic_record"):
                        pcm_buf.clear()
                        await cancel_existing_ws_stream(ws)
                        is_mic_test = (msg_type == "mic_record")
                        rec_filename = data.get("filename", "")
                        model_key = data.get("model", "gemini-pro")
                        enable_search = bool(data.get("search", True))
                        sample_rate = int(data.get("sample_rate", 16000))
                        logger.info(f"🎙️ Phiên mới: type={msg_type}, model={model_key}, search={enable_search}, sr={sample_rate}")
                    elif msg_type == "end":
                        logger.info(f"📦 Đã nhận xong {len(pcm_buf)} bytes PCM từ Mic ESP32. Bắt đầu xử lý...")
                        wav_bytes = pcm_to_wav_bytes(bytes(pcm_buf), sample_rate)

                        if is_mic_test:
                            rec_dir = Path(__file__).resolve().parent.parent / "gateway" / "recordings"
                            rec_dir.mkdir(parents=True, exist_ok=True)
                            save_name = Path(rec_filename).name if rec_filename else f"rec_{time.strftime('%Hh%Mm%Ss_%d_%m')}.wav"
                            target_file = rec_dir / save_name
                            target_file.write_bytes(wav_bytes)
                            (rec_dir / "rec_latest.wav").write_bytes(wav_bytes)
                            logger.info(f"🎉 [MIC TEST] Đã lưu tệp vào PC: {target_file}")
                            try:
                                import winsound
                                winsound.PlaySound(str(target_file), winsound.SND_FILENAME | winsound.SND_ASYNC)
                                logger.info(f"🔊 [PC SPEAKER] Đang tự động phát {save_name} ra loa máy tính...")
                            except Exception:
                                pass
                            await ws.send_str(json.dumps({"type": "done", "status": "ok", "filename": save_name}))
                            continue

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
                            voice = data.get("voice") or cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
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
                        voice = data.get("voice") or cfg.get("ai_gateway_voice", "vi-VN-HoaiMyNeural")
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

@web.middleware
async def cors_and_pna_middleware(request: web.Request, handler):
    if request.method == "OPTIONS":
        response = web.Response(status=204)
    else:
        try:
            response = await handler(request)
        except web.HTTPException as ex:
            response = ex
        except Exception as e:
            logger.error(f"Lỗi xử lý request {request.path}: {e}")
            response = web.json_response({"status": "error", "message": str(e)}, status=500)

    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS, HEAD, PUT, DELETE"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization, X-Requested-With, *"
    response.headers["Access-Control-Allow-Private-Network"] = "true"
    return response

def create_aiohttp_app(server_handler):
    app = web.Application(middlewares=[cors_and_pna_middleware])
    app.router.add_get("/health", server_handler.handle_http_health)
    app.router.add_options("/health", server_handler.handle_http_options)
    app.router.add_get("/api/speak", server_handler.handle_http_speak)
    app.router.add_post("/api/speak", server_handler.handle_http_speak)
    app.router.add_options("/api/speak", server_handler.handle_http_options)
    app.router.add_get("/api/news/speak", server_handler.handle_http_news_speak)
    app.router.add_post("/api/news/speak", server_handler.handle_http_news_speak)
    app.router.add_options("/api/news/speak", server_handler.handle_http_options)
    app.router.add_get("/api/chat", server_handler.handle_http_chat)
    app.router.add_post("/api/chat", server_handler.handle_http_chat)
    app.router.add_options("/api/chat", server_handler.handle_http_options)
    app.router.add_get("/chat", server_handler.handle_http_chat)
    app.router.add_post("/chat", server_handler.handle_http_chat)
    app.router.add_options("/chat", server_handler.handle_http_options)
    app.router.add_get("/api/config", server_handler.handle_http_config)
    app.router.add_post("/api/config", server_handler.handle_http_config)
    app.router.add_options("/api/config", server_handler.handle_http_options)
    app.router.add_get("/api/gemini/models", server_handler.handle_http_gemini_models)
    app.router.add_post("/api/gemini/models", server_handler.handle_http_gemini_models)
    app.router.add_options("/api/gemini/models", server_handler.handle_http_options)
    app.router.add_get("/api/models", server_handler.handle_http_gemini_models)
    app.router.add_post("/api/models", server_handler.handle_http_gemini_models)
    app.router.add_options("/api/models", server_handler.handle_http_options)
    app.router.add_get("/ws", server_handler.handle_websocket)
    app.router.add_get("/", server_handler.handle_websocket)
    return app

class AiGatewayWorker:
    """Worker quản lý máy chủ AI Gateway chạy ngầm bên trong DecorSpaceCompanion."""
    def __init__(self, cfg_manager, port=8765, host="0.0.0.0", enabled=True):
        self.cfg_manager = cfg_manager
        self.port = int(port)
        self.host = str(host)
        self.enabled = enabled
        self._is_running = False
        self.thread = None
        self.loop = None
        self.runner = None
        self.server_handler = AiGatewayServer(self.cfg_manager.load_config, self.cfg_manager.save_config)
        self._lock = threading.Lock()

    def is_running(self):
        return self._is_running

    def start(self):
        with self._lock:
            if self._is_running:
                return True

            def run_loop():
                self.loop = asyncio.new_event_loop()
                asyncio.set_event_loop(self.loop)
                app = create_aiohttp_app(self.server_handler)
                self.runner = web.AppRunner(app)
                try:
                    self.loop.run_until_complete(self.runner.setup())
                    site = web.TCPSite(self.runner, self.host, self.port)
                    self.loop.run_until_complete(site.start())
                    self._is_running = True
                    logger.info("=" * 65)
                    logger.info(f"  🧠 AI GATEWAY 2.0 ĐANG CHẠY TRONG COMPANION")
                    logger.info(f"  🌐 Server lắng nghe tại: http://{self.host}:{self.port}")
                    logger.info(f"  🔊 Endpoint WebSocket : ws://<PC_IP>:{self.port}/ws")
                    logger.info("=" * 65)
                    self.loop.run_forever()
                except OSError as oe:
                    logger.error(f"Không thể bind port {self.port} (Port có thể đang bị chiếm): {oe}")
                except Exception as e:
                    logger.error(f"Lỗi khởi chạy AI Gateway: {e}")
                finally:
                    self._is_running = False
                    if self.runner:
                        try:
                            self.loop.run_until_complete(self.runner.cleanup())
                        except Exception:
                            pass
                    try:
                        self.loop.close()
                    except Exception:
                        pass
                    logger.info("Máy chủ AI Gateway đã dừng.")

            self.thread = threading.Thread(target=run_loop, name="AiGatewayThread", daemon=True)
            self.thread.start()
            return True

    def stop(self):
        with self._lock:
            if not self._is_running:
                return
            logger.info("Đang dừng máy chủ AI Gateway...")
            if self.loop and self.loop.is_running():
                self.loop.call_soon_threadsafe(self.loop.stop)
            if self.thread and self.thread.is_alive():
                self.thread.join(timeout=2.0)
            self._is_running = False

    def restart(self, port=None):
        self.stop()
        if port:
            self.port = int(port)
        time.sleep(0.5)
        self.start()

    def trigger_news_broadcast(self, limit=4):
        """Kích hoạt phát thanh bản tin thời sự ra trạm từ Companion."""
        def task():
            try:
                url = f"http://127.0.0.1:{self.port}/api/news/speak?limit={limit}"
                req = urllib.request.Request(url)
                with urllib.request.urlopen(req, timeout=120) as res:
                    logger.info(f"Kết quả broadcast: {res.read().decode()}")
            except Exception as e:
                logger.error(f"Lỗi trigger tin tức: {e}")
        threading.Thread(target=task, daemon=True).start()
