file(GLOB_RECURSE SRC_FILES "*.c")

set(ALL_DEPS
    freertos
    bno08x
    lvgl
    driver
    esp_lcd
    nvs_flash
    esp_lcd_touch
    esp_lvgl_port
)


if(IDF_TARGET STREQUAL "esp32c6")
    list(APPEND ALL_DEPS "esp_lcd_jd9853" "esp_lcd_touch_axs5106" "nvs_flash")
elseif (IDF_TARGET STREQUAL "esp32s3")
    list(APPEND ALL_DEPS "esp_lcd_co5300" "esp_lcd_touch_ft3168")
endif()

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "."
    REQUIRES ${ALL_DEPS})
