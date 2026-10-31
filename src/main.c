/* Monuments of Mars port: entry point.
 *
 *   mars [MARSn.EXE] [--classic | --enhanced] [--set section.key=value] [--config FILE]
 *        [--print-settings] [--level N] [--palette 0|1] [--bright]
 *        [--screenshot out.png] [--script KEYS] [--show SCREEN]
 *
 * The port's settings (settings.h) come from the preset, mars.ini next to mars.exe, then
 * --set. F4 switches smooth movement on and off.
 *
 * main (1054:7adb): the title screen, then the menu loop: MainMenu, a game, CheckHighScore,
 * back to the menu. --level N starts with a game at level N instead. F12 saves a screenshot.
 *
 * Without a window, for testing: --screenshot writes the title screen (or, with --level, that
 * level's first frame) to a PNG. --script plays from the level with scripted keys and prints
 * where the player got to (see game_run_script in game.h); with --screenshot, of the last
 * frame. --show runs one of the menu's screens (title, menu, story, instructions, ordering,
 * keys, hiscores, notice) and writes <out>-N.png at every wait for a key, answering Esc. */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gamedata.h"
#include "game.h"
#include "hiscore.h"
#include "keyconfig.h"
#include "menu.h"
#include "platform.h"
#include "screens.h"
#include "settings.h"

typedef struct {
    const char *exe_path;
    int level; /* 1-based, 0 = not given */
    const char *screenshot;
    const char *script;
    const char *show;
    int palette; /* -1 = the game's choice */
    int bright;
    int preset;  /* -1 = from the file, else classic */
    const char *config;
    int print_settings;
    const char *sets[16];
    int n_sets;
} options_t;

static int parse_args(int argc, char **argv, options_t *o)
{
    memset(o, 0, sizeof *o);
    o->exe_path = "MARS1.EXE";
    o->palette = -1;
    o->preset = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--classic") || !strcmp(a, "--enhanced"))
            o->preset = settings_preset_by_name(a + 2);
        else if (!strcmp(a, "--set") && i + 1 < argc) {
            const char *set = argv[++i];
            if (!strncmp(set, "game.preset=", 12)) { /* a preset, not just the field */
                if ((o->preset = settings_preset_by_name(set + 12)) < 0) {
                    fprintf(stderr, "mars: game.preset must be classic or enhanced\n");
                    return -1;
                }
            } else if (o->n_sets < (int)(sizeof o->sets / sizeof o->sets[0])) {
                o->sets[o->n_sets++] = set;
            }
        } else if (!strcmp(a, "--config") && i + 1 < argc)
            o->config = argv[++i];
        else if (!strcmp(a, "--print-settings"))
            o->print_settings = 1;
        else if (!strcmp(a, "--level") && i + 1 < argc)
            o->level = atoi(argv[++i]);
        else if (!strcmp(a, "--screenshot") && i + 1 < argc)
            o->screenshot = argv[++i];
        else if (!strcmp(a, "--script") && i + 1 < argc)
            o->script = argv[++i];
        else if (!strcmp(a, "--show") && i + 1 < argc)
            o->show = argv[++i];
        else if (!strcmp(a, "--palette") && i + 1 < argc)
            o->palette = atoi(argv[++i]) ? 1 : 0;
        else if (!strcmp(a, "--bright"))
            o->bright = 1;
        else if (a[0] == '-') {
            fprintf(stderr, "usage: mars [MARSn.EXE] [--classic | --enhanced] [--set section.key=value] "
                            "[--config FILE] [--print-settings] [--level N] [--palette 0|1] [--bright] "
                            "[--screenshot out.png] [--script KEYS] [--show SCREEN]\n");
            return -1;
        } else
            o->exe_path = a;
    }
    return 0;
}

static options_t opt;

