#pragma once
#include <Arduino.h>

enum Tone : uint8_t {
  TONE_NONE,
  TONE_BLE_ON,   // chuông bật Bluetooth (assets/wifi_on.adp, tên file từ thời WiFi)
  TONE_BLE_OFF,
  TONE_VOLUME,
  TONE_BOOT,  // tiếng ding lúc gạt công tắc mở nguồn
  TONE_TEST_SOFT,  // bản thử: chuỗi tông 400 Hz–4 kHz để đo méo loa (gõ t / T qua Serial)
  TONE_TEST_LOUD,
};

void audioBegin();
void audioPlaySound(int id);  // phát âm số id (xem sounds.h), cắt âm đang phát
void audioTone(Tone t);
void audioStop();             // dừng và chờ đóng file (trước khi ghi đè/xoá)
void audioWake();             // vừa chạm phím: bật sẵn ngõ ra trong lúc chờ tổ hợp
void audioOff();              // tắt ampli và I2S (không chờ, xem audioIdle) -> ngủ nhẹ
void audioShutdown();         // tắt hẳn (trước khi ngủ sâu)

int  audioPlayingSound();     // âm đang phát, -1 nếu không
bool audioBusy();             // đang phát âm phím hoặc âm báo
bool audioIdle();             // đã tắt ampli và I2S -> ngủ được

void    audioSetVolume(uint8_t v);  // 0..100
uint8_t audioVolume();

String soundPath(int id);        // âm số id, PCM 16-bit
String legacySoundPath(int id);  // âm bản cũ (ADPCM), vẫn phát được tới khi nạp lại
