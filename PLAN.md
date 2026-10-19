# SoundPad — Plan

4 phím cơ phát âm thanh từ kho âm không giới hạn số lượng (chỉ giới hạn bởi bộ nhớ). Chế độ đơn: mỗi phím phát âm được gán; chế độ ngẫu nhiên: phím nào cũng phát 1 âm bất kỳ. Nạp âm, gán phím, chọn chế độ và chỉnh âm lượng qua web (React) kết nối Bluetooth LE. Chạy pin.

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
| Nhấn 1 phím | Chờ 0,1 s; không có phím thứ 2 → chế độ đơn: phát âm gán cho phím; chế độ ngẫu nhiên: phát 1 trong các âm đã nạp (không lặp lại âm vừa phát) |
| Đang phát, nhấn phím khác | Tắt âm cũ, phát âm mới |
| Nhấn lại phím đang phát | Phát lại từ đầu |
| `[3]+[4]` | Tăng âm lượng; giữ → tăng liên tục; mỗi nấc bíp ở mức mới |
| `[1]+[2]` | Giảm âm lượng; giữ → giảm liên tục; mỗi nấc bíp ở mức mới |
| Giữ `[1]+[4]` ~1 giây | Bật/tắt Bluetooth (nhả sớm thì bỏ qua) |
| Tổ hợp khác | Bỏ qua |

## 3. Bluetooth LE

- Tên thiết bị "SoundPad" (`BLE_NAME` trong `esp/include/config.h`). Không còn WiFi.
- Mở máy: Bluetooth mặc định tắt. Bật/tắt bằng cách giữ `[1]+[4]` ~1 giây (nghe chuông).
- Bật mà không ai kết nối trong 2 phút → tự tắt. Trang web còn kết nối thì không tắt.
- Giao thức (dịch vụ GATT, lệnh chữ + dữ liệu nạp âm): xem đầu `esp/src/ble.cpp`.
- Sau khi đã bật Bluetooth một lần, chip không ngủ nhẹ nữa tới lần ngủ sâu kế tiếp (5 phút không dùng).

## 4. Pin và tiết kiệm điện

1. Web hiện % pin (đọc điện áp pin qua chân có sẵn trên board).
2. Nhấn phím chỉ phát đúng âm của phím, không kèm âm báo nào.
3. Pin < ~3,3 V → không phát, ngủ sâu (không kêu).
4. Đang dùng: chip ngủ nhẹ, nhấn phím phát ngay.
5. Không dùng 5 phút và Bluetooth tắt → ngủ sâu (không kêu). Ampli tắt bằng chân SD (<1 µA).
6. Xung nhịp thấp (80 MHz), giảm công suất phát Bluetooth (0 dBm), giới hạn âm lượng tối đa, tắt mạch nguồn màn hình (GPIO15).

Thức dậy từ ngủ sâu:

| Cách | Kết quả |
|---|---|
| Nhấn 1 phím | Dậy (~0,3–0,5 s) và phát âm của phím đó |
| Giữ tổ hợp phím | Dậy và làm việc của tổ hợp (giữ tới khi nghe bíp) |
| Giữ `[1]+[4]` ~1 giây | Dậy và bật Bluetooth |
| Gạt công tắc / nút RST | Khởi động lại |
| Cắm USB | Không tự dậy, vẫn sạc. Đang thức mà cắm vào máy tính thì không ngủ nữa (giữ cổng COM) cho tới khi rút |
| Ngủ vì pin cạn, nhấn phím | Dậy kiểm tra pin; còn yếu → ngủ lại, không kêu |

Ước lượng với pin 500 mAh (đo lại khi test):

| Trạng thái | Dòng | Thời gian |
|---|---|---|
| Công tắc tắt | 0 | Không hao |
| Bật, chờ / ngủ sâu | ~4–8 mA | ~3–4 ngày |
| Đang phát | ~80–200 mA | ~2,5–6 giờ phát liên tục |
| Bluetooth bật (chip không ngủ) | ~25–40 mA | Ít hơn WiFi cũ (~100–150 mA) nhiều lần |

## 5. Âm báo (board tự tạo)

