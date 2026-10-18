@echo off
chcp 65001 >nul
cd /d "%~dp0tools\transcoder"
call start_transcoder.bat

