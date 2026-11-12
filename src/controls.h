/* Port extra: the Controls screen, the main menu's C in place of the original's Config
 * Keyboard. A table of the game's actions with two keyboard keys and two controller buttons
 * each: move with the arrows or the d-pad / stick, Enter or A to change one (then press the new
 * key or button; Esc or Start to cancel), Del or X to clear it, Esc or B to leave. What's set
 * is saved in mars.ini as the screen closes (bind.h). */
#ifndef MARS_CONTROLS_H
#define MARS_CONTROLS_H

#include "gamedata.h"

void controls_screen(const gamedata_t *g);

#endif
