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

/* The CGA state changed (video memory or palette): show it on the next poll. */
void plat_dirty(void);

/* Port extras: F12 writes a screenshot (shotNNN.png next to the working directory). */
int plat_screenshot(const char *path);

#endif
