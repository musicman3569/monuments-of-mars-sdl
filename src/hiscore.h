/* The high-score table, MARSn.HS: ten lines of "<score><name>" (a decimal number immediately
 * followed by the name, CR LF), as SaveHighScores (1054:766b) writes them. */
#ifndef MARS_HISCORE_H
#define MARS_HISCORE_H

#include "gamedata.h"

/* LoadHighScores (1054:7526), from TitleScreen: the file, or the built-in table. */
void hiscore_load(const gamedata_t *g);

/* ShowHighScores (1054:76f2): the table on a cleared palette-0 screen. */
void hiscore_show(const gamedata_t *g);

/* The main menu's H: the table, "PRESS ANY KEY", the player walking on both sides. */
void hiscore_menu(const gamedata_t *g);

/* CheckHighScore (1054:77be), after a game: if `score` makes the table, ask for a name and
 * save the table. Returns 1 if it did. */
int hiscore_check(const gamedata_t *g, unsigned long score);

#endif
