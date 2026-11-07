#include "about_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "app_cfg.h"

#define TAG "AboutConfig"

// The About page (ADR-008): what runs, and what the other app slot holds


static void version_text(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "Version %s", esp_app_get_description()->version);
}


static void build_text(char * buf, size_t size, void * ctx) {
    const esp_app_desc_t * app = esp_app_get_description();
    snprintf(buf, size, "Built %s\nESP-IDF %s", app->date, app->idf_ver);
}


// The running slot, and the other: its app's version, or that it holds none (it showed garbage, read from an empty
// slot without checking the read)
static void slots_text(char * buf, size_t size, void * ctx) {
    const esp_partition_t * running = esp_ota_get_running_partition();
    const esp_partition_t * other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t other_app;
    const char * other_version = "empty";
    if (other != NULL && esp_ota_get_partition_description(other, &other_app) == ESP_OK) {
        other_version = other_app.version;
    }
    snprintf(buf, size, "Running %s\n%s: %s", running != NULL ? running->label : "?",
             other != NULL ? other->label : "Other slot", other_version);
}


void about_settings_summary(char * buf, size_t size, void * ctx) {
    snprintf(buf, size, "%s", esp_app_get_description()->version);
}


settings_page_t * create_about_settings_page(void) {
    settings_page_t * page = settings_page_create("About");
    settings_row_status(page, version_text, NULL);
    settings_row_status(page, build_text, NULL);
    settings_row_status(page, slots_text, NULL);
    return page;
}
