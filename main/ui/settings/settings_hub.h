#ifndef SETTINGS_HUB_H_
#define SETTINGS_HUB_H_

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The settings tile (ADR-008): the hub and its pages, and the power button's menu */
void create_config_view(lv_obj_t * tile);

#ifdef __cplusplus
}
#endif

#endif  // SETTINGS_HUB_H_
