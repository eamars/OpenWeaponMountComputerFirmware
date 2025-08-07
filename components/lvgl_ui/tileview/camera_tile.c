
#include "camera_tile.h"

lv_obj_t *cam_ing;


void camera_tile_init(lv_obj_t *parent)
{
    cam_ing = lv_img_create(parent);
}