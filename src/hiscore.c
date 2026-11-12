#include "hiscore.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gamepad.h"
#include "gfx.h"
#include "platform.h"
#include "text.h"

#define NAME_LEN 30 /* each name has 30 bytes at DS:060A + 30 * i */

/* DS:05A9 (scores, longs) and DS:060A (names). Eleven, not ten: CheckHighScore's shift writes
 * an eleventh entry past the end of both tables (over [05D1] and [0736], unused). */
static unsigned long scores[11];
static char names[11][NAME_LEN];

static void path(const gamedata_t *g, char *out, size_t len)
{
    gamedata_path(g, "HS", out, len);
}

/* The file is read like fscanf(f, "%lu") then fscanf(f, "%[^\n]s") ten times, in text mode.
 * A conversion that doesn't match leaves its entry alone, so an empty name keeps the old one
 * (as in the original, where the tables start out zeroed). */
void hiscore_load(const gamedata_t *g)
{
    char p[1100];
    path(g, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) {
        /* the built-in table: constants in LoadHighScores, names from the EXE */
        static const unsigned long defaults[6] = {110250, 84900, 67350, 46500, 27400, 150};
        const char *s = gamedata_find_exact(g, "ROGER WILCO");
        for (int i = 0; i < 6; i++, s = gamedata_next_string(g, s)) {
            scores[i] = defaults[i];
            snprintf(names[i], NAME_LEN, "%s", s ? s : "");
        }
        return;
    }
    int c = fgetc(f);
#define NEXT() do { c = fgetc(f); if (c == '\r') c = fgetc(f); } while (0)
    if (c == '\r')
        NEXT();
    for (int i = 0; i < 10; i++) {
        while (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f') /* %lu */
            NEXT();
        if (c >= '0' && c <= '9') {
            unsigned long v = 0;
            while (c >= '0' && c <= '9') {
                v = v * 10 + (unsigned long)(c - '0');
                NEXT();
            }
            scores[i] = v & 0xFFFFFFFFUL;
        }
        if (c != EOF && c != '\n') { /* %[^\n] */
            size_t n = 0;
            while (c != EOF && c != '\n') {
                if (n + 1 < NAME_LEN) /* the original doesn't stop at 30 */
                    names[i][n++] = (char)c;
                NEXT();
            }
            names[i][n] = 0;
        }
    }
#undef NEXT
    fclose(f);
}

/* SaveHighScores (1054:766b): fprintf "%lu" then "%s\n", in text mode. */
static void hiscore_save(const gamedata_t *g)
{
    char p[1100];
    path(g, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f)
        return;
    for (int i = 0; i < 10; i++)
        fprintf(f, "%lu%s\r\n", scores[i], names[i]);
    fclose(f);
}

void hiscore_show(const gamedata_t *g)
{
    text_set_high_font(g, FONT_ALT);
    cga_set_mode(0, 0); /* SetVideoMode(4) */
    draw_text(2, "*** HIGH SCORES ***", 1, 10);
    text_set_high_font(g, FONT_NORMAL);
    for (int i = 0; i < 10; i++) {
        const int row = i * 2 + 4;
        draw_number(2, 2, i + 1, row, 4);
        draw_text(2, ".", row, 5);
        draw_number(1, 8, (long)scores[i], row, 7);
        draw_text(3, names[i], row, 0x10);
    }
    draw_text(2, "PRESS ANY KEY TO CONTINUE.", 0x18, 7);
    plat_dirty();
}

/* The player walking on the spot at both sides until a key is pressed (not taken). */
static void walk_until_key(const gamedata_t *g)
{
    while (!plat_key_pressed())
        for (int i = 0; i < 4; i++) {
            blit_tile_screen(tile_ptr(g, 0x1B + i), 4, 1);    /* facing right */
            blit_tile_screen(tile_ptr(g, 0x21 + i), 4, 0x4C); /* facing left */
            plat_dirty();
            plat_delay(70);
        }
}

void hiscore_menu(const gamedata_t *g)
{
    hiscore_show(g);
    draw_text(2, "PRESS ANY KEY TO CONTINUE.", 0x18, 7);
    walk_until_key(g);
    plat_getch();
}

/* InputName (1054:057a): type up to `max` characters at (row, col) with DrawChar, '_' as the
 * cursor. Letters, digits, space . ! ? are taken (upper-cased); Backspace deletes; Enter ends.
 * An extended key gives 0 then its scancode, which is read as a character: the arrows type
 * H, K, M, P, as in the original. */
static void input_name(int col, int row, int max, char *buf, int color)
{
    int len = 0, c;
    for (;;) {
        if (len + 1 <= max)
            draw_char(color, '_', row, col + len);
        plat_dirty();
        do
            c = plat_getch();
        while (c == 0);
        if (plat_quitting())
            break;
        if (c == 8) {
            if (len < max)
                draw_char(color, ' ', row, col + len);
            if (len)
                len--;
            continue;
        }
        if (len >= max && c != '\r')
            continue;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ' || c == '.' ||
            c == '!' || c == '?' || (c >= '0' && c <= '9')) {
            buf[len] = (char)toupper(c);
            draw_char(color, toupper(c), row, col + len);
            len++;
            continue;
        }
        if (c == '\r')
            break;
    }
    buf[len] = 0;
}

