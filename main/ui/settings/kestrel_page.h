#ifndef KESTREL_PAGE_H_
#define KESTREL_PAGE_H_

#include <lvgl.h>
#include "settings_ui.h"

/** @brief The Kestrel page of the settings, and its line on the hub (ADR-008) */
settings_page_t * create_kestrel_settings_page(void);
void kestrel_settings_summary(char * buf, size_t size, void * ctx);

#endif  // KESTREL_PAGE_H_
