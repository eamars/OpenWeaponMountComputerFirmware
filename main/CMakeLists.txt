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

    # Kestrel pull over BLE (ADR-007)
    bt
    kestrel_link

)



idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "."
    REQUIRES ${ALL_DEPS})
