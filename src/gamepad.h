/* Port extra: modern game controllers (Xbox, PlayStation, Switch, 8BitDo, ...) through SDL's
 * game-controller layer, which follows controllers being plugged in and out at any time. A
 * gamecontrollerdb.txt next to the port's EXE adds more controller mappings.
 *
 * The original only knows the keyboard, so the controller works by pressing and releasing keys
 * for the player: in game through the game's keyboard ISR (scancodes, as if typed), in the
 * menus and other screens into the BIOS buffer that getch reads. Xbox names:
 *
 *   in game:            the bindings (bind.h, the Controls screen); by default
 *                       d-pad left / right   walk (the left stick always does)
 *                       A                    jump
 *                       X                    fire
 *                       B or Y               use a switch, lever or terminal (Enter)
 *                       Start                the pause menu (always: Continue, Options,
 *                                            Quit to Menu, Quit Game)
 *                       Back / View          quit (Q, then the QUIT (Y/N) question)
 *                       LB / RB              save / restore (S / R, then ARE YOU SURE)
 *   Y/N questions:      A or Start = Y       B or Back = N
 *   the pause menu:     up / down choose, A = Enter, left / right or A switch an option,
 *                       B or Back = back (Esc), Start = back to the game
 *   menus and screens:  A or Start = Enter (play, go on)   B or Back = Esc
 *   the main menu:      up / down choose (and left / right in its Options), A or Start take
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
 * Esc, SC_PAUSE_MENU) instead of playing. menu.c: the main menu or its Options are up, so
 * the d-pad and stick type arrow keys (0 then the scancode). */
void pad_set_menu(bool on);

/* In game, with no Y/N question or pause menu up: the keyboard's and controller's bindings
 * apply (bind.h). */
bool pad_playing(void);

/* The controls screen: while on, the controller's buttons (and the stick, as STICK_ numbers)
 * go to plat_raw_push as pressed instead of doing anything. */
void pad_set_raw(bool on);

/* The buttons pad_update works with, by number (PAD_N_; the ones up to PAD_BINDABLE can be
 * bound) and as bits (PAD_), OR-ed together over every controller (for tests). */
enum {
    PAD_N_A, PAD_N_B, PAD_N_X, PAD_N_Y, PAD_N_START, PAD_N_BACK, PAD_N_LB, PAD_N_RB,
    PAD_N_LEFT, PAD_N_RIGHT, PAD_N_UP, PAD_N_DOWN, PAD_N_LT, PAD_N_RT, PAD_N_L3, PAD_N_R3,
    PAD_BINDABLE,
    PAD_N_STICK_LEFT = PAD_BINDABLE, PAD_N_STICK_RIGHT, PAD_N_STICK_UP, PAD_N_STICK_DOWN,
    PAD_BUTTONS
};
enum {
    PAD_A = 1 << PAD_N_A, PAD_B = 1 << PAD_N_B, PAD_X = 1 << PAD_N_X, PAD_Y = 1 << PAD_N_Y,
    PAD_START = 1 << PAD_N_START, PAD_BACK = 1 << PAD_N_BACK, PAD_LB = 1 << PAD_N_LB, PAD_RB = 1 << PAD_N_RB,
    PAD_LEFT = 1 << PAD_N_LEFT, PAD_RIGHT = 1 << PAD_N_RIGHT, PAD_UP = 1 << PAD_N_UP, PAD_DOWN = 1 << PAD_N_DOWN,
    PAD_LT = 1 << PAD_N_LT, PAD_RT = 1 << PAD_N_RT, PAD_L3 = 1 << PAD_N_L3, PAD_R3 = 1 << PAD_N_R3,
    PAD_STICK_LEFT = 1 << PAD_N_STICK_LEFT, PAD_STICK_RIGHT = 1 << PAD_N_STICK_RIGHT,
    PAD_STICK_UP = 1 << PAD_N_STICK_UP, PAD_STICK_DOWN = 1 << PAD_N_STICK_DOWN,
};

/* What pad_update does with the controllers' state, given as PAD_ bits. */
void pad_apply(unsigned held);

#endif
