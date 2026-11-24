@echo off
chcp 65001 > nul
title Decor Mic Test Server - Port 8766
cd /d "%~dp0"

echo ======================================================================
echo    🎙️ KHOI CHAY MAY CHU WEBSOCKET MIC TEST (CONG 8766)
echo    Nghe thu truc tiep ban thu am tu Tram Decor ESP32-S3 tren PC
echo ======================================================================
echo.

python mic_listener_server.py
pause
