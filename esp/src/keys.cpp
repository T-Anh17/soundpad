// Đọc 4 phím, nhận ra nhấn đơn và tổ hợp 2 phím.
//
// Nhấn 1 phím -> chờ COMBO_WINDOW_MS xem có phím thứ 2 không:
//   không có     -> phát âm của phím đó
//   [1]+[2]      -> giảm âm lượng (giữ: giảm liên tục)
//   [3]+[4]      -> tăng âm lượng (giữ: tăng liên tục)
//   [1]+[4]      -> giữ BLE_HOLD_MS thì bật/tắt Bluetooth (nhả sớm: bỏ qua)
//   tổ hợp khác  -> bỏ qua
// Có phím thứ 2 rồi thì chờ thêm TRIPLE_WINDOW_MS xem có phím thứ 3 không (3 phím nhấn "cùng lúc"
// vẫn xuống lệch nhau vài chục ms, và bộ 3 nào cũng chứa một tổ hợp 2 phím ở trên).
//
// 3 phím bất kỳ giữ cùng lúc -> đổi chế độ đơn <-> ngẫu nhiên (một lần, nhả hết phím mới đổi tiếp).
//   Lỡ đã đổi âm lượng (phím thứ 3 tới muộn) thì trả âm lượng về như cũ.
//
// Hai phím xuống lệch nhau hơn COMBO_WINDOW_MS (phím đầu đã phát): phím sau chưa phát vội.
//   giữ cả hai COMBO_CONFIRM_MS -> là tổ hợp: cắt âm phím đầu, làm việc của tổ hợp
//   nhả sớm hơn                 -> chỉ là gõ nhanh 2 phím: phát phím sau như thường

#include "keys.h"
#include "app.h"
#include "audio.h"
#include "config.h"

namespace {

enum Combo : uint8_t { NONE, VOL_DOWN, VOL_UP, BLE, MODE };

bool down[KEY_COUNT];          // trạng thái đã chống dội
uint32_t changedAt[KEY_COUNT];

int8_t firstKey = -1;          // phím đầu đang chờ cửa sổ tổ hợp
uint32_t firstAt;

int8_t pairA = -1, pairB = -1; // 2 phím trong cửa sổ chờ, đang chờ xem có phím thứ 3 không
uint32_t pairAt;

int8_t lateA = -1, lateB = -1; // tổ hợp nhấn lệch đang chờ xác nhận (lateB chưa phát)
uint32_t lateAt;

Combo combo = NONE;            // tổ hợp đang giữ
uint8_t comboA, comboB;
uint32_t nextRepeat;           // âm lượng: lần đổi tiếp theo. Bluetooth: lúc giữ đủ lâu
bool bleDone;                  // đã bật/tắt Bluetooth trong lần giữ này
uint8_t volBefore;             // âm lượng trước tổ hợp âm lượng (trả lại nếu hoá ra là 3 phím)

Combo comboOf(uint8_t a, uint8_t b) {
  if (a > b) std::swap(a, b);
  if (a == 0 && b == 1) return VOL_DOWN;
  if (a == 2 && b == 3) return VOL_UP;
  if (a == 0 && b == 3) return BLE;
  return NONE;
}

// held: lúc bắt đầu giữ cả hai phím
void startCombo(Combo c, uint8_t a, uint8_t b, uint32_t held, uint32_t now) {
  log_i("To hop [%d]+[%d]", a + 1, b + 1);
  combo = c;
  comboA = a;
  comboB = b;
  if (c == BLE) {
    bleDone = false;
    nextRepeat = held + BLE_HOLD_MS;
    return;
  }
  volBefore = audioVolume();
  onVolumeStep(c == VOL_UP ? 1 : -1);
  nextRepeat = now + VOLUME_REPEAT_DELAY_MS;
}

// 3 phím cùng giữ: bỏ mọi việc đang chờ / đang làm, đổi chế độ
void startModeCombo() {
  log_i("To hop 3 phim");
  firstKey = pairA = pairB = lateA = lateB = -1;
  if (combo == VOL_DOWN || combo == VOL_UP) {
    audioSetVolume(volBefore);
    onVolumeDone();
  }
  combo = MODE;
  audioStop();  // cắt âm phím / bíp âm lượng
  onModeToggle();
}

void onPress(uint8_t k, uint32_t now) {
  audioWake();                // bật sẵn ngõ ra âm thanh trong lúc chờ tổ hợp
  // đang giữ / chờ tổ hợp: bỏ qua phím khác (phím thứ 3 do keysUpdate() đếm)
  if (combo != NONE || lateA >= 0 || pairA >= 0) return;

  // Cùng phím xuống lại trong cửa sổ chờ: dội phím (dài hơn DEBOUNCE_MS) hoặc gõ lại thật nhanh.
  // Trước đây bị coi là "phím thứ 2 không thành tổ hợp" -> huỷ luôn, nhấn mà không kêu
  if (firstKey == k) return;

  if (firstKey >= 0) {
    // phím thứ 2 trong cửa sổ chờ: phím đầu chưa phát. Chờ thêm xem có phím thứ 3 không
    pairA = firstKey;
    pairB = k;
    pairAt = now;
    firstKey = -1;
    return;
  }

  // Phím khác đang giữ (đã phát) và hợp thành tổ hợp -> chờ xem có giữ cả hai không
  for (uint8_t a = 0; a < KEY_COUNT; a++) {
    if (a != k && down[a] && comboOf(a, k) != NONE) {
      lateA = a;
      lateB = k;
      lateAt = now;
      return;
    }
  }

  firstKey = k;
  firstAt = now;
}

}  // namespace

