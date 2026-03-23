
#include "app/buzzer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* kTag = "buzzer";
// 已改为GPIO9，未被其他功能占用
static constexpr gpio_num_t kBuzzerGpio = GPIO_NUM_9;
static constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_0;
static constexpr ledc_channel_t kLedcChannel = LEDC_CHANNEL_0;
static constexpr ledc_timer_bit_t kDutyResolution = LEDC_TIMER_10_BIT;
static constexpr uint32_t kDuty = (1U << 9);  // 50% duty for a passive buzzer.

struct ToneStep {
  uint32_t freq_hz;
  uint32_t duration_ms;
};

// 经典8-bit电子音阶（Mario/FC风格）
static constexpr ToneStep kBootToneSteps[] = {
  // 清澈高频电子音自检序列
  {2637, 80}, // E7
  {3136, 80}, // G7
  {3520, 80}, // A7
  {2794, 80}, // F#7
  {3136, 80}, // G7
  {3951, 80}, // B7
  {4186, 120}, // C8
  {3520, 80}, // A7
  {3136, 80}, // G7
  {2637, 80}, // E7
  {3136, 80}, // G7
  {3520, 80}, // A7
  {2794, 80}, // F#7
  {3136, 80}, // G7
  {3951, 80}, // B7
  {4186, 120}, // C8
  {3520, 80}, // A7
  {3136, 80}, // G7
  {2637, 80}, // E7
  {4186, 200}, // C8
};

static esp_err_t ConfigureTimer(uint32_t freq_hz) {
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = kLedcMode;
  timer_cfg.timer_num = kLedcTimer;
  timer_cfg.duty_resolution = kDutyResolution;
  timer_cfg.freq_hz = freq_hz;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  return ledc_timer_config(&timer_cfg);
}

static esp_err_t ConfigureChannel() {
  ledc_channel_config_t channel_cfg = {};
  channel_cfg.gpio_num = static_cast<int>(kBuzzerGpio);
  channel_cfg.speed_mode = kLedcMode;
  channel_cfg.channel = kLedcChannel;
  channel_cfg.intr_type = LEDC_INTR_DISABLE;
  channel_cfg.timer_sel = kLedcTimer;
  channel_cfg.duty = 0;
  channel_cfg.hpoint = 0;
  return ledc_channel_config(&channel_cfg);
}

static bool StartTone(uint32_t freq_hz) {
  esp_err_t err = ConfigureTimer(freq_hz);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "timer config failed for %lu Hz: %s", static_cast<unsigned long>(freq_hz),
             esp_err_to_name(err));
    return false;
  }

  err = ledc_set_duty(kLedcMode, kLedcChannel, kDuty);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "set duty failed: %s", esp_err_to_name(err));
    return false;
  }

  err = ledc_update_duty(kLedcMode, kLedcChannel);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "update duty failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

static void StopTone() {
  (void)ledc_stop(kLedcMode, kLedcChannel, 0);
}

void BuzzerPlayTone(unsigned freq_hz, unsigned duration_ms) {
  ConfigureChannel();
  if (StartTone(freq_hz)) {
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    StopTone();
  }
  gpio_reset_pin(kBuzzerGpio);
}

void BuzzerPlaySequence(const unsigned* freq_hz, const unsigned* duration_ms, unsigned count) {
  ConfigureChannel();
  for (unsigned i = 0; i < count; ++i) {
    if (StartTone(freq_hz[i])) {
      vTaskDelay(pdMS_TO_TICKS(duration_ms[i]));
      StopTone();
      vTaskDelay(pdMS_TO_TICKS(30));
    }
  }
  gpio_reset_pin(kBuzzerGpio);
}

void RunBootBuzzerTest() {
#if defined(B_PIN_DEFAULT) && (B_PIN_DEFAULT == 8)
  ESP_LOGW(kTag, "GPIO8 is also configured as HUB75 B pin in this build; boot buzzer test may conflict with panel wiring");
#endif

  ESP_LOGI(kTag, "boot buzzer test start on GPIO%u", static_cast<unsigned>(kBuzzerGpio));

  esp_err_t err = ConfigureTimer(kBootToneSteps[0].freq_hz);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "initial timer config failed: %s", esp_err_to_name(err));
    return;
  }

  err = ConfigureChannel();
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "channel config failed: %s", esp_err_to_name(err));
    return;
  }

  for (const ToneStep& step : kBootToneSteps) {
    ESP_LOGI(kTag, "tone %lu Hz for %lu ms", static_cast<unsigned long>(step.freq_hz),
             static_cast<unsigned long>(step.duration_ms));
    if (!StartTone(step.freq_hz)) break;
    vTaskDelay(pdMS_TO_TICKS(step.duration_ms));
    StopTone();
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  StopTone();
  gpio_reset_pin(kBuzzerGpio);
  ESP_LOGI(kTag, "boot buzzer test done");
}
