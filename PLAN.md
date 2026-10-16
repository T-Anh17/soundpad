# SoundPad — Plan

4 phím cơ, mỗi phím phát 1 đoạn âm thanh. Đổi âm thanh và chỉnh âm lượng qua web local. Chạy pin.

## 1. Phần cứng

| Linh kiện | Ghi chú |
|---|---|
| ESP32-S3 (LilyGO T-Display-S3, màn hình hỏng) | |
| 4 switch phím cơ `[1] [2] [3] [4]` | GPIO 1 / 2 / 3 / 10, chân còn lại → GND (dùng pull-up trong chip) |
| Ampli số MAX98357A (I2S, 3W) | VIN → **cực (+) pin, sau công tắc** (không lấy 3V3), GND → (−) pin bằng dây riêng. DIN → GPIO 11, BCLK → GPIO 12, LRC → GPIO 13, SD → GPIO 18. Chân GAIN để trống (9 dB) |
| Tụ hoá 100–470 µF | Hàn thẳng VIN–GND của module ampli, đúng cực |
| Loa mini điện thoại | Không nối chân loa nào xuống GND |
| Pin Li-Po 3.7V 500 mAh, có mạch bảo vệ | Sạc qua USB của board (~500 mA = 1C). Phải gạt công tắc ON mới sạc |
| Công tắc gạt mini trên dây (+) pin | Tắt hẳn khi cất máy |

Sơ đồ:

```
Phím [1] [2] [3] [4] ── GPIO 1 / 2 / 3 / 10      chân còn lại ── GND

GPIO 11 ── MAX98357A DIN            MAX98357A VIN  ── (+) pin, sau công tắc, + tụ 100–470µF xuống GND
GPIO 12 ── MAX98357A BCLK           MAX98357A GND  ── (−) pin (dây riêng; GND board cũng về đây)
GPIO 13 ── MAX98357A LRC            MAX98357A GAIN ── để trống
GPIO 18 ── MAX98357A SD             MAX98357A + / − ── loa
```

- Chân SD (SD_MODE): firmware kéo thấp = ampli tắt (<1 µA) khi rảnh, lúc ngủ nhẹ và ngủ sâu; kéo
  cao = bật. Chưa nối thì đặt `AMP_SD_PIN -1` (module tự bật nhờ điện trở kéo lên có sẵn, tốn ~2,4 mA lúc chờ).
- Chân GAIN: để trống = 9 dB. Nối GND = 12 dB, GND qua 100kΩ = 15 dB, VIN = 6 dB, VIN qua 100kΩ = 3 dB.
  Âm lượng tối đa chỉnh bằng `VOLUME_MAX_GAIN` trong `esp/include/config.h`.
- Ra loa là kiểu cầu (BTL): **không nối chân loa nào xuống GND**.
- Ampli ăn thẳng từ pin để dòng lúc kêu to không làm sụt 3V3 (méo, board tự khởi động lại).

## 2. Phím

| Thao tác | Kết quả |
|---|---|
| Nhấn 1 phím | Chờ 0,05 s; không có phím thứ 2 → phát âm của phím đó |
| Đang phát, nhấn phím khác | Tắt âm cũ, phát âm mới |
| Nhấn lại phím đang phát | Phát lại từ đầu |
| `[3]+[4]` | Tăng âm lượng; giữ → tăng liên tục; mỗi nấc bíp ở mức mới |
| `[1]+[2]` | Giảm âm lượng; giữ → giảm liên tục; mỗi nấc bíp ở mức mới |
| Nút **KEY** có sẵn trên board (GPIO 14) | Bật/tắt WiFi |
| Tổ hợp khác | Bỏ qua |

## 3. WiFi

- Mở máy: WiFi mặc định tắt. Bật/tắt bằng nút KEY trên board.
- Không ai dùng web trong 2 phút → WiFi tự tắt.

## 4. Pin và tiết kiệm điện

