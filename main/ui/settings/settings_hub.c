#include "settings_hub.h"

#include <stdio.h>

#include "app_cfg.h"
#include "digital_level_view.h"
#include "system_config.h"
#include "sensor_config.h"
#include "about_page.h"
#include "wifi_page.h"
#include "ota_mode.h"
#include "power_page.h"
#include "timer_view.h"
#include "dope_view.h"
#include "kestrel_page.h"
#include "settings_ui.h"


/*
----------------------------------------------------
The settings tile (ADR-008): the hub, a page a row, each row saying what its page holds
----------------------------------------------------
*/

static void advanced_summary(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "Log, sensor, charging");
}


// Advanced: what a shooter does not change at a match. Its rows come from the modules that own the settings
static void reset_advanced(void * ctx) {
    system_settings_reset_advanced();
#if USE_BNO085
    sensor_settings_reset_advanced();
#endif  // USE_BNO085
#if USE_PMIC
    pmic_settings_reset_advanced();
#endif  // USE_PMIC
}


static settings_page_t * create_advanced_settings_page(void) {
    settings_page_t * page = settings_page_create("Advanced");
    system_settings_add_advanced_rows(page);
#if USE_BNO085
    sensor_settings_add_advanced_rows(page);
#endif  // USE_BNO085
#if USE_PMIC
    pmic_settings_add_advanced_rows(page);
#endif  // USE_PMIC
    settings_row_reset(page, "Reset Advanced", "Reset the Advanced settings? Remote control turns back on.",
                       reset_advanced, NULL);
    return page;
}


void create_config_view(lv_obj_t * tile) {
    settings_page_t * hub = settings_ui_create(tile);

    create_settings_update_row(hub);    // hidden until a newer firmware is found
#if USE_BNO085
    settings_row_link(hub, LV_SYMBOL_BELL, "Timer", countdown_timer_settings_summary, NULL,
                      create_countdown_timer_settings_page());
    settings_row_link(hub, LV_SYMBOL_LIST, "Dope", dope_settings_summary, NULL, create_dope_settings_page());
    settings_row_link(hub, LV_SYMBOL_BLUETOOTH, "Kestrel", kestrel_settings_summary, NULL,
                      create_kestrel_settings_page());
    settings_row_link(hub, LV_SYMBOL_GPS, "Level", digital_level_settings_summary, NULL,
                      create_digital_level_settings_page());
#endif  // USE_BNO085
    settings_row_link(hub, LV_SYMBOL_IMAGE, "Display", system_display_settings_summary, NULL,
                      create_display_settings_page());
    settings_row_link(hub, LV_SYMBOL_WIFI, "Wi-Fi", wifi_settings_summary, NULL, create_wifi_settings_page());
#if USE_PMIC
    settings_row_link(hub, LV_SYMBOL_BATTERY_FULL, "Power", pmic_settings_summary, NULL, create_power_settings_page());
#endif  // USE_PMIC
    settings_row_link(hub, LV_SYMBOL_SETTINGS, "Advanced", advanced_summary, NULL, create_advanced_settings_page());
    settings_row_link(hub, LV_SYMBOL_FILE, "About", about_settings_summary, NULL, create_about_settings_page());

#if USE_PMIC
    create_power_menu_overlay();
#endif  // USE_PMIC

    settings_ui_start();
}
