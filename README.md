# soundpad

SoundPad: 4 phím cơ phát âm thanh, chạy pin, cài đặt qua Bluetooth LE. Xem [PLAN.md](PLAN.md).

- `esp/` firmware (PlatformIO): `pio run -e soundpad -t upload`
- `web/` trang cài đặt (React + Web Bluetooth): `cd web && npm install && npm run dev`

## Nạp firmware (nút RST trên board đã hỏng)

Không cần nút RST: ESP32-S3 nạp qua cổng USB gốc, `pio` tự đưa chip vào chế độ nạp rồi tự khởi động lại.

1. Cắm USB vào máy tính.
2. Nhấn 1 phím bất kỳ để đánh thức board (đang ngủ sâu thì cổng COM không hiện). Thức rồi mà thấy cắm USB thì board không ngủ nữa, cổng COM giữ nguyên.
3. `cd esp && pio run -t upload` (bản dùng thật; bản có log: `pio run -e soundpad-debug -t upload`).

Không thấy cổng COM / nạp báo lỗi kết nối (firmware treo, khởi động lại liên tục): vào chế độ nạp bằng tay, thay RST bằng ngắt điện.

1. Rút USB, gạt công tắc pin sang OFF (board tắt hẳn).
2. Giữ nút BOOT (GPIO0) trên board, cắm USB, chờ 1 giây rồi thả.
3. Chạy lại lệnh nạp. Nạp xong mà board chưa chạy thì rút USB cắm lại.
