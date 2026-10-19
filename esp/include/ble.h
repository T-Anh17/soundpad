#pragma once

void bleBegin();    // chuẩn bị (Bluetooth vẫn tắt)
void bleUpdate();   // gọi trong loop(): xử lý lệnh / dữ liệu nạp âm, tự tắt khi không ai dùng

void bleOn();
void bleOff();
bool bleIsOn();
bool bleStackUp();  // ngăn xếp Bluetooth đã chạy (từ lần bật đầu tới lúc ngủ sâu) -> không ngủ nhẹ được
