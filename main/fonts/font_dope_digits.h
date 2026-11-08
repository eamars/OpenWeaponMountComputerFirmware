#ifndef FONT_DOPE_DIGITS_H_
#define FONT_DOPE_DIGITS_H_

#include <lvgl.h>

// Montserrat Medium, digits, '.' and '-' only, 4 bpp (lv_font_conv 1.5.3), for the elevation on the level view
// (ADR-007): larger than LVGL's built-in fonts, at a few tens of KB of flash
LV_FONT_DECLARE(font_dope_digits_64);
LV_FONT_DECLARE(font_dope_digits_84);

#endif  // FONT_DOPE_DIGITS_H_
