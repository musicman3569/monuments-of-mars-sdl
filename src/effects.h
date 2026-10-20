/* Genus PCX Effects 1.04: the transitions that put the title and menu pictures on screen.
 *
 * The library keeps three settings: the effect type (1bea:000a, 0-9), a granularity (1bea:0080,
 * 1-64: the size of the strips and cells, rounded down to whole bytes, 8 pixels in CGA, for
 * columns) and a delay between steps (1bea:00d8, 0-500 units of the loop calibrated by
 * 1d10:003b: 1/100 of 18 timer ticks, about 9.9 ms). 1c09:0006 then shows a picture with the
 * type's routine, where the "variant" mostly picks a direction (1, 2, 4, 8, or two of them
 * for the diagonals). Each step copies part of the picture to the same place on screen,
 * except type 3, which slides it in. The port has the five types the game uses. */
#ifndef MARS_EFFECTS_H
#define MARS_EFFECTS_H

#include "pcx.h"

void effects_set_type(int type);        /* 1bea:000a; type 9 also seeds the library's RNG */
void effects_set_granularity(int g);    /* 1bea:0080 */
void effects_set_delay(int units);      /* 1bea:00d8 */

/* 1c09:0006(variant, x = 0, y = 0, picture). */
void effects_show(const pcx_image_t *img, int variant);

#endif
