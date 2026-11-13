#ifndef MAIN_TILEVIEW_H
#define MAIN_TILEVIEW_H

#include "lvgl.h"

typedef void (*tile_update_enable_cb_t) (bool);

void create_main_tileview(lv_obj_t *parent);

/**
 * @brief Show a tile at once and run the tile change (the old tile's disable, the new one's enable callback):
 * lv_tileview_set_tile() alone does not send LV_EVENT_VALUE_CHANGED. Returns the tile that was active, for the way
 * back. Call with the LVGL lock held.
 */
lv_obj_t * main_tileview_jump_to(lv_obj_t * tile);

/**
 * @brief The directions a tile can be swiped away in, changed after the tile was added: a neighbour that has no use
 * in the unit's present state is not offered (ADR-017). Call with the LVGL lock held.
 */
void main_tileview_set_tile_dir(lv_obj_t * tile, lv_dir_t dir);

// Shared state, defined in main_tileview.c
extern lv_obj_t * main_tileview;
extern lv_obj_t * default_tile;
extern lv_obj_t * tile_sensor_calibration_view;
extern lv_obj_t * tile_ota_mode_view;

#endif // MAIN_TILEVIEW_H