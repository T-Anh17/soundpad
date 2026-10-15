// Phát âm thanh bằng I2S PDM ra 1 chân -> lọc RC -> ampli analog HXJ8002.
// PDM chạy bằng DMA nên không tốn CPU và không bị giật khi WiFi bận.
//
// File âm thanh nén IMA ADPCM 4-bit (web nén trước khi gửi): 1 byte = 2 mẫu, nibble thấp
// trước, trạng thái bắt đầu từ 0. Nhẹ hơn PCM 16-bit 4 lần.
//
// Chống "bụp" và "xì":
// - Tắt bộ lọc high-pass có sẵn trong khối PDM. Nó kéo mọi mức một chiều về giữa (~1,65V)
//   trong vài ms nên đoạn kéo mức khi bật/tắt bị vọt quá, và lúc tắt chân nhảy thẳng từ
//   1,65V xuống 0V -> "bụp". Thay bằng bộ lọc DC phần mềm chỉ áp lên âm thanh.
// - Mức nghỉ (im lặng) đặt thấp, vừa đủ chỗ cho biên độ lớn nhất, thay vì ở giữa. Nhiễu
//   nguồn 3V3 lọt ra chân tỉ lệ với mức nghỉ nên nhỏ đi ~5 lần (bớt xì), và bước nâng/hạ
//   mức khi bật/tắt cũng nhỏ đi chừng ấy (bớt bụp).
// - Chân âm thanh chỉ được nối vào I2S sau khi PDM đã chạy ổn ở mức sàn, và tách ra trước
//   khi dừng, nên lúc bật/tắt I2S không lọt xung lạ ra loa.
// - Bản không ngủ: bật một lần lúc khởi động rồi giữ luôn, không bật/tắt theo từng lần phát.
//   Bản có ngủ: chỉ tắt khi sắp ngủ nhẹ, bật lại ngay lúc chạm phím (trong lúc chờ tổ hợp).
// - Âm phím vào/ra êm vài ms, nhấn phím khác lúc đang phát thì âm cũ được hạ êm rồi mới
//   chuyển, đổi âm lượng lúc đang phát thì mức trượt dần -> không kêu "tách".

#include "audio.h"
#include "config.h"
#include "wifi_sound.h"
#include <FFat.h>
#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

