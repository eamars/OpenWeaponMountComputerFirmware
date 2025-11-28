file(GLOB_RECURSE SRC_FILES "*.c" "*.cpp")

set(ALL_DEPS
    # Base requirements
    freertos
    lvgl
    driver
    esp_lcd
    nvs_flash
    esp_lcd_touch
    esp_lvgl_port
    app_update
    esp_timer

    # Sensor
    bno08x

    # 1.47" LCD display
    esp_lcd_jd9853
    esp_lcd_touch_axs5106

    # 1.64" AMOLED display
    esp_lcd_sh8601
    esp_lcd_touch_ft3168

    # Wifi part
    esp_wifi
    wifi_provisioning
    esp_event
    esp_http_server
    esp_http_client

    # json parser
    json_parser

    # OTA
    esp_https_ota

    # MDNS
    mdns

    # PMIC
    XPowersLib

    # USB driver
    esp_tinyusb
)


# if(IDF_TARGET STREQUAL "esp32c6")
#     list(APPEND ALL_DEPS "esp_lcd_jd9853" "esp_lcd_touch_axs5106")
# elseif (IDF_TARGET STREQUAL "esp32s3")
#     list(APPEND ALL_DEPS "esp_lcd_co5300" "esp_lcd_touch_ft3168")
# endif()

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "."
    REQUIRES ${ALL_DEPS})
