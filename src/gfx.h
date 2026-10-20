/* The game's 16x16 tile blitters and its two back buffers.
 *
 * A tile is 128 bytes: 8 even scanlines then 8 odd ones, each 8 bytes of alternating
 * mask / pixel bytes (see docs/notes.md). Every blitter takes (tile, row, col) where row is a
 * scanline pair (0..5Fh, steps of 8 per tile) and col a byte column (0..4Fh, 4 per tile),
 * so the destination is base + row * 80 + col, with the odd scanlines at +2000h.
 *
 * The back buffers use the same layout as video memory. Back buffer 1 (DS:4983) holds the
 * level's static picture; back buffer 2 (DS:8A26) is the frame being built: every frame
 * starts as a copy of buffer 1, gets the moving things drawn into it, and is copied to the
 * screen below the status line. */
#ifndef MARS_GFX_H
#define MARS_GFX_H

#include <stdint.h>

#include "cga.h"

extern uint8_t back1[CGA_VRAM_SIZE]; /* DS:4983 */
extern uint8_t back2[CGA_VRAM_SIZE]; /* DS:8A26 */

/* BlitMasked_Buf2_Collide's samples (DS:492A-4931), taken before drawing; true = the byte
 * there isn't black. */
typedef struct {
    uint8_t wall_left, wall_right;   /* 492A / 492B: row +5, col +0 / +3 */
    uint8_t ground_l, ground_r;      /* 492C / 492D: row +8, col +1 / +2 */
    uint8_t sunk_l, sunk_r;          /* 492E / 492F: row +7, col +1 / +2 */
    uint8_t ceiling_l, ceiling_r;    /* 4930 / 4931: row -1, col +1 / +2 */
} collide_t;
extern collide_t collide;

/* The bytes the moving things' blitters sample before drawing (DS:075B-0760), raw. The game
 * has one near-copy of BlitMasked_Buf2 per kind of thing, each sampling its own spots; the
 * updaters then compare the bytes with 0, or with FFh (four pixels of colour 3). */
extern uint8_t probe[6];

/* A byte of the game's data segment where the back buffers live: back buffer 1 at DS:4983,
 * back buffer 2 at DS:8A26, each 3E80h bytes (the 15999 bytes CopyBackground copies, rounded
 * up). Samples above the top of buffer 2 land in buffer 1, as in the original; anything else
 * reads as 0 (the original would read whatever variable is there). */
uint8_t ds_peek(long ds_off);
#define DS_BACK1 0x4983L
#define DS_BACK2 0x8A26L

/* Opaque: copy the pixel bytes, ignore the mask. */
void blit_tile_buf1(const uint8_t *tile, int row, int col);   /* BlitTile_Buf1, 1054:18a4 */
void blit_tile_buf2(const uint8_t *tile, int row, int col);   /* BlitTile_Buf2, 1054:1970 */
void blit_tile_screen(const uint8_t *tile, int row, int col); /* BlitTile_Screen, 1054:1b72 */

/* Masked: dst = (dst & mask) | pixels. */
void blit_masked_buf2(const uint8_t *tile, int row, int col);         /* 1054:1c3f */
void blit_masked_buf2_collide(const uint8_t *tile, int row, int col); /* 1054:1ebb */

/* The sampling near-copies of BlitMasked_Buf2 (dst = (dst & mask) | pixels into buffer 2):
 * offsets are from the tile's top-left byte, 80 per row of the buffer (a scanline pair). */
void blit_drip(const uint8_t *tile, int row, int col);       /* 1054:2092 -> probe[0..1] */
void blit_boulder(const uint8_t *tile, int row, int col);    /* 1054:21e8 -> probe[0..3] */
void blit_vplat(const uint8_t *tile, int row, int col);      /* 1054:2354 -> probe[0..2] */
void blit_enemy49(const uint8_t *tile, int row, int col);    /* 1054:24ba -> probe[0..3] */
void blit_enemy4a(const uint8_t *tile, int row, int col);    /* 1054:262f -> probe[0..3] */
void blit_enemy4b(const uint8_t *tile, int row, int col);    /* 1054:279a -> probe[0..3] */
void blit_hplat(const uint8_t *tile, int row, int col);      /* 1054:2907 -> probe[4..5] */

void copy_background(void); /* CopyBackground, 1054:3cc5 */
void present_frame(void);   /* PresentFrame, 1054:3ce3 */

#endif
