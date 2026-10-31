#include "gfx.h"

#include <string.h>

#include "smooth.h"

uint8_t back1[CGA_VRAM_SIZE];
uint8_t back2[CGA_VRAM_SIZE];
collide_t collide;
uint8_t probe[6];

/* The status line takes the first 8 scanlines (4 rows of each bank). */
#define PLAYFIELD 0x140

/* The original writes past the end of a buffer only if a caller passes a row or column off
 * the playfield; the port drops such tiles rather than corrupting memory. */
static int in_range(int base_off, int row, int col)
{
    return row >= 0 && col >= 0 &&
           base_off + row * CGA_STRIDE + col + 7 * CGA_STRIDE + 3 + CGA_BANK < CGA_VRAM_SIZE;
}

static void blit_opaque(uint8_t *base, int base_off, const uint8_t *t, int row, int col)
{
    if (!in_range(base_off, row, col))
        return;
    if (base == back2)
        smooth_opaque(t, row, col);
    uint8_t *d = base + row * CGA_STRIDE + col;
    for (int i = 0; i < 8; i++, t += 8, d += CGA_STRIDE) {
        d[0] = t[1];
        d[1] = t[3];
        d[2] = t[5];
        d[3] = t[7];
        d[CGA_BANK + 0] = t[0x41];
        d[CGA_BANK + 1] = t[0x43];
        d[CGA_BANK + 2] = t[0x45];
        d[CGA_BANK + 3] = t[0x47];
    }
}

static void blit_masked(uint8_t *base, const uint8_t *t, int row, int col)
{
    if (!in_range(0, row, col)) {
        smooth_id = 0;
        return;
    }
    smooth_masked(t, row, col); /* every caller draws into buffer 2 */
    uint8_t *d = base + row * CGA_STRIDE + col;
    for (int i = 0; i < 8; i++, t += 8, d += CGA_STRIDE) {
        for (int b = 0; b < 4; b++) {
            d[b] = (uint8_t)((t[2 * b] & d[b]) | t[2 * b + 1]);
            d[CGA_BANK + b] = (uint8_t)((t[0x40 + 2 * b] & d[CGA_BANK + b]) | t[0x41 + 2 * b]);
        }
    }
}

void blit_tile_buf1(const uint8_t *tile, int row, int col)
{
    blit_opaque(back1, 0, tile, row, col);
}

void blit_tile_buf2(const uint8_t *tile, int row, int col)
{
    blit_opaque(back2, 0, tile, row, col);
}

void blit_tile_screen(const uint8_t *tile, int row, int col)
{
    blit_opaque(cga_vram + PLAYFIELD, PLAYFIELD, tile, row, col);
}

void blit_masked_buf2(const uint8_t *tile, int row, int col)
{
    blit_masked(back2, tile, row, col);
}

#define BUF_BYTES 0x3E80

uint8_t ds_peek(long ds_off)
{
    if (ds_off >= DS_BACK2 && ds_off < DS_BACK2 + BUF_BYTES)
        return back2[ds_off - DS_BACK2];
    if (ds_off >= DS_BACK1 && ds_off < DS_BACK1 + BUF_BYTES)
        return back1[ds_off - DS_BACK1];
    return 0;
}

static uint8_t sample(int off)
{
    return ds_peek(DS_BACK2 + off) != 0;
}

/* Sample n bytes of buffer 2 into probe[first..], then draw. */
static void blit_probe(const uint8_t *tile, int row, int col, int first, int n, const int *offs)
{
    const int at = row * CGA_STRIDE + col;
    for (int i = 0; i < n; i++)
        probe[first + i] = ds_peek(DS_BACK2 + at + offs[i]);
    blit_masked(back2, tile, row, col);
}

#define ROW(r) ((r) * CGA_STRIDE)

void blit_drip(const uint8_t *tile, int row, int col)
{
    static const int o[] = {ROW(8) + 1, ROW(8) + 2}; /* under it */
    blit_probe(tile, row, col, 0, 2, o);
}

void blit_boulder(const uint8_t *tile, int row, int col)
{
    static const int o[] = {ROW(8), ROW(8) + 3, ROW(4) - 1, ROW(4) + 4}; /* under; left, right */
    blit_probe(tile, row, col, 0, 4, o);
}

void blit_vplat(const uint8_t *tile, int row, int col)
{
    static const int o[] = {-ROW(14), ROW(11) + 1, ROW(11) + 2}; /* above a rider; under it */
    blit_probe(tile, row, col, 0, 3, o);
}

void blit_enemy49(const uint8_t *tile, int row, int col)
{
    static const int o[] = {-ROW(1) - 1, -ROW(1) + 4, ROW(1) - 1, ROW(1) + 4};
    blit_probe(tile, row, col, 0, 4, o);
}

void blit_enemy4a(const uint8_t *tile, int row, int col)
{
    static const int o[] = {ROW(1), ROW(1) + 3, -1, 4};
    blit_probe(tile, row, col, 0, 4, o);
}

void blit_enemy4b(const uint8_t *tile, int row, int col)
{
    static const int o[] = {ROW(8), ROW(8) + 3, -1, 4};
    blit_probe(tile, row, col, 0, 4, o);
}

void blit_hplat(const uint8_t *tile, int row, int col)
{
    static const int o[] = {ROW(1) - 1, ROW(1) + 4};
    blit_probe(tile, row, col, 4, 2, o);
}

void blit_masked_buf2_collide(const uint8_t *tile, int row, int col)
{
    const int at = row * CGA_STRIDE + col;
    collide.wall_left = sample(at + 5 * CGA_STRIDE);
    collide.wall_right = sample(at + 5 * CGA_STRIDE + 3);
    collide.ground_l = sample(at + 8 * CGA_STRIDE + 1);
    collide.ground_r = sample(at + 8 * CGA_STRIDE + 2);
    collide.sunk_l = sample(at + 7 * CGA_STRIDE + 1);
    collide.sunk_r = sample(at + 7 * CGA_STRIDE + 2);
    collide.ceiling_l = sample(at - CGA_STRIDE + 1);
    collide.ceiling_r = sample(at - CGA_STRIDE + 2);
    blit_masked(back2, tile, row, col);
}

void copy_background(void)
{
    memcpy(back2, back1, 15999);
    smooth_frame_begin();
}

void present_frame(void)
{
    memcpy(cga_vram + PLAYFIELD, back2, 0x1E78);
    memcpy(cga_vram + CGA_BANK + PLAYFIELD, back2 + CGA_BANK, 0x1E78);
    smooth_present();
}
