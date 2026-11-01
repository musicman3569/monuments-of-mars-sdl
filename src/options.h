/* Port extra: the Options menu, the port's enhancements switched at once and saved in
 * mars.ini as it closes (settings_save: only the ones switched). The pause menu (game.c) and
 * the main menu (menu.c) both show it, each with its own way of reading keys. */
#ifndef MARS_OPTIONS_H
#define MARS_OPTIONS_H

#include <stdbool.h>

/* The next key pressed, as a make code: 48h / 50h up / down, 4Bh / 4Dh left / right, 1Ch
 * Enter, 01h Esc, SC_PAUSE_MENU (the controller's Start in game); anything else is ignored. */
typedef int (*menu_key_fn)(void);

/* A box of stars (the game's banners'), `width` across, in the middle of the screen below the
 * status line: the title, then the lines, the chosen one marked. */
void menu_box(const char *title, const char *const *lines, int n, int sel, int width);

/* Options. Sets *physics_changed when game.physics was switched. Returns 1 when
 * SC_PAUSE_MENU closes the whole menu, 0 for Back / Esc. */
int options_menu(menu_key_fn key, bool *physics_changed);

#endif
