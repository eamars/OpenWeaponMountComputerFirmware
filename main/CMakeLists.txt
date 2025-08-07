file(GLOB_RECURSE SRC_FILES "*.c")

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "."
    REQUIRES "esp_bsp" "freertos" "bno08x" "lvgl")
