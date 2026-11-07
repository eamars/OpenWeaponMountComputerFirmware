#ifndef PMIC_CONFIG_H_
#define PMIC_CONFIG_H_

#include "lvgl.h"
#include "settings_ui.h"

// The power management settings page and the power menu. They show the PMIC service (pmic_axp2101.h) through
// the UI bridge and ask the service for changes (ADR-005 step 3).

/** @brief The Power page of the settings and its line on the hub; Advanced's charging rows and their part
 * of its Reset (ADR-008) */
settings_page_t * create_power_settings_page(void);
void pmic_settings_summary(char * buf, size_t size, void * ctx);
void pmic_settings_add_advanced_rows(settings_page_t * page);
void pmic_settings_reset_advanced(void);

/** @brief The power button's menu (Reboot, Power off), built once at start-up */
void create_power_menu_overlay(void);

/** @brief Show the power menu (Reboot, Power Off). LVGL lock held; does nothing before the menu exists. */
void power_menu_make_visible();

#endif  // PMIC_CONFIG_H_
