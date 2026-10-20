/* The keyboard setups: MARSn.KBD holds one letter, A-G or X, naming one of eight fixed sets of
 * left / right / jump / fire keys. */
#ifndef MARS_KEYCONFIG_H
#define MARS_KEYCONFIG_H

#include "gamedata.h"

/* ApplyKeyConfig (1054:6a94): set the keyboard ISR's four keys for setup `c`. */
void keyconfig_apply(int c);

/* LoadKeyConfig (1054:6c07), which MainMenu calls first: MARSn.KBD's letter, or 'D'. */
void keyconfig_load(const gamedata_t *g);

/* ConfigKeyboard (1054:6c5e), the main menu's C: show the setups, take a letter (or Esc),
 * apply it and save it. */
void keyconfig_screen(const gamedata_t *g);

#endif