1. Web hiện % pin (đọc điện áp pin qua chân có sẵn trên board).
2. Nhấn phím chỉ phát đúng âm của phím, không kèm âm báo nào.
3. Pin < ~3,3 V → không phát, ngủ sâu (không kêu).
4. Đang dùng: chip ngủ nhẹ, nhấn phím phát ngay.
5. Không dùng 5 phút và WiFi tắt → ngủ sâu (không kêu). Ampli tắt bằng chân SD (<1 µA).
6. Xung nhịp thấp (80 MHz), giảm công suất WiFi, giới hạn âm lượng tối đa, tắt mạch nguồn màn hình (GPIO15).

Thức dậy từ ngủ sâu:

| Cách | Kết quả |
|---|---|
| Nhấn 1 phím | Dậy (~0,3–0,5 s) và phát âm của phím đó |
| Giữ tổ hợp phím | Dậy và làm việc của tổ hợp (giữ tới khi nghe bíp) |
| Nhấn nút KEY | Dậy và bật WiFi |
| Gạt công tắc / nút RST | Khởi động lại |
| Cắm USB | Không tự dậy, vẫn sạc. Đang thức mà cắm vào máy tính thì không ngủ nữa (giữ cổng COM) cho tới khi rút |
| Ngủ vì pin cạn, nhấn phím | Dậy kiểm tra pin; còn yếu → ngủ lại, không kêu |

Ước lượng với pin 500 mAh (đo lại khi test):

| Trạng thái | Dòng | Thời gian |
|---|---|---|
| Công tắc tắt | 0 | Không hao |
| Bật, chờ / ngủ sâu | ~4–8 mA | ~3–4 ngày |
| Đang phát | ~80–200 mA | ~2,5–6 giờ phát liên tục |
| WiFi bật 2 phút | ~100–150 mA | ~1% pin mỗi lần |

## 5. Âm báo (board tự tạo)

| Sự kiện | Âm báo |
|---|---|
| Bật WiFi | Chuông báo sân bay, giây 1–5 (`esp/assets/wifi_chime.mp3` → `wifi_on.adp`, nhúng vào firmware) |
| Tắt WiFi | Chuông báo sân bay, giây 9–13 (→ `wifi_off.adp`) |
| Đổi âm lượng | Bíp ở mức mới |

Nhấn 1 phím không có âm báo, chỉ phát âm của phím.

## 6. Lưu âm thanh

- Lưu trong flash 16 MB, phân vùng FFat ~12,8 MB. Âm thanh nén IMA ADPCM 4-bit, 22321 Hz mono (~11 KB/giây) → ~20 phút tổng cộng.
- Mất điện vẫn nhớ âm thanh và âm lượng.

## 7. Web local

- Bật WiFi → điện thoại vào WiFi "SoundPad" (mạng mở) → trang cài đặt tự bật lên (captive portal). Nếu không tự bật: mở trình duyệt vào `192.168.4.1`.
- Mỗi phím: tên âm thanh, độ dài, nút Phát thử / Đổi (mp3, m4a, wav, hoặc video mp4 / mov) / Xoá.
- Chọn file xong hiện hộp chọn đoạn: sóng âm, kéo hai đầu hoặc gõ giây Từ / Đến, nghe thử trên điện thoại rồi mới lưu.
- Trình duyệt tự tách âm thanh khỏi video, đổi định dạng, cắt khoảng lặng, chuẩn hoá âm lượng, nén ADPCM trước khi gửi lên.
- Thanh âm lượng (đồng bộ với âm lượng chỉnh bằng phím).
- Phím đang phát sáng viền. Hiện bộ nhớ còn trống và % pin.

## 8. Thư mục và thứ tự làm

```
soundpad/
├── esp/    firmware (PlatformIO, Arduino core 3.x)
├── docs/   sơ đồ nối dây (wiring.html)
└── web/    giao diện web
```

1. [x] Giao diện web + server giả lập để xem trên máy tính
2. [x] Firmware: phím, âm thanh, WiFi, pin, chế độ ngủ (PlatformIO, đã build; chưa test trên board)
3. [ ] Ghép web vào firmware, nạp board, test thật, đo dòng
4. [x] Sơ đồ nối dây chi tiết có hình → `docs/wiring.html`
