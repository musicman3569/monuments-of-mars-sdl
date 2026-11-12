/* The title screen and the main menu. */
#ifndef MARS_MENU_H
#define MARS_MENU_H

#include <stdbool.h>
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

/* Port extra: the volume selector, shown when mars.exe starts without a MARSn.EXE. A box of the
 * volumes present[0..3] says are there, by the names the game's ordering info gives them (read
 * from g, any volume's data), with QUIT after them; `sel` (1..4) is chosen first. Arrows, the
 * d-pad or stick and Enter / A, or a volume's digit; Esc, Q or B quits. Returns the volume
 * (1..4), or 0 to quit. */
int volume_menu(const gamedata_t *g, const bool present[4], int sel);

#endif
