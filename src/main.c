/* Monuments of Mars port: entry point.
 *
 *   mars [MARSn.EXE] [--level N] [--screenshot out.png] [--palette 0|1] [--bright]
 *
 * For now this shows the title screen, then a level viewer: the level as DrawLevel draws it,
 * with the animated tiles running at the game's 18.2 frames per second and the moving things
 * shown in their first frame. PgUp / PgDn change level, Esc quits, F12 saves a screenshot.
 *
 * --screenshot writes the title screen (or, with --level, that level) to a PNG and exits
 * without opening a window. */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gamedata.h"
#include "gfx.h"
#include "level.h"
#include "pcx.h"
#include "platform.h"
#include "text.h"

typedef struct {
    const char *exe_path;
    int level; /* 1-based, 0 = not given */
    const char *screenshot;
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
        else if (!strcmp(a, "--palette") && i + 1 < argc)
            o->palette = atoi(argv[++i]) ? 1 : 0;
        else if (!strcmp(a, "--bright"))
            o->bright = 1;
        else if (a[0] == '-') {
            fprintf(stderr, "usage: mars [MARSn.EXE] [--level N] [--screenshot out.png] "
                            "[--palette 0|1] [--bright]\n");
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

/* SetVideoMode (1054:0a36): mode 4, black background, palette 0 low intensity.
 * SetVideoModePal1Bright (1054:0aa7): the same with palette 1 bright. --palette overrides. */
static void set_video_mode(int palette, int bright)
{
    cga_clear();
    cga_cur_palette = opt.palette >= 0 ? opt.palette : palette;
    cga_cur_bright = opt.palette >= 0 ? opt.bright : bright;
    plat_dirty();
}

/* TitleScreen (1054:5ee9), so far: the CF1 picture and the volume's subtitle. */
static int title_screen(const gamedata_t *g, char *err, size_t errlen)
{
    static pcx_image_t pic;
    char path[1100];
    gamedata_path(g, "CF1", path, sizeof path);
    if (pcx_load(path, &pic, err, errlen) != 0)
        return -1;
    set_video_mode(0, 0); /* main's SetVideoMode(4) before TitleScreen */
    pcx_show(&pic);
    text_set_high_font(g, FONT_NORMAL);
    const char *vol = gamedata_find_string(g, "VOL. ");
    if (vol)
        draw_text(1, vol, 20, 9);
    plat_dirty();
    return 0;
}

/* main's level start (1054:7c01-7c94): status line, then DrawLevel and DrawShots. */
static void start_level(level_t *lv, const gamedata_t *g, int number)
{
    set_video_mode(0, 0);
    text_set_high_font(g, FONT_NORMAL);
    draw_text(3, "     ", 0, 0x14);
    draw_number(1, 8, 0, 0, 0x21); /* DrawScore(0) */
    draw_text(3, "LEVEL    ", 0, 0);
    draw_text(3, "SCORE", 0, 0x1B);
    draw_number(1, 2, number + 1, 0, 7);
    level_draw(lv, g, number);
    draw_text(3, "SHOTS  ", 0, 9 + 1); /* DrawShots(9, 0, 5) */
    draw_number(1, 2, 5, 0, 9 + 8);
}

static void show_level_frame(level_t *lv, const gamedata_t *g)
{
    level_animate_tiles(lv, g);
    copy_background();
    level_draw_sprites_preview(lv, g);
    present_frame();
    plat_dirty();
}

static void level_viewer(const gamedata_t *g, int number)
{
    static level_t lv;
    start_level(&lv, g, number);
    plat_isr_installed = true;
    unsigned last = plat_ticks();
    while (!plat_quitting()) {
        show_level_frame(&lv, g);
        while (plat_ticks() == last && !plat_quitting()) { /* wait for the timer tick ([CCD1]) */
            plat_poll();
            SDL_Delay(1);
        }
        last = plat_ticks();

        const int sc = isr_keys.scancode;
        isr_keys.scancode = 0;
        if (sc == 0x01)
            break;
        if (sc == 0x51 || sc == 0x49) { /* port extra: PgDn / PgUp */
            number = (number + (sc == 0x51 ? 1 : LEVELS - 1)) % LEVELS;
            start_level(&lv, g, number);
        }
    }
    plat_isr_installed = false;
}

int main(int argc, char **argv)
{
    if (parse_args(argc, argv, &opt) != 0)
        return 2;
    static gamedata_t data;
    char err[300];
    if (gamedata_load(&data, opt.exe_path, err, sizeof err) != 0) {
        fail(err);
        return 1;
    }
    const int level = opt.level >= 1 && opt.level <= LEVELS ? opt.level - 1 : 0;

    if (opt.screenshot) {
        if (opt.level) {
            static level_t lv;
            start_level(&lv, &data, level);
            show_level_frame(&lv, &data);
        } else if (title_screen(&data, err, sizeof err) != 0) {
            fail(err);
            gamedata_free(&data);
            return 1;
        }
        const int rc = plat_screenshot(opt.screenshot);
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
    level_viewer(&data, level);

    plat_shutdown();
    gamedata_free(&data);
    return 0;
}
