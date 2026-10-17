#pragma once
#include <Arduino.h>

enum Tone : uint8_t {
  TONE_NONE,
  TONE_WIFI_ON,
  TONE_WIFI_OFF,
  TONE_VOLUME,
  TONE_BOOT,  // tiếng ding lúc gạt công tắc mở nguồn
};

void audioBegin();
void audioPlayKey(uint8_t key);  // phát âm của phím (cắt âm đang phát)
void audioTone(Tone t);
void audioStop();             // dừng và chờ đóng file (trước khi ghi đè/xoá)
void audioWake();             // vừa chạm phím: bật sẵn ngõ ra trong lúc chờ tổ hợp
void audioOff();              // tắt ampli và I2S (không chờ, xem audioIdle) -> ngủ nhẹ
void audioShutdown();         // tắt hẳn (trước khi ngủ sâu)

int  audioPlayingKey();       // phím đang phát, -1 nếu không
bool audioBusy();             // đang phát âm phím hoặc âm báo
bool audioIdle();             // đã tắt ampli và I2S -> ngủ được

void    audioSetVolume(uint8_t v);  // 0..100
uint8_t audioVolume();

String soundPath(uint8_t key);        // âm phím, PCM 16-bit
String legacySoundPath(uint8_t key);  // âm phím bản cũ (ADPCM), vẫn phát được tới khi nạp lại
