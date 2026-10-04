# Dịch Vụ Giám Sát Phần Cứng PC (PC Hardware Telemetry Service)

Dịch vụ chạy trên máy tính Windows để đọc các thông số phần cứng thực tế và truyền về ESP32 qua Wi-Fi để hiển thị lên màn hình **PC Status HUD** (240x320).

---

## 1. Các Thông Số Được Thu Thập
- **CPU**: % Tải thực tế, Xung nhịp (GHz), Số luồng/core, Nhiệt độ (°C).
- **GPU**: % Sử dụng (Hỗ trợ card rời NVIDIA và đồ họa tích hợp Intel HD/AMD Radeon qua Windows PDH Engine), Nhiệt độ (°C), VRAM đã dùng / tổng VRAM, Công suất (Watts).
- **RAM**: % Bộ nhớ đã dùng, Dung lượng đã dùng (GB) / Tổng dung lượng RAM (GB).
- **Ổ Cứng**: % Dung lượng ổ đĩa hệ thống C:.
- **Mạng**: Tốc độ tải về Download (KB/s hoặc MB/s) và Tải lên Upload (KB/s).

---

## 2. Cách Khởi Chạy

### Cách 1: Chạy Có Cửa Sổ Giám Sát Trực Quan
- Bấm đúp chuột vào file **`start_service.bat`**.
- Màn hình console sẽ hiện bảng log thông số phần cứng trực tiếp và trạng thái truyền về ESP32 theo thời gian thực.

### Cách 2: Chạy Hoàn Toàn Ẩn Danh Dưới Nền (Background Service)
- Bấm đúp chuột vào file **`start_service_hidden.vbs`**.
- Tiến trình sẽ âm thầm chạy ngầm dưới nền Windows mà không hiện bất kỳ cửa sổ dòng lệnh nào.
- Để dừng dịch vụ khi đang chạy ngầm: Bấm đúp vào file **`stop_service.bat`**.

### Cách 3: Tự Động Chạy Cùng Windows Khi Khởi Động Máy
1. Nhấn tổ hợp phím **`Windows + R`**, nhập: `shell:startup` rồi bấm Enter (thư mục Startup sẽ mở ra).
2. Tạo một **Shortcut** trỏ tới file `start_service_hidden.vbs` và dán vào thư mục Startup này.
3. Từ nay, mỗi khi bạn bật máy tính, thông số PC sẽ tự động gửi tới Trạm Decor ESP32 mà không cần bấm gì thêm!

---

## 3. Cấu Hình Địa Chỉ IP ESP32
Mở file `config.json` để thay đổi địa chỉ IP của ESP32 nếu mạng nhà bạn cấp IP khác:
```json
{
  "esp32_ip": "192.168.1.233",
  "fallback_host": "tramvutru.local",
  "interval_seconds": 1.0,
  "timeout_seconds": 1.5
}
```
*(Mẹo: Bạn có thể xem địa chỉ IP hiện tại của ESP32 ngay trên dải thông tin đầu trang Web Control Center).*