| Sự kiện | Âm báo |
|---|---|
| Bật Bluetooth | Chuông báo sân bay, giây 1–5 (`esp/assets/wifi_chime.mp3` → `wifi_on.adp`, nhúng vào firmware; tên file từ thời WiFi) |
| Tắt Bluetooth | Chuông báo sân bay, giây 9–13 (→ `wifi_off.adp`) |
| Mở nguồn (gạt công tắc / RST) | Tiếng ding máy bay (`esp/assets/boot_ding.mp3` → `boot_ding.p16`, nâng 1 quãng tám lên ~984 Hz cho loa nhỏ) |
| Đổi âm lượng | Bíp ở mức mới |

Nhấn 1 phím không có âm báo, chỉ phát âm của phím.

## 6. Lưu âm thanh

- Không giới hạn số âm: âm số N là `/sN.p16` (N tới 9999), thêm được tới khi đầy bộ nhớ. Tên các âm lưu chung trong `/names.txt` (bản 10 ô cũ lưu tên trong NVS, tự chuyển sang khi khởi động lần đầu). Phím gán âm nào và chế độ đơn / ngẫu nhiên lưu trong NVS. Phím N mặc định phát âm số N.
- Lưu trong flash 16 MB, phân vùng FFat ~12,8 MB. Âm lưu PCM 16-bit, 22321 Hz mono (~44 KB/giây) → ~4,8 phút tổng cộng. Không nén ADPCM 4-bit nữa: giọng nói bị "sạn" (SNR ~24 dB), rè ở mọi mức âm lượng. Âm phím bản cũ (`/sN.adp`) vẫn phát được tới khi nạp lại.
- Mất điện vẫn nhớ âm thanh và âm lượng.

## 7. Web (React + Web Bluetooth, thư mục `web/`)

- Chạy trên máy tính: `cd web && npm install && npm run dev` → mở `http://localhost:5173` bằng Chrome / Edge.
- Điện thoại: Web Bluetooth chỉ chạy trên trang `https://` (hoặc `localhost`), nên phải đưa bản build (`npm run build` → `web/dist/`) lên host tĩnh có HTTPS (vd GitHub Pages). Android: Chrome. iPhone: Safari không có Web Bluetooth → dùng app Bluefy.
- Bật Bluetooth trên board (`[1]+[4]`) → bấm Kết nối → chọn "SoundPad".
- Chế độ phím: Đơn (4 ô chọn âm cho phím 1–4) / Ngẫu nhiên.
- Danh sách âm: tên, độ dài, phím đang dùng, nút Phát thử / Đổi / Xoá. Nút "+ Thêm âm thanh" ở cuối (mp3, m4a, wav, hoặc video mp4 / mov).
- Chọn file xong hiện hộp chọn đoạn: sóng âm, kéo thanh Từ / Đến, nghe thử trên điện thoại rồi mới lưu.
- Nạp qua BLE chậm hơn WiFi: ~20–50 KB/giây, tức 10 giây âm (~440 KB) mất khoảng 10–20 giây. Web hiện % và thời gian còn lại.
- Trình duyệt tự tách âm thanh khỏi video, đổi định dạng, cắt khoảng lặng, lọc trầm dưới 700 Hz, chỉnh âm cho rõ giọng, nén động nhẹ, chuẩn hoá âm lượng rồi gửi lên dạng PCM 16-bit. Firmware lọc tiếp mọi âm dưới 1,2 kHz (`SPEAKER_HPF_HZ`) trước khi ra loa: loa nhỏ không phát nổi dải dưới, chỉ ra tiếng rè.
- Thanh âm lượng (đồng bộ với âm lượng chỉnh bằng phím).
- Âm đang phát sáng viền. Hiện bộ nhớ còn trống và % pin.

## 8. Thư mục và thứ tự làm

```
soundpad/
├── esp/    firmware (PlatformIO, Arduino core 3.x)
├── docs/   sơ đồ nối dây (wiring.html)
└── web/    giao diện web (React + Vite, Web Bluetooth)
```

1. [x] Giao diện web (bản đầu: HTML + server giả lập; nay: React + Web Bluetooth)
2. [x] Firmware: phím, âm thanh, WiFi → Bluetooth LE, pin, chế độ ngủ (PlatformIO, đã build; chưa test trên board)
3. [ ] Ghép web vào firmware, nạp board, test thật, đo dòng
4. [x] Sơ đồ nối dây chi tiết có hình → `docs/wiring.html`
