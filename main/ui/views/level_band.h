#ifndef LEVEL_BAND_H_
#define LEVEL_BAND_H_

#include <stdbool.h>
#include <lvgl.h>

#include "countdown_timer.h"

// The band at the bottom of the level view (ADR-007): the timer bar and time, and the current dope target, on a
// light dark overlay through which the level's colour shows. Shown while there is a dope target or the timer is
// armed; the level alone otherwise. Tap the band: next target; hold: previous, both going round the card (after the
// last, the first); the current target is the card's (dope_view.h, ADR-018). Tap the time or the bar: start,
// pause or continue the timer (also the hardware button); hold: re-arm with the full time.
// All calls with the LVGL lock held, unless said otherwise.

void level_band_create(lv_obj_t * overlay, countdown_timer_t * timer);
/**
 * @brief Delete the band (the level's foreground is another one, ADR-017). Every other call here is safe without a
 * band and shows nothing; the timer cannot be shown until the band is created again
 */
void level_band_delete(void);
void level_band_set_rotation(lv_display_rotation_t rotation);


/** @brief The countdown timer's listener (countdown_timer_setup). Any task; takes the LVGL lock */
void level_band_timer_update_cb(void * arg, int time_left_ms);
/** @brief Show or hide the timer on the level view (armed by a preset, hidden when disarmed). Any task */
void enable_countdown_timer_widget(bool enable);
bool get_countdown_timer_widget_enabled(void);

void level_band_show_timer(bool show);
bool level_band_timer_shown(void);
/** @brief The time left changed, or the timer's state did */
void level_band_update_timer(int time_left_ms);

#endif  // LEVEL_BAND_H_
