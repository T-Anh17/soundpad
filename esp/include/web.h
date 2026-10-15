#pragma once

void webBegin();    // đăng ký các trang (WiFi vẫn tắt)
void webUpdate();   // gọi trong loop(): DNS, HTTP, tự tắt WiFi khi không ai dùng

void wifiOn();
void wifiOff();
bool wifiIsOn();