namespace {

enum : int8_t {   // Cmd.key < 0
  CMD_TONE = -1,
  CMD_STOP = -2,
  CMD_OFF  = -3,  // hạ mức, tắt I2S
  CMD_WAKE = -4,  // bật I2S, nâng mức (không làm gì nếu đang bật)
};

struct Cmd {
  int8_t key;     // >=0: phát phím, <0: CMD_*
  uint8_t tone;   // Tone khi key = CMD_TONE
};

struct Note {
  uint16_t freq;  // 0 = lặng
  uint16_t ms;    // 0 = hết chuỗi
};

// Tiếng mở WiFi kiểu giọt rơi: rất ngắn, cao độ trượt xuống và tắt dần.
const Note T_WIFI_ON[]  = {{1100, 48}, {0, 0}};
const Note T_WIFI_OFF[] = {{660, 62}, {0, 0}};
const Note T_VOLUME[]   = {{0, 36}, {0, 0}};  // tần số được chọn theo mức âm lượng hiện tại
const Note *const TONES[] = {nullptr, T_WIFI_ON, T_WIFI_OFF, T_VOLUME};

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

// Bỏ thành phần một chiều của file (cắt tần dưới ~14 Hz), thay cho bộ lọc của khối PDM
struct DcBlock {
  int32_t x1 = 0, y1 = 0;
  int32_t next(int32_t x) {
    y1 = x - x1 + y1 - (y1 >> 8);  // hệ số 1 - 1/256
    x1 = x;
    return y1;
  }
};

// Mức mẫu đưa ra PDM: -32768 = chân gần 0V, 32767 = gần 3,3V.
// SILENCE: mức nghỉ, chừa 1,6 lần biên độ lớn nhất (lọc DC có thể vọt) + khoảng an toàn
const int32_t FLOOR = -32768;
const int32_t SILENCE = FLOOR + (int32_t)(VOLUME_MAX_GAIN * 32767 * 1.6f) + 1500;

const size_t CHUNK = 256;                       // mẫu mỗi lần ghi
const uint32_t FADE_IN = SAMPLE_RATE * 3 / 1000;
const uint32_t FADE_OUT = SAMPLE_RATE * 6 / 1000;
const char *const WIFI_SOUND_PATH = "/wifi.adp";

QueueHandle_t queue;
i2s_chan_handle_t tx;
int16_t floorBuf[CHUNK];
int16_t silenceBuf[CHUNK];
int16_t lastSignal = 0;  // mẫu vừa phát (chưa cộng mức nghỉ), để hạ êm khi bị cắt ngang

volatile int8_t playingKey = -1;
volatile bool busy = false;
volatile bool idle = true;
volatile uint8_t volume = VOLUME_DEFAULT;

uint32_t fileCrc32(File &file) {
  uint32_t crc = 0xFFFFFFFFu;
  while (file.available()) {
    crc ^= (uint8_t)file.read();
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
  }
  return ~crc;
}

void installWifiSound() {
  File current = FFat.open(WIFI_SOUND_PATH, "r");
  if (current) {
    bool matches = current.size() == WIFI_SOUND_ADP_LEN && fileCrc32(current) == WIFI_SOUND_ADP_CRC32;
    current.close();
    if (matches) return;
  }

  File out = FFat.open(WIFI_SOUND_PATH, "w");
  if (!out) {
    log_e("Khong tao duoc am thanh WiFi");
    return;
  }
  size_t written = out.write(WIFI_SOUND_ADP, WIFI_SOUND_ADP_LEN);
  out.close();
  if (written != WIFI_SOUND_ADP_LEN) log_e("Ghi am thanh WiFi loi: %u/%u", written, WIFI_SOUND_ADP_LEN);
}

// Âm lượng theo cong bình phương (giống cảm nhận của tai), dạng Q16
int32_t gainQ16(uint8_t v) {
  float g = v / 100.0f;
  return (int32_t)(g * g * VOLUME_MAX_GAIN * 65536);
}

void writeLevels(const int16_t *s, size_t n) {
  size_t written;
  i2s_channel_write(tx, s, n * sizeof(int16_t), &written, portMAX_DELAY);
}

// Ghi âm thanh: cộng mức nghỉ rồi đưa ra PDM
void writeSignal(int16_t *s, size_t n) {
  if (!n) return;
  lastSignal = s[n - 1];
  for (size_t j = 0; j < n; j++) s[j] = constrain(SILENCE + s[j], -32768, 32767);
  writeLevels(s, n);
}

// Kéo mức từ from tới to theo nửa chu kỳ cos (không gãy góc ở hai đầu)
void ramp(int32_t from, int32_t to, uint32_t ms) {
  static int16_t buf[CHUNK];
  const size_t total = max<size_t>(1, SAMPLE_RATE * ms / 1000);
  for (size_t i = 0; i < total; i += CHUNK) {
    size_t n = min(CHUNK, total - i);
    for (size_t j = 0; j < n; j++) {
      float s = 0.5f - 0.5f * cosf(PI * (i + j + 1) / total);  // 0 -> 1
      buf[j] = from + (to - from) * s;
    }
    writeLevels(buf, n);
  }
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

// Chân về GPIO thường, mức 0. Đặt mức trước rồi mới chuyển nên không có lúc thả nổi
void pinLow(uint8_t pin) {
  gpio_set_level((gpio_num_t)pin, 0);
  esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
}

void powerUp(uint32_t ms) {
  size_t loaded;
  do {  // nạp sẵn mức sàn vào bộ đệm DMA
    i2s_channel_preload_data(tx, floorBuf, sizeof(floorBuf), &loaded);
  } while (loaded == sizeof(floorBuf));
  i2s_channel_enable(tx);
  // chạy mức sàn ~20ms cho bộ lọc nội bộ của PDM ổn định, rồi mới nối chân vào I2S
  for (int i = 0; i < 2; i++) writeLevels(floorBuf, CHUNK);
  esp_rom_gpio_connect_out_signal(AUDIO_PIN, I2S0O_SD_OUT_IDX, false, false);
  ramp(FLOOR, SILENCE, ms);
  lastSignal = 0;
  idle = false;
}

void powerDown(uint32_t ms) {
  fadeOutLast();
  ramp(SILENCE, FLOOR, ms);
  for (int i = 0; i < 8; i++) writeLevels(floorBuf, CHUNK);  // đẩy hết dữ liệu cũ trong DMA
  pinLow(AUDIO_PIN);  // tách chân khỏi I2S trước khi dừng
  i2s_channel_disable(tx);
  idle = true;
}

void playerTask(void *) {
  static int16_t buf[CHUNK];
  static uint8_t raw[CHUNK / 2];   // CHUNK mẫu ADPCM
  File file;
  bool wifiSound = false;
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
    // tiếng ding WiFi nhỏ hơn âm phím nhưng luôn nghe được, kể cả khi âm lượng để thấp
    uint8_t vol = volume;
    return wifiSound ? (int32_t)(gainQ16(max<uint8_t>(vol, 40)) * WIFI_SOUND_GAIN) : gainQ16(vol);
  };
  auto openSound = [&](const String &path) {
    file = FFat.open(path, "r");
    if (!file) return false;
    adpcm = Adpcm();
    dc = DcBlock();
    filePos = 0;
    fileTotal = file.size() * 2;
    gain = fileGain();
    return true;
  };

#if !ENABLE_SLEEP
  powerUp(AUDIO_BOOT_RAMP_MS);  // bản không ngủ: bật một lần, giữ luôn
#endif

  for (;;) {
    bool active = file || note;
    Cmd cmd;

    // đang bật thì không được ngừng ghi: hết dữ liệu DMA sẽ phát lại khối cũ
    if (xQueueReceive(queue, &cmd, active || !idle ? 0 : portMAX_DELAY) == pdTRUE) {
      if (cmd.key == CMD_WAKE) {  // không cắt âm đang phát
        if (idle) powerUp(AUDIO_RAMP_UP_MS);
        continue;
      }

      if (file) file.close();
      wifiSound = false;
      playingKey = -1;
      note = nullptr;
      if (!idle) fadeOutLast();

      if (cmd.key == CMD_STOP) { busy = false; continue; }
      if (cmd.key == CMD_OFF) { busy = false; if (!idle) powerDown(AUDIO_RAMP_DOWN_MS); continue; }

      if (cmd.key >= 0) {  // chỉ phát âm của phím, không kèm âm báo
        if (!openSound(soundPath(cmd.key))) { busy = false; continue; }
        playingKey = cmd.key;
      } else if (cmd.tone == TONE_WIFI_ON) {
        wifiSound = true;
        if (!openSound(WIFI_SOUND_PATH)) { wifiSound = false; busy = false; continue; }
      } else if (cmd.tone != TONE_NONE) {
        note = TONES[cmd.tone];
        startNote();
        // âm báo âm lượng phát đúng mức mới (ở 0% vẫn kêu khẽ để biết đã chạm đáy);
        // các âm báo khác luôn nghe được
        uint8_t vol = volume;
        uint8_t v = cmd.tone == TONE_VOLUME ? max<uint8_t>(vol, 10) : max<uint8_t>(vol, 40);
        float level = cmd.tone == TONE_VOLUME ? TONE_LEVEL : WIFI_TONE_LEVEL;
        toneAmp = 32767 * level * gainQ16(v) / 65536;
        toneFrequency = cmd.tone == TONE_VOLUME ? 520 + (uint16_t)vol * 3
                       : cmd.tone == TONE_WIFI_ON ? 1100 : 0;
        glideEndFrequency = cmd.tone == TONE_WIFI_ON ? 740 : 0;
      } else {
        busy = false;
        continue;
      }
      busy = true;
      if (idle) powerUp(AUDIO_RAMP_UP_MS);
      continue;
    }

    if (!active) {  // đang bật mà rảnh: giữ mức nghỉ
      writeLevels(silenceBuf, CHUNK);
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
      size_t got = file.read(raw, sizeof(raw));
      if (!got) {
        file.close();
        wifiSound = false;
        playingKey = -1;
        busy = false;
        fadeOutLast();  // file bị cắt giữa chừng thì vẫn tắt êm
        continue;
      }
      n = got * 2;
      int32_t target = fileGain();  // âm lượng đổi lúc đang phát: trượt dần trong 1 khối
      for (size_t j = 0; j < n; j++, filePos++) {
        uint8_t nib = j & 1 ? raw[j / 2] >> 4 : raw[j / 2] & 0x0F;
        int32_t s = dc.next(adpcm.next(nib));
        int32_t env = 65536;  // vào/ra êm vài ms để không kêu "tách" ở hai đầu đoạn cắt
        if (filePos < FADE_IN) env = filePos * 65536 / FADE_IN;
        uint32_t left = fileTotal > filePos ? fileTotal - filePos : 0;
        if (left < FADE_OUT) env = min<int32_t>(env, left * 65536 / FADE_OUT);
        int32_t g = gain + (target - gain) * (int32_t)j / (int32_t)n;
        buf[j] = constrain((s * ((g * env) >> 16)) >> 16, -32768, 32767);
      }
      gain = target;
    }
    writeSignal(buf, n);
  }
}

void send(int8_t key, Tone t) {
  Cmd c{key, (uint8_t)t};
  xQueueOverwrite(queue, &c);
}

}  // namespace

