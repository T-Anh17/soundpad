// WiFi riêng "SoundPad" + web cài đặt.
// Có captive portal: điện thoại vào WiFi là tự bật trang cài đặt
// (DNS trả mọi tên miền về 192.168.4.1, các địa chỉ kiểm tra mạng của
// Android/iOS/Windows được chuyển về trang chính).
//
// API (giống web/dev_server.py):
//   GET  /info   GET /state
//   POST /play?btn=N   /volume?v=0..100[&save=1]   /delete?btn=N
//   POST /upload?btn=N&size=BYTES&name=...  (multipart, IMA ADPCM 4-bit 22050 Hz mono)

#include "web.h"
#include <DNSServer.h>
#include <FFat.h>
#include <WebServer.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "app.h"
#include "audio.h"
#include "config.h"
#include "power.h"
#include "web_page.h"

namespace {

WebServer server(80);
DNSServer dns;
const IPAddress AP_IP(192, 168, 4, 1);

bool on = false;
uint32_t lastUse = 0;

File upFile;
int upKey = -1;

void touch() {
  lastUse = millis();
  markActivity();
}

String nameKey(uint8_t k) { return "n" + String(k + 1); }

int keyArg() {
  int k = server.arg("btn").toInt() - 1;
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

void redirectHome() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void handleRoot() {
  touch();
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", (const char *)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
}

void handleInfo() {
  touch();
  String j = "{\"vol\":" + String(audioVolume());
  j += ",\"bat\":" + String(batteryPercent());
  j += ",\"free\":" + String((uint32_t)FFat.freeBytes());
  j += ",\"total\":" + String((uint32_t)FFat.totalBytes());
  j += ",\"playing\":" + String(audioPlayingKey());
  j += ",\"keys\":[";
  for (uint8_t k = 0; k < KEY_COUNT; k++) {
    size_t size = 0;
    File f = FFat.open(soundPath(k), "r");
    if (f) { size = f.size(); f.close(); }
    if (k) j += ',';
    j += "{\"name\":" + jsonStr(prefs.getString(nameKey(k).c_str(), "")) + ",\"size\":" + String((uint32_t)size) + "}";
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void handleState() {
  touch();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json",
              "{\"playing\":" + String(audioPlayingKey()) + ",\"vol\":" + String(audioVolume()) +
              ",\"bat\":" + String(batteryPercent()) + "}");
}

void handlePlay() {
  touch();
  int k = keyArg();
  if (k < 0) return server.send(400, "text/plain", "btn?");
  audioPlayKey(k);
  server.send(200, "text/plain", "OK");
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
  int k = keyArg();
  if (k < 0) return server.send(400, "text/plain", "btn?");
  audioStop();
  FFat.remove(soundPath(k));
  prefs.remove(nameKey(k).c_str());
  server.send(200, "text/plain", "OK");
}

// Ghi ra file tạm, xong mới thay file cũ -> upload hỏng giữa chừng không mất âm cũ
void handleUploadData() {
  touch();
  HTTPUpload &up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      upKey = keyArg();
      if (upKey < 0) return;
      FFat.remove("/tmp.pcm");  // sót lại từ lần mất điện giữa chừng
      // Không đủ chỗ cho cả file tạm lẫn file cũ -> xoá file cũ trước (upload hỏng thì phím đó mất âm)
      if (FFat.freeBytes() < (size_t)server.arg("size").toInt() + 16384) {
        audioStop();
        FFat.remove(soundPath(upKey));
        prefs.remove(nameKey(upKey).c_str());
      }
      upFile = FFat.open("/tmp.pcm", "w");
      if (!upFile) upKey = -1;
      break;
    case UPLOAD_FILE_WRITE:
      if (upFile && upFile.write(up.buf, up.currentSize) != up.currentSize) {
        upFile.close();
        FFat.remove("/tmp.pcm");
        upKey = -1;  // thường là hết bộ nhớ
      }
      break;
    case UPLOAD_FILE_END:
      if (!upFile) { upKey = -1; return; }
      upFile.close();
      audioStop();
      FFat.remove(soundPath(upKey));
      FFat.rename("/tmp.pcm", soundPath(upKey));
      prefs.putString(nameKey(upKey).c_str(), server.arg("name").substring(0, 60));
      log_i("Phim %d <- %s (%u byte)", upKey + 1, server.arg("name").c_str(), up.totalSize);
      break;
    case UPLOAD_FILE_ABORTED:
      if (upFile) upFile.close();
      FFat.remove("/tmp.pcm");
      upKey = -1;
      break;
  }
}

void handleUploadDone() {
  if (upKey >= 0) server.send(200, "text/plain", "OK");
  else server.send(500, "text/plain", "Lưu thất bại (hết bộ nhớ?)");
  upKey = -1;
}

}  // namespace

void webBegin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/state", HTTP_GET, handleState);
  server.on("/play", HTTP_POST, handlePlay);
  server.on("/volume", HTTP_POST, handleVolume);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  // Mọi địa chỉ khác (kể cả trang kiểm tra mạng của điện thoại) -> trang chính.
  // Không tính là "đang dùng" để điện thoại tự kiểm tra mạng không giữ WiFi bật mãi.
  server.onNotFound(redirectHome);
}

void wifiOn() {
  if (on) return;
  WiFi.mode(WIFI_AP);
  // Điện thoại ở gần: giảm công suất ngay từ đầu cho đỡ tốn pin và đỡ nhiễu vào ampli
  // (ampli nằm sát anten, lúc phát sóng mạnh loa kêu rột rột).
  // Gọi thẳng IDF vì WiFi.setTxPower() bỏ qua khi AP chưa báo đã chạy; đơn vị 0,25 dBm
  esp_wifi_set_max_tx_power(34);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, strlen(AP_PASS) ? AP_PASS : nullptr);
  esp_wifi_set_max_tx_power(34);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);
  server.begin();
  on = true;
  lastUse = millis();
  log_i("WiFi bat: %s -> http://192.168.4.1", AP_SSID);
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
  dns.processNextRequest();
  server.handleClient();
  if (millis() - lastUse > WIFI_IDLE_OFF_MS) {
    wifiOff();
    audioTone(TONE_WIFI_OFF);
  }
}
