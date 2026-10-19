// Cài đặt qua Bluetooth LE: web (web/, React + Web Bluetooth) kết nối thẳng, không cần WiFi.
// Bật / tắt bằng [1]+[4] như WiFi cũ. Không ai kết nối trong BLE_IDLE_OFF_MS thì tự tắt.
//
// Dịch vụ SVC_UUID. Âm có số hiệu id (xem sounds.h), phím K = 0..KEY_COUNT-1:
//   STATE  đọc   JSON {v, b, mv, p, r, m:[id của phím], n, lv, f, t, mtu, up}
//                n = số âm, lv = phiên bản danh sách (đổi thì web tải lại danh sách)
//   LIST   ghi số vị trí, rồi đọc   một trang danh sách: dòng đầu là vị trí trang sau (-1 = hết),
//                mỗi dòng sau "id\tcỡ\ttên" (một thuộc tính BLE tối đa 512 byte nên phải chia trang)
//   EVT    báo   "1" mỗi khi STATE đổi -> web đọc lại STATE
//   CMD    ghi   lệnh chữ: "p ID" phát, "s" dừng, "v V" / "V V" âm lượng (V = lưu), "r 0|1" chế độ,
//                "m K ID" gán phím, "d ID" xoá, "u ID BYTES TÊN" bắt đầu nạp (ID = -1: âm mới),
//                "e" nạp xong
//   DATA   ghi   dữ liệu âm sau lệnh "u" (PCM 16-bit little-endian 22321 Hz mono), nối tiếp nhau
//
// Lệnh và dữ liệu đi chung một hàng đợi, xử lý trong loop() theo đúng thứ tự gửi: hàm gọi lại của
// BLE chạy trong task Bluetooth, không đụng FFat / Preferences / âm thanh ở đó. Hàng đầy thì
// task Bluetooth chờ -> điện thoại tự gửi chậm lại, không mất gói. Riêng LIST trả lời ngay trong
// task Bluetooth (sounds.cpp có khoá) để lần đọc ngay sau đó thấy đúng trang.

#include "ble.h"
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <FFat.h>
#include "app.h"
#include "audio.h"
#include "config.h"
#include "power.h"
#include "sounds.h"

