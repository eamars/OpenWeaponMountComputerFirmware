#ifndef POINT_OF_AIM_VIEW_H_
#define POINT_OF_AIM_VIEW_H_

#include "lvgl.h"

#define POI_EVENT_POLLER_TASK_STACK 4096
#define POI_EVENT_POLLER_TASK_PRIORITY 4


void enable_point_of_aim_view(bool enable);
void create_point_of_aim_view(lv_obj_t * parent);


#endif  // POINT_OF_AIM_VIEW_H_