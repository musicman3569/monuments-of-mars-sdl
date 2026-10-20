#include "keyconfig.h"

#include <stdio.h>

#include "cga.h"
#include "platform.h"
#include "text.h"

/* [CCDC]: the setup letter as a 1-character string, shown by ConfigKeyboard. */
static char current[2];

void keyconfig_apply(int c)
{
    /* PC/XT scancodes: 1D Ctrl, 38 Alt, 2A left Shift, 36 right Shift, 39 Space, 52 Ins,
     * 53 Del, 43 F9, 44 F10, 4B / 4D the arrows (which the ISR always accepts for walking). */
    static const struct { char c; unsigned char left, right, jump, fire; } setups[] = {
        {'A', 0x1D, 0x38, 0x36, 0x39}, {'B', 0x1D, 0x38, 0x39, 0x36},
        {'C', 0x38, 0x1D, 0x2A, 0x39}, {'D', 0x38, 0x1D, 0x39, 0x2A},
        {'E', 0x52, 0x53, 0x38, 0x39}, {'F', 0x43, 0x44, 0x36, 0x39},
        {'G', 0x4B, 0x4D, 0x1D, 0x38}, {'X', 0x4B, 0x4D, 0x39, 0x2A},
        {'a', 0x1D, 0x38, 0x36, 0x39}, /* the only lower-case letter it takes */
    };
    current[0] = (char)c;
    current[1] = 0;
    for (size_t i = 0; i < sizeof setups / sizeof setups[0]; i++) {
        if (setups[i].c == (char)c) {
            isr_keys.key_left = setups[i].left;   /* [C8EC] */
            isr_keys.key_right = setups[i].right; /* [C92D] */
            isr_keys.key_jump = setups[i].jump;   /* [C8AE] */
            isr_keys.key_fire = setups[i].fire;   /* [8A25] */
            return;
        }
    }
    /* any other letter leaves the keys as they were */
}

void keyconfig_load(const gamedata_t *g)
{
    char path[1100];
    gamedata_path(g, "KBD", path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        keyconfig_apply('D');
        return;
    }
    keyconfig_apply(fgetc(f) & 0xFF); /* an empty file gives EOF: no setup, as in the original */
    fclose(f);
}

/* SaveKeyConfig (1054:6bbe). */
static void keyconfig_save(const gamedata_t *g, int c)
{
    char path[1100];
    gamedata_path(g, "KBD", path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fputc(c, f);
    fclose(f);
}

void keyconfig_screen(const gamedata_t *g)
{
    /* The screen's 18 strings follow each other in the data segment from "CONFIG KEYBOARD";
     * the last one, "SAVING SETUP...", is shown after a choice. */
    static const struct { int color, row, col, alt_font; } at[17] = {
        {2, 0, 12, 0}, {1, 2, 1, 1}, {1, 3, 1, 1}, {3, 5, 1, 1}, {3, 6, 1, 1}, {3, 7, 1, 1},
        {3, 8, 1, 1}, {3, 9, 1, 1}, {3, 10, 1, 1}, {3, 11, 1, 1}, {3, 12, 1, 1}, {3, 13, 1, 1},
        {3, 15, 1, 1}, {3, 16, 1, 1}, {3, 17, 1, 1}, {2, 19, 8, 0}, {1, 21, 5, 0},
    };
    cga_set_mode(0, 0); /* SetVideoMode(4) */
    plat_dirty();
    const char *s = gamedata_find_exact(g, "CONFIG KEYBOARD");
    for (int i = 0; i < 17 && s; i++, s = gamedata_next_string(g, s)) {
        text_set_high_font(g, at[i].alt_font ? FONT_ALT : FONT_NORMAL);
        draw_text(at[i].color, s, at[i].row, at[i].col);
        if (i == 15) { /* "(CURRENT SETTING IS  .)": the letter goes in the gap */
            text_set_high_font(g, FONT_ALT);
            draw_text(1, current, 19, 28);
        }
    }
    text_set_high_font(g, FONT_NORMAL);

    int c;
    for (;;) {
        c = plat_getch(); /* [0761] */
        if (c == 27)
            return;
        if (c >= 'a' && c <= 'g')
            c -= 0x20;
        if (c == 'x')
            c = 'X';
        if ((c >= 'A' && c <= 'G') || c == 'X')
            break;
    }
    keyconfig_apply(c);
    text_set_high_font(g, FONT_ALT);
    if (s)
        draw_text(2, s, 24, 0);
    text_set_high_font(g, FONT_NORMAL);
    keyconfig_save(g, c);
}
