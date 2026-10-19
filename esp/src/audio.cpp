// Phát âm thanh qua I2S chuẩn (Philips, 16-bit mono) tới ampli số MAX98357A.
// I2S chạy bằng DMA nên không tốn CPU và không bị giật khi Bluetooth bận.
//
// Âm phím lưu PCM 16-bit little-endian (/sN.p16, web xử lý sẵn trước khi gửi). Chuông Bluetooth nhúng
// trong firmware và âm phím bản cũ (/sN.adp) là IMA ADPCM 4-bit: 1 byte = 2 mẫu, nibble thấp
// trước, trạng thái bắt đầu từ 0. ADPCM nhẹ hơn 4 lần nhưng giọng nói bị "sạn" (SNR ~24 dB).
//
// MAX98357A nhận số nên im lặng là mẫu 0, không có mức nghỉ, lọc RC hay nhiễu nguồn 3V3 như
// ngõ ra PDM cũ. Chip tự chống "bụp" khi bật/tắt và tự tắt tiếng khi mất xung BCLK.
// - Bản không ngủ: bật I2S một lần lúc khởi động rồi giữ luôn.
//   Bản có ngủ: chỉ tắt khi sắp ngủ nhẹ, bật lại ngay lúc chạm phím (trong lúc chờ tổ hợp).
// - Âm phím vào/ra êm vài ms, nhấn phím khác lúc đang phát thì âm cũ được hạ êm rồi mới
//   chuyển, đổi âm lượng lúc đang phát thì mức trượt dần -> không kêu "tách".
// - Chân SD_MODE của ampli (AMP_SD_PIN): ampli chỉ bật khi có âm cần phát, rảnh AMP_IDLE_OFF_MS
//   thì tắt -> lúc im loa không xì, đỡ hao pin (~2,4 mA lúc chờ, tắt còn <1 µA).

#include "audio.h"
#include "config.h"
#include "wifi_sound.h"
#include <FFat.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_rom_gpio.h"

