#include "power.h"
#include "audio.h"
#include "config.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"

namespace {

int mv = 0;
uint32_t lastRead = 0;

int readMv() {
  // Chạy pin thì mạch chia áp đo pin chỉ có điện khi LCD_POWER_PIN cao (để thấp đọc ra ~0,2 V).
  // Bật lúc đo rồi tắt lại cho đỡ hao
  digitalWrite(LCD_POWER_PIN, HIGH);
  delay(5);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
  digitalWrite(LCD_POWER_PIN, LOW);
  return sum / 8 * 2;  // board chia áp 1/2
}

// Chip còn chạy thì pin không thể dưới mức này -> số đo sai, đừng coi là pin cạn
bool valid() { return mv >= BAT_MIN_VALID_MV; }

// Điện áp Li-Po lúc nghỉ -> %
const int16_t CURVE[][2] = {
  {4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50}, {3750, 40},
  {3700, 30}, {3650, 20}, {3600, 12}, {3500, 5}, {3300, 0},
};

}  // namespace

void powerBegin() {
  // Màn hình đã tháo: tắt nguồn màn hình và đèn nền
  gpio_hold_dis((gpio_num_t)LCD_POWER_PIN);
  pinMode(LCD_POWER_PIN, OUTPUT);
  digitalWrite(LCD_POWER_PIN, LOW);
  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, LOW);

  mv = readMv();
  lastRead = millis();
}

void powerUpdate(bool audioBusy) {
  if (audioBusy || millis() - lastRead < 5000) return;
  lastRead = millis();
  mv = (mv * 7 + readMv() * 3) / 10;  // làm mượt
}

int batteryMv() { return mv; }

int batteryPercent() {
  if (!valid()) return -1;
  if (mv >= CURVE[0][0]) return 100;
  for (size_t i = 1; i < sizeof(CURVE) / sizeof(CURVE[0]); i++) {
    if (mv >= CURVE[i][0]) {
      int v0 = CURVE[i][0], v1 = CURVE[i - 1][0], p0 = CURVE[i][1], p1 = CURVE[i - 1][1];
      return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
    }
  }
  return 0;
}

bool batteryCritical() { return valid() && mv < BAT_CRITICAL_MV; }

uint32_t wakeKeyMask() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return 0;
  uint64_t gpios = esp_sleep_get_ext1_wakeup_status();
  uint32_t mask = 0;
  for (uint8_t i = 0; i < KEY_COUNT; i++)
    if (gpios & (1ULL << KEY_PINS[i])) mask |= 1u << i;
  return mask;
}

void lightSleep(uint32_t maxMs) {
  for (uint8_t i = 0; i < KEY_COUNT; i++) gpio_wakeup_enable((gpio_num_t)KEY_PINS[i], GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)maxMs * 1000);

  esp_light_sleep_start();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (uint8_t i = 0; i < KEY_COUNT; i++) gpio_wakeup_disable((gpio_num_t)KEY_PINS[i]);
}

void deepSleep() {
  audioShutdown();

  uint64_t mask = 0;
  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    gpio_num_t pin = (gpio_num_t)KEY_PINS[i];
    mask |= 1ULL << pin;
    rtc_gpio_pullup_en(pin);    // giữ điện trở kéo lên khi ngủ sâu
    rtc_gpio_pulldown_dis(pin);
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

  gpio_hold_en((gpio_num_t)LCD_POWER_PIN);
  Serial.flush();
  esp_deep_sleep_start();
}
