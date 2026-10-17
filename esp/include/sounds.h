#pragma once
#include <Arduino.h>

// Kho SOUND_COUNT ô âm thanh và cách phím chọn ô:
//   chế độ đơn       -> phím phát ô được gán cho nó (mặc định phím N -> ô N)
//   chế độ ngẫu nhiên -> phím nào cũng phát một ô bất kỳ đã có âm (không lặp lại ô vừa phát)

void soundsBegin();               // đọc cài đặt, quét các ô đã có âm (sau FFat.begin)
void soundsRefresh();             // quét lại sau khi nạp / xoá âm
bool soundExists(uint8_t slot);
size_t soundBytes(uint8_t slot);  // cỡ theo PCM 16-bit (âm bản cũ ADPCM quy đổi x4), 0 = trống
int  soundForKey(uint8_t key);    // ô sẽ phát khi nhấn phím, -1 = không có âm

uint8_t keySound(uint8_t key);    // ô gán cho phím (chế độ đơn)
void setKeySound(uint8_t key, uint8_t slot);
bool randomMode();
void setRandomMode(bool on);
