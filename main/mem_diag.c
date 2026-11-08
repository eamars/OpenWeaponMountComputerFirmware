#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_attr.h"
#include "esp_debug_helpers.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_rom_sys.h"

#include "app_cfg.h"
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

// Runs in any context, including ISRs and critical sections: no allocation, ROM printing only
static void IRAM_ATTR mem_diag_alloc_failed_hook(size_t size, uint32_t caps, const char *function_name) {
    esp_rom_printf(DRAM_STR("alloc_failed: size=%u caps=0x%08x func=%s\n"),
                   (unsigned) size, (unsigned) caps, function_name);
    esp_backtrace_print(8);
}

void mem_diag_install_alloc_failure_hook(void) {
    esp_err_t ret = heap_caps_register_failed_alloc_callback(mem_diag_alloc_failed_hook);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register alloc failure hook: %s", esp_err_to_name(ret));
    }
}

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
static void mem_diag_stack_report_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(300000));

        UBaseType_t capacity = uxTaskGetNumberOfTasks() + 4;
        TaskStatus_t *tasks = heap_caps_malloc(capacity * sizeof(TaskStatus_t), HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
        if (tasks == NULL) {
            ESP_LOGE(TAG, "stack_rpt: no memory for %u tasks", (unsigned) capacity);
            continue;
        }

        UBaseType_t count = uxTaskGetSystemState(tasks, capacity, NULL);
        ESP_LOGI(TAG, "stack_rpt: begin count=%u", (unsigned) count);
        for (UBaseType_t i = 0; i < count; i++) {
            ESP_LOGI(TAG, "stack_rpt: %s %s min_free=%u",
                     tasks[i].pcTaskName,
                     esp_ptr_external_ram(tasks[i].pxStackBase) ? "PSRAM" : "INTERNAL",
                     (unsigned) tasks[i].usStackHighWaterMark);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        ESP_LOGI(TAG, "stack_rpt: end");

        heap_caps_free(tasks);
    }
}
#endif  // CONFIG_FREERTOS_USE_TRACE_FACILITY

void mem_diag_start_stack_report(void) {
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    BaseType_t ret = xTaskCreateWithCaps(mem_diag_stack_report_task, "mem_stack_rpt", 4096, NULL, 1, NULL, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stack report task");
    }
#endif  // CONFIG_FREERTOS_USE_TRACE_FACILITY
}


bool mem_diag_cpu_report(uint32_t window_ms) {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    UBaseType_t capacity = uxTaskGetNumberOfTasks() + 8;
    TaskStatus_t *before = heap_caps_malloc(2 * capacity * sizeof(TaskStatus_t), HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (before == NULL) {
        return false;
    }
    TaskStatus_t *after = before + capacity;

    configRUN_TIME_COUNTER_TYPE total_before = 0, total_after = 0;
    UBaseType_t n_before = uxTaskGetSystemState(before, capacity, &total_before);
    vTaskDelay(pdMS_TO_TICKS(window_ms));
    UBaseType_t n_after = uxTaskGetSystemState(after, capacity, &total_after);

    // The total is the run-time clock, so a task's share is of one core; both cores together make 200%
    uint64_t elapsed = (uint64_t) (total_after - total_before);
    ESP_LOGI(TAG, "cpu_rpt: begin window_ms=%" PRIu32 " tasks=%u", window_ms, (unsigned) n_after);
    for (UBaseType_t i = 0; i < n_after && elapsed > 0; i++) {
        configRUN_TIME_COUNTER_TYPE start = after[i].ulRunTimeCounter;  // a task created in the window: all of it
        for (UBaseType_t j = 0; j < n_before; j++) {
            if (before[j].xHandle == after[i].xHandle) {
                start = before[j].ulRunTimeCounter;
                break;
            }
        }
        uint64_t used = (uint64_t) (after[i].ulRunTimeCounter - start);
        uint32_t permille = (uint32_t) ((used * 1000) / elapsed);
        if (used > 0) {
            ESP_LOGI(TAG, "cpu_rpt: %s %" PRIu32 ".%" PRIu32 "%%", after[i].pcTaskName, permille / 10, permille % 10);
        }
    }
    ESP_LOGI(TAG, "cpu_rpt: end");

    heap_caps_free(before);
    return true;
#else
    (void) window_ms;
    return false;
#endif  // CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
}
