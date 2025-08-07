file(GLOB_RECURSE SRC_FILES "*.c")


idf_component_register(SRCS ${SRC_FILES}
                    INCLUDE_DIRS "."
                    REQUIRES "lvgl__lvgl" "esp_bsp" "freertos" "spi_flash"  "driver" "esp_hw_support" "espressif__esp_lvgl_port")
