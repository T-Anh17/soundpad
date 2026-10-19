#pragma once
#include <Arduino.h>

enum Tone : uint8_t {
  TONE_NONE,
  TONE_WIFI_ON,
  TONE_WIFI_OFF,
  TONE_VOLUME,
  TONE_BOOT,  // tiếng ding lúc gạt công tắc mở nguồn
  TONE_TEST_SOFT,  // bản thử: chuỗi tông 400 Hz–4 kHz để đo méo loa (gõ t / T qua Serial)
  TONE_TEST_LOUD,
};

void audioBegin();
void audioPlaySound(uint8_t slot);  // phát âm ô slot (0..SOUND_COUNT-1), cắt âm đang phát
void audioTone(Tone t);
void audioStop();             // dừng và chờ đóng file (trước khi ghi đè/xoá)
void audioWake();             // vừa chạm phím: bật sẵn ngõ ra trong lúc chờ tổ hợp
void audioOff();              // tắt ampli và I2S (không chờ, xem audioIdle) -> ngủ nhẹ
void audioShutdown();         // tắt hẳn (trước khi ngủ sâu)

int  audioPlayingSound();     // ô âm đang phát, -1 nếu không
bool audioBusy();             // đang phát âm phím hoặc âm báo
bool audioIdle();             // đã tắt ampli và I2S -> ngủ được

void    audioSetVolume(uint8_t v);  // 0..100
uint8_t audioVolume();

String soundPath(uint8_t slot);        // ô âm, PCM 16-bit
String legacySoundPath(uint8_t slot);  // âm bản cũ (ADPCM), vẫn phát được tới khi nạp lại
