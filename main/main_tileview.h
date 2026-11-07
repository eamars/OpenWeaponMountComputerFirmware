#ifndef MAIN_TILEVIEW_H
#define MAIN_TILEVIEW_H

#include "lvgl.h"

typedef void (*tile_update_enable_cb_t) (bool);

void create_main_tileview(lv_obj_t *parent);

// Shared state, defined in main_tileview.c
extern lv_obj_t * main_tileview;
extern lv_obj_t * default_tile;
extern lv_obj_t * tile_sensor_calibration_view;
extern lv_obj_t * tile_ota_mode_view;
extern lv_obj_t * tile_low_power_mode_view;

#endif // MAIN_TILEVIEW_H