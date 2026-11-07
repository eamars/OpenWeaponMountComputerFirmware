#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_core_dump.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

#include "app_cfg.h"
#include "crash_report.h"

#define TAG "CrashReport"

#define BT_MAX_DEPTH (sizeof(((esp_core_dump_bt_info_t *) 0)->bt) / sizeof(uint32_t))
#define WDT_USERS_MAGIC 0x55544457  // "WDTU"

// The task watchdog panics from its interrupt, so the core dump names the task it interrupted, not the
// watchdog users that missed their feed. The watchdog's ISR hook saves their names here; RTC memory keeps
// them through the panic reset, and the magic tells a saved list from power-on contents (ADR-005 B20)
typedef struct {
    uint32_t magic;
    uint32_t len;
    char names[96];
} wdt_users_record_t;

static RTC_NOINIT_ATTR wdt_users_record_t wdt_users_record;


static void wdt_users_append(void *opaque, const char *msg) {
    // esp_task_wdt_print_triggered_tasks sends a caption first, then "\n - ", the name and the CPU per user
    int *calls = (int *) opaque;
    if ((*calls)++ == 0) {
        return;
    }
    if (strcmp(msg, "\n - ") == 0) {
        msg = (wdt_users_record.len == 0) ? "" : ", ";
    }

    size_t room = sizeof(wdt_users_record.names) - 1 - wdt_users_record.len;
    size_t n = strnlen(msg, room);
    memcpy(wdt_users_record.names + wdt_users_record.len, msg, n);
    wdt_users_record.len += n;
    wdt_users_record.names[wdt_users_record.len] = '\0';
}


// Called by the task watchdog's ISR after it printed the users that missed their feed, before the panic
void esp_task_wdt_isr_user_handler(void) {
    int calls = 0;
    wdt_users_record.len = 0;
    wdt_users_record.names[0] = '\0';
    esp_task_wdt_print_triggered_tasks(wdt_users_append, &calls, NULL);
    wdt_users_record.magic = WDT_USERS_MAGIC;
}


static const char *reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:       return "UNKNOWN";
        case ESP_RST_POWERON:       return "POWERON";
        case ESP_RST_EXT:           return "EXT";
        case ESP_RST_SW:            return "SW";
        case ESP_RST_PANIC:         return "PANIC";
        case ESP_RST_INT_WDT:       return "INT_WDT";
        case ESP_RST_TASK_WDT:      return "TASK_WDT";
        case ESP_RST_WDT:           return "WDT";
        case ESP_RST_DEEPSLEEP:     return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:      return "BROWNOUT";
        case ESP_RST_SDIO:          return "SDIO";
        case ESP_RST_USB:           return "USB";
        case ESP_RST_JTAG:          return "JTAG";
        case ESP_RST_EFUSE:         return "EFUSE";
        case ESP_RST_PWR_GLITCH:    return "PWR_GLITCH";
        case ESP_RST_CPU_LOCKUP:    return "CPU_LOCKUP";
        default:                    return "OTHER";
    }
}

static bool reset_reason_is_crash(esp_reset_reason_t reason) {
    return reason == ESP_RST_PANIC ||
           reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT ||
           reason == ESP_RST_WDT;
}

static void log_core_dump_summary(void) {
    esp_err_t ret = esp_core_dump_image_check();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No valid core dump in flash: %s", esp_err_to_name(ret));
        return;
    }

    esp_core_dump_summary_t *summary = heap_caps_malloc(sizeof(esp_core_dump_summary_t), HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (summary == NULL) {
        ESP_LOGE(TAG, "No memory for core dump summary");
        return;
    }

    ret = esp_core_dump_get_summary(summary);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read core dump summary: %s", esp_err_to_name(ret));
        heap_caps_free(summary);
        return;
    }

    uint32_t depth = summary->exc_bt_info.depth;
    if (depth > BT_MAX_DEPTH) {
        depth = BT_MAX_DEPTH;
    }

    // " 0x%08x" per entry: 11 characters
    char bt_line[BT_MAX_DEPTH * 11 + 1];
    size_t len = 0;
    bt_line[0] = '\0';
    for (uint32_t i = 0; i < depth; i++) {
        len += snprintf(bt_line + len, sizeof(bt_line) - len, " 0x%08" PRIx32, summary->exc_bt_info.bt[i]);
    }

    ESP_LOGW(TAG, "Stored core dump: task=%.*s pc=0x%08" PRIx32 " depth=%" PRIu32 "%s",
             (int) sizeof(summary->exc_task), summary->exc_task,
             summary->exc_pc,
             summary->exc_bt_info.depth,
             summary->exc_bt_info.corrupted ? " (backtrace corrupted)" : "");
    ESP_LOGW(TAG, "Backtrace:%s", bt_line);

    heap_caps_free(summary);
}

void crash_report_log_last_reset(void) {
    esp_reset_reason_t reason = esp_reset_reason();

    // Read once: a later reset of another kind must not report this list again
    uint32_t saved_magic = wdt_users_record.magic;
    wdt_users_record.magic = 0;

    if (!reset_reason_is_crash(reason)) {
        ESP_LOGI(TAG, "Reset reason: %s (%d)", reset_reason_name(reason), (int) reason);
        return;
    }

    ESP_LOGW(TAG, "Reset reason: %s (%d)", reset_reason_name(reason), (int) reason);
    if (reason == ESP_RST_TASK_WDT && saved_magic == WDT_USERS_MAGIC &&
        wdt_users_record.len < sizeof(wdt_users_record.names)) {
        // The core dump below names the interrupted task, not these
        ESP_LOGW(TAG, "Task watchdog: not fed in time by %s", wdt_users_record.names);
    }
    log_core_dump_summary();
}
