#include <inttypes.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "mem_diag.h"

#define TAG "MemDiag"

void mem_diag_log_checkpoint(const char *checkpoint) {
    ESP_LOGI(
        TAG,
        "memopt: %s free_int=%" PRIu32 " largest_int=%" PRIu32 " min_int=%" PRIu32
        " free_dma=%" PRIu32 " largest_dma=%" PRIu32 " min_dma=%" PRIu32
        " free_spiram=%" PRIu32 " largest_spiram=%" PRIu32,
        checkpoint,
        (uint32_t) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (uint32_t) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (uint32_t) heap_caps_get_free_size(MALLOC_CAP_DMA),
        (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        (uint32_t) heap_caps_get_minimum_free_size(MALLOC_CAP_DMA),
        (uint32_t) heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)
    );
}
