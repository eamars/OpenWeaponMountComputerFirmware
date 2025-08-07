file(GLOB_RECURSE SRC_FILES "*.c")

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "include" "sh2" "src"
    REQUIRES "driver" "esp_timer"
)