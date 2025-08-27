#include <lvgl.h>

#include "esp_log.h"

#include "main_tileview.h"
#include "digital_level_view.h"
#include "digital_level_view_controller.h"
#include "send_it_view.h"
#include "config_view.h"
#include "countdown_timer_config_view.h"
#include "dope_config_view.h"
#include "acceleration_analysis_view.h"
#include "bno085.h"
#include "system_config.h"
#include "low_power_mode.h"


#define TAG "MainTileView"


typedef void (*tile_update_enable_cb_t) (bool);


lv_obj_t * main_tileview = NULL;
lv_obj_t * tile_low_power_mode_view = NULL;

lv_obj_t * last_tile = NULL;


lv_obj_t * get_last_tile() {
    return last_tile;
}

void tile_change_callback(lv_event_t * e) {
    // Store the previous tile to determine the switch event
    static lv_obj_t * previous_tile = NULL;

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

        // Record the last tile
        last_tile = previous_tile;
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


static void force_update_tile(lv_timer_t * timer) {
    lv_obj_t * active_tile = lv_tileview_get_tile_active(main_tileview);
    lv_tileview_set_tile(main_tileview, active_tile, LV_ANIM_OFF);

    lv_timer_del(timer);
}

static void main_tile_view_rotation_event_callback(lv_event_t * e) {
    lv_obj_t * tileview = lv_event_get_target(e);
    // Remember current active tile
    lv_obj_t * active_tile = lv_tileview_get_tile_active(tileview);

    // Force reset to the current tile
    if (active_tile) {
        // Some workaround to force the tile to update after the redraw
        lv_timer_create(force_update_tile, 1, NULL);
    }
}


void create_main_tileview(lv_obj_t *parent)
{
    main_tileview = lv_tileview_create(parent);

    // Hide the slider
    lv_obj_set_scrollbar_mode(main_tileview, LV_SCROLLBAR_MODE_OFF);

    // Add callback to the scroll event
    lv_obj_add_event_cb(main_tileview, tile_change_callback, LV_EVENT_VALUE_CHANGED, NULL);

    // Tiles at column 0 are reserved for control purposes
    tile_low_power_mode_view = lv_tileview_add_tile(main_tileview, 0, 0, LV_DIR_NONE);  // The tile can only be entered automatically
    lv_obj_set_user_data(tile_low_power_mode_view, enable_low_power_mode);  // Use enter and exit code to activate and deactivate the low power mode
    create_low_power_mode_view(tile_low_power_mode_view);

    // "Send It" view (most left tile)
    lv_obj_t * tile_send_it_level_view = lv_tileview_add_tile(main_tileview, 1, 1, LV_DIR_RIGHT);  // send it view can only be swiped right
    lv_obj_set_user_data(tile_send_it_level_view, enable_send_it_view);
    create_send_it_view(tile_send_it_level_view);
    lv_obj_add_event_cb(tile_send_it_level_view, send_it_view_rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);

    // Timer config view (swiped up from digital level view)
    lv_obj_t * tile_countdown_timer_config_view = lv_tileview_add_tile(main_tileview, 2, 0, LV_DIR_BOTTOM);
    lv_obj_set_user_data(tile_countdown_timer_config_view, enable_countdown_timer_config_view);
    create_countdown_timer_config_view(tile_countdown_timer_config_view);
    lv_obj_add_event_cb(tile_countdown_timer_config_view, countdown_timer_rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);

    // Digital level view (main tile)
    lv_obj_t * tile_digital_level_view = lv_tileview_add_tile(main_tileview, 2, 1, LV_DIR_ALL);
    lv_obj_set_user_data(tile_digital_level_view, enable_digital_level_view_controller);
    create_digital_level_view(tile_digital_level_view);
    lv_obj_add_event_cb(tile_digital_level_view, digital_level_view_rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);

    // Dope view (swiped down from digital level view)
    lv_obj_t * tile_dope_config_view = lv_tileview_add_tile(main_tileview, 2, 2, LV_DIR_TOP);
    lv_obj_set_user_data(tile_dope_config_view, enable_dope_config_view);
    create_dope_config_view(tile_dope_config_view);
    lv_obj_add_event_cb(tile_dope_config_view, dope_config_view_rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);

    // Configuration view (Swiped right from digital level view)
    lv_obj_t * tile_config_view = lv_tileview_add_tile(main_tileview, 3, 1, LV_DIR_HOR);
    create_config_view(tile_config_view);

    // Acceleration analysis view (swiped right from configuration view)
    lv_obj_t * tile_acceleration_analysis_view = lv_tileview_add_tile(main_tileview, 4, 1, LV_DIR_HOR);
    lv_obj_set_user_data(tile_acceleration_analysis_view, enable_acceleration_analysis_view);
    create_acceleration_analysis_view(tile_acceleration_analysis_view);

    // Switch to the default view
    lv_tileview_set_tile(main_tileview, tile_digital_level_view, LV_ANIM_OFF);
    // lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);
    // lv_tileview_set_tile(main_tileview, tile_low_power_mode_view, LV_ANIM_OFF);
    lv_obj_send_event(main_tileview, LV_EVENT_VALUE_CHANGED, (void *) main_tileview);

    lv_obj_add_event_cb(main_tileview, main_tile_view_rotation_event_callback, LV_EVENT_SIZE_CHANGED, NULL);
}
