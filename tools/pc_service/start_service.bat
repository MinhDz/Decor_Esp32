@echo off
chcp 65001 >nul
title TramDecor - PC Telemetry Monitor
cd /d "%~dp0"

echo =======================================================
echo   TRẠM DECOR VŨ TRỤ - KHỞI ĐỘNG DỊCH VỤ GIÁM SÁT PC
echo =======================================================
echo.
python pc_monitor.py
pause