namespace {

#define SVC_UUID   "7a1e0001-5b2c-4f3a-9d8e-1c2b3a4d5e6f"
#define STATE_UUID "7a1e0002-5b2c-4f3a-9d8e-1c2b3a4d5e6f"
#define LIST_UUID  "7a1e0003-5b2c-4f3a-9d8e-1c2b3a4d5e6f"
#define EVT_UUID   "7a1e0004-5b2c-4f3a-9d8e-1c2b3a4d5e6f"
#define CMD_UUID   "7a1e0005-5b2c-4f3a-9d8e-1c2b3a4d5e6f"
#define DATA_UUID  "7a1e0006-5b2c-4f3a-9d8e-1c2b3a4d5e6f"

const size_t NAME_BYTES = 60;   // tên tối đa (byte UTF-8)
const size_t PAGE_BYTES = 480;  // một trang LIST, chừa chỗ cho dòng đầu
const char *const TMP_PATH = "/tmp.pcm";

struct Msg {
  bool data;      // true: dữ liệu âm, false: lệnh chữ
  uint16_t len;
  uint8_t buf[512];
};

QueueHandle_t inbox;
BLEServer *server = nullptr;
BLECharacteristic *stateChr, *evtChr;
bool started = false;   // đã khởi động ngăn xếp Bluetooth (giữ tới lúc ngủ sâu, xem bleStackUp)
bool on = false;        // đang quảng bá / đang kết nối
volatile bool connected = false;
volatile uint16_t mtu = 23;
uint32_t lastUse = 0;
String lastState;

File upFile;
int upId = -1;
uint32_t upSize = 0, upGot = 0;
String upName, upResult;  // upResult: "", "busy", "ok" hoặc lý do lỗi

void touch() {
  lastUse = millis();
  markActivity();
}

String jsonStr(const String &s) {
  String o = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o + "\"";
}

// Bỏ ký tự điều khiển (tab, xuống dòng là dấu phân cách trong LIST), cắt còn NAME_BYTES byte,
// không cắt giữa một chữ UTF-8 (chữ Việt 2–3 byte)
String clipName(String s) {
  for (size_t i = 0; i < s.length(); i++)
    if ((uint8_t)s[i] < 0x20) s.setCharAt(i, ' ');
  s.trim();
  if (s.length() <= NAME_BYTES) return s;
  size_t n = NAME_BYTES;
  while (n && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
  return s.substring(0, n);
}

String stateJson() {
  String j = "{\"v\":" + String(audioVolume());
  j += ",\"b\":" + String(batteryPercent());
  j += ",\"mv\":" + String(batteryMv());
  j += ",\"p\":" + String(audioPlayingSound());
  j += ",\"r\":" + String(randomMode() ? 1 : 0);
  j += ",\"m\":[";
  for (uint8_t k = 0; k < KEY_COUNT; k++) j += (k ? "," : "") + String(keySound(k));
  j += "],\"n\":" + String((uint32_t)soundCount());
  j += ",\"lv\":" + String(soundsVersion());
  j += ",\"f\":" + String((uint32_t)FFat.freeBytes());
  j += ",\"t\":" + String((uint32_t)FFat.totalBytes());
  j += ",\"mtu\":" + String(mtu);
  j += ",\"up\":" + jsonStr(upResult) + "}";
  return j;
}

// Cập nhật STATE, có thay đổi thì báo cho web
void publish() {
  String s = stateJson();
  if (s == lastState) return;
  lastState = s;
  stateChr->setValue(s);
  if (connected) {
    evtChr->setValue("1");
    evtChr->notify();
  }
}

// Xoá âm (cả bản cũ ADPCM) và tên
void removeSound(int id) {
  audioStop();
  setSoundName(id, "");
  FFat.remove(soundPath(id));
  if (FFat.exists(legacySoundPath(id))) FFat.remove(legacySoundPath(id));
  soundsRefresh();
}

void abortUpload(const char *why) {
  if (upFile) upFile.close();
  if (FFat.exists(TMP_PATH)) FFat.remove(TMP_PATH);
  if (upId >= 0 && *why) log_w("Nap am %d hong: %s", upId + 1, why);
  upId = -1;
  upResult = why;
}

// Ghi ra file tạm, xong mới thay file cũ -> nạp hỏng giữa chừng không mất âm cũ
void beginUpload(int id, uint32_t size, const String &name) {
  abortUpload("");
  if (id < 0) id = soundNewId();
  if (id < 0 || id >= SOUND_ID_MAX || !size) return abortUpload("Lệnh nạp sai");
  // Thay âm cũ mà không đủ chỗ cho cả file tạm lẫn file cũ -> xoá file cũ trước
  if (soundExists(id) && FFat.freeBytes() < size + 16384) removeSound(id);
  if (FFat.freeBytes() < size + 16384) return abortUpload("Hết bộ nhớ");
  upFile = FFat.open(TMP_PATH, "w");
  if (!upFile) return abortUpload("Không tạo được file");
  upId = id;
  upSize = size;
  upGot = 0;
  upName = clipName(name);
  upResult = "busy";
  log_i("Nap am %d: %s, %u byte", id + 1, upName.c_str(), size);
}

void uploadData(const uint8_t *d, size_t n) {
  if (upId < 0) return;  // chưa có lệnh "u" hoặc đã hỏng: bỏ qua phần còn lại
  if (upGot + n > upSize) return abortUpload("Dữ liệu dài hơn đã báo");
  if (upFile.write(d, n) != n) return abortUpload("Ghi lỗi (hết bộ nhớ?)");
  upGot += n;
}

void endUpload() {
  if (upId < 0) return;
  if (upGot != upSize) return abortUpload("Thiếu dữ liệu, nạp lại");
  upFile.close();
  int id = upId;
  if (soundExists(id)) removeSound(id);
  FFat.rename(TMP_PATH, soundPath(id));
  soundsRefresh();
  setSoundName(id, upName);
  log_i("Am %d <- %s (%u byte)", id + 1, upName.c_str(), upSize);
  upId = -1;
  upResult = "ok";
}

void runCommand(String c) {
  touch();
  char op = c.length() ? c[0] : 0;
  String rest = c.length() > 2 ? c.substring(2) : "";
  int a = rest.toInt();
  int sp = rest.indexOf(' ');
  String tail = sp >= 0 ? rest.substring(sp + 1) : "";

  switch (op) {
    case 'p':
      if (soundExists(a)) audioPlaySound(a);
      break;
    case 's':
      audioStop();
      break;
    case 'v':
    case 'V':
      audioSetVolume(constrain(a, 0, 100));
      if (op == 'V') saveVolume();
      break;
    case 'r':
      setRandomMode(a == 1);
      log_i("Che do %s", randomMode() ? "ngau nhien" : "don");
      break;
    case 'm':
      if (a >= 0 && a < KEY_COUNT) setKeySound(a, tail.toInt());
      break;
    case 'd':
      if (soundExists(a)) removeSound(a);
      break;
    case 'u': {  // "u ID BYTES TÊN"
      int sp2 = tail.indexOf(' ');
      beginUpload(a, tail.toInt(), sp2 >= 0 ? tail.substring(sp2 + 1) : "");
      break;
    }
    case 'e':
      endUpload();
      break;
    default:
      log_w("Lenh la: %s", c.c_str());
  }
}

void enqueue(bool data, BLECharacteristic *c) {
  Msg m;
  size_t n = min(c->getLength(), sizeof(m.buf));
  m.data = data;
  m.len = n;
  memcpy(m.buf, c->getData(), n);
  xQueueSend(inbox, &m, portMAX_DELAY);
}

struct CmdCb : BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override { enqueue(false, c); }
};
struct DataCb : BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override { enqueue(true, c); }
};
struct ListCb : BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    int next;
    String page = soundsPage(String(c->getValue()).toInt(), PAGE_BYTES, next);
    c->setValue(String(next) + "\n" + page);
  }
};
struct ServerCb : BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    connected = true;
    log_i("May ket noi BLE");
  }
  void onDisconnect(BLEServer *) override {
    connected = false;
    mtu = 23;
    log_i("May ngat BLE");
    if (on) BLEDevice::startAdvertising();  // cho kết nối lại
  }
  void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *p) override { mtu = p->mtu.mtu; }
};

