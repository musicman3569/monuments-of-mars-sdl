/* Port extra: the controls, the game's actions bound to keyboard keys and controller buttons
 * (two of each per action), set on the Controls screen (controls.c, the main menu's C) and
 * saved in mars.ini's [keys] and [pad] sections.
 *
 * The game itself still sees PC/XT scancodes through its keyboard ISR: while playing, a bound
 * key or button presses the action's own scancode (bind_code), whatever key it is. Keys bound
 * to nothing go through as themselves, except those whose scancode is an action's: so Space no
 * longer jumps once jump is bound elsewhere. In the menus, the Y/N questions and the pause menu
 * the keyboard is the keyboard and the controller works as before (gamepad.h).
 *
 * Fixed: Esc and the controller's Start open the pause menu (the first slot of MENU), and the
 * left stick walks. Without bindings in mars.ini the keyboard's walk, jump and fire keys come
 * from MARSn.KBD's setup letter, as the original's Config Keyboard chose them. */
#ifndef MARS_BIND_H
#define MARS_BIND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

enum {
    ACT_LEFT, ACT_RIGHT, ACT_JUMP, ACT_FIRE, ACT_USE, ACT_MENU, ACT_SAVE, ACT_RESTORE, ACT_QUIT,
    ACT_PAUSE, ACT_SOUND, ACT_GIVE_UP, ACTIONS
};
#define BIND_SLOTS 2

/* key: SDL_Scancode (0 = none). pad: a PAD_ button number (gamepad.h), -1 = none. */
typedef struct {
    int key[ACTIONS][BIND_SLOTS];
    int pad[ACTIONS][BIND_SLOTS];
} bindings_t;

extern bindings_t bindings;

/* The defaults: setup D's keys (Alt / Ctrl walk, Space jumps, Shift fires) with the arrows. */
void bind_defaults(void);

/* MARSn.KBD's setup, as the scancodes of its left / right / jump / fire keys: the keyboard's
 * bindings for those, unless mars.ini (or the Controls screen) set them. */
void bind_seed_setup(int left, int right, int jump, int fire);

/* Slot `slot` of action `a` is fixed (Esc and Start for the pause menu). */
bool bind_locked(int a, bool pad, int slot);

/* What's shown: the action's name, and a key's or button's (at most 7 characters). */
const char *bind_action_name(int a);
const char *bind_key_name(int key);
const char *bind_pad_name(int button);

/* Set slot `slot` of action `a` to a key or button (0 / -1 clears it), taking it from any
 * other action or slot it was on. */
void bind_set_key(int a, int slot, int key);
void bind_set_pad(int a, int slot, int button);

/* mars.ini: settings.c hands over keys.<action> and pad.<action>. bind_ini_set returns 0, or -1
 * with a message; bind_ini_value writes the value as the file has it ("SPACE, LALT", "-" for
 * none) and returns false for other keys. bind_write writes both sections with comments;
 * bind_save writes every binding into mars.ini (settings_save). */
bool bind_is_key(const char *key);
int bind_ini_set(const char *key, const char *value, char *err, size_t errlen);
bool bind_ini_value(const char *key, char *out, size_t len);
void bind_write(FILE *f);
void bind_save(void);

/* While playing: the action a key or button is bound to (-1: none), the scancode an action
 * presses, and whether a key's own scancode is an action's (so unbound, it does nothing). */
int bind_key_action(int key);
int bind_pad_action(int button);
int bind_code(int a);
bool bind_reserved(int xt);

/* An action's key or button went down / up: the first one down presses its scancode through
 * the game's ISR, the last one up lets go of it. */
void bind_press(int a);
void bind_release(int a);

#endif
