---
name: c-style
description: Enforce and review this project's C/C++ coding style for ESP-IDF firmware. Use this skill whenever writing, editing, or reviewing .c, .h, .cpp, .hpp, CMakeLists, ESP-IDF component code, LVGL view code, FreeRTOS task/event code, driver wrappers, or embedded configuration code in this repository. Apply it proactively for C/C++ refactors, bug fixes, feature work, and code-review requests; do not wait for the user to ask for style explicitly.
---

# C/C++ Style Guide

This skill captures the C/C++ style used in this repository. Apply it proactively when writing or reviewing firmware code.

The strongest style signal is tracked history: project-owned code is primarily under `main/`, with locally adapted driver code in `components/bno08x` and `components/esp_lcd_touch_ft3168`. Treat `components/XPowersLib` and other third-party code as external unless the user asks to modify it.

The style is direct embedded C with small C++ bridge islands: explicit setup, clear task/event control flow, practical comments, and conservative abstractions.

---

## Rule 1 - Match the local module shape

Keep code organized as small module pairs: `feature.c` / `feature.h`, or `feature.cpp` / `feature.h` when C++ is required to bridge a C++ library. Public APIs are declared in the header and implemented in the source file. File-local helpers, callbacks, and state stay in the source file as `static`.

Prefer the existing module prefixes:

- `wifi_*`, `wifi_config_*`, `wifi_provision_*`
- `create_*_view`, `enable_*_view`, `set_rotation_*`
- `load_*_config`, `save_*_config`
- `*_event_cb`, `on_*_pressed`, `*_task`

**Wrong:**
```c
void update(lv_event_t *e) {
    ...
}
```

**Right:**
```c
static void update_rotation_event_cb(lv_event_t *e) {
    ...
}
```

When reviewing: check whether a new symbol should be public. If not, make it `static` and name it with the module's existing vocabulary.

---

## Rule 2 - Use the project's C naming conventions

Use `snake_case` for functions, variables, fields, files, and callbacks. Use `*_t` typedef names for structs and function pointer types. Use `*_e` for event/control enum typedefs when that matches the surrounding file. Use `ALL_CAPS` for macros and enum values.

Keep ESP-IDF and LVGL naming as-is. Do not wrap library types just to make names prettier.

**Wrong:**
```c
typedef struct {
    int TimeoutMs;
} TimerConfig;

void StartTimer(TimerConfig *config);
```

**Right:**
```c
typedef struct {
    int timeout_ms;
} timer_config_t;

void start_timer(timer_config_t *config);
```

When reviewing: flag CamelCase in project-owned C APIs unless it comes from an external library type or constant.

---

## Rule 3 - Match formatting in the current file

Use four-space indentation. Use K&R braces for most functions and control blocks:

```c
esp_err_t wifi_request_start() {
    if (!wifi_is_expired()) {
        return esp_wifi_start();
    }

    return ESP_ERR_INVALID_STATE;
}
```

Pointer spacing varies in the existing code. Match the surrounding file instead of imposing a repo-wide rewrite. Avoid formatting-only churn outside the edited area.

Keep designated initializers for ESP-IDF/LVGL config structs, and align fields only where the local file already does so.

**Right:**
```c
gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << ctx->interrupt_pin),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE,
};
```

When reviewing: distinguish style consistency from broad cleanup. Prefer focused fixes over reformatting whole files.

---

## Rule 4 - Use ESP-IDF error handling idioms

Functions that can fail should return `esp_err_t` unless the surrounding module uses another explicit contract. Validate arguments early, return `ESP_ERR_INVALID_ARG` for invalid pointers or values, and use ESP-IDF helpers where they keep failure paths clear:

- `ESP_ERROR_CHECK(...)` for initialization paths where failure is fatal.
- `ESP_RETURN_ON_ERROR(...)` when returning from the current function.
- `ESP_GOTO_ON_ERROR(...)` for cleanup paths.
- `ESP_LOGE/W/I/D(TAG, ...)` for useful firmware diagnostics.

**Wrong:**
```c
int init_sensor(sensor_t *ctx) {
    if (ctx == NULL) {
        return -1;
    }
    if (sensor_open(ctx) != 0) {
        printf("failed\n");
        return -1;
    }
    return 0;
}
```

**Right:**
```c
esp_err_t sensor_init(sensor_t *ctx) {
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = sensor_open(ctx);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to open sensor: %s", esp_err_to_name(ret));

    return ESP_OK;
}
```

When reviewing: flag ad-hoc integer error codes, silent failures, and `printf` diagnostics in firmware modules.

---

## Rule 5 - Keep configuration state in the established NVS pattern

Persistent config modules use a consistent pattern:

- `#define TAG "ModuleName"`
- `#define NVS_NAMESPACE "SHORT_KEY"`
- `HEAPS_CAPS_ATTR module_config_t module_config;`
- `const module_config_t module_config_default = { ... };`
- `load_module_config()` delegates to `load_config(...)`
- `save_module_config()` delegates to `save_config(...)`
- menu callbacks update the config object directly, then save/reload/reset through shared UI helpers

**Right:**
```c
#define TAG "SystemConfig"
#define NVS_NAMESPACE "SC"

HEAPS_CAPS_ATTR system_config_t system_config;
const system_config_t system_config_default = {
    .rotation = LV_DISPLAY_ROTATION_180,
    .screen_brightness_normal_pct = SCREEN_BRIGHTNESS_100_PCT,
    .global_log_level = ESP_LOG_INFO,
};

esp_err_t load_system_config() {
    return load_config(NVS_NAMESPACE, &system_config, &system_config_default, sizeof(system_config));
}
```

