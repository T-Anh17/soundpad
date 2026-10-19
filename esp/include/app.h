#pragma once
#include <Preferences.h>

// Dùng chung giữa các module (định nghĩa trong main.cpp)
extern Preferences prefs;

void markActivity();  // có người dùng -> lùi thời điểm ngủ sâu
void saveVolume();

// Sự kiện từ phím (keys.cpp gọi, main.cpp xử lý)
void onKeyPlay(uint8_t key);
void onVolumeStep(int dir);  // +1 tăng, -1 giảm
void onVolumeDone();         // nhả tổ hợp âm lượng
void onBleToggle();
