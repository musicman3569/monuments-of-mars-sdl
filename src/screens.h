/* The main menu's text screens: the story (S), the instructions (I) and how to order (O).
 *
 * They're straight-line code: DrawText calls with constant strings, font switches, the odd
 * video mode change or tile, and getch between pages. Rather than copy their text (which
 * differs between the volumes), the port runs that code from the user's EXE, like ptomb-port's
 * screenvm: a small interpreter that understands the dozen instruction forms these routines
 * use and sends each call to the port's own functions. Anything else stops the screen with an
 * error instead of guessing. The routines are found through MainMenu's key tests. */
#ifndef MARS_SCREENS_H
#define MARS_SCREENS_H

#include "gamedata.h"

/* Each returns 0, or -1 (with a message on stderr) if the routine can't be found or run. */
int screen_story(const gamedata_t *g);        /* ShowStory, 1054:6750 in MARS1 */
int screen_instructions(const gamedata_t *g); /* ShowInstructions, 1054:6106 */
int screen_ordering(const gamedata_t *g);     /* ShowOrderingInfo, 1054:68f2 */

/* ShowSpecialNotice (1054:5dc3): drawn line by line until a key is pressed; then waits for a
 * key without taking it. Shown once on the first run (no MARSn.SPD) and after the instructions. */
void show_special_notice(const gamedata_t *g);

#endif
