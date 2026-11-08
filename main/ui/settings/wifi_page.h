#ifndef WIFI_PAGE_H_
#define WIFI_PAGE_H_

#include "lvgl.h"

#include "wifi.h"
#include "settings_ui.h"


/** @brief The Wi-Fi page of the settings (ADR-008); needs ui_bridge_init() first, not the Wi-Fi service */
settings_page_t * create_wifi_settings_page(void);
/** @brief One line for the hub: the Wi-Fi state */
void wifi_settings_summary(char * buf, size_t size, void * ctx);

#endif  // WIFI_PAGE_H_
