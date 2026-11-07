#ifndef ABOUT_CONFIG_H_
#define ABOUT_CONFIG_H_

#include <stdint.h>
#include "lvgl.h"
#include "settings_ui.h"

/** @brief The About page of the settings, and its line on the hub (ADR-008) */
settings_page_t * create_about_settings_page(void);
void about_settings_summary(char * buf, size_t size, void * ctx);

#endif  // ABOUT_CONFIG_H_