/* Settings: the preset, then the INI file, then the command line (see settings.h). */
static int load_settings(const options_t *o)
{
    char path[1024];
    if (o->config) {
        snprintf(path, sizeof path, "%s", o->config);
    } else {
        char *base = SDL_GetBasePath();
        snprintf(path, sizeof path, "%smars.ini", base ? base : "");
        SDL_free(base);
    }
    static settings_pair_t pairs[32];
    int n = settings_read_file(path, pairs, 32);
    if (n < 0) {
        fprintf(stderr, "mars: can't read %s\n", path);
        n = 0;
    } else if (n == 0 && o->config) {
        fprintf(stderr, "mars: %s is missing or empty\n", path);
    }

    int preset = o->preset;
    for (int i = 0; i < n && preset < 0; i++)
        if (!strcmp(pairs[i].key, "game.preset") && (preset = settings_preset_by_name(pairs[i].value)) < 0)
            fprintf(stderr, "mars: %s:%d: game.preset must be classic or enhanced\n", path, pairs[i].line);
    settings_reset(preset < 0 ? PRESET_CLASSIC : (preset_t)preset);

    char err[160];
    for (int i = 0; i < n; i++)
        if (strcmp(pairs[i].key, "game.preset") != 0 &&
            settings_set(pairs[i].key, pairs[i].value, err, sizeof err) != 0)
            fprintf(stderr, "mars: %s:%d: %s (ignored)\n", path, pairs[i].line, err);

    for (int i = 0; i < o->n_sets; i++) {
        char key[64];
        const char *eq = strchr(o->sets[i], '=');
        if (!eq) {
            fprintf(stderr, "mars: --set needs section.key=value, not '%s'\n", o->sets[i]);
            return -1;
        }
        snprintf(key, sizeof key, "%.*s", (int)(eq - o->sets[i]), o->sets[i]);
        if (settings_set(key, eq + 1, err, sizeof err) != 0) {
            fprintf(stderr, "mars: %s\n", err);
            return -1;
        }
    }
    return 0;
}

static void fail(const char *msg)
{
    fprintf(stderr, "mars: %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Monuments of Mars", msg, NULL);
}

/* --show: one screen, without a window. */
static int show(gamedata_t *g, const char *name, char *err, size_t errlen)
{
    char base[1100];
    snprintf(base, sizeof base, "%s", opt.screenshot ? opt.screenshot : name);
    char *dot = strrchr(base, '.');
    if (dot && !strcmp(dot, ".png"))
        *dot = 0;
    plat_capture_waits(base);
    keyconfig_load(g);
    hiscore_load(g);
    if (!strcmp(name, "title"))
        return title_screen(g, err, errlen); /* creates MARSn.SPD if it isn't there */
    if (!strcmp(name, "menu"))
        return main_menu(g, err, errlen) < 0 ? -1 : 0;
    if (!strcmp(name, "story"))
        return screen_story(g);
    if (!strcmp(name, "instructions"))
        return screen_instructions(g);
    if (!strcmp(name, "ordering"))
        return screen_ordering(g);
    if (!strcmp(name, "keys")) {
        keyconfig_screen(g);
        return 0;
    }
    if (!strcmp(name, "hiscores")) {
        hiscore_menu(g);
        return 0;
    }
    if (!strcmp(name, "notice")) {
        show_special_notice(g);
        return 0;
    }
    snprintf(err, errlen, "--show: no screen called %s", name);
    return -1;
}

int main(int argc, char **argv)
{
    if (parse_args(argc, argv, &opt) != 0 || load_settings(&opt) != 0)
        return 2;
    if (opt.print_settings) {
        settings_write(stdout);
        return 0;
    }
    cga_palette_override = opt.palette;
    cga_bright_override = opt.bright;
    static gamedata_t data;
    char err[300];
    if (gamedata_load(&data, opt.exe_path, err, sizeof err) != 0) {
        fail(err);
        return 1;
    }
    const int level = opt.level >= 1 && opt.level <= LEVELS ? opt.level - 1 : 0;

    if (opt.show) {
        const int rc = show(&data, opt.show, err, sizeof err);
        if (rc != 0)
            fprintf(stderr, "mars: %s\n", err);
        gamedata_free(&data);
        return rc != 0;
    }
    if (opt.screenshot || opt.script) {
        int rc = 0;
        if (opt.level || opt.script) {
            keyconfig_load(&data);
            game_run_script(&data, level, 5, opt.script ? opt.script : "1");
        } else {
            rc = title_draw(&data, err, sizeof err);
            if (rc != 0)
                fail(err);
        }
        if (rc == 0 && opt.screenshot && (rc = plat_screenshot(opt.screenshot)) != 0)
            fprintf(stderr, "mars: can't write %s\n", opt.screenshot);
        gamedata_free(&data);
        return rc != 0;
    }

    if (plat_init("Monuments of Mars", err, sizeof err) != 0) {
        fail(err);
        gamedata_free(&data);
        return 1;
    }
    int rc = 0;
    if (opt.level) { /* port extra: a game first */
        hiscore_load(&data);
        keyconfig_load(&data);
        game_play(&data, level, 5);
    } else if (title_screen(&data, err, sizeof err) != 0) {
        fail(err);
        rc = 1;
    }
    /* the menu loop (1054:7b77) */
    while (rc == 0 && !plat_quitting()) {
        const int shots = main_menu(&data, err, sizeof err);
        if (shots < 0) {
            fail(err);
            rc = 1;
        }
        if (shots <= 0)
            break;
        game_play(&data, 0, shots);
    }

    plat_shutdown();
    gamedata_free(&data);
    return rc;
}
