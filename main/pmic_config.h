#ifndef PMIC_CONFIG_H_
#define PMIC_CONFIG_H_

#include "lvgl.h"

// The power management settings page and the power menu. They show the PMIC service (pmic_axp2101.h) through
// the UI bridge and ask the service for changes (ADR-005 step 3).

lv_obj_t * create_power_management_view_config(lv_obj_t *parent, lv_obj_t * parent_menu_page);

/** @brief Show the power menu (Reboot, Power Off). LVGL lock held; does nothing before the menu exists. */
void power_menu_make_visible();

#endif  // PMIC_CONFIG_H_
