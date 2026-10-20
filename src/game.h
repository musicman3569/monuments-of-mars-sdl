/* Playing the game: main's level start and frame loop (1054:7c1a-806e) and everything they
 * call, from UpdatePlayer down to the tile effects. */
#ifndef MARS_GAME_H
#define MARS_GAME_H

#include "gamedata.h"

/* What main does between MainMenu and the frame loop (1054:7bec-7c17), then play from level
 * `level` (0-based) with `shots` shots (MainMenu gives 5, or 20 with the cheat). Returns when
 * the game is over, the player quits (Q / Esc, then Y) or the window closes. */
void game_play(gamedata_t *g, int level, int shots);

/* Port extra for testing (--script): play with the keys given by `script` instead of the
 * keyboard, as fast as possible, then print where the player got to. Tokens are a frame count
 * followed by the keys held, from l r j f e (left, right, jump, fire, Enter): "20r 6rj 40". */
void game_run_script(gamedata_t *g, int first_level, int start_shots, const char *script);

#endif
