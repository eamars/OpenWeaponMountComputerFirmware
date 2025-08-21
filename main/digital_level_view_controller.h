#ifndef DIGITAL_LEVEL_VIEW_CONTROLLER_H
#define DIGITAL_LEVEL_VIEW_CONTROLLER_H

#include <lvgl.h>
#include <stdint.h>
#include "esp_err.h"


esp_err_t digital_level_view_controller_init();

void enable_digital_level_view_controller(bool enable);

#endif // DIGITAL_LEVEL_VIEW_CONTROLLER_H