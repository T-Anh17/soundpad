#include "sounds.h"
#include <FFat.h>
#include "app.h"
#include "audio.h"
#include "config.h"
#include "esp_random.h"

namespace {

uint8_t keyMap[KEY_COUNT];
bool random_ = false;
uint32_t sizes[SOUND_COUNT];  // nhớ sẵn để nhấn phím không phải hỏi FFat
int8_t lastRandom = -1;

String mapKey(uint8_t key) { return "m" + String(key + 1); }

}  // namespace

void soundsBegin() {
  for (uint8_t k = 0; k < KEY_COUNT; k++) {
    uint8_t s = prefs.getUChar(mapKey(k).c_str(), k);  // bản cũ: phím N lưu ở ô N
    keyMap[k] = s < SOUND_COUNT ? s : k;
  }
  random_ = prefs.getBool("random", false);
  soundsRefresh();
}

void soundsRefresh() {
  for (uint8_t s = 0; s < SOUND_COUNT; s++) {
    sizes[s] = 0;
    if (FFat.exists(soundPath(s))) {
      File f = FFat.open(soundPath(s), "r");
      if (f) { sizes[s] = f.size(); f.close(); }
    } else if (FFat.exists(legacySoundPath(s))) {
      File f = FFat.open(legacySoundPath(s), "r");
      if (f) { sizes[s] = f.size() * 4; f.close(); }
    }
  }
}

bool soundExists(uint8_t slot) { return slot < SOUND_COUNT && sizes[slot]; }
size_t soundBytes(uint8_t slot) { return slot < SOUND_COUNT ? sizes[slot] : 0; }

int soundForKey(uint8_t key) {
  if (!random_) return soundExists(keyMap[key]) ? keyMap[key] : -1;

  int8_t pool[SOUND_COUNT];
  uint8_t n = 0;
  for (uint8_t s = 0; s < SOUND_COUNT; s++)
    if (soundExists(s)) pool[n++] = s;
  if (!n) return -1;
  int8_t pick;
  do pick = pool[esp_random() % n];
  while (n > 1 && pick == lastRandom);
  lastRandom = pick;
  return pick;
}

uint8_t keySound(uint8_t key) { return keyMap[key]; }

void setKeySound(uint8_t key, uint8_t slot) {
  if (key >= KEY_COUNT || slot >= SOUND_COUNT) return;
  keyMap[key] = slot;
  prefs.putUChar(mapKey(key).c_str(), slot);
}

bool randomMode() { return random_; }

void setRandomMode(bool on) {
  random_ = on;
  lastRandom = -1;
  prefs.putBool("random", on);
}
