/* Text on the graphics screen, as DrawText (1054:0028) does it through the BIOS.
 *
 * DrawText maps letters and digits to characters 80h-FFh, which come from the game's own font
 * (INT 1Fh points at DS:0006, or DS:01A6 for the alternate set). Space and punctuation stay
 * below 80h and so come from the BIOS ROM font; the port uses its own font8x8 for those. */
#ifndef MARS_TEXT_H
#define MARS_TEXT_H

#include "gamedata.h"

/* SetHighFont (1054:0001): FONT_NORMAL or FONT_ALT. */
void text_set_high_font(const gamedata_t *g, unsigned ds_off);

/* DrawText(color, s, row, col): BIOS INT 10h AH=09h per character in mode 4. color bits 0-1
 * are the ink, bit 7 XORs; background pixels are written as colour 0. row / col are 8x8 cells. */
void draw_text(int color, const char *s, int row, int col);

/* Port extra: characters straight from the BIOS-style font, without DrawText's mapping (which
 * has no '>', say): for the pause menu's marker. */
void draw_ascii(int color, const char *s, int row, int col);

/* DrawChar (1054:01d6), what InputName types with: one character, mapped like DrawText except
 * that '_' is kept and '=' and '?' are not (they come out as characters 7Eh and 80h). */
void draw_char(int color, int ch, int row, int col);

/* DrawNumber (1054:0316): the number right-aligned so it starts at col + width - (len + 1). */
void draw_number(int color, int width, long value, int row, int col);

#endif