namespace {

enum : int16_t {  // Cmd.key < 0
  CMD_TONE = -1,
  CMD_STOP = -2,
  CMD_OFF  = -3,  // hạ mức, tắt I2S
  CMD_WAKE = -4,  // bật I2S, nâng mức (không làm gì nếu đang bật)
};

struct Cmd {
  int16_t key;    // >=0: phát âm số key, <0: CMD_*
  uint8_t tone;   // Tone khi key = CMD_TONE
};

struct Note {
  uint16_t freq;  // 0 = lặng
  uint16_t ms;    // 0 = hết chuỗi
};

// (không dùng nữa, chuông là file) Tiếng mở kiểu giọt rơi: rất ngắn, cao độ trượt xuống và tắt dần.
const Note T_WIFI_ON[]  = {{1100, 48}, {0, 0}};
const Note T_WIFI_OFF[] = {{660, 62}, {0, 0}};
const Note T_VOLUME[]   = {{0, 36}, {0, 0}};  // tần số được chọn theo mức âm lượng hiện tại
// Đo méo: mỗi tông 1 giây, cách 0,4 giây, mức cố định không theo âm lượng. Ghi âm lại rồi so
// độ méo theo tần số: méo giảm mạnh ở tần số cao là do loa (màng rung quá biên), không đổi là do mạch
const Note T_TEST[] = {{400, 1000}, {0, 400}, {700, 1000}, {0, 400}, {1000, 1000}, {0, 400},
                       {1500, 1000}, {0, 400}, {2500, 1000}, {0, 400}, {4000, 1000}, {0, 0}};
const float TEST_SOFT_LEVEL = 0.1f, TEST_LOUD_LEVEL = 0.4f;  // so với hết thang số
const Note *const TONES[] = {nullptr, T_WIFI_ON, T_WIFI_OFF, T_VOLUME, nullptr, T_TEST, T_TEST};

const int16_t IMA_STEP[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
  97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
  724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
  4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
  18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
const int8_t IMA_INDEX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

// Giải mã IMA ADPCM, phải khớp từng bước với encodeAdpcm() trong web/index.html
struct Adpcm {
  int32_t pred = 0;
  int32_t index = 0;
  int16_t next(uint8_t n) {
    int32_t step = IMA_STEP[index];
    int32_t d = step >> 3;
    if (n & 4) d += step;
    if (n & 2) d += step >> 1;
    if (n & 1) d += step >> 2;
    pred = constrain(n & 8 ? pred - d : pred + d, -32768, 32767);
    index = constrain(index + IMA_INDEX[n & 7], 0, 88);
    return pred;
  }
};

// Bỏ thành phần một chiều của file (cắt tần dưới ~14 Hz): DC chỉ làm nóng loa và gây "tách"
// ở đầu/cuối đoạn
struct DcBlock {
  int32_t x1 = 0, y1 = 0;
  int32_t next(int32_t x) {
    y1 = x - x1 + y1 - (y1 >> 8);  // hệ số 1 - 1/256
    x1 = x;
    return y1;
  }
};

// MAX98357A CHỈ nhận LRCLK 8 / 16 / 32–48 / 88,2–96 kHz (datasheet: "22.05kHz and 24kHz are NOT
// supported", dải 3 là 30,4–50,4 kHz). Chạy I2S thẳng ở SAMPLE_RATE (22321 Hz) thì ampli dò
// sai xung -> rè ở mọi mức âm lượng, với loa nào cũng vậy. Nên I2S chạy gấp đôi (44642 Hz),
// mọi xử lý vẫn ở SAMPLE_RATE và chỉ nhân đôi số mẫu ở bước cuối (upsample, xem writeLevels)
const uint32_t I2S_RATE = SAMPLE_RATE * 2;
static_assert(I2S_RATE >= 30400 && I2S_RATE <= 50400, "MAX98357A khong nhan LRCLK nay");

// MCLK = 256 * I2S_RATE (BCLK = MCLK / 8). Phần lẻ của 160 MHz / MCLK phải nhỏ hơn 1/126
// để driver chọn bộ chia nguyên (xung đều, không rung pha)
const uint32_t I2S_MCLK = I2S_RATE * 256;
static_assert(160000000UL % I2S_MCLK * 126 < I2S_MCLK, "SAMPLE_RATE khong cho bo chia xung nguyen");

// Bộ nhân đôi mẫu: mỗi mẫu vào ra 2 mẫu, mẫu chẵn là mẫu gốc (trễ UP_TAPS / 2), mẫu lẻ là giá
// trị nửa mẫu sau nội suy bằng sinc cửa sổ Kaiser (lọc nửa băng 63 hệ số). Chèn mẫu thô
// (lặp lại / chèn 0) thì sinh ảnh tần 11–22 kHz nghe như tiếng xì rè, bộ lọc của ampli ở
// 44,6 kHz cắt từ ~21 kHz nên không chặn được
const int UP_TAPS = 32;
float upCoef[UP_TAPS];
int16_t upHist[UP_TAPS * 2];  // lịch sử chép đôi để đọc liền một dải, không cần vòng modulo
int upPos = 0;

void upsampleInit() {
  auto i0 = [](float x) {  // hàm Bessel I0 cho cửa sổ Kaiser
    float sum = 1, term = 1;
    for (int k = 1; k < 25; k++) {
      term *= (x / (2 * k)) * (x / (2 * k));
      sum += term;
    }
    return sum;
  };
  const float beta = 7.0f;  // ~70 dB chặn ảnh tần
  float total = 0;
  for (int j = 0; j < UP_TAPS; j++) {
    float t = j - (UP_TAPS - 1) / 2.0f;  // ±0,5 … ±15,5 mẫu tính từ điểm nội suy
    float r = t / (UP_TAPS / 2.0f);
    upCoef[j] = sinf(PI * t) / (PI * t) * i0(beta * sqrtf(1 - r * r)) / i0(beta);
    total += upCoef[j];
  }
  for (float &c : upCoef) c /= total;  // tín hiệu chậm đi qua giữ nguyên mức
}

// Lọc thông cao Butterworth bậc 4 (2 tầng biquad) ở SPEAKER_HPF_HZ cho mọi âm ra loa. Đo bằng
// chuỗi tông thử (loa nhỏ trong hộp kín, ghi âm bằng điện thoại): 400 Hz méo 84% (phát to thì hài
// còn to hơn tông gốc), 700 Hz 18%, 1 kHz 6–10%, từ 1,5 kHz chỉ 1–2% -> mạch sạch, phần dưới
// ~1 kHz màng loa rung quá biên, chỉ ra tiếng rè. Web lọc dưới 700 Hz là chưa đủ
struct Biquad {
  float b0, b1, b2, a1, a2;
  float z1 = 0, z2 = 0;
  void highPass(float fc, float q) {
    float w = 2 * PI * fc / SAMPLE_RATE, cw = cosf(w), al = sinf(w) / (2 * q), a0 = 1 + al;
    b0 = b2 = (1 + cw) / 2 / a0;
    b1 = -(1 + cw) / a0;
    a1 = -2 * cw / a0;
    a2 = (1 - al) / a0;
  }
  float next(float x) {  // dạng chuyển vị II
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};
Biquad hpf[2];

const size_t CHUNK = 256;                       // mẫu mỗi lần ghi (ở SAMPLE_RATE)
const uint32_t FADE_IN = SAMPLE_RATE * 3 / 1000;
const uint32_t FADE_OUT = SAMPLE_RATE * 6 / 1000;
const char *const WIFI_ON_PATH = "/wifi_on.adp";
const char *const WIFI_OFF_PATH = "/wifi_off.adp";
const char *const BOOT_PATH = "/boot.p16";

QueueHandle_t queue;
i2s_chan_handle_t tx;
int16_t silenceBuf[CHUNK];  // toàn 0
int16_t lastSignal = 0;     // mẫu vừa phát, để hạ êm khi bị cắt ngang
bool ampOn = AMP_SD_PIN < 0;  // không nối chân SD thì ampli luôn chạy
uint32_t quietSamples = 0;    // số mẫu im lặng liên tiếp, để tắt ampli khi rảnh

// Số lần I2S hụt dữ liệu (DMA hết mẫu mới, phát ra mẫu 0 -> tiếng lẹt xẹt). Chỉ để chẩn đoán
volatile uint32_t underruns = 0;
bool IRAM_ATTR onUnderrun(i2s_chan_handle_t, i2s_event_data_t *, void *) {
  underruns++;
  return false;
}

volatile int16_t playingSound = -1;
volatile bool busy = false;
volatile bool idle = true;
volatile uint8_t volume = VOLUME_DEFAULT;

// So file với bản nhúng theo từng khối. Bản cũ đọc từng byte để tính CRC (~89 nghìn lần đọc FFat
// ở 80 MHz) mỗi lần bật máy -> phím chưa nhận trong lúc đó
bool fileMatches(File &file, const uint8_t *data, size_t len) {
  if (file.size() != len) return false;
  static uint8_t buf[1024];
  for (size_t pos = 0; pos < len;) {
    size_t n = file.read(buf, min(sizeof(buf), len - pos));
    if (!n || memcmp(buf, data + pos, n)) return false;
    pos += n;
  }
  return true;
}

// Chép âm nhúng trong firmware ra FFat (chỉ khi khác bản đang có) để player đọc như âm phím
void installSound(const char *path, const uint8_t *data, size_t len) {
  File current = FFat.open(path, "r");
  if (current) {
    bool matches = fileMatches(current, data, len);
    current.close();
    if (matches) return;
  }

  File out = FFat.open(path, "w");
  if (!out) {
    log_e("Khong tao duoc %s", path);
    return;
  }
  size_t written = out.write(data, len);
  out.close();
  if (written != len) log_e("Ghi %s loi: %u/%u", path, written, len);
}

void installWifiSounds() {
  if (FFat.exists("/wifi.adp")) FFat.remove("/wifi.adp");  // bản cũ dùng chung 1 âm cho bật/tắt
  installSound(WIFI_ON_PATH, WIFI_ON_ADP, WIFI_ON_ADP_LEN);
  installSound(WIFI_OFF_PATH, WIFI_OFF_ADP, WIFI_OFF_ADP_LEN);
  installSound(BOOT_PATH, BOOT_P16, BOOT_P16_LEN);
}

// Âm lượng theo cong bình phương (giống cảm nhận của tai), dạng Q16
int32_t gainQ16(uint8_t v) {
  float g = v / 100.0f;
  return (int32_t)(g * g * VOLUME_MAX_GAIN * 65536);
}

// Lọc thông cao (xem Biquad) rồi ghi n mẫu SAMPLE_RATE ra I2S thành 2n mẫu I2S_RATE (xem
// upsampleInit). Mọi đường ra loa, kể cả im lặng, đều qua đây để bộ lọc không bị gián đoạn
void writeLevels(const int16_t *s, size_t n) {
  static int16_t out[CHUNK * 2];
  while (n) {
    size_t m = min(n, CHUNK);
    for (size_t i = 0; i < m; i++) {
      float v = hpf[1].next(hpf[0].next(s[i]));
      upPos = (upPos + UP_TAPS - 1) % UP_TAPS;
      upHist[upPos] = upHist[upPos + UP_TAPS] = (int16_t)constrain(lrintf(v), -32768L, 32767L);
      const int16_t *x = upHist + upPos;  // x[j] = mẫu cách đây j mẫu
      float acc = 0;
      for (int j = 0; j < UP_TAPS; j++) acc += upCoef[j] * x[j];
      out[2 * i] = x[UP_TAPS / 2];
      out[2 * i + 1] = (int16_t)constrain(lrintf(acc), -32768L, 32767L);
    }
    size_t written;
    i2s_channel_write(tx, out, m * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    s += m;
    n -= m;
  }
}

void writeSignal(int16_t *s, size_t n) {
  if (!n) return;
  lastSignal = s[n - 1];
  writeLevels(s, n);
}

// Âm đang phát bị cắt ngang: hạ êm về im lặng thay vì nhảy bậc
void fadeOutLast() {
  if (!lastSignal) return;
  static int16_t buf[FADE_OUT];
  for (uint32_t j = 0; j < FADE_OUT; j++)
    buf[j] = (int32_t)lastSignal * (int32_t)(FADE_OUT - 1 - j) / (int32_t)FADE_OUT;
  writeSignal(buf, FADE_OUT);
  lastSignal = 0;
}

// đẩy hết âm cuối còn trong bộ đệm DMA (~33ms) ra loa
void flush() {
  for (int i = 0; i < 4; i++) writeLevels(silenceBuf, CHUNK);
}

// Bật/tắt ampli bằng chân SD_MODE (thấp = tắt). Lúc tắt chân được giữ (hold) ở mức thấp để
// ampli vẫn tắt trong lúc chip ngủ nhẹ / ngủ sâu
void ampEnable(bool on) {
#if AMP_SD_PIN >= 0
  if (on == ampOn) return;
  if (on) {
    gpio_hold_dis((gpio_num_t)AMP_SD_PIN);
    gpio_set_level((gpio_num_t)AMP_SD_PIN, 1);
    // ampli cần vài ms để khởi động; phát sớm hơn thì mất đầu âm
    for (uint32_t i = 0; i < SAMPLE_RATE * AMP_WAKE_MS / 1000; i += CHUNK) writeLevels(silenceBuf, CHUNK);
  } else {
    flush();
    gpio_set_level((gpio_num_t)AMP_SD_PIN, 0);
    gpio_hold_en((gpio_num_t)AMP_SD_PIN);
  }
  ampOn = on;
#endif
}

void powerUp() {
  i2s_channel_enable(tx);
  lastSignal = 0;
  idle = false;
}

void powerDown() {
  fadeOutLast();
  ampEnable(false);
  flush();
  i2s_channel_disable(tx);
  idle = true;
}

void playerTask(void *) {
  static int16_t buf[CHUNK];
  static uint8_t raw[CHUNK * 2];   // CHUNK mẫu PCM 16-bit (ADPCM chỉ dùng CHUNK / 2 byte đầu)
  File file;
  bool pcm = false;                // file đang phát: PCM 16-bit hay ADPCM
  uint32_t underrunsAtStart = 0, readMaxUs = 0;  // chẩn đoán: in ra khi phát xong âm phím
  float chime = 0;                 // âm báo nhúng (Bluetooth, mở nguồn): mức so với âm phím. 0 = âm phím
  Adpcm adpcm;
  DcBlock dc;
  uint32_t filePos = 0, fileTotal = 0;  // mẫu đã phát / tổng số mẫu của file
  int32_t gain = 0;                // Q16, trượt dần tới mức đích
  const Note *note = nullptr;
  uint32_t noteTotal = 0, noteLeft = 0;
  float phase = 0;
  float toneAmp = 0;
  uint16_t toneFrequency = 0;
  uint16_t glideEndFrequency = 0;

  auto startNote = [&]() {
    noteTotal = noteLeft = (uint32_t)note->ms * SAMPLE_RATE / 1000;
    phase = 0;
  };
  auto fileGain = [&]() {
    // tiếng ding nhỏ hơn âm phím nhưng luôn nghe được, kể cả khi âm lượng để thấp
    uint8_t vol = volume;
    return chime ? (int32_t)(gainQ16(max<uint8_t>(vol, 40)) * chime) : gainQ16(vol);
  };
  auto openSound = [&](const String &path, bool isPcm) {
    file = FFat.open(path, "r");
    if (!file) return false;
    pcm = isPcm;
    adpcm = Adpcm();
    dc = DcBlock();
    filePos = 0;
    fileTotal = pcm ? file.size() / 2 : file.size() * 2;
    gain = fileGain();
    return true;
  };

#if !ENABLE_SLEEP
  powerUp();  // bản không ngủ: bật một lần, giữ luôn
#endif

  for (;;) {
    bool active = file || note;
    Cmd cmd;

    // đang bật thì không được ngừng ghi: hết dữ liệu DMA sẽ phát lại khối cũ
    if (xQueueReceive(queue, &cmd, active || !idle ? 0 : portMAX_DELAY) == pdTRUE) {
      if (cmd.key == CMD_WAKE) {  // không cắt âm đang phát
        if (idle) powerUp();
        quietSamples = 0;
        ampEnable(true);  // bật sẵn trong lúc chờ tổ hợp phím
        continue;
      }

      if (file) file.close();
      chime = 0;
      playingSound = -1;
      note = nullptr;
      if (!idle) fadeOutLast();

      if (cmd.key == CMD_STOP) { busy = false; continue; }
      if (cmd.key == CMD_OFF) { busy = false; if (!idle) powerDown(); continue; }

      if (cmd.key >= 0) {  // chỉ phát âm của phím, không kèm âm báo
        // âm nạp từ bản cũ (ADPCM) vẫn phát được tới khi nạp lại
        bool ok = FFat.exists(soundPath(cmd.key)) ? openSound(soundPath(cmd.key), true)
                                                  : openSound(legacySoundPath(cmd.key), false);
        if (!ok) { busy = false; continue; }
        log_i("Phat am %d: %s, %u byte", cmd.key + 1, pcm ? "PCM 16-bit" : "ADPCM (ban cu)", file.size());
        underrunsAtStart = underruns;
        readMaxUs = 0;
        playingSound = cmd.key;
      } else if (cmd.tone == TONE_BOOT) {  // ding mở nguồn, PCM 16-bit
        chime = BOOT_SOUND_GAIN;
        if (!openSound(BOOT_PATH, true)) {
          chime = 0;
          busy = false;
          continue;
        }
      } else if (cmd.tone == TONE_BLE_ON || cmd.tone == TONE_BLE_OFF) {  // chuông bật / tắt Bluetooth
        chime = BLE_SOUND_GAIN;
        if (!openSound(cmd.tone == TONE_BLE_ON ? WIFI_ON_PATH : WIFI_OFF_PATH, false)) {
          chime = 0;
          busy = false;
          continue;
        }
      } else if (cmd.tone != TONE_NONE) {
        note = TONES[cmd.tone];
        startNote();
        // âm báo âm lượng phát đúng mức mới (ở 0% vẫn kêu khẽ để biết đã chạm đáy);
        // các âm báo khác luôn nghe được
        uint8_t vol = volume;
        uint8_t v = cmd.tone == TONE_VOLUME ? max<uint8_t>(vol, 10) : max<uint8_t>(vol, 40);
        float level = TONE_LEVEL;
        toneAmp = 32767 * level * gainQ16(v) / 65536;
        if (cmd.tone == TONE_TEST_SOFT || cmd.tone == TONE_TEST_LOUD)
          toneAmp = 32767 * (cmd.tone == TONE_TEST_SOFT ? TEST_SOFT_LEVEL : TEST_LOUD_LEVEL);
        // bíp âm lượng 1400–1800 Hz: trên SPEAKER_HPF_HZ, dải loa nhỏ phát sạch
        toneFrequency = cmd.tone == TONE_VOLUME ? 1400 + (uint16_t)vol * 4
                       : 0;
        glideEndFrequency = 0;
      } else {
        busy = false;
        continue;
      }
      busy = true;
      if (idle) powerUp();
      quietSamples = 0;
      ampEnable(true);
      continue;
    }

    if (!active) {  // đang bật mà rảnh: giữ mức nghỉ, im lâu thì tắt ampli
      writeLevels(silenceBuf, CHUNK);
      quietSamples += CHUNK;
      if (ampOn && quietSamples >= SAMPLE_RATE * AMP_IDLE_OFF_MS / 1000) ampEnable(false);
      continue;
    }

    size_t n;
    if (note) {
      n = min<size_t>(CHUNK, noteLeft);
      uint16_t startFreq = toneFrequency ? toneFrequency : note->freq;
      uint16_t endFreq = glideEndFrequency ? glideEndFrequency : startFreq;
      for (size_t j = 0; j < n; j++) {
        if (!startFreq) { buf[j] = 0; continue; }
        uint32_t pos = noteTotal - noteLeft + j, left = noteLeft - j;
        const uint32_t attack = SAMPLE_RATE * 4 / 1000;
        const uint32_t release = SAMPLE_RATE * 9 / 1000;
        float env = 1.0f;
        if (glideEndFrequency) {
          if (pos < SAMPLE_RATE * 2 / 1000) {
            env = 0.5f - 0.5f * cosf(PI * pos / (SAMPLE_RATE * 2 / 1000));
          }
          float decay = 1.0f - (float)pos / noteTotal;
          env *= decay * decay;
        } else if (pos < attack) env = 0.5f - 0.5f * cosf(PI * pos / attack);
        else if (left < release) env = 0.5f - 0.5f * cosf(PI * left / release);
        float progress = (float)pos / noteTotal;
        float freq = startFreq + ((int32_t)endFreq - startFreq) * progress;
        float inc = 2 * PI * freq / SAMPLE_RATE;
        buf[j] = (int16_t)(sinf(phase) * toneAmp * env);
        phase += inc;
        if (phase > 2 * PI) phase -= 2 * PI;
      }
      noteLeft -= n;
      if (!noteLeft) {
        note++;
        if (note->ms) startNote();
        else { note = nullptr; busy = false; }
      }
    } else {
      uint32_t t0 = micros();
      size_t got = file.read(raw, pcm ? CHUNK * 2 : CHUNK / 2);
      readMaxUs = max<uint32_t>(readMaxUs, micros() - t0);
      if (!got) {
        if (playingSound >= 0)
          log_i("Am %d xong: hut du lieu %u lan, doc file lau nhat %u us", playingSound + 1,
                underruns - underrunsAtStart, readMaxUs);
        file.close();
        chime = 0;
        playingSound = -1;
        busy = false;
        fadeOutLast();  // file bị cắt giữa chừng thì vẫn tắt êm
        continue;
      }
      n = pcm ? got / 2 : got * 2;
      int32_t target = fileGain();  // âm lượng đổi lúc đang phát: trượt dần trong 1 khối
      for (size_t j = 0; j < n; j++, filePos++) {
        int16_t raw16 = pcm ? (int16_t)(raw[2 * j] | raw[2 * j + 1] << 8)
                            : adpcm.next(j & 1 ? raw[j / 2] >> 4 : raw[j / 2] & 0x0F);
        int32_t s = dc.next(raw16);
        int32_t env = 65536;  // vào/ra êm vài ms để không kêu "tách" ở hai đầu đoạn cắt
        if (filePos < FADE_IN) env = filePos * 65536 / FADE_IN;
        uint32_t left = fileTotal > filePos ? fileTotal - filePos : 0;
        if (left < FADE_OUT) env = min<int32_t>(env, left * 65536 / FADE_OUT);
        int32_t g = gain + (target - gain) * (int32_t)j / (int32_t)n;
        // 64-bit: g * env tràn int32 khi gain >= 0,5 (âm lượng cao) -> âm phím ra rác hoặc im
        int32_t v = (int32_t)(((int64_t)s * g * env) >> 32);
        buf[j] = constrain(v, -32768, 32767);
      }
      gain = target;
    }
    writeSignal(buf, n);
  }
}

void send(int16_t key, Tone t) {
  Cmd c{key, (uint8_t)t};
  xQueueOverwrite(queue, &c);
}

}  // namespace

String soundPath(int id) { return "/s" + String(id + 1) + ".p16"; }
String legacySoundPath(int id) { return "/s" + String(id + 1) + ".adp"; }

void audioBegin() {
  installWifiSounds();
  upsampleInit();
  hpf[0].highPass(SPEAKER_HPF_HZ, 0.5412f);  // Q của Butterworth bậc 4
  hpf[1].highPass(SPEAKER_HPF_HZ, 1.3066f);
#if AMP_SD_PIN >= 0
  esp_rom_gpio_pad_select_gpio(AMP_SD_PIN);
  gpio_set_level((gpio_num_t)AMP_SD_PIN, 0);  // ampli tắt tới khi có âm cần phát
  gpio_set_direction((gpio_num_t)AMP_SD_PIN, GPIO_MODE_OUTPUT);
  gpio_hold_dis((gpio_num_t)AMP_SD_PIN);  // vừa thức dậy từ ngủ sâu
  gpio_hold_en((gpio_num_t)AMP_SD_PIN);
#endif

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  // Hết dữ liệu thì DMA phát mẫu 0 (im lặng) thay vì lặp lại khối cũ
  chanCfg.auto_clear = true;
  // Bộ đệm DMA ~32ms (mặc định ~65ms): đang phát im lặng thì âm phím vào hàng sau chừng ấy
  chanCfg.dma_desc_num = 6;
  chanCfg.dma_frame_num = 240;  // mẫu ở I2S_RATE
  ESP_ERROR_CHECK(i2s_new_channel(&chanCfg, &tx, nullptr));

  i2s_std_config_t cfg = {};
  cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_RATE);
  cfg.clk_cfg.clk_src = I2S_CLK_SRC_PLL_160M;  // xem I2S_MCLK
  // Mono: ESP32-S3 phát cùng một mẫu ở cả kênh trái lẫn phải (slot_mask = BOTH), nên chân
  // SD_MODE chọn kênh nào (trái / phải / trộn) ampli cũng nhận đúng âm
  cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;  // MAX98357A không cần MCLK
  cfg.gpio_cfg.bclk = (gpio_num_t)AUDIO_BCLK_PIN;
  cfg.gpio_cfg.ws = (gpio_num_t)AUDIO_LRC_PIN;
  cfg.gpio_cfg.dout = (gpio_num_t)AUDIO_DIN_PIN;
  cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &cfg));
  i2s_event_callbacks_t cbs = {};
  cbs.on_send_q_ovf = onUnderrun;
  ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx, &cbs, nullptr));

  queue = xQueueCreate(1, sizeof(Cmd));
  xTaskCreatePinnedToCore(playerTask, "player", 4096, nullptr, 5, nullptr, 1);
}

