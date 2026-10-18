# Hướng Dẫn Đồng Bộ Tự Động Ảnh, Video, Tài Liệu Từ Google Drive Vào Dự Án Decor ESP32

Tài liệu này hướng dẫn cách thiết lập tính năng **tự động đồng bộ thời gian thực**: Khi bạn chụp ảnh, quay video hoặc lưu tài liệu từ **điện thoại** và tải vào Google Drive, file sẽ **tự động tải xuống máy tính ngay lập tức** trong thư mục:
```text
Decor_Esp32\assets\drive_sync\
```
Bạn không cần phải tải thủ công từng file trên máy tính nữa.

---

## 1. Cài Đặt Google Drive Cho Máy Tính (Nếu máy chưa có)

1. Tải phần mềm chính thức từ Google: **[Tải Google Drive cho máy tính](https://www.google.com/drive/download/)**
2. Chạy file cài đặt và đăng nhập cùng tài khoản Google mà bạn đang dùng trên điện thoại.

---

## 2. Có 2 Cách Thiết Lập (Chọn 1 trong 2 cách bên dưới)

---

###  Cách A (Khuyên Dùng - Đơn giản nhất, chuẩn 100% của Google)
Cách này thêm trực tiếp thư mục `assets\drive_sync` của dự án vào danh sách đồng bộ của Google Drive:

1. **Trên máy tính**:
   - Nhấp vào biểu tượng **Google Drive** ở góc dưới cùng bên phải thanh Taskbar (gần đồng hồ).
   - Bấm vào biểu tượng **Bánh răng ⚙ (Cài đặt)** > Chọn **Preferences (Tùy chọn)**.
   - Chọn tab **My Computer (Máy tính của tôi)** ở cột bên trái.
   - Bấm nút **Add folder (Thêm thư mục)**.
   - Trỏ tới thư mục: 
     ```text
     C:\Users\admin\Documents\GitHub\Decor_Esp32\assets\drive_sync
     ```
   - Tích chọn ô **Sync with Google Drive (Đồng bộ hóa với Google Drive)** > Bấm **Done (Xong)** > Bấm **Save (Lưu)**.

2. **Trên điện thoại**:
   - Mở ứng dụng **Google Drive**.
   - Ở thanh dưới cùng, chọn tab **Computers (Máy tính)** > Chọn máy tính của bạn > Bạn sẽ thấy thư mục **`drive_sync`** xuất hiện tại đây!
   - *(Mẹo hay)*: Bấm dấu **...** bên cạnh thư mục `drive_sync` > Chọn **"Add to Home screen" (Thêm vào màn hình chính)** trên điện thoại. Từ nay bạn chỉ cần bấm biểu tượng đó trên màn hình điện thoại là mở thẳng thư mục!

3. **Trải nghiệm**:
   - Chụp ảnh hoặc chọn video trên điện thoại > Bấm **Chia sẻ (Share)** > Chọn **Google Drive** > Lưu vào `drive_sync`.
   - Vài giây sau, file sẽ tự động tải về và xuất hiện ngay trong `Decor_Esp32\assets\drive_sync` trên máy tính!

---

###  Cách B (Nếu muốn thư mục nằm trong "Drive của tôi / My Drive")
Nếu bạn muốn thư mục nằm ngay ở trang chính của Google Drive trên điện thoại:

1. **Trên Google Drive**: Tạo một thư mục có tên là `Decor_Sync`.
2. **Trên máy tính**:
   - Mở ổ đĩa ảo **`G:\My Drive`** (hoặc `C:\Users\admin\My Drive`).
   - Nhấp chuột phải vào thư mục `Decor_Sync` > Chọn **Offline access (Truy cập khi không có mạng)** > Chọn **Available offline (Có sẵn khi không có mạng)** *(để Google Drive luôn tự động tải toàn bộ nội dung file về máy tính)*.
   - Bấm đúp vào file **`tools\drive_sync\setup_drive_sync.bat`** trong dự án > Chọn mục **[3]** để tự động liên kết thư mục `Decor_Sync` vào `assets\drive_sync`.

---

## 3. Quản Lý Dung Lượng & Git
- Thư mục `assets\drive_sync\` đã được cấu hình trong [`.gitignore`](file:///c:/Users/admin/Documents/GitHub/Decor_Esp32/.gitignore) của dự án.
- Do đó, các file ảnh/video dung lượng lớn tải từ điện thoại về sẽ **không bao giờ bị đẩy lên GitHub**, giữ cho repository của dự án luôn nhẹ và sạch sẽ.

