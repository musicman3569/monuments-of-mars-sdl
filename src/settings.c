#include "settings.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

settings_t settings;

typedef enum { T_BOOL, T_PRESET, T_PHYSICS } type_t;

/* Every setting, with its value in each preset. Rows of one section stay together (the
 * written file follows this order). */
static const struct {
    const char *key;   /* section.name */
    type_t type;
    size_t offset;     /* in settings_t */
    int value[3];      /* in each preset (preset_t) */
    const char *help;
} table[] = {
    {"game.preset", T_PRESET, offsetof(settings_t, preset), {PRESET_CLASSIC, PRESET_ENHANCED, PRESET_MODERN},
     "classic = the original game; enhanced = with the display and input enhancements;\n"
     "; modern = rewritten to run at 120 steps a second. Settings below override it"},
    {"game.physics", T_PHYSICS, offsetof(settings_t, physics), {PHYSICS_CLASSIC, PHYSICS_CLASSIC, PHYSICS_MODERN},
     "classic = the original's 18 steps a second, things moving 4 pixels across at a time;\n"
     "; modern = 120 steps a second, a pixel at a time, drawn at the monitor's refresh rate,\n"
     "; with sounds that no longer pause the game. The levels and rules are the same"},
    {"display.smooth", T_BOOL, offsetof(settings_t, smooth), {0, 1, 0},
     "smooth movement: things glide between the game's steps (18 a second, 4 pixels across),\n"
     "; shown at the monitor's refresh rate. Only the picture changes; the game runs as before.\n"
     "; F4 switches. Not used with game.physics = modern, which draws every step itself"},
    {"display.crt", T_BOOL, offsetof(settings_t, crt), {0, 0, 0},
     "the CRT look of RetroArch's 2xsal-level2-crt shader preset: the picture doubled with\n"
     "; 2xSaL's smoothed edges, then scaled to the window under a shadow mask. Alt+C switches"},
    {"display.fullscreen", T_BOOL, offsetof(settings_t, fullscreen), {1, 1, 1},
     "start full screen (off = in a window; --window too). F11 or Alt+Enter switches"},
    {"input.quick_taps", T_BOOL, offsetof(settings_t, quick_taps), {0, 1, 1},
     "a key or button tapped and let go between two of the game's frames (18 a second) still\n"
     "; counts for the next frame. Off = the original, which only sees what is held as a frame\n"
     "; starts, so a quick tap of jump or Enter can be missed"},
};
#define N_SETTINGS (sizeof table / sizeof table[0])

static int *field(size_t i)
{
    return (int *)((char *)&settings + table[i].offset);
}

void settings_reset(preset_t preset)
{
    for (size_t i = 0; i < N_SETTINGS; i++)
        *field(i) = table[i].value[preset];
    settings.preset = (int)preset;
}

int settings_preset_by_name(const char *name)
{
    if (!strcmp(name, "classic"))
        return PRESET_CLASSIC;
    if (!strcmp(name, "enhanced"))
        return PRESET_ENHANCED;
    if (!strcmp(name, "modern"))
        return PRESET_MODERN;
    return -1;
}

static int parse_bool(const char *v)
{
    static const char *const yes[] = {"1", "on", "true", "yes"}, *const no[] = {"0", "off", "false", "no"};
    for (int i = 0; i < 4; i++) {
        if (!strcmp(v, yes[i]))
            return 1;
        if (!strcmp(v, no[i]))
            return 0;
    }
    return -1;
}

int settings_set(const char *key, const char *value, char *err, size_t errlen)
{
    for (size_t i = 0; i < N_SETTINGS; i++) {
        if (strcmp(key, table[i].key) != 0)
            continue;
        const int v = table[i].type == T_BOOL     ? parse_bool(value)
                      : table[i].type == T_PRESET ? settings_preset_by_name(value)
                      : !strcmp(value, "classic") ? PHYSICS_CLASSIC
                      : !strcmp(value, "modern")  ? PHYSICS_MODERN
                                                  : -1;
        if (v < 0) {
            snprintf(err, errlen, "%s must be %s, not '%s'", key,
                     table[i].type == T_BOOL     ? "on or off"
                     : table[i].type == T_PRESET ? "classic, enhanced or modern"
                                                 : "classic or modern",
                     value);
            return -1;
        }
        *field(i) = v;
        return 0;
    }
    snprintf(err, errlen, "unknown setting '%s'", key);
    return -1;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return s;
}

int settings_read_file(const char *path, settings_pair_t *pairs, int max)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[256], section[32] = "";
    int n = 0, lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *c = strpbrk(line, ";#"); /* comments, whole-line or trailing */
        if (c)
            *c = '\0';
        char *s = trim(line);
        if (!*s)
            continue;
        if (*s == '[') {
            char *close = strchr(s, ']');
            if (!close || close[1]) {
                fprintf(stderr, "mars: %s:%d: bad section header\n", path, lineno);
                continue;
            }
            *close = '\0';
            snprintf(section, sizeof section, "%s", trim(s + 1));
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "mars: %s:%d: expected key = value\n", path, lineno);
            continue;
        }
        *eq = '\0';
        const char *k = trim(s), *v = trim(eq + 1);
        if (n == max) {
            fprintf(stderr, "mars: %s:%d: too many settings, ignoring the rest\n", path, lineno);
            break;
        }
        /* A key in a section is section.key; one before any section may name its section itself. */
        if (*section)
            snprintf(pairs[n].key, sizeof pairs[n].key, "%s.%s", section, k);
        else
            snprintf(pairs[n].key, sizeof pairs[n].key, "%s", k);
        snprintf(pairs[n].value, sizeof pairs[n].value, "%s", v);
        pairs[n].line = lineno;
        n++;
    }
    const int bad = ferror(f);
    fclose(f);
    return bad ? -1 : n;
}

void settings_write(FILE *f)
{
    fprintf(f, "; Monuments of Mars port settings. Put this file next to mars.exe as mars.ini.\n"
               "; Command-line --set section.key=value overrides it.\n");
    char section[32] = "";
    for (size_t i = 0; i < N_SETTINGS; i++) {
        const char *dot = strchr(table[i].key, '.');
        const int slen = (int)(dot - table[i].key);
        if ((int)strlen(section) != slen || strncmp(section, table[i].key, (size_t)slen) != 0) {
            snprintf(section, sizeof section, "%.*s", slen, table[i].key);
            fprintf(f, "\n[%s]\n", section);
        }
        static const char *const presets[] = {"classic", "enhanced", "modern"}, *const physics[] = {"classic", "modern"};
        const int v = *field(i);
        fprintf(f, "; %s\n%s = %s\n", table[i].help, dot + 1,
                table[i].type == T_BOOL ? (v ? "on" : "off") : table[i].type == T_PRESET ? presets[v] : physics[v]);
    }
}
