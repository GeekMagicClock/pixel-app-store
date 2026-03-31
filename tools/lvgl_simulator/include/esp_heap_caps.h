#pragma once

#include <stddef.h>

#define MALLOC_CAP_SPIRAM 0x1
#define MALLOC_CAP_INTERNAL 0x2
#define MALLOC_CAP_8BIT 0x4

#ifdef __cplusplus
extern "C" {
#endif

void *heap_caps_malloc(size_t size, unsigned caps);
void *heap_caps_calloc(size_t n, size_t size, unsigned caps);
void heap_caps_free(void *ptr);

#ifdef __cplusplus
}
#endif
