/* The port's own settings: which enhancements are on. Like ptomb-port's.
 *
 * Three presets:
 *   classic   the original game (the default)
 *   enhanced  the original game's rules, with the display and input enhancements
 *   modern    the game rewritten to run at 120 steps a second (game.physics = modern)
 * Each setting has a value for each preset: enhancements are off in classic.
 *
 * Where they come from, later ones winning:
 *   1. the preset (from --classic / --enhanced / --modern, else game.preset in the file,
 *      else classic)
 *   2. mars.ini next to mars.exe (or the --config file)
 *   3. the command line: --set section.key=value
 *
 * Adding an enhancement: add a field to settings_t, a row to the table in settings.c (classic
 * value = the original behaviour), and test `settings.field` where the behaviour changes. Keep
 * the classic path exactly as it was. */
#ifndef MARS_SETTINGS_H
#define MARS_SETTINGS_H

#include <stddef.h>
#include <stdio.h>

typedef enum { PRESET_CLASSIC, PRESET_ENHANCED, PRESET_MODERN } preset_t;
typedef enum { PHYSICS_CLASSIC, PHYSICS_MODERN } physics_t;

typedef struct {
    int preset;     /* game.preset: preset_t */
    int physics;    /* game.physics: physics_t. modern (enhancement): things move a pixel at a
                       time, 120 steps a second, drawn at the monitor's refresh rate */
    int smooth;     /* display.smooth (enhancement): moving things glide between the game's
                       steps, drawn at the monitor's refresh rate (smooth.h) */
    int crt;        /* display.crt (port extra): the CRT look (crt.h) */
    int fullscreen; /* display.fullscreen (port extra): start full screen (on in every preset) */
    int quick_taps; /* input.quick_taps (enhancement): a key or button tapped between two
                       frames still counts for the next one */
} settings_t;

extern settings_t settings;

/* Set everything to a preset's values. */
void settings_reset(preset_t preset);

/* Set one setting from text, e.g. ("display.smooth", "on"). Returns 0, or -1 with a message. */
int settings_set(const char *key, const char *value, char *err, size_t errlen);

/* A preset's name to its value ("classic" / "enhanced" / "modern"); -1 if unknown. */
int settings_preset_by_name(const char *name);

/* Read an INI file into key/value pairs without applying them, so the caller can pick the
 * preset first. Returns the number of pairs, 0 if the file doesn't exist, -1 on a read error.
 * Malformed lines are reported on stderr and skipped. */
typedef struct {
    char key[48];
    char value[48];
    int line;
} settings_pair_t;
int settings_read_file(const char *path, settings_pair_t *pairs, int max);

/* Write the current settings as an INI file, with comments (--print-settings). */
void settings_write(FILE *f);

#endif
