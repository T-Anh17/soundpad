// SoundPad - 4 phím cơ phát 4 đoạn âm thanh, cài đặt qua web local.
// Xem ../PLAN.md

#include <Arduino.h>
#include <FFat.h>
#include <Preferences.h>
#include "app.h"
#include "audio.h"
#include "config.h"
#include "keys.h"
#include "power.h"
#include "sounds.h"
#include "ble.h"

Preferences prefs;

static uint32_t lastActivity = 0;

void markActivity() { lastActivity = millis(); }
void saveVolume() { prefs.putUChar("vol", audioVolume()); }

// ---------- Sự kiện phím ----------

// Nhấn phím chỉ phát âm (ô gán cho phím, hoặc ô ngẫu nhiên), không kèm âm báo nào
void onKeyPlay(uint8_t key) {
  markActivity();
  if (batteryCritical()) {  // pin cạn: không phát
    log_w("Pin can (%d mV), bo qua phim [%d]", batteryMv(), key + 1);
    return;
  }
  int slot = soundForKey(key);
  if (slot < 0) {
    log_w("Phim [%d]: chua co am (%s)", key + 1, randomMode() ? "ngau nhien" : "che do don");
    return;
  }
  log_i("Phim [%d] -> am %d%s", key + 1, slot + 1, randomMode() ? " (ngau nhien)" : "");
  audioPlaySound(slot);
}

void onVolumeStep(int dir) {
  markActivity();
  audioSetVolume(constrain(audioVolume() + dir * VOLUME_STEP, 0, 100));
  audioTone(TONE_VOLUME);
  log_i("Am luong %d%% (phim)", audioVolume());
}

void onVolumeDone() { saveVolume(); }

void onBleToggle() {
  markActivity();
  if (bleIsOn()) {
    bleOff();
    audioTone(TONE_BLE_OFF);
  } else {
    // Kêu xong hẳn rồi mới bật: lúc khởi động sóng (hiệu chuẩn RF) kéo dòng lớn, nguồn ampli bị giật
    audioTone(TONE_BLE_ON);
    delay(20);  // chờ player nhận lệnh
    while (audioBusy()) delay(5);
    // Tắt hẳn ampli + I2S trong lúc sóng khởi động: cộng dồn hai dòng là sụt áp, board reset (lý do 9)
    // (từng gặp với WiFi, BLE kéo ít hơn nhưng giữ cho chắc).
    // Âm phát sau đó tự bật lại
    audioShutdown();
    delay(300);
    bleOn();
  }
}

void onModeToggle() {
  markActivity();
  setRandomMode(!randomMode());
  audioTone(randomMode() ? TONE_MODE_RANDOM : TONE_MODE_FIXED);
  log_i("Che do %s (3 phim)", randomMode() ? "ngau nhien" : "don");
}

// ---------- Main ----------

void setup() {
  Serial.begin(115200);
  powerBegin();
  // 9 = sụt áp (brownout), 4 = lỗi chương trình (panic), 11 = reset qua USB -> xem lý do mất COM
  log_i("Khoi dong, ly do reset: %d", esp_reset_reason());

  prefs.begin("soundpad", false);
  audioSetVolume(prefs.getUChar("vol", VOLUME_DEFAULT));

  if (!FFat.begin(true)) log_e("Khong mo duoc FFat - kiem tra partitions.csv");
  // Bản cũ lưu PCM không nén (/sN.pcm): xoá cho trống bộ nhớ, cần nạp lại âm
  for (uint8_t k = 1; k <= KEY_COUNT; k++)
    if (FFat.exists("/s" + String(k) + ".pcm")) FFat.remove("/s" + String(k) + ".pcm");
  soundsBegin();
  audioBegin();
  bleBegin();

#if ENABLE_SLEEP
  if (batteryCritical()) deepSleep();  // pin cạn: ngủ tiếp, không kêu
#endif

  // Ding khi gạt công tắc mở nguồn / nhấn RST. Không kêu khi nhấn phím đánh thức khỏi ngủ sâu
  // (phát luôn âm phím) hay khi sụt áp khởi động lại (kêu liên hồi nếu pin yếu)
  esp_reset_reason_t why = esp_reset_reason();
  if (why != ESP_RST_DEEPSLEEP && why != ESP_RST_BROWNOUT) audioTone(TONE_BOOT);

  keysBegin(wakeKeyMask());
  markActivity();
  // millis(): bật công tắc bao lâu thì phím mới nhận (chưa tính thời gian bootloader, ~0,3s)
  log_i("San sang sau %lu ms. Pin %d mV (%d%%), am luong %d%%, ly do reset %d", millis(), batteryMv(),
        batteryPercent(), audioVolume(), esp_reset_reason());
}

void loop() {
  keysUpdate();
  bleUpdate();

#if CORE_DEBUG_LEVEL >= 3
  // Bản thử: in nhịp "còn sống" để biết lúc mất COM chip còn chạy hay đã khởi động lại
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat >= 5000) {
    lastBeat = millis();
    log_i("Song %lus, BLE %s, RAM trong %u, pin %d mV (%d%%), ly do reset %d", millis() / 1000,
          bleIsOn() ? "bat" : "tat", ESP.getFreeHeap(),
          batteryMv(), batteryPercent(), esp_reset_reason());
  }
  // Bản thử: gõ t (nhỏ) / T (to) qua Serial để phát chuỗi tông đo méo loa, b để bật/tắt Bluetooth
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'b') onBleToggle();
    if (c == 't' || c == 'T') {
      markActivity();
      log_i("Phat chuoi tong thu (%s)", c == 't' ? "nho" : "to");
      audioTone(c == 't' ? TONE_TEST_SOFT : TONE_TEST_LOUD);
    }
  }
#endif

  bool playing = audioBusy();
  // Lúc WiFi bật số đo pin từng tụt về ~3,1 V dù pin còn 3,7 V (sai số, không phải pin cạn) -> bỏ qua,
  // nếu không sẽ tưởng pin cạn rồi bỏ qua phím. Bluetooth cũng phát sóng nên bỏ qua như vậy
  powerUpdate(playing || bleIsOn());
  if (playing) markActivity();

#if ENABLE_SLEEP
  // Đang cắm USB vào máy tính: không ngủ để giữ cổng COM (ngủ là chip tắt USB).
  // Chờ 3s sau khởi động cho máy tính kịp nhận USB.
  bool usb = millis() < 3000 || HWCDC::isPlugged();
  bool quiet = !playing && !bleIsOn() && !keysActive() && !usb;
  if (quiet && batteryCritical()) deepSleep();

  uint32_t idleFor = millis() - lastActivity;
  if (quiet && idleFor >= DEEP_SLEEP_IDLE_MS) deepSleep();

  // Rảnh: tắt ampli và I2S rồi ngủ nhẹ tới khi nhấn phím (hoặc tới giờ ngủ sâu).
  // Chờ AUDIO_OFF_IDLE_MS để nhấn liên tiếp không phải bật/tắt ngõ ra mỗi lần
  // Ngăn xếp Bluetooth đã chạy thì không ngủ nhẹ (sóng không chạy được qua giấc ngủ nhẹ), chờ
  // ngủ sâu cho sạch. Chỉ tốn thêm vài phút sau mỗi lần dùng web
  if (quiet && !bleStackUp() && idleFor >= AUDIO_OFF_IDLE_MS) {
    audioOff();
    if (audioIdle()) {
      lightSleep(DEEP_SLEEP_IDLE_MS - idleFor);
      return;
    }
  }
#endif

  delay(1);
}
