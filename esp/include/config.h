#pragma once
#include <Arduino.h>

// ---------------- Chân ----------------
static const uint8_t KEY_PINS[] = {1, 2, 3, 10};  // phím [1] [2] [3] [4], chân còn lại -> GND
#define KEY_COUNT 4
#define WIFI_KEY_PIN   14   // nút KEY có sẵn trên board: nhấn để bật/tắt WiFi

#define AUDIO_PIN      16   // tín hiệu âm thanh -> 1kΩ -> HXJ8002 R (tụ 15nF xuống GND)
#define AUDIO_CLK_PIN  12   // xung PDM nội bộ, KHÔNG nối gì
#define BAT_ADC_PIN    4    // đo pin, có sẵn trên board (chia áp 1/2)
#define LCD_POWER_PIN  15   // nguồn màn hình (đã tháo) -> giữ LOW cho đỡ hao
#define LCD_BL_PIN     38   // đèn nền màn hình -> giữ LOW

// ---------------- Âm thanh ----------------
#define SAMPLE_RATE      22050  // phải trùng RATE trong web/index.html
#define VOLUME_DEFAULT   70     // %
#define VOLUME_STEP      5      // % mỗi nấc khi chỉnh bằng phím
#define TONE_LEVEL       0.004f  // tiếng xác nhận chỉnh âm lượng, nhẹ hơn âm phím
#define WIFI_TONE_LEVEL  0.00005f // âm báo WiFi cực nhẹ, tách biệt với âm lượng âm phím
#define WIFI_SOUND_GAIN  0.3f    // tiếng ding WiFi so với âm phím (0,3 ≈ nhỏ hơn 10 dB). Quá nhỏ (vd 0,004) thì chỉ còn tiếng xì
#define VOLUME_MAX_GAIN  0.11f  // mức to nhất. PDM chế độ DAC to gấp đôi codec cũ (cũ 0,22, rè từ ~0,3). Giảm nếu loa rè hoặc board tự khởi động lại khi kêu to

// Bật/tắt ngõ ra âm thanh: mức nghỉ được nâng/hạ từ từ để loa không kêu "bụp"
#define AUDIO_BOOT_RAMP_MS  400   // lúc khởi động (bản không ngủ: bật một lần rồi giữ luôn)
#define AUDIO_RAMP_UP_MS    45    // lúc thức dậy, lọt trong COMBO_WINDOW_MS nên không làm phím chậm
#define AUDIO_RAMP_DOWN_MS  300   // trước khi ngủ
#define AUDIO_OFF_IDLE_MS   3000  // (bản có ngủ) im bao lâu thì hạ mức, tắt I2S để ngủ nhẹ

// ---------------- Phím ----------------
#define DEBOUNCE_MS             15
#define COMBO_WINDOW_MS         50   // chờ phím thứ 2 trước khi phát
#define VOLUME_REPEAT_DELAY_MS  400  // giữ tổ hợp bao lâu thì bắt đầu đổi liên tục
#define VOLUME_REPEAT_MS        250

// ---------------- WiFi ----------------
#define AP_SSID           "SoundPad"
#define AP_PASS           ""   // trống = mạng mở. Muốn đặt mật khẩu thì ít nhất 8 ký tự
#define WIFI_IDLE_OFF_MS  (2 * 60 * 1000UL)

// ---------------- Pin & ngủ ----------------
#define DEEP_SLEEP_IDLE_MS  (5 * 60 * 1000UL)
#define BAT_CRITICAL_MV     3300  // dưới mức này: không phát, ngủ sâu

#ifndef ENABLE_SLEEP
#define ENABLE_SLEEP 1
#endif
