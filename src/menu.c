#include "menu.h"

#include <stdio.h>

#include "cga.h"
#include "effects.h"
#include "gamepad.h"
#include "hiscore.h"
#include "controls.h"
#include "keyconfig.h"
#include "options.h"
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

/* Port extra: the menu's lines, chosen with the arrows / d-pad as well as by their letters.
 * MARSn.CF2 is the same picture in every volume: line k is lettered in the 13 rows from
 * LINE_ROW(k), magenta with a cyan capital. */
enum { M_JOURNEY, M_INSTRUCTIONS, M_STORY, M_OPTIONS, M_CONFIG, M_HIGH, M_QUIT, M_ITEMS };
#define LINE_ROW(k) (78 + 14 * (k))

static int pic_px(const pcx_image_t *p, int x, int y)
{
    return (p->pixels[y * CGA_STRIDE + x / 4] >> (6 - 2 * (x & 3))) & 3;
}

static void pic_set(pcx_image_t *p, int x, int y, int c)
{
    uint8_t *b = &p->pixels[y * CGA_STRIDE + x / 4];
    const int shift = 6 - 2 * (x & 3);
    *b = (uint8_t)((*b & ~(3 << shift)) | c << shift);
}

/* "Ordering Info." becomes "Options" (the ordering info still shows on quitting), lettered
 * from the picture's own: its O, the b of "Keyboard" upside down for the p (the picture has
 * no p), and the "tions" of "Instructions", centred on the line as the others are. */
static void letter_options(pcx_image_t *p)
{
    static pcx_image_t src;
    src = *p;
    const int y0 = LINE_ROW(M_OPTIONS), x0 = 123;
    for (int y = y0; y < y0 + 13; y++)
        for (int x = 0; x < CGA_W; x++)
            pic_set(p, x, y, 0);
    for (int y = 0; y < 13; y++) {
        for (int x = 0; x < 12; x++) /* O, from x 92 */
            pic_set(p, x0 + x, y0 + y, pic_px(&src, 92 + x, y0 + y));
        for (int x = 0; x < 44; x++) /* tions, from x 170, after the p and a 2-pixel gap */
            pic_set(p, x0 + 24 + x, y0 + y, pic_px(&src, 170 + x, LINE_ROW(M_INSTRUCTIONS) + y));
    }
    for (int y = 0; y < 10; y++) /* p: the b (x 180, rows 0-9) into rows 12-3 */
        for (int x = 0; x < 9; x++)
            pic_set(p, x0 + 13 + x, y0 + 12 - y, pic_px(&src, 180 + x, LINE_ROW(M_CONFIG) + y));
}

/* The chosen line in white instead of magenta. */
static void highlight(int k)
{
    for (int y = LINE_ROW(k) - 1; y < LINE_ROW(k) + 13; y++)
        for (int x = 0; x < CGA_W; x++)
            if (cga_get_in(cga_vram, x, y) == 2)
                cga_set_in(cga_vram, x, y, 3);
    plat_dirty();
}

/* Options' keys from the BIOS buffer (the controller's d-pad types arrows meanwhile). */
static int options_key(void)
{
    const int c = plat_getch();
    if (c == 0)
        return plat_getch();
    return c == '\r' || c == ' ' ? 0x1C : c == 27 ? 0x01 : 0;
}

int main_menu(const gamedata_t *g, char *err, size_t errlen)
{
    static pcx_image_t pic;
    char path[1100];
    gamedata_path(g, "CF2", path, sizeof path);
    if (pcx_load(path, &pic, err, errlen) != 0)
        return -1;
    letter_options(&pic);
    effects_set_granularity(8);
    effects_set_delay(1);
    keyconfig_load(g);
    int sel = M_JOURNEY;
    while (!plat_quitting()) {
        cga_set_mode(1, 1); /* SetVideoModePal1Bright(4) */
        show_menu_picture(&pic);
        while (plat_kbhit() && !plat_quitting())
            plat_getch();
        int act = -1;
        while (act < 0 && !plat_quitting()) {
            highlight(sel);
            pad_set_menu(true);
            const int c = plat_getch(); /* [0761] */
            pad_set_menu(false);
            switch (c) {
            case 0: { /* an extended key: up / down choose */
                const int sc = plat_getch();
                if (sc == 0x48 || sc == 0x50) {
                    sel = (sel + (sc == 0x48 ? M_ITEMS - 1 : 1)) % M_ITEMS;
                    pcx_show(&pic);
                }
                break;
            }
            case '\r': case ' ': act = sel; break;
            case 'J': case 'j': act = M_JOURNEY; break;
            case 'I': case 'i': act = M_INSTRUCTIONS; break;
            case 'S': case 's': act = M_STORY; break;
            case 'O': case 'o': act = M_OPTIONS; break;
            case 'C': case 'c': act = M_CONFIG; break;
            case 'H': case 'h': act = M_HIGH; break;
            case 27: case 'q': case 'Q': act = M_QUIT; break;
            case '>':
                if (plat_caps_lock()) /* the cheat: 20 shots */
                    return 20;
                break;
            }
            if (act == M_OPTIONS) { /* over the picture, which then comes back as it was */
                bool physics_changed = false;
                sel = act;
                text_set_high_font(g, FONT_NORMAL);
                pad_set_menu(true);
                options_menu(options_key, &physics_changed);
                pad_set_menu(false);
                pcx_show(&pic);
                act = -1;
            }
        }
        if (plat_quitting())
            break;
        sel = act; /* the line stays chosen when the menu comes back */
        switch (act) {
        case M_QUIT:
            plat_nosound();
            screen_ordering(g);
            /* the game then prints a shareware notice in text mode: left out (full version) */
            return 0;
        case M_JOURNEY:
            return 5;
        case M_STORY:
            screen_story(g);
            break;
        case M_INSTRUCTIONS:
            screen_instructions(g);
            break;
        case M_CONFIG:
            controls_screen(g);
            break;
        case M_HIGH:
            hiscore_menu(g);
            break;
        }
    }
    return 0;
}
