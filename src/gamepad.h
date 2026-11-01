/* Port extra: modern game controllers (Xbox, PlayStation, Switch, 8BitDo, ...) through SDL's
 * game-controller layer, which follows controllers being plugged in and out at any time. A
 * gamecontrollerdb.txt next to the port's EXE adds more controller mappings.
 *
 * The original only knows the keyboard, so the controller works by pressing and releasing keys
 * for the player: in game through the game's keyboard ISR (scancodes, as if typed), in the
 * menus and other screens into the BIOS buffer that getch reads. Xbox names:
 *
 *   in game:            left stick / d-pad   walk (the arrows)
 *                       A                    jump (the configured jump key)
 *                       X                    fire (the configured fire key)
 *                       B or Y               use a switch, lever or terminal (Enter)
 *                       Start                the pause menu (port extra: Continue, Options,
 *                                            Quit to Menu, Quit Game)
 *                       Back / View          quit (Q, then the QUIT (Y/N) question)
 *                       LB / RB              save / restore (S / R, then ARE YOU SURE)
 *   Y/N questions:      A or Start = Y       B or Back = N
 *   the pause menu:     up / down choose, A = Enter, left / right or A switch an option,
 *                       B or Back = back (Esc), Start = back to the game
 *   menus and screens:  A or Start = Enter (play, go on)   B or Back = Esc
 *
 * The main menu's other choices, the high-score name and the cheats need the keyboard. */
#ifndef MARS_GAMEPAD_H
#define MARS_GAMEPAD_H

#include <stdbool.h>

union SDL_Event;

/* Called by platform.c: plat_init, every event plat_poll takes, and once per poll after the
 * events. */
void pad_init(void);
void pad_event(const union SDL_Event *ev);
void pad_update(void);

/* game.c: a Y/N question is up, so A / B answer it instead of jumping / using. */
void pad_set_yes_no(bool on);

/* game.c: the pause menu is up, so the buttons move through it (the up / down arrows, Enter,
 * Esc, SC_PAUSE_MENU) instead of playing. */
void pad_set_menu(bool on);

/* The buttons pad_update works with, OR-ed together over every controller (for tests). */
enum {
    PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3,
    PAD_START = 1 << 4, PAD_BACK = 1 << 5, PAD_LB = 1 << 6, PAD_RB = 1 << 7,
    PAD_LEFT = 1 << 8, PAD_RIGHT = 1 << 9, PAD_UP = 1 << 10, PAD_DOWN = 1 << 11,
    PAD_BUTTONS = 12
};

/* What pad_update does with the controllers' state, given as PAD_ bits. */
void pad_apply(unsigned held);

#endif
