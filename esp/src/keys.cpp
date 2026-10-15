// Đọc 4 phím, nhận ra nhấn đơn và tổ hợp 2 phím.
//
// Nhấn 1 phím -> chờ COMBO_WINDOW_MS xem có phím thứ 2 không:
//   không có     -> phát âm của phím đó
//   [1]+[2]      -> giảm âm lượng (giữ: giảm liên tục)
//   [3]+[4]      -> tăng âm lượng (giữ: tăng liên tục)
//   tổ hợp khác  -> bỏ qua
// Nút KEY có sẵn trên board (WIFI_KEY_PIN) -> bật/tắt WiFi

#include "keys.h"
#include "app.h"
#include "audio.h"
#include "config.h"

namespace {

enum Combo : uint8_t { NONE, VOL_DOWN, VOL_UP };

bool down[KEY_COUNT];          // trạng thái đã chống dội
uint32_t changedAt[KEY_COUNT];
bool wifiDown;                 // nút KEY
uint32_t wifiChangedAt;

int8_t firstKey = -1;          // phím đầu đang chờ cửa sổ tổ hợp
uint32_t firstAt;

Combo combo = NONE;            // tổ hợp đang giữ
uint8_t comboA, comboB;
uint32_t nextRepeat;

Combo comboOf(uint8_t a, uint8_t b) {
  if (a > b) std::swap(a, b);
  if (a == 0 && b == 1) return VOL_DOWN;
  if (a == 2 && b == 3) return VOL_UP;
  return NONE;
}

void onPress(uint8_t k, uint32_t now) {
  audioWake();                // bật sẵn ngõ ra âm thanh trong lúc chờ tổ hợp
  if (combo != NONE) return;  // đang giữ tổ hợp: bỏ qua phím khác

  if (firstKey < 0) {
    firstKey = k;
    firstAt = now;
    return;
  }

  // phím thứ 2 trong cửa sổ chờ
  uint8_t a = firstKey;
  firstKey = -1;
  Combo c = comboOf(a, k);
  if (c == NONE) return;

  combo = c;
  comboA = a;
  comboB = k;
  onVolumeStep(c == VOL_UP ? 1 : -1);
  nextRepeat = now + VOLUME_REPEAT_DELAY_MS;
}

}  // namespace

void keysBegin(uint32_t wakeMask) {
  for (uint8_t i = 0; i < KEY_COUNT; i++) pinMode(KEY_PINS[i], INPUT_PULLUP);
  pinMode(WIFI_KEY_PIN, INPUT_PULLUP);
  delay(2);

  // Phím đã đánh thức chip nhưng được nhả ra trước khi khởi động xong -> coi như vừa nhấn.
  // Phím vẫn đang giữ sẽ được keysUpdate() nhận ra bình thường.
  uint32_t now = millis();
  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    if ((wakeMask & (1u << i)) && digitalRead(KEY_PINS[i]) == HIGH) onPress(i, now);
  }
  if ((wakeMask & (1u << KEY_COUNT)) && digitalRead(WIFI_KEY_PIN) == HIGH) onWifiToggle();
}

void keysUpdate() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    bool raw = digitalRead(KEY_PINS[i]) == LOW;
    // nhận thay đổi ngay, rồi khoá DEBOUNCE_MS để bỏ qua dội phím
    if (raw != down[i] && now - changedAt[i] >= DEBOUNCE_MS) {
      down[i] = raw;
      changedAt[i] = now;
      if (raw) onPress(i, now);
    }
  }

  bool raw = digitalRead(WIFI_KEY_PIN) == LOW;
  if (raw != wifiDown && now - wifiChangedAt >= DEBOUNCE_MS) {
    wifiDown = raw;
    wifiChangedAt = now;
    if (raw) onWifiToggle();
  }

  if (combo != NONE) {
    if (!down[comboA] || !down[comboB]) {
      onVolumeDone();
      combo = NONE;
    } else if ((int32_t)(now - nextRepeat) >= 0) {
      onVolumeStep(combo == VOL_UP ? 1 : -1);
      nextRepeat = now + VOLUME_REPEAT_MS;
    }
  }

  if (firstKey >= 0 && now - firstAt >= COMBO_WINDOW_MS) {
    uint8_t k = firstKey;
    firstKey = -1;
    onKeyPlay(k);
  }
}

bool keysActive() {
  if (firstKey >= 0 || combo != NONE || wifiDown) return true;
  for (uint8_t i = 0; i < KEY_COUNT; i++)
    if (down[i]) return true;
  return false;
}
