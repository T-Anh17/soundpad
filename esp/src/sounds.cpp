#include "sounds.h"
#include <FFat.h>
#include <vector>
#include "app.h"
#include "audio.h"
#include "config.h"
#include "esp_random.h"

namespace {

struct Entry {
  int id;
  uint32_t size;
  String name;
};

std::vector<Entry> list;  // xếp theo id
SemaphoreHandle_t lock;
int keyMap[KEY_COUNT];
bool random_ = false;
int lastRandom = -1;
uint32_t version = 0;
const char *const NAMES_PATH = "/names.txt";  // mỗi dòng "id\ttên"

struct Guard {
  Guard() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(lock); }
};

Entry *find(int id) {
  for (auto &e : list)
    if (e.id == id) return &e;
  return nullptr;
}

// "s12.p16" -> 11; tên khác -> -1
int idOf(const String &file, const char *ext) {
  if (file.length() < 6 || file[0] != 's' || !file.endsWith(ext)) return -1;
  String n = file.substring(1, file.length() - strlen(ext));
  for (char c : n)
    if (!isdigit(c)) return -1;
  int v = n.toInt();
  return v >= 1 && v <= SOUND_ID_MAX ? v - 1 : -1;
}

void saveNames() {
  File f = FFat.open(NAMES_PATH, "w");
  if (!f) {
    log_e("Khong ghi duoc %s", NAMES_PATH);
    return;
  }
  for (auto &e : list)
    if (e.name.length()) f.print(String(e.id) + "\t" + e.name + "\n");
  f.close();
}

void loadNames() {
  File f = FFat.open(NAMES_PATH, "r");
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      int tab = line.indexOf('\t');
      if (tab < 0) continue;
      if (Entry *e = find(line.substring(0, tab).toInt())) e->name = line.substring(tab + 1);
    }
    f.close();
    return;
  }
  // Bản cũ lưu tên 10 ô trong NVS ("n1".."n10"): chuyển sang file một lần
  for (int id = 0; id < 10; id++) {
    String k = "n" + String(id + 1);
    if (!prefs.isKey(k.c_str())) continue;
    if (Entry *e = find(id)) e->name = prefs.getString(k.c_str());
    prefs.remove(k.c_str());
  }
  saveNames();
}

String keyName(uint8_t key) { return "k" + String(key + 1); }

}  // namespace

void soundsBegin() {
  lock = xSemaphoreCreateMutex();
  for (uint8_t k = 0; k < KEY_COUNT; k++) {
    String mk = "m" + String(k + 1);  // bản 10 ô lưu 1 byte ở "mN"
    keyMap[k] = prefs.isKey(keyName(k).c_str()) ? prefs.getShort(keyName(k).c_str())
              : prefs.isKey(mk.c_str())         ? prefs.getUChar(mk.c_str())
                                                : k;
  }
  random_ = prefs.getBool("random", false);
  soundsRefresh();
  loadNames();
}

void soundsRefresh() {
  std::vector<Entry> found;
  File root = FFat.open("/");
  while (File f = root.openNextFile()) {
    String n = f.name();
    int id = idOf(n, ".p16");
    uint32_t size = f.size();
    if (id < 0 && (id = idOf(n, ".adp")) >= 0) size *= 4;  // âm bản cũ ADPCM
    f.close();
    if (id < 0 || !size) continue;
    bool dup = false;  // có cả .p16 lẫn .adp: player phát .p16
    for (auto &e : found)
      if (e.id == id) { dup = true; if (n.endsWith(".p16")) e.size = size; }
    if (!dup) found.push_back({id, size, ""});
  }
  std::sort(found.begin(), found.end(), [](const Entry &a, const Entry &b) { return a.id < b.id; });

  Guard g;
  for (auto &e : found)
    if (Entry *old = find(e.id)) e.name = old->name;
  list.swap(found);
  version++;
}

bool soundExists(int id) {
  Guard g;
  return find(id) != nullptr;
}

size_t soundBytes(int id) {
  Guard g;
  Entry *e = find(id);
  return e ? e->size : 0;
}

size_t soundCount() {
  Guard g;
  return list.size();
}

int soundForKey(uint8_t key) {
  Guard g;
  if (!random_) return find(keyMap[key]) ? keyMap[key] : -1;
  size_t n = list.size();
  if (!n) return -1;
  int pick;
  do pick = list[esp_random() % n].id;
  while (n > 1 && pick == lastRandom);
  lastRandom = pick;
  return pick;
}

int soundNewId() {
  Guard g;
  int id = 0;  // list xếp theo id: tìm chỗ hở đầu tiên
  for (auto &e : list) {
    if (e.id != id) break;
    id++;
  }
  return id < SOUND_ID_MAX ? id : -1;
}

String soundName(int id) {
  Guard g;
  Entry *e = find(id);
  return e ? e->name : "";
}

void setSoundName(int id, const String &name) {
  {
    Guard g;
    Entry *e = find(id);
    if (!e || e->name == name) return;
    e->name = name;
    version++;
  }
  saveNames();
}

uint32_t soundsVersion() { return version; }

String soundsPage(size_t from, size_t maxBytes, int &next) {
  Guard g;
  String out;
  size_t i = from;
  for (; i < list.size(); i++) {
    String line = String(list[i].id) + "\t" + String(list[i].size) + "\t" + list[i].name + "\n";
    if (out.length() + line.length() > maxBytes && i > from) break;
    out += line;
  }
  next = i < list.size() ? (int)i : -1;
  return out;
}

int keySound(uint8_t key) { return keyMap[key]; }

void setKeySound(uint8_t key, int id) {
  if (key >= KEY_COUNT || id < 0 || id >= SOUND_ID_MAX) return;
  keyMap[key] = id;
  prefs.putShort(keyName(key).c_str(), id);
  version++;
}

bool randomMode() { return random_; }

void setRandomMode(bool on) {
  random_ = on;
  lastRandom = -1;
  prefs.putBool("random", on);
}
