#include <lvgl.h>

#include "main_tileview.h"
#include "digital_level_view.h"
#include "send_it_view.h"
#include "config_view.h"
#include "countdown_timer_config_view.h"
#include "dope_config_view.h"
#include "acceleration_analysis_view.h"

#include "bno085.h"

#include "esp_log.h"

#define TAG "main_tileview"


typedef void (*tile_update_enable_cb_t) (bool);


lv_obj_t * main_tileview = NULL;



void tile_change_callback(lv_event_t * e) {
    // Record the previous active view
    static lv_obj_t * previous_tile = NULL;

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_VALUE_CHANGED) {
        // Get active tile
        lv_obj_t * tileview = lv_event_get_target(e);
        lv_obj_t * active_tile = lv_tileview_get_tile_active(tileview);

        ESP_LOGI(TAG, "Active: %p", active_tile);

        if (active_tile != previous_tile) {
            // Disable the callback of the previous view
            if (previous_tile != NULL) {
                tile_update_enable_cb_t tile_update_enable_cb = lv_obj_get_user_data(previous_tile);
                if (tile_update_enable_cb != NULL) {
                    tile_update_enable_cb(false);
                }
            }
            
            ESP_LOGI(TAG, "Active tile: %p", active_tile);

            // Record
            previous_tile = active_tile;

            // Run the callback to enable the current view
            tile_update_enable_cb_t tile_update_enable_cb = lv_obj_get_user_data(active_tile);
            if (tile_update_enable_cb) {
                tile_update_enable_cb(true);
            }
            else {
                ESP_LOGW(TAG, "No enable callback associated with tile %p", active_tile);
            }
        }
    }
}


void create_main_tileview(lv_obj_t *parent)
{
    main_tileview = lv_tileview_create(parent);

    // Hide the slider
    lv_obj_set_scrollbar_mode(main_tileview, LV_SCROLLBAR_MODE_OFF);

    // Add callback to the scroll event
    lv_obj_add_event_cb(main_tileview, tile_change_callback, LV_EVENT_VALUE_CHANGED, NULL);

    // Tile 0, 0: "Send It" view
    lv_obj_t * tile_send_it_level_view = lv_tileview_add_tile(main_tileview, 0, 1, LV_DIR_RIGHT);  // send it view can only be swiped right
    lv_obj_set_user_data(tile_send_it_level_view, enable_send_it_view);
    create_send_it_view(tile_send_it_level_view);

    // Tile 0, 0: Timer config view
    lv_obj_t * tile_countdown_timer_config_view = lv_tileview_add_tile(main_tileview, 1, 0, LV_DIR_BOTTOM);
    create_countdown_timer_config_view(tile_countdown_timer_config_view);

    // Tile 0, 1: Digital Level Tile
    lv_obj_t * tile_digital_level_view = lv_tileview_add_tile(main_tileview, 1, 1, LV_DIR_ALL);
    lv_obj_set_user_data(tile_digital_level_view, enable_digital_level_view);  // store the callback
    create_digital_level_view(tile_digital_level_view);
    lv_obj_add_event_cb(tile_digital_level_view, digital_level_review_rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);

    // Tile 1, 2: Dope config view (it has to be created after the digital level view)
    lv_obj_t * tile_dope_config_view = lv_tileview_add_tile(main_tileview, 1, 2, LV_DIR_TOP);
    create_dope_config_view(tile_dope_config_view);

    // Tile 2, 1
    lv_obj_t * tile_config_view = lv_tileview_add_tile(main_tileview, 2, 1, LV_DIR_HOR);
    create_config_view(tile_config_view);

    // Tile 3, 1
    // lv_obj_t * tile_acceleration_analysis_view = lv_tileview_add_tile(main_tileview, 3, 1, LV_DIR_HOR);
    // lv_obj_set_user_data(tile_acceleration_analysis_view, enable_acceleration_analysis_view);
    // create_acceleration_analysis_view(tile_acceleration_analysis_view);

    // Switch to the default view
    // lv_tileview_set_tile(main_tileview, tile_digital_level_view, LV_ANIM_OFF);
    // lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
    lv_tileview_set_tile(main_tileview, tile_digital_level_view, LV_ANIM_OFF);
    lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
}
