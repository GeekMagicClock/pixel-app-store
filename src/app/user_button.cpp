#include "app/user_button.h"

#include "app/display_control.h"
#include "app/app_update_server.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#include "ui/lvgl_lua_app_carousel.h"

static const char *kTag = "app";

// Reuse the original Arduino project's button pin (src_old/lib/btn.cpp).
#ifndef USER_BUTTON_GPIO
#define USER_BUTTON_GPIO 38
#endif
static constexpr int kUserButtonGpio = USER_BUTTON_GPIO; // active-low, INPUT_PULLUP
static bool g_user_button_gpio_inited = false;
static uint64_t UserButtonPinMask() {
#if USER_BUTTON_GPIO < 0 || USER_BUTTON_GPIO >= 64
  return 0;
#else
  if (kUserButtonGpio < 0 || kUserButtonGpio >= 64) {
    return 0;
  }
  return 1ULL << static_cast<unsigned>(kUserButtonGpio);
#endif
}

static void InitUserButtonGpio() {
  if (kUserButtonGpio < 0) {
    return;
  }
  gpio_config_t io_conf{};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = UserButtonPinMask();
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io_conf);
  g_user_button_gpio_inited = true;
}

static void EnsureUserButtonGpioInited() {
  if (g_user_button_gpio_inited) return;
  InitUserButtonGpio();
}

struct ButtonContext {
  MatrixPanel_I2S_DMA *display;
  SemaphoreHandle_t display_mutex;
  ButtonEventFlags flags;

  uint8_t brightness = 64;
  uint8_t saved_brightness = 64;
  bool screen_off = false;
};

static void ToggleScreen(ButtonContext &ctx) {
  if (ctx.screen_off) {
    ctx.brightness = ctx.saved_brightness;
    ctx.screen_off = false;
  } else {
    ctx.saved_brightness = ctx.brightness;
    ctx.brightness = 0;
    ctx.screen_off = true;
  }
  (void)DisplayControlSetBrightness(ctx.brightness);
  ESP_LOGI(kTag, "button: screen %s (brightness=%u)", ctx.screen_off ? "off" : "on", ctx.brightness);
}

static void CycleBrightness(ButtonContext &ctx) {
  if (ctx.screen_off) {
    ctx.brightness = ctx.saved_brightness;
    ctx.screen_off = false;
  } else {
    if (ctx.brightness < 64) ctx.brightness = 64;
    else if (ctx.brightness < 128) ctx.brightness = 128;
    else if (ctx.brightness < 192) ctx.brightness = 192;
    else if (ctx.brightness < 255) ctx.brightness = 255;
    else ctx.brightness = 64;
  }
  (void)DisplayControlSetBrightness(ctx.brightness);
  ESP_LOGI(kTag, "button: brightness=%u", ctx.brightness);
}

static void ButtonTask(void *arg) {
  auto &ctx = *static_cast<ButtonContext *>(arg);
  constexpr int64_t kDebounceUs = 30 * 1000;
  constexpr int64_t kLongPressUs = 1200 * 1000;
  constexpr TickType_t kPollDelay = pdMS_TO_TICKS(10);

  // If the button line is held low at boot (wiring/bootstraps), don't treat it as a "new press"
  // that immediately triggers long-press actions (e.g. turning the screen off).
  const int64_t boot_us = esp_timer_get_time();
  const bool boot_raw_pressed = (gpio_get_level(static_cast<gpio_num_t>(kUserButtonGpio)) == 0);

  bool ignore_until_release = boot_raw_pressed;
  bool stable_pressed = boot_raw_pressed;
  bool last_raw_pressed = boot_raw_pressed;
  int64_t last_raw_change_us = boot_us;
  int64_t press_start_us = 0;
  bool long_handled = false;

  while (true) {
    const int64_t now_us = esp_timer_get_time();
    const bool raw_pressed = (gpio_get_level(static_cast<gpio_num_t>(kUserButtonGpio)) == 0);

    if (raw_pressed != last_raw_pressed) {
      last_raw_pressed = raw_pressed;
      last_raw_change_us = now_us;
    }

    if ((now_us - last_raw_change_us) >= kDebounceUs && raw_pressed != stable_pressed) {
      stable_pressed = raw_pressed;
      if (stable_pressed) {
        if (!ignore_until_release) {
          press_start_us = now_us;
          long_handled = false;
        }
      } else {
        if (ignore_until_release) {
          ignore_until_release = false;
        } else if (!long_handled) {
          // Short press: next screen.
          ESP_LOGI(kTag, "button: click -> next screen");
          AppUpdateServerNotifyManualAppAction();
          LvglLuaAppCarouselRequestNext();
        }
      }
    }

    if (stable_pressed && !ignore_until_release && !long_handled && press_start_us > 0 &&
        (now_us - press_start_us) >= kLongPressUs) {
      (void)ctx;
      long_handled = true;
      ESP_LOGW(kTag, "button: long press -> reboot");
      vTaskDelay(pdMS_TO_TICKS(30));
      esp_restart();
    }

    vTaskDelay(kPollDelay);
  }
}

bool IsUserButtonPressed() {
  if (kUserButtonGpio < 0) return false;
  EnsureUserButtonGpioInited();
  return gpio_get_level(static_cast<gpio_num_t>(kUserButtonGpio)) == 0;
}

void StartUserButtonTask(MatrixPanel_I2S_DMA &display,
                         SemaphoreHandle_t display_mutex,
                         const ButtonEventFlags &flags) {
  if (kUserButtonGpio < 0) {
    ESP_LOGW(kTag, "button disabled (USER_BUTTON_GPIO=%d)", kUserButtonGpio);
    return;
  }

  EnsureUserButtonGpioInited();
  static ButtonContext ctx;
  ctx.display = &display;
  ctx.display_mutex = display_mutex;
  ctx.flags = flags;
  ctx.brightness = DisplayControlGetBrightness();
  ctx.saved_brightness = ctx.brightness;

  ESP_LOGI(kTag, "button enabled gpio=%d mode=active-low short=next-app long=reboot", kUserButtonGpio);
  xTaskCreate(&ButtonTask, "button", 4096, &ctx, tskIDLE_PRIORITY + 1, nullptr);
}
