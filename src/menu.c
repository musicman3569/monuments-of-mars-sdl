#include "menu.h"

#include <stdio.h>

#include "cga.h"
#include "effects.h"
#include "hiscore.h"
#include "keyconfig.h"
#include "pcx.h"
#include "platform.h"
#include "screens.h"
#include "text.h"

/* The title picture: with the transition TitleScreen uses (granularity 16, the delay loop
 * calibrated, type 9 = random cells, no delay), or at once for --screenshot. */
static int title_picture(const gamedata_t *g, int transition, char *err, size_t errlen)
{
    static pcx_image_t pic;
    char path[1100];
    gamedata_path(g, "CF1", path, sizeof path);
    if (pcx_load(path, &pic, err, errlen) != 0)
        return -1;
    cga_set_mode(0, 0); /* main's SetVideoMode(4) before TitleScreen */
    if (transition) {
        effects_set_granularity(16);
        effects_set_type(9);
        effects_show(&pic, 0);
    } else {
        pcx_show(&pic);
    }
    text_set_high_font(g, FONT_NORMAL);
    const char *vol = gamedata_find_string(g, "VOL. ");
    if (vol)
        draw_text(1, vol, 20, 9);
    plat_dirty();
    return 0;
}

int title_draw(const gamedata_t *g, char *err, size_t errlen)
{
    return title_picture(g, 0, err, errlen);
}

int title_screen(const gamedata_t *g, char *err, size_t errlen)
{
    char path[1100];
    hiscore_load(g);
    while (plat_kbhit() && !plat_quitting())
        plat_getch();
    if (title_picture(g, 1, err, errlen) != 0)
        return -1;
    plat_getch();
    cga_clear(); /* ClearScreen */
    /* MARSn.SPD only marks that the notice has been seen: it's created empty */
    gamedata_path(g, "SPD", path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        f = fopen(path, "wb");
        show_special_notice(g);
    }
    if (f)
        fclose(f);
    text_set_high_font(g, FONT_NORMAL);
    return 0;
}

/* MainMenu's choice of transition: rand() % 5 picks the type, then rand() % 4 (% 2 for the
 * split) the variant. */
static void show_menu_picture(const pcx_image_t *pic)
{
    static const int types[5] = {6, 1, 3, 7, 0};
    static const int variants[5][4] = {
        {4, 8, 1, 2}, {1, 2, 0, 0}, {4, 8, 1, 2}, {6, 10, 5, 9}, {4, 8, 1, 2},
    };
    const int t = plat_rand() % 5;
    effects_set_type(types[t]);
    effects_show(pic, variants[t][plat_rand() % (t == 1 ? 2 : 4)]);
}

int main_menu(const gamedata_t *g, char *err, size_t errlen)
{
    static pcx_image_t pic;
    char path[1100];
    gamedata_path(g, "CF2", path, sizeof path);
    if (pcx_load(path, &pic, err, errlen) != 0)
        return -1;
    effects_set_granularity(8);
    effects_set_delay(1);
    keyconfig_load(g);
    while (!plat_quitting()) {
        cga_set_mode(1, 1); /* SetVideoModePal1Bright(4) */
        show_menu_picture(&pic);
        while (plat_kbhit() && !plat_quitting())
            plat_getch();
        const int c = plat_getch(); /* [0761] */
        if (plat_quitting())
            break;
        switch (c) {
        case 27: case 'q': case 'Q':
            plat_nosound();
            screen_ordering(g);
            /* the game then prints a shareware notice in text mode: left out (full version) */
            return 0;
        case 'J': case 'j': case '\r': case ' ':
            return 5;
        case 'S': case 's':
            screen_story(g);
            break;
        case 'I': case 'i':
            screen_instructions(g);
            break;
        case 'O': case 'o':
            screen_ordering(g);
            break;
        case 'C': case 'c':
            keyconfig_screen(g);
            break;
        case '>':
            if (plat_caps_lock()) /* the cheat: 20 shots */
                return 20;
            break;
        case 'H': case 'h':
            hiscore_menu(g);
            break;
        }
    }
    return 0;
}
