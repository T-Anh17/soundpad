#pragma once
#include <Arduino.h>

// ---------------- Chân ----------------
static const uint8_t KEY_PINS[] = {1, 2, 3, 10};  // phím [1] [2] [3] [4], chân còn lại -> GND
#define KEY_COUNT 4
#define SOUND_COUNT 10  // số ô âm thanh lưu được (chế độ đơn: mỗi phím gán 1 ô; ngẫu nhiên: phát bất kỳ ô nào)

// Ampli số MAX98357A (I2S). VIN -> (+) pin sau công tắc, GND -> (−) pin. Xem PLAN.md
#define AUDIO_DIN_PIN  11   // -> DIN
#define AUDIO_BCLK_PIN 12   // -> BCLK
#define AUDIO_LRC_PIN  13   // -> LRC (WS)
#define AMP_SD_PIN     18   // -> SD (SD_MODE). Thấp = ampli tắt. Không nối (ampli luôn bật) thì đặt -1
#define BAT_ADC_PIN    4    // đo pin, có sẵn trên board (chia áp 1/2)
#define LCD_POWER_PIN  15   // nguồn màn hình (đã tháo) -> giữ LOW, chỉ bật lúc đo pin
#define LCD_BL_PIN     38   // đèn nền màn hình -> giữ LOW

// ---------------- Âm thanh ----------------
// Tần số mẫu của âm lưu và xử lý. Phải trùng RATE trong web/index.html. Ra I2S thì nhân đôi
// (44642 Hz, MAX98357A không nhận 22 kHz, xem audio.cpp): MCLK = 256 x 44642 = 160 MHz / 14
// chia chẵn (xung I2S đều). Đổi số này thì các âm đã lưu phát sai cao độ
#define SAMPLE_RATE      22321
#define VOLUME_DEFAULT   70     // %
#define SPEAKER_HPF_HZ   1200   // bỏ mọi âm dưới mức này trước khi ra loa (loa nhỏ không phát nổi, chỉ ra
                               // tiếng rè, xem audio.cpp). 1000 vẫn còn hơi rè. Đổi loa lớn hơn thì hạ
                               // xuống (vd 300–500)
#define VOLUME_STEP      5      // % mỗi nấc khi chỉnh bằng phím
#define TONE_LEVEL       0.3f    // tiếng bíp chỉnh âm lượng so với âm phím (0,004 cũ ≈ −57 dBFS: không nghe thấy)
#define WIFI_TONE_LEVEL  0.00005f // âm báo WiFi cực nhẹ, tách biệt với âm lượng âm phím
#define WIFI_SOUND_GAIN  0.3f    // tiếng ding WiFi so với âm phím (0,3 ≈ nhỏ hơn 10 dB). Quá nhỏ (vd 0,004) thì chỉ còn tiếng xì
#define BOOT_SOUND_GAIN  1.0f    // tiếng ding mở nguồn so với âm phím (ding ~984 Hz dưới SPEAKER_HPF_HZ, bị lọc bớt ~7 dB)
#define VOLUME_MAX_GAIN  1.0f   // mức to nhất (1 = hết thang số). Từng hạ còn 0,7 vì rè, nhưng rè thật ra do
                                // I2S 22 kHz (ampli không nhận) và dải trầm loa không phát nổi, đã sửa bằng
                                // I2S 44,6 kHz + SPEAKER_HPF_HZ. Muốn to hơn nữa: chân GAIN của ampli nối GND (12 dB)

#define AUDIO_OFF_IDLE_MS   3000  // (bản có ngủ) im bao lâu thì tắt I2S để ngủ nhẹ

// Tắt ampli bằng chân SD_MODE khi rảnh: loa im tuyệt đối (hết xì nền), đỡ hao pin
#define AMP_WAKE_MS      10    // MAX98357A cần chừng này để bật. Mất đầu âm -> tăng (giữ dưới COMBO_WINDOW_MS)
#define AMP_IDLE_OFF_MS  3000  // im bao lâu thì tắt ampli (bản không ngủ; bản có ngủ tắt theo AUDIO_OFF_IDLE_MS)

// ---------------- Phím ----------------
#define DEBOUNCE_MS             15
#define COMBO_WINDOW_MS         100  // chờ phím thứ 2 trước khi phát (phím đơn trễ chừng này)
#define COMBO_CONFIRM_MS        150  // 2 phím nhấn lệch nhau: giữ cả hai chừng này mới là tổ hợp
#define VOLUME_REPEAT_DELAY_MS  400  // giữ tổ hợp bao lâu thì bắt đầu đổi liên tục
#define VOLUME_REPEAT_MS        250
#define WIFI_HOLD_MS            1000 // giữ [1]+[4] bao lâu thì bật/tắt WiFi

// ---------------- WiFi ----------------
#define AP_SSID           "SoundPad"
#define AP_PASS           "123456788"  // ít nhất 8 ký tự. Trống = mạng mở
#define WIFI_IDLE_OFF_MS  (2 * 60 * 1000UL)

// ---------------- Pin & ngủ ----------------
#define DEEP_SLEEP_IDLE_MS  (5 * 60 * 1000UL)
#define BAT_CRITICAL_MV     3300  // dưới mức này: không phát, ngủ sâu
#define BAT_MIN_VALID_MV    2500  // đọc dưới mức này là đo sai (chip không chạy nổi) -> bỏ qua
#define BAT_CRITICAL_READS  3     // phải đo thấp chừng này lần liền (5s/lần) mới coi là pin cạn
#define BAT_SETTLE_MS       20    // bật nguồn mạch đo pin rồi chờ chừng này mới đọc

#ifndef ENABLE_SLEEP
#define ENABLE_SLEEP 1
#endif
