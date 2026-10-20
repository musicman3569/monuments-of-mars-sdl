/* Monuments of Mars port: entry point.
 *
 *   mars [MARSn.EXE] [--level N] [--screenshot out.png] [--script KEYS] [--palette 0|1]
 *        [--bright]
 *
 * For now this shows the title screen, then plays the game from level 1 (or --level N) until
 * it's over or the player quits (Q / Esc, then Y). The main menu isn't ported yet. F12 saves
 * a screenshot.
 *
 * --screenshot writes the title screen (or, with --level, that level's first frame) to a PNG
 * and exits without opening a window. --script plays from the level with scripted keys, without
 * a window, and prints where the player got to; with --screenshot, of the last frame. It's for
 * testing (see game_run_script in game.h). */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gamedata.h"
#include "game.h"
#include "keyconfig.h"
#include "pcx.h"
#include "platform.h"
#include "text.h"

typedef struct {
    const char *exe_path;
    int level; /* 1-based, 0 = not given */
    const char *screenshot;
    const char *script;
    int palette; /* -1 = the game's choice */
    int bright;
} options_t;

static int parse_args(int argc, char **argv, options_t *o)
{
    memset(o, 0, sizeof *o);
    o->exe_path = "MARS1.EXE";
    o->palette = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--level") && i + 1 < argc)
            o->level = atoi(argv[++i]);
        else if (!strcmp(a, "--screenshot") && i + 1 < argc)
            o->screenshot = argv[++i];
        else if (!strcmp(a, "--script") && i + 1 < argc)
            o->script = argv[++i];
        else if (!strcmp(a, "--palette") && i + 1 < argc)
            o->palette = atoi(argv[++i]) ? 1 : 0;
        else if (!strcmp(a, "--bright"))
            o->bright = 1;
        else if (a[0] == '-') {
            fprintf(stderr, "usage: mars [MARSn.EXE] [--level N] [--screenshot out.png] "
                            "[--script KEYS] [--palette 0|1] [--bright]\n");
            return -1;
        } else
            o->exe_path = a;
    }
    return 0;
}

static options_t opt;

static void fail(const char *msg)
{
    fprintf(stderr, "mars: %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Monuments of Mars", msg, NULL);
}

/* TitleScreen (1054:5ee9), so far: the CF1 picture and the volume's subtitle. */
static int title_screen(const gamedata_t *g, char *err, size_t errlen)
{
    static pcx_image_t pic;
    char path[1100];
    gamedata_path(g, "CF1", path, sizeof path);
    if (pcx_load(path, &pic, err, errlen) != 0)
        return -1;
    cga_set_mode(0, 0); /* main's SetVideoMode(4) before TitleScreen */
    plat_dirty();
    pcx_show(&pic);
    text_set_high_font(g, FONT_NORMAL);
    const char *vol = gamedata_find_string(g, "VOL. ");
    if (vol)
        draw_text(1, vol, 20, 9);
    plat_dirty();
    return 0;
}

int main(int argc, char **argv)
{
    if (parse_args(argc, argv, &opt) != 0)
        return 2;
    cga_palette_override = opt.palette;
    cga_bright_override = opt.bright;
    static gamedata_t data;
    char err[300];
    if (gamedata_load(&data, opt.exe_path, err, sizeof err) != 0) {
        fail(err);
        return 1;
    }
    const int level = opt.level >= 1 && opt.level <= LEVELS ? opt.level - 1 : 0;

    if (opt.screenshot || opt.script) {
        if (opt.level || opt.script) {
            keyconfig_load(&data);
            game_run_script(&data, level, 5, opt.script ? opt.script : "1");
        }
        else if (title_screen(&data, err, sizeof err) != 0) {
            fail(err);
            gamedata_free(&data);
            return 1;
        }
        const int rc = opt.screenshot ? plat_screenshot(opt.screenshot) : 0;
        if (rc != 0)
            fprintf(stderr, "mars: can't write %s\n", opt.screenshot);
        gamedata_free(&data);
        return rc != 0;
    }

    if (plat_init("Monuments of Mars", err, sizeof err) != 0) {
        fail(err);
        gamedata_free(&data);
        return 1;
    }
    if (!opt.level) {
        if (title_screen(&data, err, sizeof err) != 0) {
            fail(err);
            plat_shutdown();
            gamedata_free(&data);
            return 1;
        }
        plat_getch();
    }
    /* TODO MainMenu (1054:6eb8) and the high scores; for now straight into the game, after the
     * one thing MainMenu does first: load the key setup. */
    keyconfig_load(&data);
    game_play(&data, level, 5);

    plat_shutdown();
    gamedata_free(&data);
    return 0;
}
