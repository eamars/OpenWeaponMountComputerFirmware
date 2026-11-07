#ifndef KESTREL_CONFIG_VIEW_H_
#define KESTREL_CONFIG_VIEW_H_

#include <lvgl.h>

/** @brief The Kestrel page in the settings menu (ADR-007): the mode, the HUD name, a Pull and its status */
lv_obj_t * create_kestrel_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page);

#endif  // KESTREL_CONFIG_VIEW_H_
