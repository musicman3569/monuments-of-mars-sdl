/* The keyboard setups: MARSn.KBD holds one letter, A-G or X, naming one of eight fixed sets of
 * left / right / jump / fire keys, which the original's Config Keyboard chose. The port's
 * Controls screen (controls.h) took its place; the letter still gives the keyboard's walk, jump
 * and fire keys while mars.ini doesn't (bind.h). */
#ifndef MARS_KEYCONFIG_H
#define MARS_KEYCONFIG_H

#include "gamedata.h"

/* LoadKeyConfig (1054:6c07), which MainMenu calls first: MARSn.KBD's letter, or 'D'. Sets the
 * scancodes the game's ISR takes for walking, jumping and firing to the arrows, Space and Left
 * Shift (the bound keys press those: bind_code), and hands the setup's keys to bind_seed_setup.
 * An empty file gives no setup: the defaults stay. */
void keyconfig_load(const gamedata_t *g);

#endif
