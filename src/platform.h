/* What the game code needs from the machine, implemented with SDL2 in platform.c.
 *
 * The original is single-threaded and blocks in places (delay() in animations, getch() in
 * menus). The port keeps that structure: every blocking call keeps pumping SDL events and the
 * display through plat_poll(), so the window stays live. */
#ifndef MARS_PLATFORM_H
#define MARS_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

/* The game's keyboard ISR (1054:0499) state. scancode is [C8AA], the last make or break code
 * (PC/XT set 1) from port 60h. The held flags follow the configured keys and the arrows. */
typedef struct {
    int scancode;     /* [C8AA] */
    bool jump;        /* [C8AD] */
    bool fire;        /* [C92C] */
    bool left;        /* [CCCE] */
    bool right;       /* [CCD2] */
    int key_jump, key_fire, key_left, key_right; /* [C8AE] [8A25] [C8EC] [C92D] */
} isr_keys_t;

extern isr_keys_t isr_keys;
extern bool plat_isr_installed; /* the game's INT 9 handler is in place (else the BIOS one) */

/* For gamepad.c, which presses keys for the controller: a make or break code (PC/XT set 1)
 * through the game's ISR, and a character (or 0, scancode) into the BIOS buffer. */
void plat_isr_key(int code);
void plat_push_key(int c);

/* Port extra for the Controls screen: while raw input is on, every key or button pressed is
 * queued as itself (an SDL scancode, or a PAD_N_ number with pad set) and does nothing else;
 * F4, F11, Alt+Enter, Alt+C and F12 still do theirs. plat_raw_take takes the next one, false
 * if there is none; without a window it writes a screenshot (as getch does) and gives Esc. */
void plat_raw_begin(void);
void plat_raw_end(void);
void plat_raw_push(bool pad, int code);
bool plat_raw_take(bool *pad, int *code);

/* Port extra: the make code the controller's Start sends in game, for the pause menu. No PC/XT
 * key sends it, so the game's own key tests never see it. */
#define SC_PAUSE_MENU 0x70

/* Port extras for the pause menu: leave the program (plat_quitting says yes from then on), and
 * full screen on or off (display.fullscreen, as F11 does). */
void plat_request_quit(void);
void plat_set_fullscreen(bool on);

/* The frame loop, around each frame (input.quick_taps): a jump, fire, walk or Enter key
 * pressed and let go since the last frame counts as held for this one. */
void plat_frame_input_begin(void);
void plat_frame_input_end(void);
/* ... and the pause menu closed: what was tapped in it is not the game's. */
void plat_forget_taps(void);

/* The window's title, which says whether display.smooth is on (F4 switches it). */
void plat_set_title(void);

int plat_init(const char *title, char *err, size_t errlen);
void plat_shutdown(void);

/* Handle window events and redraw the window if the picture changed. */
void plat_poll(void);
bool plat_quitting(void);

/* Turbo C conio: kbhit / getch. getch blocks (keeping the window alive) and returns 0 then
 * the scancode for extended keys, like the BIOS. Returns 27 (Esc) once the window closes. */
bool plat_kbhit(void);
int plat_getch(void);

/* For loops that wait for a key without taking it (while (!kbhit()) ...): plat_kbhit, except
 * that without a window it writes a screenshot and says yes. */
bool plat_key_pressed(void);

/* Port extra for --show: without a window, every wait for a key (getch, plat_key_pressed)
 * writes <base>-N.png, then acts as if Esc was pressed. */
void plat_capture_waits(const char *base);

/* Turbo C rand(): never seeded, so it starts from 1. The menu's choice of transition and the
 * drips share it. */
int plat_rand(void);

/* Turbo C delay(ms). */
void plat_delay(int ms);

/* Caps Lock is on: bit 6 of the BIOS keyboard flags at 0040:0017 (the game reads them through
 * the far pointer at [CCD6]). */
bool plat_caps_lock(void);

/* BIOS timer ticks (18.2 Hz) since start: the timer ISR at 1054:7aba counts them in [CCD1]. */
unsigned plat_ticks(void);

/* Turbo C sound / nosound: PC speaker square wave. */
void plat_sound(int hz);
void plat_nosound(void);

/* game.physics = modern: a sound effect that doesn't stop the game. Between begin and end,
 * plat_sound, plat_nosound and plat_delay don't happen at once but are written down; end
 * then plays them from the audio thread, cutting off the effect before, unless that one was
 * kept: a kept effect plays to its end, and the effects ended meanwhile aren't heard. */
void plat_sfx_begin(void);
void plat_sfx_end(bool keep);

/* For game.physics = modern's frame loop: seconds since start, the display's refresh rate,
 * and a short wait (a millisecond) that keeps the window alive. */
double plat_seconds(void);
int plat_refresh_hz(void);
void plat_idle(void);

/* The CGA state changed (video memory or palette): show it on the next poll. */
void plat_dirty(void);

/* Port extras: F12 writes a screenshot (shotNNN.png next to the working directory). */
int plat_screenshot(const char *path);

#endif