/* The handheld's (port extra; it has no keyboard): the d-pad's up and down pick the character
 * under the cursor, shown in its place (none is the '_' cursor), right takes it and moves on,
 * left or B goes back to the one before, A or Start ends, with the one picked if any. */
static void input_name_pad(int col, int row, int max, char *buf, int color)
{
    static const char set[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.!?";
    const int n_set = (int)sizeof set - 1;
    int len = 0, pick = 0;
    pad_set_menu(true); /* the d-pad types arrow keys */
    for (;;) {
        if (len < max)
            draw_char(color, pick ? set[pick] : '_', row, col + len);
        plat_dirty();
        const int c = plat_getch();
        if (plat_quitting())
            break;
        const int ext = c == 0 ? plat_getch() : 0;
        if (ext == 0x48 || ext == 0x50) { /* up / down */
            pick = (pick + (ext == 0x48 ? 1 : n_set - 1)) % n_set;
        } else if (ext == 0x4D) { /* right */
            if (len < max) {
                buf[len] = set[pick];
                draw_char(color, set[pick], row, col + len);
                len++;
                pick = 0;
            }
        } else if (ext == 0x4B || c == 27) { /* left, B */
            if (len < max)
                draw_char(color, ' ', row, col + len);
            pick = 0;
            if (len) {
                const char *at = strchr(set, buf[--len]);
                pick = at ? (int)(at - set) : 0;
            }
        } else if (c == '\r') {
            if (pick && len < max)
                buf[len++] = set[pick];
            break;
        }
    }
    pad_set_menu(false);
    while (len && buf[len - 1] == ' ')
        len--;
    buf[len] = 0;
}

int hiscore_check(const gamedata_t *g, unsigned long score)
{
    plat_isr_installed = false; /* setvect(9, the BIOS handler) */
    while (plat_kbhit() && !plat_quitting())
        plat_getch();
    int i = 0;
    while (i < 10 && scores[i] >= score)
        i++;
    if (i == 10)
        return 0;
    for (int j = 10; j > i; j--) {
        scores[j] = scores[j - 1];
        memcpy(names[j], names[j - 1], NAME_LEN);
    }
    names[i][0] = 0;
    scores[i] = score;
    hiscore_show(g);
    text_set_high_font(g, FONT_NORMAL);
    draw_text(1, "NEW HIGH SCORE!!", 0, 0x0C);
    if (PLAT_HANDHELD)
        input_name_pad(0x10, i * 2 + 4, 0x14, names[i], 1);
    else
        input_name(0x10, i * 2 + 4, 0x14, names[i], 1);
    hiscore_show(g);
    draw_text(2, "PRESS ANY KEY TO CONTINUE.", 0x18, 7);
    walk_until_key(g);
    hiscore_save(g);
    return 1;
}
