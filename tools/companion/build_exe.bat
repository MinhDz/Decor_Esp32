@echo off
title Dong goi Decor Space Companion thanh file .exe
cd /d "%~dp0"
echo ========================================================
echo   BAT DAU DONG GOI DECOR SPACE COMPANION SANG FILE .EXE
echo ========================================================
python "%~dp0build_companion.py"
echo.
pause
