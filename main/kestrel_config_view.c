#include "kestrel_config_view.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#include "kestrel_service.h"
#include "settings_ui.h"

#define TAG "KestrelConfigView"
#define KESTREL_PAGE_REFRESH_MS 300

// The Kestrel page (ADR-008): the mode, and what the mode needs. HUD mode: this unit's HUD name and how to pair it.
// Phone mode: the picked Kestrel, and a search listing the Kestrels nearby to pick from. Each change is stored when
// made; a change cancels a running pull (kestrel_settings_changed)

static const char * const mode_names[] = {"Off", "HUD (Device mode)", "Phone mode"};

static settings_page_t * kestrel_page = NULL;
static settings_page_t * pairing_page = NULL;
static settings_page_t * nearby_page = NULL;
static uint32_t shown_found_id = UINT32_MAX;
static uint8_t built_mode = UINT8_MAX;      // the mode the page's rows were built for


static void store_kestrel_settings(void) {
    kestrel_settings_changed();
    ESP_ERROR_CHECK_WITHOUT_ABORT(save_kestrel_settings());
}


static void build_kestrel_page(void);


// The rows follow the mode; rebuilt once the current event is over (the change comes from a row)
static void rebuild_async(void * p) {
    build_kestrel_page();
}


static int get_mode(void * ctx) {
    return kestrel_settings.mode;
}


static void set_mode(int index, void * ctx) {
    kestrel_settings.mode = (uint8_t) index;
    store_kestrel_settings();
    lv_async_call(rebuild_async, NULL);
}


static void status_text(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "%s", kestrel_get_status().message);
}


static void hud_name_text(char * buf, size_t size, void * ctx) {
    char name[16];
    kestrel_hud_name(name, sizeof(name));
    snprintf(buf, size, "This unit's HUD name\n%s", name);
}


static void picked_text(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "Your Kestrel\n%s", kestrel_settings.peer_name[0] ? kestrel_settings.peer_name :
             "None picked yet");
}


/*
----------------------------------------------------
Phone mode: the Kestrels nearby, filled in as the search hears them
----------------------------------------------------
*/

static void pick_found(void * ctx) {
    int index = (int) (intptr_t) ctx;
    ESP_ERROR_CHECK_WITHOUT_ABORT(kestrel_choose(index));   // stores it; a pull for another Kestrel ends
    shown_found_id = UINT32_MAX;                            // shows the pick
}


static void found_text(char * buf, size_t size, void * ctx) {
    int index = (int) (intptr_t) ctx;
    kestrel_status_t s = kestrel_get_status();
    if (index >= s.found_count) {
        buf[0] = '\0';
        return;
    }
    bool picked = memcmp(s.found[index].addr, kestrel_settings.peer_addr, sizeof(kestrel_settings.peer_addr)) == 0 &&
                  kestrel_settings.peer_name[0] != '\0';
    snprintf(buf, size, "%s%d dBm", picked ? "Picked, " : "", s.found[index].rssi);
}


// LVGL timer: the list follows the search (each Kestrel heard adds a row); the names are kept for the rows
static char found_names[KESTREL_FOUND_MAX][KESTREL_NAME_SIZE];

static void nearby_timer_cb(lv_timer_t * timer) {
    if (nearby_page == NULL) {
        return;
    }
    // The page follows the mode wherever it changed (Reset, the remote control): rebuilt when it is not under a
    // list of its own rows (the mode's choice list points at its row)
    if (kestrel_settings.mode != built_mode &&
        (!settings_ui_is_open(kestrel_page) || settings_ui_is_top(kestrel_page))) {
        build_kestrel_page();
    }
    kestrel_status_t s = kestrel_get_status();
    if (s.found_id == shown_found_id) {
        return;
    }
    shown_found_id = s.found_id;

    settings_page_clear(nearby_page);
    settings_row_status(nearby_page, status_text, NULL);
    for (int i = 0; i < s.found_count && i < KESTREL_FOUND_MAX; i++) {
        snprintf(found_names[i], sizeof(found_names[i]), "%s", s.found[i].name);
        settings_row_action(nearby_page, NULL, found_names[i], found_text, (void *) (intptr_t) i, pick_found,
                            NULL, NULL, false);
    }
    settings_page_relayout(nearby_page);
}


static void find_kestrels(void * ctx) {
    kestrel_request_search();       // the status row shows the progress; the list fills as Kestrels are heard
    shown_found_id = UINT32_MAX;
    settings_ui_open(nearby_page);
}


static void search_text(char * buf, size_t size, void * ctx) {
    kestrel_status_t s = kestrel_get_status();
    if (s.found_count > 0) {
        snprintf(buf, size, "%d found last time", s.found_count);
    }
    else {
        buf[0] = '\0';
    }
}


/*
----------------------------------------------------
The page
----------------------------------------------------
*/

static void reset_kestrel(void * ctx) {
    kestrel_settings_reset();
    store_kestrel_settings();
    lv_async_call(rebuild_async, NULL);
}


static void build_kestrel_page(void) {
    built_mode = kestrel_settings.mode;
    settings_page_clear(kestrel_page);
    settings_row_choice(kestrel_page, "Kestrel", mode_names, 3, get_mode, set_mode, NULL);

    if (kestrel_settings.mode == KESTREL_MODE_HUD) {
        settings_row_status(kestrel_page, hud_name_text, NULL);
        settings_row_link(kestrel_page, NULL, "How to pair", NULL, NULL, pairing_page);
    }
    else if (kestrel_settings.mode == KESTREL_MODE_PHONE) {
        settings_row_status(kestrel_page, picked_text, NULL);
        settings_row_action(kestrel_page, LV_SYMBOL_REFRESH, "Find Kestrels nearby", search_text, NULL, find_kestrels,
                            NULL, NULL, false);
    }
    // Pull is on the dope tile, where its targets go; its outcome shows here too
    if (kestrel_settings.mode != KESTREL_MODE_OFF) {
        settings_row_status(kestrel_page, status_text, NULL);
    }

    settings_row_reset(kestrel_page, "Reset Kestrel", "Reset Kestrel? It turns off and forgets the picked Kestrel.",
                       reset_kestrel, NULL);
    settings_page_relayout(kestrel_page);
}


void kestrel_settings_summary(char * buf, size_t size, void * ctx) {
    if (kestrel_settings.mode == KESTREL_MODE_HUD) {
        kestrel_hud_name(buf, size);    // the HUD name says HUD mode
    }
    else if (kestrel_settings.mode == KESTREL_MODE_PHONE) {
        snprintf(buf, size, "%s", kestrel_settings.peer_name[0] ? kestrel_settings.peer_name : "Phone, none picked");
    }
    else {
        snprintf(buf, size, "Off");
    }
}


settings_page_t * create_kestrel_settings_page(void) {
    // How to pair, as a page of text
    pairing_page = settings_page_create("How to pair");
    lv_obj_t * text = lv_label_create(settings_page_body(pairing_page));
    lv_obj_set_width(text, lv_pct(100));
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(text, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(text, "Once, on the Kestrel:\nBluetooth > Conct = Device > HUD Connect > Name > New, "
                      "then pick this unit's HUD name.");

    nearby_page = settings_page_create("Kestrels nearby");
    lv_timer_create(nearby_timer_cb, KESTREL_PAGE_REFRESH_MS, NULL);

    kestrel_page = settings_page_create("Kestrel");
    build_kestrel_page();
    return kestrel_page;
}
