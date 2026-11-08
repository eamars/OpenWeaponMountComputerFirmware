#ifndef ACCELERATION_ANALYSIS_VIEW_H
#define ACCELERATION_ANALYSIS_VIEW_H

#include <lvgl.h>
#include <stdint.h>

typedef struct {
    uint32_t crc32;
    float recoil_accel_threshold;
} acceleration_record_config_t;


void create_acceleration_analysis_view(lv_obj_t *parent);
void enable_acceleration_analysis_view(bool enable);

#endif // ACCELERATION_ANALYSIS_VIEW_H