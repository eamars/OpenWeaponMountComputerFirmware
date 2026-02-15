#ifndef MAIN_TILEVIEW_H
#define MAIN_TILEVIEW_H

#include "lvgl.h"

typedef void (*tile_update_enable_cb_t) (bool);

void create_main_tileview(lv_obj_t *parent);

#endif // MAIN_TILEVIEW_H