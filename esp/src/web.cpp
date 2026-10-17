// WiFi riêng "SoundPad" + web cài đặt.
// Có captive portal: điện thoại vào WiFi là tự bật trang cài đặt
// (DNS trả mọi tên miền về AP_IP, các địa chỉ kiểm tra mạng của
// Android/iOS/Windows được chuyển về trang chính).
//
// API (giống web/dev_server.py), ô âm N = 1..SOUND_COUNT, phím K = 1..KEY_COUNT:
//   GET  /info   GET /state
//   POST /play?slot=N   /volume?v=0..100[&save=1]   /delete?slot=N
//   POST /assign?key=K&slot=N   /mode?random=0|1
//   POST /upload?slot=N&size=BYTES&name=...  (multipart, PCM 16-bit little-endian 22321 Hz mono)

#include "web.h"
#include <FFat.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_wifi.h"
#include "app.h"
#include "audio.h"
#include "config.h"
#include "power.h"
#include "sounds.h"
#include "web_page.h"

namespace {

WebServer server(80);
WiFiUDP dns;
uint8_t dnsLogged = 0;  // chỉ in vài câu hỏi DNS đầu mỗi lần bật WiFi
// Địa chỉ "công cộng" thay vì 192.168.4.1: điện thoại Samsung thấy DNS trả về địa chỉ nội bộ thì
// coi là mạng không có internet (hiện "chọn Wi-Fi tốt nhất") chứ không tự bật trang cài đặt
const IPAddress AP_IP(4, 3, 2, 1);
#define AP_URL "http://4.3.2.1/"

bool on = false;
uint32_t lastUse = 0;

File upFile;
int upSlot = -1;
bool upOldPage = false;  // upload đến từ trang web bản cũ (định dạng khác)

void touch() {
  lastUse = millis();
  markActivity();
}

String nameKey(uint8_t slot) { return "n" + String(slot + 1); }

int slotArg() {
  int s = server.arg("slot").toInt() - 1;
  return (s >= 0 && s < SOUND_COUNT) ? s : -1;
}

int keyArg() {
  int k = server.arg("key").toInt() - 1;
  return (k >= 0 && k < KEY_COUNT) ? k : -1;
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

// DNS: mọi tên miền -> AP_IP. Tự viết vì DNSServer của Arduino 3.1.1 bỏ qua câu hỏi có kèm phần
// mở rộng EDNS (ARCOUNT = 1), mà Android đời mới luôn gửi kèm -> trả về rỗng, điện thoại không gửi
// được trang kiểm tra mạng, Samsung báo "the current network has poor quality"
void dnsReply() {
  static uint8_t buf[512];
  while (int len = dns.parsePacket()) {
    if (len > (int)sizeof(buf)) { dns.flush(); continue; }
    dns.read(buf, len);
    // Đầu gói 12 byte; chỉ trả lời câu truy vấn thường (QR = 0, OPCODE = 0) có 1 câu hỏi
    if (len < 17 || (buf[2] & 0xF8) || buf[4] || buf[5] != 1) continue;
    int q = 12;  // tên miền: các nhãn <độ dài><chữ>, hết bằng 0
    while (q < len && buf[q]) q += buf[q] + 1;
    if (q + 5 > len) continue;
    uint16_t qtype = buf[q + 1] << 8 | buf[q + 2];
    int qend = q + 5;

    if (dnsLogged < 20) {
      dnsLogged++;
      String name;
      for (int i = 12; i < q; i += buf[i] + 1) {
        if (name.length()) name += '.';
        name += String((const char *)buf + i + 1).substring(0, buf[i]);
      }
      log_i("DNS hoi %s (loai %u)", name.c_str(), qtype);
    }

    bool a = qtype == 1 || qtype == 255;  // A hoặc ANY; AAAA (IPv6) và loại khác: trả lời rỗng
    buf[2] = 0x84 | (buf[2] & 0x01);      // trả lời, có thẩm quyền, giữ cờ RD
    buf[3] = 0x80;                         // RA, không lỗi
    buf[6] = 0; buf[7] = a;                // số câu trả lời
    buf[8] = buf[9] = buf[10] = buf[11] = 0;  // bỏ phần mở rộng EDNS của câu hỏi
    int n = qend;
    if (a) {
      const uint8_t ans[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0x0E, 0x10, 0, 4,  // tên = câu hỏi, A, IN, TTL 3600s
                             AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]};
      memcpy(buf + n, ans, sizeof(ans));
      n += sizeof(ans);
    }
    dns.beginPacket(dns.remoteIP(), dns.remotePort());
    dns.write(buf, n);
    dns.endPacket();
  }
}

void redirectHome() {
  // In ra để biết điện thoại có hỏi tới board không (không thấy dòng nào = DNS / WiFi có vấn đề)
  log_i("Chuyen huong http://%s%s", server.hostHeader().c_str(), server.uri().c_str());
  server.sendHeader("Location", AP_URL, true);
  server.send(302, "text/plain", "");
}

void handleRoot() {
  touch();
  log_i("Mo trang cai dat");
  server.sendHeader("Content-Encoding", "gzip");
  // no-store: điện thoại không giữ bản trang cũ, nếu không sau khi nạp firmware mới trang cũ vẫn
  // gửi âm sai định dạng (vd ADPCM khi board đã đọc PCM 16-bit) -> phát ra toàn tiếng xì
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", (const char *)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
}

void handleInfo() {
  touch();
  String j = "{\"vol\":" + String(audioVolume());
  j += ",\"bat\":" + String(batteryPercent());
  j += ",\"mv\":" + String(batteryMv());
  j += ",\"free\":" + String((uint32_t)FFat.freeBytes());
  j += ",\"total\":" + String((uint32_t)FFat.totalBytes());
  j += ",\"playing\":" + String(audioPlayingSound());
  j += ",\"random\":" + String(randomMode() ? "true" : "false");
  j += ",\"map\":[";  // ô gán cho từng phím, đếm từ 0
  for (uint8_t k = 0; k < KEY_COUNT; k++) j += (k ? "," : "") + String(keySound(k));
  j += "],\"sounds\":[";
  for (uint8_t s = 0; s < SOUND_COUNT; s++) {
    // âm bản cũ (ADPCM) báo theo cỡ PCM 16-bit để web tính đúng thời lượng
    if (s) j += ',';
    j += "{\"name\":" + jsonStr(prefs.getString(nameKey(s).c_str(), "")) +
         ",\"size\":" + String((uint32_t)soundBytes(s)) + "}";
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void handleState() {
  touch();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json",
              "{\"playing\":" + String(audioPlayingSound()) + ",\"vol\":" + String(audioVolume()) +
              ",\"bat\":" + String(batteryPercent()) + ",\"mv\":" + String(batteryMv()) + "}");
}

void handlePlay() {
  touch();
  int s = slotArg();
  if (s < 0) return server.send(400, "text/plain", "slot?");
  audioPlaySound(s);
  server.send(200, "text/plain", "OK");
}

void handleAssign() {
  touch();
  int k = keyArg(), s = slotArg();
  if (k < 0 || s < 0) return server.send(400, "text/plain", "key? slot?");
  setKeySound(k, s);
  log_i("Phim [%d] <- am %d", k + 1, s + 1);
  server.send(200, "text/plain", "OK");
}

void handleMode() {
  touch();
  setRandomMode(server.arg("random") == "1");
  log_i("Che do %s", randomMode() ? "ngau nhien" : "don");
  server.send(200, "text/plain", "OK");
}

// Xoá âm của ô (cả bản cũ ADPCM) và tên
void removeSound(uint8_t s) {
  audioStop();
  FFat.remove(soundPath(s));
  if (FFat.exists(legacySoundPath(s))) FFat.remove(legacySoundPath(s));
  prefs.remove(nameKey(s).c_str());
}

void handleVolume() {
  touch();
  audioSetVolume(constrain(server.arg("v").toInt(), 0, 100));
  if (server.hasArg("save")) saveVolume();
  log_i("Am luong %d%% (web)", audioVolume());
  server.send(200, "text/plain", "OK");
}

void handleDelete() {
  touch();
  int s = slotArg();
  if (s < 0) return server.send(400, "text/plain", "slot?");
  removeSound(s);
  soundsRefresh();
  server.send(200, "text/plain", "OK");
}

// Ghi ra file tạm, xong mới thay file cũ -> upload hỏng giữa chừng không mất âm cũ
void handleUploadData() {
  touch();
  HTTPUpload &up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      upSlot = slotArg();
      // trang cũ còn trong điện thoại gửi ADPCM (s.adp): lưu vào là phát ra toàn tiếng xì
      upOldPage = !up.filename.endsWith(".p16") || !server.hasArg("slot");  // trang 4 phím gửi btn=
      if (upOldPage) upSlot = -1;
      if (upSlot < 0) return;
      FFat.remove("/tmp.pcm");  // sót lại từ lần mất điện giữa chừng
      // Không đủ chỗ cho cả file tạm lẫn file cũ -> xoá file cũ trước (upload hỏng thì phím đó mất âm)
      if (FFat.freeBytes() < (size_t)server.arg("size").toInt() + 16384) {
        removeSound(upSlot);
        soundsRefresh();
      }
      upFile = FFat.open("/tmp.pcm", "w");
      if (!upFile) upSlot = -1;
      break;
    case UPLOAD_FILE_WRITE:
      if (upFile && upFile.write(up.buf, up.currentSize) != up.currentSize) {
        upFile.close();
        FFat.remove("/tmp.pcm");
        upSlot = -1;  // thường là hết bộ nhớ
      }
      break;
    case UPLOAD_FILE_END:
      if (!upFile) { upSlot = -1; return; }
      upFile.close();
      removeSound(upSlot);
      FFat.rename("/tmp.pcm", soundPath(upSlot));
      prefs.putString(nameKey(upSlot).c_str(), server.arg("name").substring(0, 60));
      soundsRefresh();
      log_i("Am %d <- %s (%u byte)", upSlot + 1, server.arg("name").c_str(), up.totalSize);
      break;
    case UPLOAD_FILE_ABORTED:
      if (upFile) upFile.close();
      FFat.remove("/tmp.pcm");
      upSlot = -1;
      break;
  }
}

void handleUploadDone() {
  if (upSlot >= 0) server.send(200, "text/plain", "OK");
  else if (upOldPage) server.send(409, "text/plain", "Trang cũ, hãy tải lại trang rồi nạp lại");
  else server.send(500, "text/plain", "Lưu thất bại (hết bộ nhớ?)");
  upSlot = -1;
}

}  // namespace

