#ifndef DOPE_CONFIG_VIEW_H_
#define DOPE_CONFIG_VIEW_H_

#include <lvgl.h>
 
#define DOPE_CONFIG_MAX_DOPE_ITEM 12

void create_dope_config_view(lv_obj_t * parent);
lv_obj_t * create_dope_card_list_widget(lv_obj_t * parent);

#endif // DOPE_CONFIG_VIEW_H_