void startStack() {
  BLEDevice::init(BLE_NAME);
  BLEDevice::setMTU(517);
  // Điện thoại ở gần: công suất thấp cho đỡ tốn pin và đỡ nhiễu vào ampli (nằm sát anten)
  BLEDevice::setPower(ESP_PWR_LVL_N0);
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  BLEService *svc = server->createService(BLEUUID(SVC_UUID), 20);
  stateChr = svc->createCharacteristic(STATE_UUID, BLECharacteristic::PROPERTY_READ);
  svc->createCharacteristic(LIST_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE)
      ->setCallbacks(new ListCb());
  evtChr = svc->createCharacteristic(EVT_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  evtChr->addDescriptor(new BLE2902());
  svc->createCharacteristic(CMD_UUID, BLECharacteristic::PROPERTY_WRITE)->setCallbacks(new CmdCb());
  svc->createCharacteristic(DATA_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR)
      ->setCallbacks(new DataCb());
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);  // web lọc thiết bị theo dịch vụ này
  adv->setScanResponse(true);     // tên nằm ở gói trả lời quét (gói quảng bá chỉ vừa UUID 128-bit)
  started = true;
}

}  // namespace

void bleBegin() { inbox = xQueueCreate(16, sizeof(Msg)); }

void bleOn() {
  if (on) return;
  if (!started) startStack();
  lastState = "";
  upResult = "";
  publish();
  BLEDevice::startAdvertising();
  on = true;
  lastUse = millis();
  log_i("BLE bat: %s", BLE_NAME);
}

void bleOff() {
  if (!on) return;
  on = false;
  BLEDevice::stopAdvertising();
  if (connected) server->disconnect(server->getConnId());
  abortUpload("");
  log_i("BLE tat");
}

bool bleIsOn() { return on; }
bool bleStackUp() { return started; }

void bleUpdate() {
  if (!on) return;
  Msg m;
  // gửi dữ liệu nhanh hơn ghi flash: xử lý một loạt rồi mới quay lại loop()
  for (int i = 0; i < 16 && xQueueReceive(inbox, &m, 0) == pdTRUE; i++) {
    if (m.data) {
      lastUse = millis();
      uploadData(m.buf, m.len);
    } else {
      runCommand(String((const char *)m.buf, m.len));
    }
  }
  static uint32_t lastPublish = 0;
  if (millis() - lastPublish >= 200) {
    lastPublish = millis();
    publish();
  }
  if (connected) lastUse = millis();  // trang còn mở thì không tự tắt
  else if (millis() - lastUse > BLE_IDLE_OFF_MS) {
    bleOff();
    audioTone(TONE_BLE_OFF);
  }
}
