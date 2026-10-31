/* CGA 320x200 4-colour video memory (mode 4), kept in the real CGA layout.
 *
 * 2 bits per pixel, 4 pixels per byte, leftmost pixel in the top bits, 80 bytes per scanline.
 * Even scanlines are in bank 0 (offset 0000h), odd ones in bank 1 (offset 2000h), so scanline
 * y starts at (y & 1) * 2000h + (y >> 1) * 80.
 *
 * The game's blitters and its collision checks work on byte offsets in this layout (and its
 * two back buffers use the same layout), so the port keeps it rather than a linear buffer.
 * Only presenting the frame turns it into pixels. */
#ifndef MARS_CGA_H
#define MARS_CGA_H

#include <stdint.h>

#define CGA_W 320
#define CGA_H 200
#define CGA_STRIDE 80
#define CGA_BANK 0x2000
#define CGA_VRAM_SIZE 0x4000

typedef struct { uint8_t r, g, b; } rgb_t;

extern uint8_t cga_vram[CGA_VRAM_SIZE]; /* B800:0000 */

static inline int cga_offset(int x, int y)
{
    return (y & 1) * CGA_BANK + (y >> 1) * CGA_STRIDE + (x >> 2);
}

/* Raw pixel access on any buffer in CGA layout; callers keep x, y on screen. */
static inline int cga_get_in(const uint8_t *buf, int x, int y)
{
    return (buf[cga_offset(x, y)] >> (6 - 2 * (x & 3))) & 3;
}

static inline void cga_set_in(uint8_t *buf, int x, int y, int c)
{
    uint8_t *p = &buf[cga_offset(x, y)];
    const int shift = 6 - 2 * (x & 3);
    *p = (uint8_t)((*p & ~(3 << shift)) | ((c & 3) << shift));
}

void cga_clear(void);

/* palette 0 = green/red/brown (the game's), 1 = cyan/magenta/white; bright = intensity bit. */
const rgb_t *cga_palette(int palette, int bright);

/* The palette the game has selected (SetVideoMode / SetVideoModePal1Bright). */
extern int cga_cur_palette, cga_cur_bright;

/* SetVideoMode (1054:0a36): mode 4, black background, palette 0 low intensity, and
 * SetVideoModePal1Bright (1054:0aa7): the same with palette 1 bright. Clears the screen.
 * A palette override (--palette / --bright) replaces the game's choice when >= 0. */
void cga_set_mode(int palette, int bright);
extern int cga_palette_override, cga_bright_override;

/* The background colour, 0..15 (INT 10h AH=0Bh BH=0): colour 0 of the palette. The game
 * flashes it when things happen (FlashBackground and friends). */
extern int cga_cur_background;

/* The four colours on screen now: the current palette with the background colour. */
const rgb_t *cga_cur_colors(void);

/* Expand video memory to 0xAARRGGBB pixels; pitch is in pixels. */
void cga_to_argb(uint32_t *out, int pitch, const rgb_t *pal);
/* ... or any buffer in the same layout. */
void cga_buf_to_argb(const uint8_t *buf, uint32_t *out, int pitch, const rgb_t *pal);

/* Write video memory as an RGB PNG, each pixel scaled up `scale` times. Returns 0 on success. */
int cga_write_png(const char *path, const rgb_t *pal, int scale);

#endif
