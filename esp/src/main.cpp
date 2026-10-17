// SoundPad - 4 phím cơ phát 4 đoạn âm thanh, cài đặt qua web local.
// Xem ../PLAN.md

#include <Arduino.h>
#include <FFat.h>
#include <Preferences.h>
#include <WiFi.h>
#include "app.h"
#include "audio.h"
#include "config.h"
#include "keys.h"
#include "power.h"
#include "web.h"

Preferences prefs;

static uint32_t lastActivity = 0;

void markActivity() { lastActivity = millis(); }
void saveVolume() { prefs.putUChar("vol", audioVolume()); }

// ---------- Sự kiện phím ----------

// Nhấn phím chỉ phát đúng âm của phím, không kèm âm báo nào
void onKeyPlay(uint8_t key) {
  markActivity();
  if (batteryCritical()) {  // pin cạn: không phát
    log_w("Pin can (%d mV), bo qua phim [%d]", batteryMv(), key + 1);
    return;
  }
  log_i("Phat phim [%d]", key + 1);
  audioPlayKey(key);
}

void onVolumeStep(int dir) {
  markActivity();
  audioSetVolume(constrain(audioVolume() + dir * VOLUME_STEP, 0, 100));
  audioTone(TONE_VOLUME);
  log_i("Am luong %d%% (phim)", audioVolume());
}

void onVolumeDone() { saveVolume(); }

void onWifiToggle() {
  markActivity();
  if (wifiIsOn()) {
    wifiOff();
    audioTone(TONE_WIFI_OFF);
  } else {
    // Kêu xong hẳn rồi mới bật WiFi: lúc khởi động WiFi kéo dòng lớn, nguồn ampli bị giật
    audioTone(TONE_WIFI_ON);
    delay(20);  // chờ player nhận lệnh
    while (audioBusy()) delay(5);
    // Tắt hẳn ampli + I2S trong lúc WiFi khởi động: cộng dồn hai dòng là sụt áp, board reset (lý do 9).
    // Âm phát sau đó tự bật lại
    audioShutdown();
    delay(300);
    log_i("Bat WiFi");
    wifiOn();
  }
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
  for (uint8_t k = 1; k <= KEY_COUNT; k++) FFat.remove("/s" + String(k) + ".pcm");
  audioBegin();
  webBegin();

#if ENABLE_SLEEP
  if (batteryCritical()) deepSleep();  // pin cạn: ngủ tiếp, không kêu
#endif

  keysBegin(wakeKeyMask());
  markActivity();
  log_i("San sang. Pin %d mV (%d%%), am luong %d%%, ly do reset %d", batteryMv(), batteryPercent(),
        audioVolume(), esp_reset_reason());
}

void loop() {
  keysUpdate();
  webUpdate();

#if CORE_DEBUG_LEVEL >= 3
  // Bản thử: in nhịp "còn sống" để biết lúc mất COM chip còn chạy hay đã khởi động lại
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat >= 5000) {
    lastBeat = millis();
    log_i("Song %lus, WiFi %s, %d may, RAM trong %u, pin %d mV (%d%%), ly do reset %d", millis() / 1000,
          wifiIsOn() ? "bat" : "tat", wifiIsOn() ? WiFi.softAPgetStationNum() : 0, ESP.getFreeHeap(),
          batteryMv(), batteryPercent(), esp_reset_reason());
  }
#endif

  bool playing = audioBusy();
  powerUpdate(playing);
  if (playing) markActivity();

#if ENABLE_SLEEP
  // Đang cắm USB vào máy tính: không ngủ để giữ cổng COM (ngủ là chip tắt USB).
  // Chờ 3s sau khởi động cho máy tính kịp nhận USB.
  bool usb = millis() < 3000 || HWCDC::isPlugged();
  bool quiet = !playing && !wifiIsOn() && !keysActive() && !usb;
  if (quiet && batteryCritical()) deepSleep();

  uint32_t idleFor = millis() - lastActivity;
  if (quiet && idleFor >= DEEP_SLEEP_IDLE_MS) deepSleep();

  // Rảnh: tắt ampli và I2S rồi ngủ nhẹ tới khi nhấn phím (hoặc tới giờ ngủ sâu).
  // Chờ AUDIO_OFF_IDLE_MS để nhấn liên tiếp không phải bật/tắt ngõ ra mỗi lần
  if (quiet && idleFor >= AUDIO_OFF_IDLE_MS) {
    audioOff();
    if (audioIdle()) {
      lightSleep(DEEP_SLEEP_IDLE_MS - idleFor);
      return;
    }
  }
#endif

  delay(1);
}
