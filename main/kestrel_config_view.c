#include "kestrel_config_view.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "config_view.h"
#include "kestrel_service.h"

#define TAG "KestrelConfigView"
#define KESTREL_PAGE_REFRESH_MS 300

static const char * mode_options = "Off\nHUD (Device mode)\nPhone mode";

static lv_obj_t * kestrel_config_page = NULL;
static lv_obj_t * status_label = NULL;
static uint32_t shown_sequence = UINT32_MAX;


// The status, the last Target Card read (holds as the unit will show them: U up, R right) and the timing
static void show_status(void) {
    if (status_label == NULL) {
        return;
    }
    kestrel_status_t s = kestrel_get_status();
    if (s.sequence == shown_sequence) {
        return;
    }
    shown_sequence = s.sequence;

    static char text[1024];
    size_t n = snprintf(text, sizeof(text), "%s", s.message);

    if (s.have_result) {
        for (int i = 0; i < s.target_count && n < sizeof(text); i++) {
            const kl_target_t * t = &s.targets[i];
            char name[12];
            if (s.numbered) {
                snprintf(name, sizeof(name), "%d", i + 1);
            }
            else {
                snprintf(name, sizeof(name), "%c", 'A' + i);
            }
            float elevation = -t->elevation_mil;
            float wind = -t->wind1_mil;
            float gust = -t->wind2_mil;
            n += snprintf(&text[n], sizeof(text) - n, "\n%s  %lu m  %.1f%s  %s%.1f / %.1f", name,
                          (unsigned long) (t->range_cm / 100), elevation < 0 ? -elevation : elevation,
                          elevation < 0 ? "D" : "U", wind < 0 ? "L" : "R", wind < 0 ? -wind : wind,
                          gust < 0 ? -gust : gust);
        }
    }

    if (s.total_ms > 0 && n < sizeof(text)) {
        n += snprintf(&text[n], sizeof(text) - n, "\nLast pull %lu ms:", (unsigned long) s.total_ms);
        for (int p = 0; p < KESTREL_PHASE_COUNT && n < sizeof(text); p++) {
            n += snprintf(&text[n], sizeof(text) - n, " %s %lu", kestrel_phase_name(p), (unsigned long) s.phase_ms[p]);
        }
        if (n < sizeof(text)) {
            snprintf(&text[n], sizeof(text) - n, ", slowest target %lu", (unsigned long) s.target_max_ms);
        }
    }
    lv_label_set_text(status_label, text);
}


// Phone mode: the Kestrels the last search found, as buttons; rebuilt when a search ends
static lv_obj_t * found_list = NULL;
static uint32_t shown_found_id = UINT32_MAX;


static void found_list_deleted_cb(lv_event_t * e) {
    found_list = NULL;
}


static void found_clicked_cb(lv_event_t * e) {
    int index = (int) (intptr_t) lv_event_get_user_data(e);
    esp_err_t ret = kestrel_choose(index);
    update_info_msg_box_result(msg_box, ret, "Kestrel picked", "Not saved");
    config_page_rebuild(kestrel_config_page);
}


static void show_found(void) {
    if (found_list == NULL) {
        return;
    }
    kestrel_status_t s = kestrel_get_status();
    if (s.found_id == shown_found_id) {
        return;
    }
    shown_found_id = s.found_id;
    lv_obj_clean(found_list);

    // Empty: say whether a search is on, so a press on Find shows at once (owner, 2026-11-07)
    if (s.found_count == 0) {
        if (s.state == KESTREL_PULL_RUNNING) {
            lv_label_set_text(lv_label_create(found_list), "Searching...");
        }
        else if (s.found_id > 0) {
            lv_label_set_text(lv_label_create(found_list), "No Kestrel found");
        }
        return;
    }

    // One button a Kestrel, as heard; the picked one ticked
    for (int i = 0; i < s.found_count; i++) {
        bool picked = memcmp(s.found[i].addr, kestrel_settings.peer_addr, sizeof(kestrel_settings.peer_addr)) == 0 &&
                      kestrel_settings.peer_name[0] != '\0';
        lv_obj_t * button = lv_button_create(found_list);
        lv_obj_set_size(button, lv_pct(100), 60);
        if (picked) {
            lv_obj_set_style_bg_color(button, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
        }
        lv_obj_t * label = lv_label_create(button);
        lv_label_set_text_fmt(label, "%s%s  (%d dBm)", picked ? LV_SYMBOL_OK " " : "", s.found[i].name, s.found[i].rssi);
        lv_obj_center(label);
        lv_obj_add_event_cb(button, found_clicked_cb, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) i);
    }
}


