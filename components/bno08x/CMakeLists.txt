file(GLOB_RECURSE SRC_FILES "*.c")

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "include" "sh2" "src"
    REQUIRES "esp_driver_gpio" "esp_driver_i2c" "esp_timer"
)