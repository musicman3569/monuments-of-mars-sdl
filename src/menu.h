/* The title screen and the main menu. */
#ifndef MARS_MENU_H
#define MARS_MENU_H

#include <stddef.h>

#include "gamedata.h"

/* TitleScreen (1054:5ee9): load the high scores, show MARSn.CF1 and the volume's subtitle,
 * wait for a key; on the first run (no MARSn.SPD) create MARSn.SPD and show the special notice.
 * Returns 0, or -1 with a message in err. */
int title_screen(const gamedata_t *g, char *err, size_t errlen);

/* Just its picture: CF1 and the subtitle (for --screenshot). */
int title_draw(const gamedata_t *g, char *err, size_t errlen);

/* MainMenu (1054:6eb8) over MARSn.CF2: returns the shots to start a game with (5, or 20 with
 * Caps Lock + '>'), or 0 when the player quits (Esc / Q, after the ordering info). Returns -1
 * with a message in err if CF2 can't be read. */
int main_menu(const gamedata_t *g, char *err, size_t errlen);

#endif
