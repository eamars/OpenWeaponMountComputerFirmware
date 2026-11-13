#ifndef LEVEL_CARD_H_
#define LEVEL_CARD_H_

#include <lvgl.h>

// The level's "Dope" foreground (ADR-018): the dope card under the tilt box, with no timer, for shooting without a
// clock. Each set target shows its range (when the Kestrel gave one) or its name, the elevation and the wind: five
// targets as rows in portrait, four side by side in landscape. The current target (dope_view.h) has an outline; a
// tap anywhere on the card moves it to the next target and a hold to the previous, going round the card. The
// targets shown follow the current one; there is no scrolling by finger.
// All calls with the LVGL lock held.

void level_card_create(lv_obj_t * overlay);
void level_card_delete(void);
void level_card_set_rotation(lv_display_rotation_t rotation);

#endif  // LEVEL_CARD_H_
