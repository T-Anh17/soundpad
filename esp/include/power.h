#pragma once
#include <Arduino.h>

void powerBegin();                 // tắt nguồn màn hình, đọc pin lần đầu
void powerUpdate(bool skip);  // đo pin định kỳ; skip: đang phát (áp bị sụt) hoặc WiFi bật (đo sai)

int  batteryMv();
int  batteryPercent();
bool batteryCritical();

uint32_t wakeKeyMask();            // bit i = phím i đã đánh thức chip khỏi ngủ sâu
void lightSleep(uint32_t maxMs);   // ngủ nhẹ tới khi nhấn phím hoặc hết maxMs
[[noreturn]] void deepSleep();     // ngủ sâu, nhấn phím bất kỳ để dậy
