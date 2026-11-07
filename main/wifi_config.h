#ifndef WIFI_CONFIG_H_
#define WIFI_CONFIG_H_

#include "lvgl.h"

#include "wifi.h"


/** @brief The Wi-Fi settings page; needs ui_bridge_init() first, not the Wi-Fi service. */
lv_obj_t * create_wifi_config_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page);

#endif  // WIFI_CONFIG_H_
