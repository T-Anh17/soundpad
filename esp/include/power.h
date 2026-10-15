#pragma once
#include <Arduino.h>

void powerBegin();                 // tắt nguồn màn hình, đọc pin lần đầu
void powerUpdate(bool audioBusy);  // đo pin định kỳ (bỏ qua lúc đang phát vì áp bị sụt)

int  batteryMv();
int  batteryPercent();
bool batteryCritical();

uint32_t wakeKeyMask();            // bit i = phím i đã đánh thức chip khỏi ngủ sâu, bit KEY_COUNT = nút KEY
void lightSleep(uint32_t maxMs);   // ngủ nhẹ tới khi nhấn phím hoặc hết maxMs
[[noreturn]] void deepSleep();     // ngủ sâu, nhấn phím bất kỳ để dậy
