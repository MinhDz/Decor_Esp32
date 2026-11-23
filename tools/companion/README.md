# 🛰️ Decor Space Station Companion (Trạm Decor Vũ Trụ)

Ứng dụng đồng hành máy tính (PC Companion App) chạy ngầm dưới **Khay hệ thống Windows (System Tray)**, hợp nhất toàn bộ tính năng hỗ trợ PC cho Trạm Decor ESP32:
1. **Trạm Chuyển Mã Âm Nhạc & Stream (Web Transcoder Engine - Port 5050)**: Nhận link YouTube Playlist / Video web, chuyển mã thời gian thực bằng FFmpeg MJPEG + PCM 16kHz và đẩy xuống ESP32 màn hình tròn/vuông. Hỗ trợ chế độ **Audio-Only Mode 12 (Nokia S40)** phát nhạc YouTube mượt mà, hiển thị tên bài tiếng Việt, phổ âm thanh FFT VU và tự động ngắt stream khi người dùng bấm EXIT trên trạm để tối ưu tài nguyên. Cung cấp Web Dashboard tại `http://localhost:5050`.
2. **Giám Sát Phần Cứng PC (PC Hardware Telemetry - PC HUD Mode)**: Thu thập xung nhịp CPU, nhiệt độ, phần trăm RAM, dung lượng ổ đĩa, tốc độ mạng Download/Upload, thông số GPU (NVIDIA NVML / Windows PDH) và đẩy định kỳ lên API `/api/pc/metrics` của ESP32.
3. **Trợ Lý AI & Audio Streaming Gateway 2.0 (Port 8765)**: Nhận luồng âm thanh Mic từ trạm, liên kết mô hình AI (Gemini / DeepSeek), tạo giọng đọc tiếng Việt mượt mà qua Microsoft Edge Neural TTS + Google TTS fallback, stream âm thanh chunked thời gian thực xuống loa trạm (MAX98357A I2S). Hỗ trợ đọc báo thời sự VnExpress tự động trực tiếp từ khay hệ thống.
4. **Cơ Chế Khởi Động Cùng Windows**: Cho phép bật/tắt tùy chọn tự khởi động cùng máy tính Windows thông qua Registry `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. Người dùng không cần tìm file `.bat` để khởi động thủ công mỗi khi mở máy.

---

## 🚀 Tính Năng Chính

* **Biểu tượng Khay Hệ Thống (System Tray Icon)**:
  - Hiển thị trực quan trạng thái kết nối trạm: 🟢 **Online** / 🔴 **Offline**.
  - Di chuột vào icon (Tooltip) hiển thị nhanh: IP Trạm, % CPU, % RAM hiện tại của máy tính.
* **Menu Chuột Phải Nhanh (Right-click Menu)**:
  - 🛰️ **Trạm: 192.168.1.82 [🟢 Online]**: Click để mở cửa sổ Cài đặt.
  - 🎵 **Bảng Điều Khiển Âm Nhạc & Stream (Port 5050)**: Mở trình duyệt tới giao diện Stream YouTube.
  - 🖥️ **Mở Trang Web Trạm ESP32**: Mở thẳng trang cấu hình Web nhúng của trạm.
  - 📰 **Đọc Tin Thời Sự VnExpress (Loa Trạm)**: Click để trạm tự động phát thanh bản tin thời sự mới nhất ra loa.
  - ☑️ **Gửi thông số PC (PC HUD)**: Bật/Tắt gửi telemetry tức thời.
  - ☑️ **Web Transcoder Engine (Port 5050)**: Bật/Tắt máy chủ Flask stream video.
  - ☑️ **Trợ Lý AI Gateway (Port 8765)**: Bật/Tắt máy chủ AI Gateway & Audio Streaming.
  - ☑️ **Khởi động cùng Windows**: Tích chọn để tự chạy khi mở máy tính.
  - ⚙️ **Cài đặt & Cấu hình...**: Mở cửa sổ cấu hình IP, giọng đọc TTS, API Keys, cổng web, nút kiểm tra kết nối (Ping).
  - 🔄 **Khởi động lại dịch vụ**: Làm mới lại toàn bộ socket và worker.
  - ❌ **Thoát ứng dụng**: Dọn dẹp sạch sẽ các tiến trình nền và thoát.

---

## 📦 Cách Chạy Ứng Dụng

### Cách 1: Chạy từ file thực thi `.exe` hoặc script tiện ích (Khuyên dùng)
- Chạy file: **`run_companion.bat`** (trong thư mục `tools/companion/`)
- Hoặc chạy file: **`tools/companion/dist/DecorSpaceCompanion.exe`**
- Biểu tượng trạm vũ trụ sẽ xuất hiện ở góc dưới bên phải màn hình (khay hệ thống Windows cạnh đồng hồ).
- Chuột phải vào biểu tượng -> Tích chọn **"Khởi động cùng Windows"**. Từ lần sau bạn không cần làm gì nữa, máy tính vừa mở lên là mọi thứ (PC HUD, Transcoder, AI Gateway) đã tự động sẵn sàng!

### Cách 2: Chạy từ mã nguồn Python
```bash
pythonw tools/companion/main.py
```

---

## ⚙️ Cấu Hình (companion_config.json)

Tệp cấu hình được tự động lưu trữ tại thư mục ứng dụng:
```json
{
  "esp32_ip": "192.168.1.82",
  "monitor_enabled": true,
  "monitor_interval": 1.2,
  "transcoder_enabled": true,
  "transcoder_port": 5050,
  "transcoder_fps": 15,
  "transcoder_quality": 5,
  "transcoder_res": "240x240",
  "ai_gateway_enabled": true,
  "ai_gateway_port": 8765,
  "ai_gateway_voice": "vi-VN-HoaiMyNeural",
  "gemini_api_key": "",
  "openai_api_key": "",
  "openai_base_url": "https://api.deepseek.com",
  "openai_model": "deepseek-reasoner",
  "autostart": true,
  "notify_on_startup": true
}
```

---

## 🔨 Hướng Dẫn Đóng Gói Lại File `.exe`
Nếu bạn chỉnh sửa mã nguồn và muốn đóng gói lại:
- Click đúp vào file **`build_exe.bat`**
- Hoặc chạy lệnh trong terminal:
  ```bash
  python tools/companion/build_companion.py
  ```
- File thực thi sau khi đóng gói sẽ nằm tại: `tools/companion/dist/DecorSpaceCompanion.exe`.
