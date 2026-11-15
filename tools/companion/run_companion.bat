@echo off
title Khoi dong Decor Space Companion
cd /d "%~dp0"
echo Dang khoi dong Decor Space Companion duoi khay he thong...
if exist "%~dp0dist\DecorSpaceCompanion.exe" (
    start "" "%~dp0dist\DecorSpaceCompanion.exe"
) else (
    start "" pythonw "%~dp0main.py"
)
exit