void audioPlaySound(int id) { send(id, TONE_NONE); }
void audioTone(Tone t) { send(CMD_TONE, t); }

void audioStop() {
  send(CMD_STOP, TONE_NONE);
  for (int i = 0; i < 100 && (busy || playingSound >= 0 || uxQueueMessagesWaiting(queue)); i++) delay(5);
}

// Chỉ gửi khi hàng đợi trống để không đè lệnh phát đang chờ
void audioWake() {
  if (idle && !uxQueueMessagesWaiting(queue)) send(CMD_WAKE, TONE_NONE);
}

void audioOff() {
  if (!idle && !busy && !uxQueueMessagesWaiting(queue)) send(CMD_OFF, TONE_NONE);
}

void audioShutdown() {
  send(CMD_OFF, TONE_NONE);
  for (int i = 0; i < 200 && (!idle || uxQueueMessagesWaiting(queue)); i++) delay(5);
  // ampli đã tắt, chân SD_MODE đang được giữ thấp suốt lúc ngủ sâu (xem ampEnable)
}

int audioPlayingSound() { return playingSound; }
bool audioBusy() { return busy || uxQueueMessagesWaiting(queue); }
bool audioIdle() { return idle && !uxQueueMessagesWaiting(queue); }

void audioSetVolume(uint8_t v) { volume = min<uint8_t>(v, 100); }
uint8_t audioVolume() { return volume; }
