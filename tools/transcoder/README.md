# 📡 TRẠM CHUYỂN MÃ ĐA PHƯƠNG TIỆN (PC TRANSCODER) CHO ESP32-S3

Hệ thống giải pháp biến máy tính / máy chủ cục bộ (PC/Server) thành một **Trạm chuyển mã trung gian (Realtime Transcoder)** chuyên dụng để stream video và âm thanh từ các link nhúng trực tuyến (YouTube, TikTok, Facebook, Twitch, MP4, HLS...) xuống thiết bị **ESP32-S3 Trạm Decor Vũ Trụ**.

---

## 🎯 1. Tại sao cần Trạm Chuyển Mã trên PC?
Vi xử lý ESP32-S3 tuy mạnh mẽ nhưng bị giới hạn về phần cứng:
- Không thể chạy trình duyệt đầy đủ để giải mã các giao thức mã hóa video phức tạp như H.264/H.265/AV1/VP9 từ YouTube hay TikTok ở độ phân giải gốc 1080p/4K.
- Bộ nhớ RAM giới hạn không cho phép giải mã song song đa luồng video và âm thanh nén cao cấp cùng lúc.

👉 **Giải pháp tối ưu:**
1. **Máy tính (PC/Server)** tải stream từ link nhúng qua `yt-dlp` và dùng phần cứng PC (`FFmpeg`) để giải mã tức thời.
2. Băm nhỏ video thành chuỗi ảnh **MJPEG (240x240 hoặc 240x320) @ 15 FPS**, nén JPEG vừa vặn màn hình ST7789.
3. Chuyển đổi âm thanh thành **PCM thuần 16-bit Mono @ 16,000 Hz**.
4. Stream qua mạng cục bộ (LAN) xuống ESP32:
   - **Core 0 (ESP32):** Nhận socket, đẩy audio vào Ring Buffer PSRAM (384 KB ~ 12s) và phát mượt qua loa I2S DAC MAX98357A.
   - **Core 1 (ESP32):** Giải mã JPEG siêu nhanh bằng `TJpg_Decoder` và bắn SPI lên màn hình ST7789.

Nhờ đó, ESP32 chạy cực kỳ nhẹ nhàng, không bị nóng, không bị nghẽn mạng và đạt độ mượt mà cao nhất!

---

## 🚀 2. Hướng dẫn sử dụng (Cách nhanh nhất)

### Cách 1: Chạy 1-Click bằng tệp Batch
Chỉ cần nhấp đúp vào tệp:
`start_transcoder.bat` ngay tại thư mục gốc của dự án.
Trình duyệt sẽ tự động mở giao diện tại: **`http://localhost:5050`**

### Cách 2: Chạy bằng dòng lệnh Python
```bash
# Cài đặt thư viện cần thiết
pip install -r tools/transcoder/requirements.txt

# Khởi động Web Dashboard
python tools/transcoder/transcoder.py

# Hoặc stream trực tiếp 1 link YouTube xuống ESP32 qua CLI:
python tools/transcoder/transcoder.py "https://www.youtube.com/watch?v=dQw4w9WgXcQ" --esp 192.168.1.15
```

---

## 🎛️ 3. Các tính năng chính của Bảng điều khiển Web (Port 5050)
1. **Nhập link đa dạng:**
   - Hỗ trợ link YouTube (Video thông thường, Shorts, Livestream).
   - Link mạng xã hội: TikTok, Facebook Video, Twitch, Twitter/X, Bilibili...
   - Link trực tiếp: MP4, M3U8, WEBM, MP3.
   - Tệp video/audio lưu trên máy tính (nhập đường dẫn tệp ví dụ `C:/Videos/sample.mp4`).
2. **Tùy chọn độ phân giải:**
   - `240x240`: Vuông chuẩn (tối ưu nhất, viền đen điện ảnh).
   - `240x320`: Toàn màn hình dọc ST7789.
   - `crop240`: Crop tâm tràn viền.
3. **Điều chỉnh tốc độ khung hình (FPS):** 12 FPS, 15 FPS (mặc định tối ưu), 20 FPS.
4. **Màn hình Live Preview:** Hiển thị trực tiếp khung hình đang phát trên màn hình ESP32.
5. **Điều khiển âm lượng Loa I2S:** Thanh trượt từ 0% đến 100%.

---

## ⚙️ 4. Cấu trúc tệp
- `transcoder.py`: Máy chủ Flask, bộ phân tích `yt-dlp` và engine `FFmpeg` chuyển mã.
- `start_transcoder.bat`: Trình khởi chạy 1-click cho Windows.
- `requirements.txt`: Các thư viện Python cần thiết (`yt-dlp`, `imageio-ffmpeg`, `requests`, `flask`).
- `transcoder_config.json`: Lưu cấu hình IP ESP32 và tùy chọn gần nhất.

