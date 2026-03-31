#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lv_sdl_keyboard.h"
#include "lv_sdl_mouse.h"
#include "lv_sdl_window.h"
#include "lvgl.h"

#include "ui/lvgl_boot_wifi_screen.h"
#include "ui/lvgl_stock_screen6.h"

static void pump_ui(uint32_t cycles) {
  for (uint32_t i = 0; i < cycles; ++i) {
    (void)lv_timer_handler();
    usleep(5000);
  }
}

static void copy_draw_buf_to_rgba8888(const lv_draw_buf_t *draw_buf, uint32_t width, uint32_t height, uint8_t *rgba_out) {
  const uint32_t stride = draw_buf->header.stride;
  const lv_color_format_t cf = (lv_color_format_t)draw_buf->header.cf;
  const uint8_t *src = (const uint8_t *)draw_buf->data;

  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t *row = src + (size_t)y * stride;
    uint8_t *dst = rgba_out + (size_t)y * width * 4U;
    for (uint32_t x = 0; x < width; ++x) {
      uint8_t r = 0, g = 0, b = 0, a = 0xFF;
      if (cf == LV_COLOR_FORMAT_RGB565) {
        uint16_t px = ((const uint16_t *)row)[x];
        r = (uint8_t)((((px >> 11) & 0x1FU) * 2106U) >> 8);
        g = (uint8_t)((((px >> 5) & 0x3FU) * 1037U) >> 8);
        b = (uint8_t)((((px >> 0) & 0x1FU) * 2106U) >> 8);
      }
      dst[x * 4U + 0U] = r;
      dst[x * 4U + 1U] = g;
      dst[x * 4U + 2U] = b;
      dst[x * 4U + 3U] = a;
    }
  }
}

static void write_le16(FILE *fp, uint16_t value) {
  fputc((int)(value & 0xFFU), fp);
  fputc((int)((value >> 8) & 0xFFU), fp);
}

static void write_le32(FILE *fp, uint32_t value) {
  fputc((int)(value & 0xFFU), fp);
  fputc((int)((value >> 8) & 0xFFU), fp);
  fputc((int)((value >> 16) & 0xFFU), fp);
  fputc((int)((value >> 24) & 0xFFU), fp);
}

static int write_bmp24_file(const char *path, const uint8_t *rgba, uint32_t width, uint32_t height) {
  FILE *fp = fopen(path, "wb");
  if (!fp) return 4;
  const uint32_t row_bytes = ((width * 3U) + 3U) & ~3U;
  const uint32_t pixel_bytes = row_bytes * height;
  const uint32_t file_bytes = 54U + pixel_bytes;

  fputc('B', fp);
  fputc('M', fp);
  write_le32(fp, file_bytes);
  write_le16(fp, 0);
  write_le16(fp, 0);
  write_le32(fp, 54);
  write_le32(fp, 40);
  write_le32(fp, width);
  write_le32(fp, height);
  write_le16(fp, 1);
  write_le16(fp, 24);
  write_le32(fp, 0);
  write_le32(fp, pixel_bytes);
  write_le32(fp, 2835);
  write_le32(fp, 2835);
  write_le32(fp, 0);
  write_le32(fp, 0);

  uint8_t pad[3] = {0, 0, 0};
  const uint32_t pad_len = row_bytes - width * 3U;
  for (int32_t y = (int32_t)height - 1; y >= 0; --y) {
    const uint8_t *row = rgba + (size_t)y * width * 4U;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t *px = row + x * 4U;
      fputc(px[2], fp);
      fputc(px[1], fp);
      fputc(px[0], fp);
    }
    if (pad_len > 0U) fwrite(pad, 1, pad_len, fp);
  }
  fclose(fp);
  return 0;
}

static int export_active_screen_bmp(const char *path) {
  lv_obj_t *screen = lv_screen_active();
  if (!screen || !path || path[0] == '\0') return 1;

  lv_obj_invalidate(screen);
  lv_refr_now(NULL);
  lv_draw_buf_t *draw_buf = lv_display_get_buf_active(NULL);
  if (!draw_buf) return 2;

  uint32_t width = lv_display_get_horizontal_resolution(lv_display_get_default());
  uint32_t height = lv_display_get_vertical_resolution(lv_display_get_default());
  uint8_t *rgba = (uint8_t *)malloc((size_t)width * height * 4U);
  if (!rgba) return 3;
  copy_draw_buf_to_rgba8888(draw_buf, width, height, rgba);
  int rc = write_bmp24_file(path, rgba, width, height);
  free(rgba);
  return rc;
}

int main(void) {
  lv_init();

  lv_display_t *disp = lv_sdl_window_create(64, 32);
  lv_display_set_default(disp);
  const char *zoom_env = getenv("LVGL_SIM_ZOOM");
  float zoom = 8.0f;
  if (zoom_env && zoom_env[0] != '\0') {
    zoom = strtof(zoom_env, NULL);
    if (zoom < 1.0f) zoom = 1.0f;
  }
  lv_sdl_window_set_zoom(disp, zoom);
  lv_sdl_window_set_title(disp, "Pixel LVGL Simulator");
  lv_sdl_window_set_resizeable(disp, false);
  (void)lv_sdl_mouse_create();
  (void)lv_sdl_keyboard_create();

  LvglBootWifiScreenPreloadFont();
  const char *screen = getenv("LVGL_SIM_SCREEN");
  if (screen && strcmp(screen, "stock6") == 0) {
    LvglStockScreen6PreloadFont();
    LvglShowStockScreen6();
  } else if (screen && strcmp(screen, "boot_success") == 0) {
    LvglShowBootWifiSuccess("studio", "192.168.3.152");
  } else if (screen && strcmp(screen, "boot_failed") == 0) {
    LvglShowBootWifiFailed("Pixel-Setup", "192.168.4.1");
  } else {
    LvglShowBootWifiConnecting("studio-network", 20000);
  }

  pump_ui(30);

  const char *export_path = getenv("LVGL_SIM_EXPORT");
  if (export_path && export_path[0] != '\0') {
    return export_active_screen_bmp(export_path);
  }

  while (1) {
    (void)lv_timer_handler();
    usleep(5000);
  }
  return 0;
}