static void refresh_timer_cb(lv_timer_t * timer) {
    show_status();
    show_found();
}


static void status_label_deleted_cb(lv_event_t * e) {
    status_label = NULL;
}


// The page shows what the mode needs, so it is built again when the mode changes
static void mode_changed_cb(lv_event_t * e) {
    uint32_t selected = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    if (selected <= KESTREL_MODE_PHONE && selected != kestrel_settings.mode) {
        kestrel_settings.mode = (uint8_t) selected;
        kestrel_settings_changed();     // a running pull in the old mode ends (owner, 2026-11-07)
        config_page_rebuild(kestrel_config_page);
    }
}


static void search_clicked_cb(lv_event_t * e) {
    kestrel_request_search();   // the status shows the progress; the list fills when it ends
    show_status();
}


static void on_save_button_pressed(lv_event_t * e) {
    update_info_msg_box_result(msg_box, save_kestrel_settings(), "Configuration Saved", "Save failed");
}


static void on_reload_button_pressed(lv_event_t * e) {
    esp_err_t ret = load_kestrel_settings();
    kestrel_settings_changed();
    config_page_rebuild(kestrel_config_page);
    update_info_msg_box_result(msg_box, ret, "Previous Configuration Reloaded", "Reload failed");
}


// The defaults: Kestrel off and no Kestrel picked (the HUD pairing is kept by the Kestrel, not here). As on the
// other pages, Save keeps it and Reload undoes it
static void on_reset_button_pressed(lv_event_t * e) {
    kestrel_settings_reset();
    kestrel_settings_changed();
    config_page_rebuild(kestrel_config_page);
    update_info_msg_box(msg_box, "Configuration reset to default. Use reload button to undo the action");
}


static lv_obj_t * create_text(lv_obj_t * container) {
    lv_obj_t * label = lv_label_create(container);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_add_flag(label, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    return label;
}


static void fill_kestrel_config_page(lv_obj_t * page) {
    lv_obj_t * container;

    container = create_menu_container_with_text(page, NULL, "Kestrel");
    create_dropdown_list(container, mode_options, kestrel_settings.mode, mode_changed_cb, NULL);

    if (kestrel_settings.mode == KESTREL_MODE_HUD) {
        char name[16];
        kestrel_hud_name(name, sizeof(name));
        container = create_menu_container_with_text(page, NULL, "This unit's HUD name");
        lv_label_set_text_fmt(create_text(container), "%s\nPair it once on the Kestrel: Bluetooth > Conct = Device > "
                              "HUD Connect > Name > New", name);
    }
    else if (kestrel_settings.mode == KESTREL_MODE_PHONE) {
        container = create_menu_container_with_text(page, NULL, "Your Kestrel");
        lv_label_set_text_fmt(create_text(container), "%s\nThe Kestrel in its normal Bluetooth mode, the phone app "
                              "closed", kestrel_settings.peer_name[0] ? kestrel_settings.peer_name : "None picked yet");

        container = create_menu_container_with_text(page, NULL, "Find Kestrels nearby");
        create_single_button(container, LV_SYMBOL_REFRESH, search_clicked_cb);

        found_list = lv_obj_create(page);
        lv_obj_set_size(found_list, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(found_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_event_cb(found_list, found_list_deleted_cb, LV_EVENT_DELETE, NULL);
        shown_found_id = UINT32_MAX;
        show_found();
    }

    // Pull is on the dope tile, where its targets go
    container = create_menu_container_with_text(page, NULL, "Status (Pull is on the dope tile)");
    status_label = create_text(container);
    lv_obj_add_event_cb(status_label, status_label_deleted_cb, LV_EVENT_DELETE, NULL);
    shown_sequence = UINT32_MAX;
    show_status();

    container = create_menu_container_with_text(page, NULL, "Save/Reload/Reset");
    create_save_reload_reset_buttons(container, on_save_button_pressed, on_reload_button_pressed, on_reset_button_pressed);
}

lv_obj_t * create_kestrel_view_config(lv_obj_t * parent, lv_obj_t * parent_menu_page) {
    kestrel_config_page = create_config_page(parent, fill_kestrel_config_page);
    lv_timer_create(refresh_timer_cb, KESTREL_PAGE_REFRESH_MS, NULL);

    lv_obj_t * cont = lv_menu_cont_create(parent_menu_page);
    lv_obj_t * img = lv_image_create(cont);
    lv_obj_t * label = lv_label_create(cont);
    lv_image_set_src(img, LV_SYMBOL_SETTINGS);
    lv_label_set_text(label, "Kestrel");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(label, 1);
    lv_menu_set_load_page_event(parent, cont, kestrel_config_page);

    return kestrel_config_page;
}
