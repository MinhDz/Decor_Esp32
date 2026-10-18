@echo off
chcp 65001 >nul
title Huong Dan & Thiet Lap Dong Bo Google Drive - Decor ESP32
color 0A

echo ===============================================================================
echo        HỆ THỐNG ĐỒNG BỘ ẢNH/VIDEO/TÀI LIỆU TỪ GOOGLE DRIVE VỀ ESP32
echo ===============================================================================
echo.
echo Thư mục đích trong dự án:
echo   assets\drive_sync
echo.

set PROJECT_DIR=%~dp0..\..\assets\drive_sync
if not exist "%PROJECT_DIR%" (
    mkdir "%PROJECT_DIR%"
)

echo [1] Mở thư mục dự án "assets\drive_sync" trong File Explorer
echo [2] Mở trang tải Google Drive for Desktop (nếu máy chưa cài)
echo [3] Tự động tìm ổ Google Drive (G:\) và liên kết thư mục
echo [4] Xem hướng dẫn cài đặt từng bước (Điện thoại + Máy tính)
echo [5] Thoát
echo.

set /p choice="Nhập lựa chọn của bạn [1-5]: "

if "%choice%"=="1" (
    start "" explorer "%PROJECT_DIR%"
    goto end
)

if "%choice%"=="2" (
    echo Đang mở trình duyệt tải Google Drive...
    start "" "https://www.google.com/drive/download/"
    goto end
)

if "%choice%"=="3" (
    goto link_folder
)

if "%choice%"=="4" (
    start "" notepad "%~dp0README.md"
    goto end
)

goto end

:link_folder
cls
echo ===============================================================================
echo                LIÊN KẾT THƯ MỤC GOOGLE DRIVE VÀO DỰ ÁN
echo ===============================================================================
echo.
echo Nhập đường dẫn thư mục Google Drive của bạn (ví dụ: G:\My Drive\Decor_Sync)
echo Hoặc nhấn Enter để sử dụng mặc định: G:\My Drive\Decor_Sync
echo.
set /p DRIVE_PATH="Đường dẫn thư mục Google Drive: "

if "%DRIVE_PATH%"=="" (
    set DRIVE_PATH=G:\My Drive\Decor_Sync
)

if not exist "%DRIVE_PATH%" (
    echo.
    echo [THÔNG BÁO] Thư mục "%DRIVE_PATH%" chưa tồn tại trên máy tính.
    echo Đang thử tạo thư mục trên Google Drive...
    mkdir "%DRIVE_PATH%" 2>nul
    if not exist "%DRIVE_PATH%" (
        echo [LỖI] Không thể tạo thư mục tại "%DRIVE_PATH%".
        echo Hãy đảm bảo Google Drive for Desktop đã cài đặt và đang chạy.
        pause
        goto end
    )
)

echo.
echo Đang tạo liên kết từ "%PROJECT_DIR%" tới "%DRIVE_PATH%"...
echo.
rmdir "%PROJECT_DIR%" 2>nul
mklink /D "%PROJECT_DIR%" "%DRIVE_PATH%" 2>nul
if errorlevel 1 (
    mklink /J "%PROJECT_DIR%" "%DRIVE_PATH%" 2>nul
)

if exist "%PROJECT_DIR%" (
    echo.
    echo ===========================================================================
    echo [THÀNH CÔNG] Đã liên kết thư mục dự án với Google Drive!
    echo Bây giờ mọi file bạn thêm từ điện thoại vào thư mục này trên Drive sẽ
    echo tự động xuất hiện ngay lập tức trong: assets\drive_sync
    echo ===========================================================================
) else (
    echo.
    echo [LƯU Ý] Nếu không thể tạo liên kết trực tiếp, bạn hãy dùng Cách 1 (Add folder)
    echo trong cài đặt Google Drive for Desktop (xem chi tiết trong README.md).
)
pause

:end

