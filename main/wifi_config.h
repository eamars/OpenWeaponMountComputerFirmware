#ifndef WIFI_CONFIG_H_
#define WIFI_CONFIG_H_

#include "lvgl.h"

#include "wifi.h"


lv_obj_t * create_wifi_config_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page);

void wifi_config_disable_provision_interface(wireless_state_e reason);
void wifi_config_update_status(const char * state_str);
void wifi_config_disable_enable_interface();

/**
 * @brief Turn Wi-Fi on or off as the Enable WiFi switch does, and update the switch. Call with the LVGL lock held.
 */
void wifi_config_set_enable(bool enable);

#endif  // WIFI_CONFIG_H_