#ifndef LEVEL_BAND_H_
#define LEVEL_BAND_H_

#include <stdbool.h>
#include <lvgl.h>

#include "countdown_timer.h"

// The band at the bottom of the level view (ADR-007): the timer bar and time, and the current dope target, on a
// light dark overlay through which the level's colour shows. Shown while there is a dope target or the timer is
// armed; the level alone otherwise. Tap the band: next target; hold: previous, both going round the card (after the
// last, the first). Tap the time or the bar: start,
// pause or continue the timer (also the hardware button); hold: re-arm with the full time.
// All calls with the LVGL lock held, unless said otherwise.

void level_band_create(lv_obj_t * overlay, countdown_timer_t * timer);
void level_band_set_rotation(lv_display_rotation_t rotation);

/** @brief The card or the target names changed: show the current target again */
void level_band_refresh_dope(void);
/** @brief Recoil's next target: it stops at the last target (no wrap, unlike the band's tap) */
void level_band_next_target(void);

void level_band_show_timer(bool show);
bool level_band_timer_shown(void);
/** @brief The time left changed, or the timer's state did */
void level_band_update_timer(int time_left_ms);

#endif  // LEVEL_BAND_H_
