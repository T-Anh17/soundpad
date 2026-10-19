#pragma once
#include <Arduino.h>

// Kho âm thanh không giới hạn số lượng (chỉ giới hạn bởi bộ nhớ trống). Mỗi âm có số hiệu id
// (0, 1, 2…, file /s<id+1>.p16), tên lưu chung trong /names.txt. Cách phím chọn âm:
//   chế độ đơn       -> phím phát âm được gán cho nó (mặc định phím N -> âm số N)
//   chế độ ngẫu nhiên -> phím nào cũng phát một âm bất kỳ (không lặp lại âm vừa phát)
//
// Danh sách được đọc cả từ task Bluetooth (soundsPage) nên có khoá bên trong.

void soundsBegin();               // đọc cài đặt, quét các âm đã có (sau FFat.begin)
void soundsRefresh();             // quét lại sau khi nạp / xoá âm
bool soundExists(int id);
size_t soundBytes(int id);        // cỡ theo PCM 16-bit (âm bản cũ ADPCM quy đổi x4), 0 = không có
size_t soundCount();
int  soundForKey(uint8_t key);    // âm sẽ phát khi nhấn phím, -1 = không có
int  soundNewId();                // số hiệu trống nhỏ nhất cho âm mới, -1 = hết số

String soundName(int id);
void setSoundName(int id, const String &name);  // "" = xoá tên
uint32_t soundsVersion();         // tăng mỗi khi danh sách / tên đổi

// Một trang danh sách từ vị trí from, mỗi dòng "id\tcỡ\ttên", tối đa maxBytes.
// next = vị trí trang sau, hoặc -1 nếu đã hết
String soundsPage(size_t from, size_t maxBytes, int &next);

int  keySound(uint8_t key);       // âm gán cho phím (chế độ đơn)
void setKeySound(uint8_t key, int id);
bool randomMode();
void setRandomMode(bool on);
