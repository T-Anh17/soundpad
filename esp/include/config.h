#pragma once
#include <Arduino.h>

// ---------------- Chân ----------------
static const uint8_t KEY_PINS[] = {1, 2, 3, 10};  // phím [1] [2] [3] [4], chân còn lại -> GND
#define KEY_COUNT 4
#define WIFI_KEY_PIN   14   // nút KEY có sẵn trên board: nhấn để bật/tắt WiFi

// Ampli số MAX98357A (I2S). VIN -> (+) pin sau công tắc, GND -> (−) pin. Xem PLAN.md
#define AUDIO_DIN_PIN  11   // -> DIN
#define AUDIO_BCLK_PIN 12   // -> BCLK
#define AUDIO_LRC_PIN  13   // -> LRC (WS)
#define AMP_SD_PIN     18   // -> SD (SD_MODE). Thấp = ampli tắt. Không nối (ampli luôn bật) thì đặt -1
#define BAT_ADC_PIN    4    // đo pin, có sẵn trên board (chia áp 1/2)
#define LCD_POWER_PIN  15   // nguồn màn hình (đã tháo) -> giữ LOW cho đỡ hao
#define LCD_BL_PIN     38   // đèn nền màn hình -> giữ LOW

// ---------------- Âm thanh ----------------
// Phải trùng RATE trong web/index.html. 22321 = 160 MHz / 7168: MCLK = 256 x tần số mẫu
// = 160 MHz / 28 chia chẵn (xung I2S đều). Đổi số này thì các âm đã lưu phát sai cao độ
#define SAMPLE_RATE      22321
#define VOLUME_DEFAULT   70     // %
#define VOLUME_STEP      5      // % mỗi nấc khi chỉnh bằng phím
#define TONE_LEVEL       0.004f  // tiếng xác nhận chỉnh âm lượng, nhẹ hơn âm phím
#define WIFI_TONE_LEVEL  0.00005f // âm báo WiFi cực nhẹ, tách biệt với âm lượng âm phím
#define WIFI_SOUND_GAIN  0.3f    // tiếng ding WiFi so với âm phím (0,3 ≈ nhỏ hơn 10 dB). Quá nhỏ (vd 0,004) thì chỉ còn tiếng xì
#define VOLUME_MAX_GAIN  0.7f   // mức to nhất (1 = hết thang số). Ampli tăng 9 dB (chân GAIN để trống) nên
                                // trên ~0,7 đỉnh âm chạm điện áp pin -> méo. Đã thử 1,0: loa 0,8 W rè khi to.
                                // Vẫn rè -> giảm tiếp (0,6); muốn to hơn thì đổi loa / gắn hộp, đừng tăng số này

#define AUDIO_OFF_IDLE_MS   3000  // (bản có ngủ) im bao lâu thì tắt I2S để ngủ nhẹ

// Tắt ampli bằng chân SD_MODE khi rảnh: loa im tuyệt đối (hết xì nền), đỡ hao pin
#define AMP_WAKE_MS      10    // MAX98357A cần chừng này để bật. Mất đầu âm -> tăng (giữ dưới COMBO_WINDOW_MS)
#define AMP_IDLE_OFF_MS  3000  // im bao lâu thì tắt ampli (bản không ngủ; bản có ngủ tắt theo AUDIO_OFF_IDLE_MS)

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
