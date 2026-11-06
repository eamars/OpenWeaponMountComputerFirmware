file(GLOB_RECURSE SRC_FILES "*.c" "*.cpp")

set(ALL_DEPS
    # Base requirements
    freertos
    lvgl
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_ledc
    esp_driver_spi
    esp_lcd
    nvs_flash
    esp_lcd_touch
    esp_lvgl_port
    app_update
    esp_timer
    espcoredump
    esp_driver_usb_serial_jtag

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
    network_provisioning
    esp_event
    esp_http_server
    esp_http_client

    # json parser
    json_parser

    # OTA
    esp_https_ota

    # MDNS
    mdns

    # Power management
    XPowersLib
    esp_pm

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
