#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Trạm Decor ESP32 - MCP Controller cho DecorSpaceCompanion
Cung cấp các công cụ MCP để mô hình AI (Gemini / DeepSeek / XiaoZhi) có thể:
1. Điều khiển trực tiếp Trạm Decor ESP32 (Đèn Edison, LED COB, Cối xay gió, Màn hình, Âm lượng, Nhạc, Báo cáo trạm).
2. Thực hiện các thao tác cơ bản trên máy tính PC Windows (Hẹn giờ tắt máy, hủy tắt máy, khóa màn hình, đọc/ghi Clipboard, mở ứng dụng, mở YouTube và trình duyệt web).
"""

import os
import sys
import json
import shutil
import logging
import subprocess
import webbrowser
import urllib.request
import urllib.parse

logger = logging.getLogger("McpController")

# Bảng ánh xạ các ứng dụng phổ biến trên Windows
WINDOWS_APP_ALIASES = {
    "notepad": ("Notepad", ["notepad.exe"]),
    "ghi chú": ("Notepad", ["notepad.exe"]),
    "calc": ("Máy tính (Calculator)", ["calc.exe"]),
    "calculator": ("Máy tính (Calculator)", ["calc.exe"]),
    "may tinh": ("Máy tính (Calculator)", ["calc.exe"]),
    "máy tính bỏ túi": ("Máy tính (Calculator)", ["calc.exe"]),
    "paint": ("MS Paint", ["mspaint.exe"]),
    "mspaint": ("MS Paint", ["mspaint.exe"]),
    "explorer": ("File Explorer", ["explorer.exe"]),
    "file explorer": ("File Explorer", ["explorer.exe"]),
    "thu muc": ("File Explorer", ["explorer.exe"]),
    "cmd": ("Command Prompt", ["cmd.exe"]),
    "command prompt": ("Command Prompt", ["cmd.exe"]),
    "terminal": ("Windows Terminal", ["wt.exe", "cmd.exe"]),
    "powershell": ("PowerShell", ["powershell.exe"]),
    "taskmgr": ("Task Manager", ["taskmgr.exe"]),
    "task manager": ("Task Manager", ["taskmgr.exe"]),
    "quan ly tac vu": ("Task Manager", ["taskmgr.exe"]),
    "control": ("Control Panel", ["control.exe"]),
    "control panel": ("Control Panel", ["control.exe"]),
    "settings": ("Windows Settings", ["ms-settings:"]),
    "cai dat": ("Windows Settings", ["ms-settings:"]),
    "cài đặt": ("Windows Settings", ["ms-settings:"]),
    "snipping": ("Snipping Tool", ["snippingtool.exe"]),
    "snipping tool": ("Snipping Tool", ["snippingtool.exe"]),
    "chrome": ("Google Chrome", [
        "chrome.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"
    ]),
    "google chrome": ("Google Chrome", [
        "chrome.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"
    ]),
    "coccoc": ("Trình duyệt Cốc Cốc", [
        r"%LOCALAPPDATA%\CocCoc\Browser\Application\browser.exe",
        r"C:\Program Files\CocCoc\Browser\Application\browser.exe",
        r"C:\Program Files (x86)\CocCoc\Browser\Application\browser.exe"
    ]),
    "cốc cốc": ("Trình duyệt Cốc Cốc", [
        r"%LOCALAPPDATA%\CocCoc\Browser\Application\browser.exe",
        r"C:\Program Files\CocCoc\Browser\Application\browser.exe",
        r"C:\Program Files (x86)\CocCoc\Browser\Application\browser.exe"
    ]),
    "coc coc": ("Trình duyệt Cốc Cốc", [
        r"%LOCALAPPDATA%\CocCoc\Browser\Application\browser.exe",
        r"C:\Program Files\CocCoc\Browser\Application\browser.exe"
    ]),
    "edge": ("Microsoft Edge", [
        "msedge.exe",
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"
    ]),
    "microsoft edge": ("Microsoft Edge", [
        "msedge.exe",
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
    ]),
    "vscode": ("Visual Studio Code", [
        "code",
        r"%LOCALAPPDATA%\Programs\Microsoft VS Code\Code.exe",
        r"C:\Program Files\Microsoft VS Code\Code.exe"
    ]),
    "vs code": ("Visual Studio Code", [
        "code",
        r"%LOCALAPPDATA%\Programs\Microsoft VS Code\Code.exe"
    ]),
    "spotify": ("Spotify", [
        "spotify.exe",
        r"%APPDATA%\Spotify\Spotify.exe"
    ]),
    "discord": ("Discord", [
        r"%LOCALAPPDATA%\Discord\Update.exe --processStart Discord.exe"
    ]),
    "zalo": ("Zalo PC", [
        r"%LOCALAPPDATA%\Programs\Zalo\Zalo.exe"
    ]),
    "word": ("Microsoft Word", ["winword.exe"]),
    "excel": ("Microsoft Excel", ["excel.exe"]),
    "powerpoint": ("Microsoft PowerPoint", ["powerpnt.exe"]),
}

# Danh sách định nghĩa các công cụ MCP đầy đủ cho cả Trạm ESP32 và PC
MCP_TOOL_DEFINITIONS = [
    # --- NHÓM 1: CÔNG CỤ ĐIỀU KHIỂN MÁY TÍNH PC (DECOR SPACE COMPANION) ---
    {
        "name": "pc.system.schedule_shutdown",
        "description": "Hẹn giờ tắt máy tính PC (Windows) sau một số phút chỉ định, hoặc hủy lệnh hẹn giờ tắt máy đang chạy.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "minutes": {
                    "type": "integer",
                    "description": "Số phút hẹn giờ trước khi tắt máy tính (ví dụ: 15, 30, 60). Đặt 0 nếu muốn tắt máy ngay."
                },
                "cancel": {
                    "type": "boolean",
                    "description": "Đặt true nếu người dùng yêu cầu hủy hẹn giờ tắt máy."
                }
            }
        }
    },
    {
        "name": "pc.system.power",
        "description": "Thực hiện thao tác nguồn hoặc khóa màn hình trên máy tính PC: lock (khóa màn hình Windows), sleep (chế độ ngủ), restart (khởi động lại), cancel_shutdown (hủy tắt máy).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {
                    "type": "string",
                    "description": "Thao tác: lock, sleep, restart hoặc cancel_shutdown."
                }
            },
            "required": ["action"]
        }
    },
    {
        "name": "pc.clipboard.get",
        "description": "Đọc nội dung văn bản hiện đang lưu trong Clipboard (bộ nhớ tạm) của máy tính PC để đọc to, dịch thuật hoặc giải thích cho người dùng.",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    },
    {
        "name": "pc.clipboard.set",
        "description": "Ghi hoặc sao chép một đoạn văn bản vào Clipboard (bộ nhớ tạm) của máy tính PC.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "text": {
                    "type": "string",
                    "description": "Nội dung văn bản cần chép vào Clipboard của PC."
                }
            },
            "required": ["text"]
        }
    },
    {
        "name": "pc.app.open",
        "description": "Mở một ứng dụng hoặc phần mềm trên máy tính PC (ví dụ: notepad, calculator, paint, explorer, cmd, powershell, taskmgr, settings, chrome, coccoc, edge, vscode, spotify, discord, zalo, word, excel).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "app_name": {
                    "type": "string",
                    "description": "Tên ứng dụng hoặc chương trình cần mở trên PC."
                }
            },
            "required": ["app_name"]
        }
    },
    {
        "name": "pc.browser.open_youtube",
        "description": "Mở YouTube bằng trình duyệt trên máy tính PC. Có thể tìm kiếm bài hát/video theo từ khóa hoặc mở trang chủ YouTube.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "query": {
                    "type": "string",
                    "description": "Từ khóa tìm kiếm video/nhạc trên YouTube hoặc đường dẫn URL YouTube. Để trống nếu chỉ mở trang chủ YouTube."
                }
            }
        }
    },
    {
        "name": "pc.browser.open_url",
        "description": "Mở một trang web bất kỳ hoặc tìm kiếm Google bằng trình duyệt trên máy tính PC.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "url": {
                    "type": "string",
                    "description": "Địa chỉ URL trang web (ví dụ: https://vnexpress.net, facebook.com, github.com) hoặc từ khóa tìm kiếm Google."
                }
            },
            "required": ["url"]
        }
    },

    # --- NHÓM 2: CÔNG CỤ ĐIỀU KHIỂN TRẠM DECOR ESP32 ---
    {
        "name": "self.windmill.set",
        "description": "Bật, tắt hoặc điều chỉnh tốc độ cối xay gió (quạt) trên Trạm Decor ESP32.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "state": {"type": "boolean", "description": "Bật (true) hoặc tắt (false) cối xay gió."},
                "speed": {"type": "integer", "description": "Tốc độ quay từ 0 đến 100 phần trăm."},
                "mode": {"type": "string", "description": "Chế độ: off, low, medium, high hoặc breath."}
            }
        }
    },
    {
        "name": "self.edison_lights.set",
        "description": "Bật, tắt hoặc điều chỉnh độ sáng hai bóng đèn ống Edison trên Trạm Decor ESP32.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "state": {"type": "boolean", "description": "Bật (true) hoặc tắt (false) đèn Edison."},
                "side": {"type": "string", "description": "Vị trí bóng đèn: both (cả hai), left (bên trái) hoặc right (bên phải)."},
                "brightness": {"type": "integer", "description": "Độ sáng từ 0 đến 100 phần trăm."},
                "breathing": {"type": "boolean", "description": "Bật hiệu ứng nhịp thở."}
            }
        }
    },
    {
        "name": "self.cob_leds.set",
        "description": "Bật, tắt hoặc chuyển đổi hiệu ứng hai thanh đèn LED COB (đèn sao băng) trên Trạm Decor ESP32.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "state": {"type": "boolean", "description": "Bật (true) hoặc tắt (false) đèn LED COB."},
                "action": {"type": "string", "description": "Thao tác: toggle hoặc next_mode."}
            }
        }
    },
    {
        "name": "self.audio.set_volume",
        "description": "Điều chỉnh âm lượng loa phát của Trạm Decor ESP32 từ 0 đến 100 phần trăm.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "volume": {"type": "integer", "description": "Mức âm lượng từ 0 đến 100 phần trăm."}
            },
            "required": ["volume"]
        }
    },
    {
        "name": "self.screen.open_app",
        "description": "Mở ứng dụng hoặc chuyển chế độ hiển thị trên màn hình Trạm Decor ESP32: standby (màn hình chờ), pchud (thông số PC), emoji (biểu cảm trợ lý AI), diag (chẩn đoán cảm biến), games (trò chơi), media (trình phát nhạc), clock (đồng hồ), calendar (lịch vạn niên).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "app": {"type": "string", "description": "Tên màn hình: standby, pchud, emoji, diag, games, media, clock hoặc calendar."}
            },
            "required": ["app"]
        }
    },
    {
        "name": "self.screen.set_emoji_style",
        "description": "Đổi phong cách biểu cảm khuôn mặt trợ lý trên màn hình Trạm Decor: neon (Mắt Neon), cute_girl (Cô Bé Chibi), doi_mat (Đôi Mắt Robot) hoặc gome (Mèo Gome).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "style": {"type": "string", "description": "Phong cách: neon, cute_girl, doi_mat hoặc gome."}
            },
            "required": ["style"]
        }
    },
    {
        "name": "self.screen.next_wallpaper",
        "description": "Chuyển đổi sang hình nền tiếp theo trên màn hình chờ của Trạm Decor ESP32.",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    },
    {
        "name": "self.screen.set_brightness",
        "description": "Điều chỉnh độ sáng màn hình TFT của Trạm Decor ESP32 từ 5 đến 100 phần trăm.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "brightness": {"type": "integer", "description": "Độ sáng từ 5 đến 100 phần trăm."}
            },
            "required": ["brightness"]
        }
    },
    {
        "name": "self.ai.set_identity",
        "description": "Đặt tên mới cho Trợ lý AI trên trạm hoặc đổi câu nói đánh thức AI (Wake Word) khi người dùng yêu cầu đổi tên cho AI.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "ai_name": {"type": "string", "description": "Tên mới của trợ lý AI trên trạm (ví dụ: Jarvis, Gome, Na, XiaoZhi)."},
                "wake_word": {"type": "string", "description": "Câu nói đánh thức AI từ màn hình chờ/AOD (ví dụ: Jarvis ơi, Gome ơi)."}
            }
        }
    },
    {
        "name": "self.media.control",
        "description": "Điều khiển trình phát nhạc trên Trạm Decor ESP32: play (phát nhạc), pause (tạm dừng), next (bài tiếp theo), prev (bài trước đó), open (mở màn hình nhạc).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {"type": "string", "description": "Thao tác: play, pause, next, prev hoặc open."}
            },
            "required": ["action"]
        }
    },
    {
        "name": "self.station.get_report",
        "description": "Đọc báo cáo tổng hợp tình trạng Trạm Decor ESP32 (nhiệt độ phòng, độ ẩm, dung lượng pin, Wi-Fi, trạng thái đèn, quạt, màn hình).",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    },
    {
        "name": "self.telemetry.get",
        "description": "Đọc nhanh nhiệt độ, độ ẩm, phần trăm pin và tốc độ cối xay gió của Trạm Decor ESP32.",
        "inputSchema": {
            "type": "object",
            "properties": {}
        }
    }
]


# ================= THỰC THI CÁC THAO TÁC TRÊN PC WINDOWS =================

def pc_schedule_shutdown(minutes: int = None, cancel: bool = False) -> dict:
    """Hẹn giờ tắt máy tính Windows hoặc hủy lệnh hẹn giờ tắt máy."""
    try:
        if cancel or (minutes is not None and int(minutes) < 0):
            subprocess.run(["shutdown", "/a"], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
            logger.info("🛑 [PC MCP] Đã hủy hẹn giờ tắt máy tính.")
            return {
                "status": "ok",
                "success": True,
                "message": "Đã hủy lệnh hẹn giờ tắt máy tính PC thành công."
            }

        mins = int(minutes) if minutes is not None else 30
        seconds = max(0, mins * 60)
        # Hủy lịch cũ (nếu có) trước khi đặt lịch mới
        subprocess.run(["shutdown", "/a"], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
        proc = subprocess.run(
            ["shutdown", "/s", "/t", str(seconds)],
            capture_output=True,
            text=True,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        )
        if proc.returncode == 0:
            if mins == 0:
                msg = "Đã kích hoạt lệnh tắt máy tính PC ngay lập tức."
            else:
                msg = f"Đã hẹn giờ tắt máy tính PC sau {mins} phút ({seconds} giây)."
            logger.info(f"⏰ [PC MCP] {msg}")
            return {"status": "ok", "success": True, "message": msg}
        else:
            err = (proc.stderr or proc.stdout or "").strip()
            return {"status": "error", "success": False, "message": f"Không thể đặt hẹn giờ tắt máy: {err}"}
    except Exception as e:
        logger.error(f"Lỗi pc_schedule_shutdown: {e}")
        return {"status": "error", "success": False, "message": f"Lỗi hệ thống khi hẹn giờ tắt máy: {e}"}


def pc_power_control(action: str) -> dict:
    """Điều khiển nguồn / khóa màn hình máy tính PC."""
    act = (action or "").strip().lower()
    try:
        flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        if act in ("cancel", "cancel_shutdown", "abort", "huy"):
            return pc_schedule_shutdown(cancel=True)
        elif act in ("lock", "khoa", "lock_screen"):
            subprocess.run(["rundll32.exe", "user32.dll,LockWorkStation"], creationflags=flags)
            return {"status": "ok", "success": True, "message": "Đã khóa màn hình máy tính PC."}
        elif act in ("restart", "reboot", "khoi_dong_lai"):
            subprocess.run(["shutdown", "/r", "/t", "30"], creationflags=flags)
            return {"status": "ok", "success": True, "message": "Đã hẹn khởi động lại máy tính PC sau 30 giây (có thể nói hủy tắt máy để dừng)."}
        elif act in ("sleep", "ngu"):
            subprocess.run(["rundll32.exe", "powrprof.dll,SetSuspendState", "0,1,0"], creationflags=flags)
            return {"status": "ok", "success": True, "message": "Đã đưa máy tính PC vào chế độ ngủ (Sleep)."}
        else:
            return {"status": "error", "success": False, "message": f"Thao tác nguồn không hợp lệ: {action}"}
    except Exception as e:
        return {"status": "error", "success": False, "message": f"Lỗi thao tác nguồn PC: {e}"}


def pc_get_clipboard() -> dict:
    """Đọc nội dung văn bản trong Clipboard của Windows."""
    try:
        flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        proc = subprocess.run(
            ["powershell", "-NoProfile", "-Command", "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; Get-Clipboard -Raw"],
            capture_output=True,
            timeout=5,
            creationflags=flags
        )
        content = proc.stdout.decode("utf-8", errors="replace").strip()
        if not content:
            return {
                "status": "ok",
                "success": True,
                "clipboard": "",
                "message": "Clipboard trên máy tính PC hiện đang trống (hoặc không chứa dữ liệu văn bản)."
            }
        # Giới hạn độ dài hợp lý để đọc TTS và tránh tràn bộ nhớ ESP32
        truncated = content[:1500]
        logger.info(f"📋 [PC MCP] Đã đọc Clipboard ({len(content)} ký tự): {truncated[:80]}...")
        return {
            "status": "ok",
            "success": True,
            "clipboard": truncated,
            "message": f"Nội dung trong Clipboard của máy tính là: {truncated}"
        }
    except Exception as e:
        logger.error(f"Lỗi đọc Clipboard: {e}")
        return {"status": "error", "success": False, "message": f"Không thể đọc Clipboard trên PC: {e}"}


def pc_set_clipboard(text: str) -> dict:
    """Ghi văn bản vào Clipboard của Windows."""
    if not text:
        return {"status": "error", "success": False, "message": "Thiếu nội dung văn bản cần chép vào Clipboard."}
    try:
        flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        proc = subprocess.run(
            ["clip"],
            input=text.encode("utf-16le"),
            capture_output=True,
            timeout=5,
            creationflags=flags
        )
        if proc.returncode == 0:
            logger.info(f"📋 [PC MCP] Đã ghi vào Clipboard: {text[:60]}...")
            return {"status": "ok", "success": True, "message": f"Đã sao chép nội dung vào Clipboard của PC: \"{text[:100]}\""}
        return {"status": "error", "success": False, "message": "Lỗi khi ghi dữ liệu vào Clipboard."}
    except Exception as e:
        return {"status": "error", "success": False, "message": f"Lỗi khi ghi vào Clipboard: {e}"}


def pc_open_app(app_name: str) -> dict:
    """Mở một ứng dụng trên máy tính PC Windows."""
    raw_name = (app_name or "").strip()
    if not raw_name:
        return {"status": "error", "success": False, "message": "Chưa cung cấp tên ứng dụng cần mở."}

    key = raw_name.lower().strip()
    # Tìm kiếm khớp chính xác hoặc chứa trong bảng alias
    matched_entry = WINDOWS_APP_ALIASES.get(key)
    if not matched_entry:
        for alias_k, alias_val in WINDOWS_APP_ALIASES.items():
            if alias_k in key or key in alias_k:
                matched_entry = alias_val
                break

    try:
        if matched_entry:
            friendly_name, candidates = matched_entry
            for cand in candidates:
                expanded = os.path.expandvars(cand)
                if expanded.startswith("ms-"):
                    os.startfile(expanded)
                    logger.info(f"🚀 [PC MCP] Đã mở URI hệ thống: {friendly_name} ({expanded})")
                    return {"status": "ok", "success": True, "message": f"Đã mở {friendly_name} trên máy tính PC."}

                if "--processStart" in expanded:
                    subprocess.Popen(expanded, shell=True)
                    return {"status": "ok", "success": True, "message": f"Đã mở {friendly_name} trên máy tính PC."}

                if os.path.exists(expanded) or shutil.which(expanded):
                    subprocess.Popen([expanded], shell=False)
                    logger.info(f"🚀 [PC MCP] Đã mở ứng dụng: {friendly_name} ({expanded})")
                    return {"status": "ok", "success": True, "message": f"Đã mở ứng dụng {friendly_name} trên máy tính PC."}

            # Thử mở bằng lệnh start qua shell với candidate đầu tiên
            first_cmd = os.path.expandvars(candidates[0])
            subprocess.Popen(f'start "" "{first_cmd}"', shell=True)
            return {"status": "ok", "success": True, "message": f"Đã gửi lệnh mở {friendly_name} trên máy tính PC."}

        # Nếu không nằm trong alias: kiểm tra xem có trong PATH hoặc mở qua Windows Start
        found_exe = shutil.which(raw_name) or shutil.which(raw_name + ".exe")
        if found_exe:
            subprocess.Popen([found_exe], shell=False)
            logger.info(f"🚀 [PC MCP] Đã mở ứng dụng từ PATH: {found_exe}")
            return {"status": "ok", "success": True, "message": f"Đã mở ứng dụng {raw_name} trên máy tính PC."}

        # Thử mở qua os.startfile
        os.startfile(raw_name)
        logger.info(f"🚀 [PC MCP] Đã mở qua os.startfile: {raw_name}")
        return {"status": "ok", "success": True, "message": f"Đã mở {raw_name} trên máy tính PC."}
    except Exception as e:
        logger.warning(f"Không thể mở ứng dụng '{raw_name}': {e}")
        return {"status": "error", "success": False, "message": f"Không tìm thấy hoặc không thể mở ứng dụng '{raw_name}' trên PC."}


def _fetch_first_youtube_video_url(query: str) -> str:
    """Tìm URL video YouTube đầu tiên khớp với từ khóa để phát ngay lập tức."""
    try:
        search_url = f"https://www.youtube.com/results?search_query={urllib.parse.quote(query)}"
        req = urllib.request.Request(
            search_url,
            headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"}
        )
        with urllib.request.urlopen(req, timeout=4) as resp:
            html = resp.read().decode("utf-8", errors="ignore")
        import re
        match = re.search(r'"videoId":"([a-zA-Z0-9_-]{11})"', html)
        if match:
            vid = match.group(1)
            return f"https://www.youtube.com/watch?v={vid}"
    except Exception as e:
        logger.debug(f"Không lấy được videoId trực tiếp, dùng trang search: {e}")
    return f"https://www.youtube.com/results?search_query={urllib.parse.quote(query)}"


def pc_open_youtube(query: str = "", auto_play: bool = True) -> dict:
    """Mở YouTube trên trình duyệt máy tính PC (tự động phát video đầu tiên nếu có từ khóa)."""
    q = (query or "").strip()
    try:
        if not q:
            target_url = "https://www.youtube.com"
            msg = "Đã mở trang chủ YouTube trên trình duyệt máy tính PC."
        elif q.startswith("http://") or q.startswith("https://") or "youtube.com/" in q or "youtu.be/" in q:
            target_url = q if q.startswith("http") else ("https://" + q)
            msg = f"Đã mở đường dẫn YouTube trên trình duyệt máy tính PC."
        else:
            if auto_play:
                target_url = _fetch_first_youtube_video_url(q)
                msg = f"Đã mở và phát YouTube với nội dung \"{q}\" trên trình duyệt máy tính PC."
            else:
                target_url = f"https://www.youtube.com/results?search_query={urllib.parse.quote(q)}"
                msg = f"Đã mở tìm kiếm YouTube cho \"{q}\" trên trình duyệt máy tính PC."

        webbrowser.open(target_url)
        logger.info(f"📺 [PC MCP] Mở YouTube: {target_url}")
        return {"status": "ok", "success": True, "url": target_url, "message": msg}
    except Exception as e:
        logger.error(f"Lỗi mở YouTube: {e}")
        return {"status": "error", "success": False, "message": f"Không thể mở YouTube trên trình duyệt: {e}"}


def pc_open_url(url_or_query: str) -> dict:
    """Mở một trang web hoặc tìm kiếm Google trên trình duyệt PC."""
    raw = (url_or_query or "").strip()
    if not raw:
        return {"status": "error", "success": False, "message": "Thiếu địa chỉ URL hoặc từ khóa cần mở."}
    try:
        if raw.startswith("http://") or raw.startswith("https://"):
            target_url = raw
        elif "." in raw and " " not in raw:
            target_url = "https://" + raw
        else:
            target_url = f"https://www.google.com/search?q={urllib.parse.quote(raw)}"

        webbrowser.open(target_url)
        logger.info(f"🌐 [PC MCP] Mở trình duyệt: {target_url}")
        return {"status": "ok", "success": True, "url": target_url, "message": f"Đã mở trình duyệt máy tính với địa chỉ: {target_url}"}
    except Exception as e:
        return {"status": "error", "success": False, "message": f"Lỗi mở trình duyệt: {e}"}


def execute_pc_action(action: str, params: dict = None) -> dict:
    """Bộ điều phối thực thi các lệnh điều khiển PC."""
    params = params or {}
    act = (action or "").strip().lower()

    if act in ("schedule_shutdown", "pc.system.schedule_shutdown", "shutdown"):
        cancel = bool(params.get("cancel", False))
        minutes = params.get("minutes")
        return pc_schedule_shutdown(minutes=minutes, cancel=cancel)

    if act in ("power_control", "pc.system.power", "power"):
        sub_action = params.get("action") or "lock"
        return pc_power_control(sub_action)

    if act in ("get_clipboard", "pc.clipboard.get", "read_clipboard"):
        return pc_get_clipboard()

    if act in ("set_clipboard", "pc.clipboard.set", "write_clipboard"):
        text = params.get("text") or params.get("content") or ""
        return pc_set_clipboard(str(text))

    if act in ("open_app", "pc.app.open", "launch_app"):
        app_name = params.get("app_name") or params.get("app") or params.get("name") or ""
        return pc_open_app(str(app_name))

    if act in ("open_youtube", "pc.browser.open_youtube", "youtube"):
        query = params.get("query") or params.get("search") or params.get("url") or ""
        auto_play = bool(params.get("auto_play", True))
        return pc_open_youtube(str(query), auto_play=auto_play)

    if act in ("open_url", "pc.browser.open_url", "browser"):
        url = params.get("url") or params.get("query") or ""
        return pc_open_url(str(url))

    return {"status": "error", "success": False, "message": f"Không hỗ trợ thao tác PC: {action}"}


# ================= THỰC THI CÔNG CỤ MCP TRÊN TRẠM ESP32 =================

def execute_station_mcp_tool(esp32_ip: str, tool_name: str, arguments: dict = None) -> dict:
    """
    Gửi lệnh thực thi công cụ MCP xuống Trạm Decor ESP32 qua endpoint `/api/mcp/call`.
    Nếu firmware cũ chưa có `/api/mcp/call`, tự động fallback sang các REST API tương ứng của trạm.
    """
    arguments = arguments or {}
    ip = (esp32_ip or "").strip()
    if not ip or ip == "0.0.0.0":
        return {"status": "error", "success": False, "message": "Chưa xác định được địa chỉ IP của Trạm Decor ESP32."}

    # 1. Gọi trực tiếp endpoint chuẩn /api/mcp/call trên ESP32
    try:
        url = f"http://{ip}/api/mcp/call"
        payload = json.dumps({"name": tool_name, "arguments": arguments}).encode("utf-8")
        req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="replace"))
            logger.info(f"🛰️ [STATION MCP] {tool_name}({arguments}) -> {data.get('message')}")
            return data
    except urllib.error.HTTPError as he:
        if he.code != 404:
            try:
                err_data = json.loads(he.read().decode("utf-8", errors="ignore"))
                return err_data
            except Exception:
                pass
    except Exception as e:
        logger.warning(f"Lỗi kết nối /api/mcp/call tới trạm {ip}: {e}")
        return {"status": "error", "success": False, "message": f"Không thể kết nối tới Trạm Decor ({ip}): {e}"}

    # 2. Fallback sang REST API hiện có nếu trạm chưa nạp firmware mới
    try:
        if tool_name == "self.windmill.set":
            state = arguments.get("state", True)
            speed = 0 if not state else int(arguments.get("speed", 60))
            mode = 0 if speed == 0 else 2
            body = json.dumps({"target": "windmill", "mode": mode, "speed": speed}).encode("utf-8")
            req = urllib.request.Request(f"http://{ip}/api/decor/control", data=body, headers={"Content-Type": "application/json"}, method="POST")
            urllib.request.urlopen(req, timeout=4).read()
            return {"status": "ok", "success": True, "message": f"Đã điều chỉnh cối xay gió trạm ở mức {speed}%."}

        if tool_name == "self.edison_lights.set":
            state = arguments.get("state", True)
            brightness = 0 if not state else int(arguments.get("brightness", 80))
            side = arguments.get("side", "both")
            target = "edison_left" if side == "left" else ("edison_right" if side == "right" else "edison_both")
            mode = 0 if brightness == 0 else 5
            body = json.dumps({"target": target, "mode": mode, "brightness": brightness}).encode("utf-8")
            req = urllib.request.Request(f"http://{ip}/api/decor/control", data=body, headers={"Content-Type": "application/json"}, method="POST")
            urllib.request.urlopen(req, timeout=4).read()
            return {"status": "ok", "success": True, "message": f"Đã điều chỉnh đèn Edison ({side}) ở độ sáng {brightness}%."}

        if tool_name == "self.cob_leds.set":
            action = arguments.get("action", "")
            state = arguments.get("state", True)
            act_str = "next_mode" if action == "next_mode" else ("on" if state else "off")
            body = json.dumps({"target": "flowing", "action": act_str}).encode("utf-8")
            req = urllib.request.Request(f"http://{ip}/api/decor/control", data=body, headers={"Content-Type": "application/json"}, method="POST")
            urllib.request.urlopen(req, timeout=4).read()
            return {"status": "ok", "success": True, "message": f"Đã thực hiện lệnh đèn LED COB: {act_str}."}

        if tool_name in ("self.screen.open_app", "self.screen.switch_mode"):
            app_mode = arguments.get("app") or arguments.get("mode") or "standby"
            body = json.dumps({"mode": app_mode}).encode("utf-8")
            req = urllib.request.Request(f"http://{ip}/api/screen/state", data=body, headers={"Content-Type": "application/json"}, method="POST")
            urllib.request.urlopen(req, timeout=4).read()
            return {"status": "ok", "success": True, "message": f"Đã chuyển màn hình trạm sang chế độ {app_mode}."}

        if tool_name == "self.screen.set_emoji_style":
            style = arguments.get("style", "neon")
            body = json.dumps({"emoji_style": style}).encode("utf-8")
            req = urllib.request.Request(f"http://{ip}/api/screen/state", data=body, headers={"Content-Type": "application/json"}, method="POST")
            urllib.request.urlopen(req, timeout=4).read()
            return {"status": "ok", "success": True, "message": f"Đã đổi phong cách biểu cảm sang {style}."}

        if tool_name == "self.audio.set_volume":
            vol = int(arguments.get("volume", 80))
            body = json.dumps({"action": "set_volume", "volume": vol}).encode("utf-8")
            req = urllib.request.Request(f"http://{ip}/api/xiaozhi/action", data=body, headers={"Content-Type": "application/json"}, method="POST")
            urllib.request.urlopen(req, timeout=4).read()
            return {"status": "ok", "success": True, "message": f"Đã đặt âm lượng loa trạm ở mức {vol}%."}
    except Exception as e:
        return {"status": "error", "success": False, "message": f"Lỗi gọi API điều khiển trạm: {e}"}

    return {"status": "error", "success": False, "message": f"Không thể thực thi công cụ {tool_name} trên trạm."}


def execute_mcp_tool(tool_name: str, arguments: dict, esp32_ip: str = "") -> dict:
    """Hàm hợp nhất thực thi bất kỳ công cụ MCP nào (cả công cụ PC và công cụ Trạm Decor)."""
    name = (tool_name or "").strip()
    args = arguments if isinstance(arguments, dict) else {}

    if name.startswith("pc."):
        return execute_pc_action(name, args)

    if name.startswith("self.") or name == "get_daily_news":
        return execute_station_mcp_tool(esp32_ip, name, args)

    # Nếu tên rút gọn của lệnh PC
    if name in ("schedule_shutdown", "power_control", "get_clipboard", "set_clipboard", "open_app", "open_youtube", "open_url"):
        return execute_pc_action(name, args)

    return {"status": "error", "success": False, "message": f"Không tìm thấy công cụ MCP: {name}"}


def get_gemini_function_declarations() -> list[dict]:
    """Chuyển đổi danh sách công cụ MCP sang định dạng functionDeclarations của Google Gemini API."""
    declarations = []
    for tool in MCP_TOOL_DEFINITIONS:
        # Gemini không cho phép dấu chấm '.' trong tên functionDeclaration, nên ta thay '.' bằng '__'
        safe_name = tool["name"].replace(".", "__")
        schema = tool.get("inputSchema", {"type": "object", "properties": {}})
        props = schema.get("properties", {})
        decl = {
            "name": safe_name,
            "description": tool["description"],
        }
        if props:
            decl["parameters"] = {
                "type": "OBJECT",
                "properties": {
                    k: {
                        "type": v.get("type", "string").upper(),
                        "description": v.get("description", "")
                    }
                    for k, v in props.items()
                }
            }
            if "required" in schema:
                decl["parameters"]["required"] = schema["required"]
        declarations.append(decl)
    return declarations


def get_openai_tools_schema() -> list[dict]:
    """Chuyển đổi danh sách công cụ MCP sang định dạng tools của OpenAI / DeepSeek API."""
    tools = []
    for tool in MCP_TOOL_DEFINITIONS:
        safe_name = tool["name"].replace(".", "__")
        tools.append({
            "type": "function",
            "function": {
                "name": safe_name,
                "description": tool["description"],
                "parameters": tool.get("inputSchema", {"type": "object", "properties": {}})
            }
        })
    return tools


def restore_mcp_tool_name(safe_name: str) -> str:
    """Khôi phục tên công cụ MCP gốc từ tên an toàn (ví dụ: pc__app__open -> pc.app.open)."""
    return (safe_name or "").replace("__", ".")