void webBegin() {
  WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t info) {
    if (e == ARDUINO_EVENT_WIFI_AP_STACONNECTED) log_i("May vao WiFi");
    else if (e == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED)
      log_i("May nhan dia chi %s", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
    else if (e == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) log_i("May roi WiFi");
  });
  server.on("/", HTTP_GET, handleRoot);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/state", HTTP_GET, handleState);
  server.on("/play", HTTP_POST, handlePlay);
  server.on("/volume", HTTP_POST, handleVolume);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/assign", HTTP_POST, handleAssign);
  server.on("/mode", HTTP_POST, handleMode);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  // Mọi địa chỉ khác (kể cả trang kiểm tra mạng của điện thoại) -> trang chính.
  // Không tính là "đang dùng" để điện thoại tự kiểm tra mạng không giữ WiFi bật mãi.
  server.onNotFound(redirectHome);
  // Windows hỏi liên tục 2 địa chỉ này nếu bị chuyển hướng -> trả 404 cho nhẹ board
  server.on("/wpad.dat", [] { server.send(404); });
  server.on("/favicon.ico", [] { server.send(404); });
}

void wifiOn() {
  if (on) return;
  WiFi.mode(WIFI_AP);
  // Điện thoại ở gần: giảm công suất ngay từ đầu cho đỡ tốn pin và đỡ nhiễu vào ampli
  // (ampli nằm sát anten, lúc phát sóng mạnh loa kêu rột rột).
  // Gọi thẳng IDF vì WiFi.setTxPower() bỏ qua khi AP chưa báo đã chạy; đơn vị 0,25 dBm.
  // 8 = 2 dBm, mức thấp nhất: 8,5 dBm vẫn làm loa rè khi WiFi bật
  esp_wifi_set_max_tx_power(8);
  // Báo rõ DNS = board qua DHCP: không báo thì có máy dùng DNS riêng, không thấy trang kiểm tra
  // mạng bị chuyển hướng nên không tự bật trang cài đặt (captive portal)
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0), IPAddress((uint32_t)0), AP_IP);
  WiFi.softAP(AP_SSID, strlen(AP_PASS) ? AP_PASS : nullptr);
  esp_wifi_set_max_tx_power(8);
  dns.begin(53);
  dnsLogged = 0;
  server.begin();
  on = true;
  lastUse = millis();
  log_i("WiFi bat: %s -> %s", AP_SSID, AP_URL);
}

void wifiOff() {
  if (!on) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  on = false;
  log_i("WiFi tat");
}

bool wifiIsOn() { return on; }

void webUpdate() {
  if (!on) return;
  dnsReply();
  server.handleClient();
  if (millis() - lastUse > WIFI_IDLE_OFF_MS) {
    wifiOff();
    audioTone(TONE_WIFI_OFF);
  }
}
