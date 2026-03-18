#include "ui/lvgl_mem_utils.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace {

static const char *kTag = "lvgl_mem";

}  // namespace

void *LvglAllocPreferPsram(size_t size_bytes) {
  if (size_bytes == 0) return nullptr;

  void *ptr = heap_caps_malloc(size_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr) {
    ESP_LOGI(kTag, "alloc %u bytes in PSRAM", static_cast<unsigned>(size_bytes));
    return ptr;
  }

  ptr = heap_caps_malloc(size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (ptr) {
    ESP_LOGW(kTag, "PSRAM alloc failed, fallback to internal RAM for %u bytes",
             static_cast<unsigned>(size_bytes));
  } else {
    ESP_LOGE(kTag, "alloc failed for %u bytes", static_cast<unsigned>(size_bytes));
  }
  return ptr;
}

void LvglFreePreferPsram(void *ptr) {
  if (!ptr) return;
  heap_caps_free(ptr);
}

lv_draw_buf_t *LvglCreateDrawBufferPreferPsram(uint32_t width,
                                               uint32_t height,
                                               lv_color_format_t color_format,
                                               uint32_t stride) {
  lv_draw_buf_t *buf = static_cast<lv_draw_buf_t *>(heap_caps_calloc(1, sizeof(lv_draw_buf_t),
                                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!buf) {
    ESP_LOGE(kTag, "draw buf struct alloc failed (%ux%u cf=%d)",
             static_cast<unsigned>(width),
             static_cast<unsigned>(height),
             static_cast<int>(color_format));
    return nullptr;
  }

  const uint32_t resolved_stride = (stride == LV_STRIDE_AUTO || stride == 0)
                                       ? lv_draw_buf_width_to_stride(width, color_format)
                                       : stride;
  const size_t data_size = static_cast<size_t>(resolved_stride) * height;
  void *data = LvglAllocPreferPsram(data_size);
  if (!data) {
    heap_caps_free(buf);
    return nullptr;
  }

  lv_result_t ok = lv_draw_buf_init(buf, width, height, color_format, resolved_stride, data, data_size);
  if (ok != LV_RESULT_OK) {
    ESP_LOGE(kTag, "lv_draw_buf_init failed");
    LvglFreePreferPsram(data);
    heap_caps_free(buf);
    return nullptr;
  }

  lv_draw_buf_set_flag(buf, LV_IMAGE_FLAGS_MODIFIABLE);
  return buf;
}

void LvglDestroyDrawBufferPreferPsram(lv_draw_buf_t *buf) {
  if (!buf) return;
  void *data = buf->unaligned_data ? buf->unaligned_data : buf->data;
  if (data) LvglFreePreferPsram(data);
  buf->data = nullptr;
  buf->unaligned_data = nullptr;
  heap_caps_free(buf);
}