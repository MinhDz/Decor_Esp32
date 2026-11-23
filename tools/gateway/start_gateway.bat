@echo off
title Decor AI Gateway Server 2.0
chcp 65001 >nul
echo ======================================================================
echo   ĐANG KHỞI CHẠY DECOR AI GATEWAY SERVER (EDGE-TTS + GEMINI)
echo ======================================================================
cd /d "%~dp0"
python ai_gateway.py
pause
