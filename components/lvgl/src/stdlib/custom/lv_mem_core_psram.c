/**
 * @file lv_mem_core_psram.c
 */

/*********************
 *      INCLUDES
 *********************/
#include "../lv_mem.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include "esp_heap_caps.h"
#include "esp_log.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

static const char *kTag = "lvgl_mem";

void lv_mem_init(void)
{
    return; /*Nothing to init*/
}

void lv_mem_deinit(void)
{
    return; /*Nothing to deinit*/
}

lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes)
{
    /*Not supported in PSRAM-only mode. */
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    /*Not supported in PSRAM-only mode. */
    LV_UNUSED(pool);
}

void * lv_malloc_core(size_t size)
{
    if(size == 0) return NULL;
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void * lv_realloc_core(void * p, size_t new_size)
{
    if(p == NULL) return lv_malloc_core(new_size);
    if(new_size == 0) {
        lv_free_core(p);
        return NULL;
    }

    void * next = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(next == NULL) {
        ESP_LOGW(kTag, "PSRAM realloc failed (%p -> %u bytes)", p, (unsigned)new_size);
    }
    return next;
}

void lv_free_core(void * p)
{
    if(p == NULL) return;
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    if(mon_p == NULL) return;

    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t total_size = heap_caps_get_total_size(caps);
    const size_t free_size = heap_caps_get_free_size(caps);
    const size_t min_free_size = heap_caps_get_minimum_free_size(caps);
    const size_t biggest_free = heap_caps_get_largest_free_block(caps);

    lv_memzero(mon_p, sizeof(*mon_p));
    mon_p->total_size = total_size;
    mon_p->free_size = free_size;
    mon_p->free_biggest_size = biggest_free;
    mon_p->used_cnt = 0;
    mon_p->free_cnt = 0;
    mon_p->max_used = total_size > min_free_size ? total_size - min_free_size : 0;
    if(total_size > 0) {
        mon_p->used_pct = (uint8_t)(100U - ((uint64_t)100U * free_size / total_size));
        if(free_size > 0) {
            mon_p->frag_pct = (uint8_t)(100U - ((uint64_t)100U * biggest_free / free_size));
        }
    }
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity_all(true) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /*LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM*/
