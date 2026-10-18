@echo off
chcp 65001 >nul
title [TRẠM CHUYỂN MÃ TRANSCODER] ESP32-S3 DECOR STREAM
color 0B

echo ======================================================================
echo          🚀 KHỞI ĐỘNG TRẠM CHUYỂN MÃ (TRANSCODER) ESP32-S3
echo               Hỗ trợ: YouTube, TikTok, Facebook, MP4, HLS
echo ======================================================================
echo.

cd /d "%~dp0"

echo [1/3] Đang kiểm tra Python...
python --version >nul 2>&1
if errorlevel 1 (
    color 0C
    echo [LỖI] Máy tính chưa cài đặt Python! Vui lòng cài Python 3.10+ từ python.org
    pause
    exit /b 1
)

echo [2/3] Kiểm tra các thư viện phụ thuộc (yt-dlp, FFmpeg, Flask, Requests)...
python -m pip install -r requirements.txt --quiet --no-warn-script-location

echo [3/3] Đang khởi động máy chủ chuyển mã trên cổng 5050...
start "" http://localhost:5050

echo.
echo ======================================================================
echo   🌐 Bảng điều khiển Web: http://localhost:5050
echo   💡 Nhấn Ctrl + C để dừng máy chủ bất cứ lúc nào.
echo ======================================================================
echo.

python transcoder.py

pause

