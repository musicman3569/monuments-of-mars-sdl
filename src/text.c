#include "text.h"

#include <stdio.h>
#include <string.h>

#include "cga.h"
#include "font8x8.h"

static const uint8_t *high_font; /* 128 glyphs of 8 bytes for 80h-FFh */

void text_set_high_font(const gamedata_t *g, unsigned ds_off)
{
    high_font = gamedata_ds(g, ds_off, 128 * 8);
}

/* DrawText's character mapping (1054:0077-0195). */
static int map_char(unsigned char c)
{
    if (c == ' ')
        return 0x20;
    if (c == '0')
        return 0xB3;
    if (c >= '1' && c <= '9')
        return c + 0x79;
    if (strchr("*.-'/,)($:!\"=?", c))
        return c;
    return (c + 0x41) & 0xFF; /* letters: 'A' -> 82h */
}

static void put_char(int ch, int color, int row, int col)
{
    static const uint8_t blank[8];
    const uint8_t *glyph = ch < 0x80 ? font8x8_glyph(ch) : high_font ? high_font + (ch - 0x80) * 8 : blank;
    const int ink = color & 3;
    for (int y = 0; y < 8; y++) {
        const int py = row * 8 + y;
        if (py >= CGA_H)
            break;
        for (int x = 0; x < 8; x++) {
            const int px = col * 8 + x;
            if (px >= CGA_W)
                break;
            const int on = (glyph[y] >> (7 - x)) & 1;
            if (color & 0x80)
                cga_set_in(cga_vram, px, py, cga_get_in(cga_vram, px, py) ^ (on ? ink : 0));
            else
                cga_set_in(cga_vram, px, py, on ? ink : 0);
        }
    }
}

void draw_text(int color, const char *s, int row, int col)
{
    for (; *s; s++, col++) /* BIOS: AH=02h set the cursor, AH=09h write CX=1 character */
        put_char(map_char((unsigned char)*s), color, row, col);
}

void draw_number(int color, int width, long value, int row, int col)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%ld", value);
    draw_text(color, buf, row, col + width - ((int)strlen(buf) + 1));
}