String soundPath(uint8_t key) { return "/s" + String(key + 1) + ".adp"; }

void audioBegin() {
  installWifiSound();
  for (size_t i = 0; i < CHUNK; i++) {
    floorBuf[i] = FLOOR;
    silenceBuf[i] = SILENCE;
  }
  gpio_hold_dis((gpio_num_t)AUDIO_PIN);  // vừa thức dậy từ ngủ sâu

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  // Không tự xoá khi hết dữ liệu: xoá = mẫu 0 = mức giữa ~1,65V -> "bụp". Luôn ghi đủ dữ liệu
  chanCfg.auto_clear = false;
  // Bộ đệm DMA ~33ms (mặc định ~65ms): đang giữ mức nghỉ thì âm phím vào hàng sau chừng ấy
  chanCfg.dma_desc_num = 6;
  chanCfg.dma_frame_num = 120;
  ESP_ERROR_CHECK(i2s_new_channel(&chanCfg, &tx, nullptr));

  i2s_pdm_tx_gpio_config_t gpioCfg = {};
  gpioCfg.clk = (gpio_num_t)AUDIO_CLK_PIN;
  gpioCfg.dout = (gpio_num_t)AUDIO_PIN;
#if SOC_I2S_PDM_MAX_TX_LINES > 1
  gpioCfg.dout2 = I2S_GPIO_UNUSED;
#endif
  gpioCfg.invert_flags.clk_inv = false;

  // Chế độ DAC: 1 dòng PDM thẳng ra lọc RC. Chế độ codec mặc định ghép 2 kênh xen kẽ trên
  // một dây (dành cho chip nhận PDM), qua lọc RC thành nhiễu xì xèo, rè
  i2s_pdm_tx_config_t cfg = {};
  cfg.clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(SAMPLE_RATE);
  cfg.slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  cfg.slot_cfg.hp_en = false;  // giữ được mức nghỉ thấp và đoạn kéo mức (xem đầu file)
  cfg.gpio_cfg = gpioCfg;
  ESP_ERROR_CHECK(i2s_channel_init_pdm_tx_mode(tx, &cfg));
  // Chế độ DAC không cần xung PDM ở ngoài: tắt chân xung cho đỡ nhiễu 6 MHz lan sang dây âm thanh
  pinLow(AUDIO_CLK_PIN);
  pinLow(AUDIO_PIN);

  queue = xQueueCreate(1, sizeof(Cmd));
  xTaskCreatePinnedToCore(playerTask, "player", 4096, nullptr, 5, nullptr, 1);
}

void audioPlayKey(uint8_t key) { send(key, TONE_NONE); }
void audioTone(Tone t) { send(CMD_TONE, t); }

void audioStop() {
  send(CMD_STOP, TONE_NONE);
  for (int i = 0; i < 100 && (busy || playingKey >= 0 || uxQueueMessagesWaiting(queue)); i++) delay(5);
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
  gpio_hold_en((gpio_num_t)AUDIO_PIN);  // giữ 0V trong lúc ngủ sâu
}

int audioPlayingKey() { return playingKey; }
bool audioBusy() { return busy || uxQueueMessagesWaiting(queue); }
bool audioIdle() { return idle && !uxQueueMessagesWaiting(queue); }

void audioSetVolume(uint8_t v) { volume = min<uint8_t>(v, 100); }
uint8_t audioVolume() { return volume; }