void keysBegin(uint32_t wakeMask) {
  for (uint8_t i = 0; i < KEY_COUNT; i++) pinMode(KEY_PINS[i], INPUT_PULLUP);
  delay(2);

  // Phím đã đánh thức chip nhưng được nhả ra trước khi khởi động xong -> coi như vừa nhấn.
  // Phím vẫn đang giữ sẽ được keysUpdate() nhận ra bình thường.
  uint32_t now = millis();
  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    if ((wakeMask & (1u << i)) && digitalRead(KEY_PINS[i]) == HIGH) onPress(i, now);
  }
}

void keysUpdate() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    bool raw = digitalRead(KEY_PINS[i]) == LOW;
    // nhận thay đổi ngay, rồi khoá DEBOUNCE_MS để bỏ qua dội phím
    if (raw != down[i] && now - changedAt[i] >= DEBOUNCE_MS) {
      down[i] = raw;
      changedAt[i] = now;
      log_i("Phim [%d] %s", i + 1, raw ? "nhan" : "nha");
      if (raw) onPress(i, now);
    }
  }

  uint8_t held = 0;
  for (uint8_t i = 0; i < KEY_COUNT; i++) held += down[i];
  if (held >= 3 && combo != MODE) startModeCombo();

  if (pairA >= 0 && now - pairAt >= TRIPLE_WINDOW_MS) {
    uint8_t a = pairA, b = pairB;
    pairA = pairB = -1;
    Combo c = comboOf(a, b);
    if (c != NONE) startCombo(c, a, b, pairAt, now);  // đã nhả thì keysUpdate sau kết thúc luôn
  }

  if (lateA >= 0) {
    uint8_t a = lateA, b = lateB;
    if (!down[a] || !down[b]) {
      lateA = lateB = -1;
      onKeyPlay(b);  // nhả sớm: chỉ là gõ nhanh
    } else if (now - lateAt >= COMBO_CONFIRM_MS) {
      lateA = lateB = -1;
      audioStop();  // cắt âm phím đầu
      startCombo(comboOf(a, b), a, b, lateAt, now);
    }
  }

  if (combo == MODE) {
    if (!held) combo = NONE;  // nhả hết phím mới nhận phím mới
  } else if (combo != NONE) {
    bool both = down[comboA] && down[comboB];
    if (combo == BLE) {
      if (!both) {
        combo = NONE;
      } else if (!bleDone && (int32_t)(now - nextRepeat) >= 0) {
        bleDone = true;  // giữ tiếp không bật/tắt lại, phải nhả ra rồi giữ lần nữa
        onBleToggle();
      }
    } else if (!both) {
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
  if (firstKey >= 0 || pairA >= 0 || lateA >= 0 || combo != NONE) return true;
  for (uint8_t i = 0; i < KEY_COUNT; i++)
    if (down[i]) return true;
  return false;
}
