#pragma once
#include <Arduino.h>

void keysBegin(uint32_t wakeMask);  // wakeMask: bit i = phím i đã đánh thức chip, bit KEY_COUNT = nút KEY
void keysUpdate();                  // gọi liên tục trong loop()
bool keysActive();                  // đang giữ phím / đang chờ tổ hợp