When writing a new config feature, follow this shape before inventing a separate storage abstraction.

---

## Rule 6 - Build LVGL views imperatively and locally

LVGL UI code is intentionally direct: create the object, set its style/layout, attach callbacks, and return it. Use small helper functions only when they remove repeated UI setup already present in the module.

Prefer existing helpers from `config_view.c` for settings UI:

- `create_menu_container_with_text`
- `create_dropdown_list`
- `create_spin_box`
- `create_colour_picker`
- `create_switch`
- `create_save_reload_reset_buttons`
- `create_info_msg_box` / `update_info_msg_box`

Use LVGL locks when updating UI from tasks:

```c
if (lvgl_port_lock(LVGL_UNLOCK_WAIT_TIME_MS)) {
    update_digital_level_view(display_roll, sensor_pitch_thread_unsafe);
    lvgl_port_unlock();
}
```

When reviewing: flag task-to-LVGL updates that do not lock, and avoid introducing framework-like UI layers that do not match this codebase.

---

## Rule 7 - Treat FreeRTOS tasks, event groups, queues, and timers explicitly

Task code favors visible state machines and explicit event bits. Define event bits with `1 << n`, create event groups lazily only where the current module already does, and report allocation failures.

Task functions use `void *p` or `void *args`, cast to the context at the top, then loop. Creation uses `xTaskCreate(...)` with named stack/priority macros and checks `pdPASS`.

**Right:**
```c
BaseType_t rtos_return = xTaskCreate(
    sensor_poller_task,
    "sensor_poller",
    SENSOR_EVENT_POLLER_TASK_STACK,
    NULL,
    SENSOR_EVENT_POLLER_TASK_PRIORITY,
    &sensor_poller_task_handle
);
if (rtos_return != pdPASS) {
    ESP_LOGE(TAG, "Failed to allocate memory for sensor_poller_task");
    return ESP_FAIL;
}
```

For ISR handlers, keep work minimal and use ISR-safe APIs like `xEventGroupSetBitsFromISR(...)`, then `portYIELD_FROM_ISR(...)` when needed.

When reviewing: flag hidden background behavior, unchecked task creation, and ISR handlers that do non-trivial work.

---

## Rule 8 - Use project memory placement deliberately

This firmware deliberately uses PSRAM for many allocations and static buffers. Prefer:

- `heap_caps_malloc`, `heap_caps_calloc`, `heap_caps_realloc`
- `heap_caps_free`
- `HEAPS_CAPS_ALLOC_DEFAULT_FLAGS`
- `HEAPS_CAPS_ATTR` for static buffers/config objects that should live in external RAM

Use plain `free` only when it matches the allocator used by the surrounding code or library object.

**Wrong:**
```c
uint8_t *buf = malloc(size);
...
free(buf);
```

**Right:**
```c
uint8_t *buf = heap_caps_malloc(size, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
if (!buf) {
    return ESP_ERR_NO_MEM;
}
...
heap_caps_free(buf);
```

When reviewing: flag accidental heap changes that could move large buffers out of PSRAM or mix allocators unsafely.

---

## Rule 9 - Comments explain hardware intent and concurrency assumptions

Comments should explain why a hardware setting, task policy, memory placement, or LVGL lock exists. Keep comments short and practical. Preserve useful `TODO`, `FIXME`, and `NOTE` markers when they describe real incomplete work or constraints.

**Wrong:**
```c
// Set value to true
enabled = true;
```

**Right:**
```c
// Clear event bit prior to enabling the interrupt so the next wait sees a fresh edge.
xEventGroupClearBits(ctx->sensor_event_control, SENSOR_INTERRUPT_EVENT_BIT);
gpio_intr_enable(ctx->interrupt_pin);
```

When reviewing: flag misleading comments and empty narration, but do not demand Doxygen for every internal helper. Public driver headers may use `@brief` style when the surrounding header already does.

---

## Rule 10 - Keep C++ as a bridge, not a second architecture

Use C++ only when needed for C++ libraries such as `XPowersLib`. Keep the public header C-callable with `extern "C"` guards. Keep implementation procedural and consistent with the C modules.

**Right:**
```c
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t axp2101_init(axp2101_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, gpio_num_t interrupt_pin);

#ifdef __cplusplus
}
#endif
```

Avoid introducing classes, templates, exceptions, RTTI-heavy patterns, or STL containers into project firmware unless the surrounding module already depends on them and the user explicitly wants that direction.

When reviewing: flag C++ abstractions that make the firmware harder to inspect or call from C.

---

## Review Workflow

When asked to review C/C++ code:

1. Read the file or diff in full, plus nearby header/source pair when relevant.
2. Prioritize behavioral risks first: task safety, LVGL locking, memory placement, error handling, ISR safety, config persistence, and hardware state.
3. Then list style violations against these rules with file and line references.
4. Suggest the smallest correction that matches the surrounding module.
5. If no issues are found, say so and mention any residual test or hardware-verification gap.

When writing C/C++ code:

Apply these rules before editing. Match the current file's local conventions, keep edits scoped, and prefer existing helpers and patterns over new abstractions.
