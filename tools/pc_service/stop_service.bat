@echo off
chcp 65001 >nul
title TramDecor - Dung Dich Vu PC Telemetry
cd /d "%~dp0"

echo =======================================================
echo   TRẠM DECOR VŨ TRỤ - DỪNG DỊCH VỤ GIÁM SÁT PC
echo =======================================================
echo.
powershell -Command "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*pc_monitor.py*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force; Write-Host ('[OK] Đã dừng tiến trình PID: ' + $_.ProcessId) }"
echo.
echo Đã hoàn tất!
pause

