#include "settings.h"

#include <ctype.h>
#include <stdbool.h>
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

/* Setting i's value as the file writes it. */
static const char *value_text(size_t i)
{
    static const char *const presets[] = {"classic", "enhanced", "modern"}, *const physics[] = {"classic", "modern"};
    const int v = *field(i);
    return table[i].type == T_BOOL ? (v ? "on" : "off") : table[i].type == T_PRESET ? presets[v] : physics[v];
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
        fprintf(f, "; %s\n%s = %s\n", table[i].help, dot + 1, value_text(i));
    }
}

static char save_path[1024];

void settings_set_path(const char *path)
{
    snprintf(save_path, sizeof save_path, "%s", path ? path : "");
}

/* A line of the file into s (256 bytes), cut short if longer. */
static void copy_line(char *s, const char *line)
{
    size_t n = strlen(line);
    if (n > 255)
        n = 255;
    memcpy(s, line, n);
    s[n] = 0;
}

/* The setting a line of the file sets ("section.key", in `out`), given the section it's in;
 * false for anything else. */
static bool line_key(const char *line, const char *section, char *out, size_t outlen)
{
    char s[256];
    copy_line(s, line);
    char *c = strpbrk(s, ";#");
    if (c)
        *c = '\0';
    char *eq = strchr(s, '=');
    if (!eq || *trim(s) == '[')
        return false;
    *eq = '\0';
    const char *k = trim(s);
    if (*section)
        snprintf(out, outlen, "%s.%s", section, k);
    else
        snprintf(out, outlen, "%s", k);
    return true;
}

/* The section a line starts, into `section`; false if it isn't a section header. */
static bool line_section(const char *line, char *section, size_t len)
{
    char s[256];
    copy_line(s, line);
    char *c = strpbrk(s, ";#");
    if (c)
        *c = '\0';
    char *t = trim(s), *close = strchr(t, ']');
    if (*t != '[' || !close)
        return false;
    *close = '\0';
    snprintf(section, len, "%s", trim(t + 1));
    return true;
}

int settings_save(const char *const *keys, int n)
{
    if (!*save_path || n == 0)
        return 0;
    /* the file as lines (without their ends), plus room for the new ones */
    enum { MAX_LINES = 1000 };
    static char lines[MAX_LINES][256];
    int count = 0;
    FILE *f = fopen(save_path, "r");
    if (f) {
        while (count < MAX_LINES - 3 * (int)N_SETTINGS && fgets(lines[count], sizeof lines[count], f)) {
            lines[count][strcspn(lines[count], "\r\n")] = '\0';
            count++;
        }
        fclose(f);
    }
    for (int k = 0; k < n; k++) {
        size_t i = 0;
        while (i < N_SETTINGS && strcmp(table[i].key, keys[k]) != 0)
            i++;
        if (i == N_SETTINGS)
            continue;
        const char *dot = strchr(keys[k], '.');
        char want[32], section[32] = "", key[64];
        snprintf(want, sizeof want, "%.*s", (int)(dot - keys[k]), keys[k]);
        int at = -1, end_of_section = -1; /* the key's line; the last line of its section */
        for (int l = 0; l < count; l++) {
            if (line_section(lines[l], section, sizeof section))
                continue;
            if (!strcmp(section, want) && strspn(lines[l], " \t") != strlen(lines[l]))
                end_of_section = l;
            if (line_key(lines[l], section, key, sizeof key) && !strcmp(key, keys[k]))
                at = l;
        }
        if (at < 0 && end_of_section < 0) /* no such section yet: one at the end */
            for (int l = count - 1; l >= 0 && end_of_section < 0; l--)
                if (line_section(lines[l], section, sizeof section) && !strcmp(section, want))
                    end_of_section = l;
        if (at >= 0) { /* (a key before any section names its section itself) */
            char sec[32] = "", comment[256] = "";
            for (int l = 0; l < at; l++)
                line_section(lines[l], sec, sizeof sec);
            const char *c = strpbrk(lines[at], ";#"); /* a comment after the value stays */
            if (c)
                copy_line(comment, c);
            snprintf(lines[at], sizeof lines[at], "%s = %s%s%.200s", *sec ? dot + 1 : keys[k], value_text(i),
                     *comment ? " " : "", comment);
        } else {
            if (end_of_section < 0) {
                if (count > 0 && lines[count - 1][0])
                    lines[count++][0] = '\0';
                snprintf(lines[count++], sizeof lines[0], "[%s]", want);
                end_of_section = count - 1;
            }
            memmove(lines[end_of_section + 2], lines[end_of_section + 1],
                    (size_t)(count - end_of_section - 1) * sizeof lines[0]);
            snprintf(lines[end_of_section + 1], sizeof lines[0], "%s = %s", dot + 1, value_text(i));
            count++;
        }
    }
    /* written whole to a new file, then put in the old one's place */
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", save_path);
    f = fopen(tmp, "w");
    if (!f) {
        fprintf(stderr, "mars: can't write %s\n", tmp);
        return -1;
    }
    for (int l = 0; l < count; l++)
        fprintf(f, "%s\n", lines[l]);
    const bool bad = ferror(f) != 0;
    if (fclose(f) != 0 || bad) {
        fprintf(stderr, "mars: can't write %s\n", tmp);
        remove(tmp);
        return -1;
    }
    remove(save_path);
    if (rename(tmp, save_path) != 0) {
        fprintf(stderr, "mars: can't replace %s\n", save_path);
        return -1;
    }
    return 0;
}
