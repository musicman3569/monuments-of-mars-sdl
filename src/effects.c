#include "effects.h"

#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "platform.h"

/* The library's settings in DGROUP, with their initial values. */
static int fx_type;        /* [0F4A] */
static int fx_gran = 8;    /* [0F4C] */
static int fx_delay;       /* [0F4E] */
static uint16_t rnd_lo, rnd_hi; /* [0F46] / [0F48] */

void effects_set_type(int type)
{
    if (type < 0 || type > 9)
        return;
    fx_type = type;
    if (type == 9) { /* srand(INT 1Ah's tick count, low word) */
        rnd_lo = (uint16_t)plat_ticks();
        rnd_hi = 0;
    }
}

void effects_set_granularity(int g)
{
    if (g >= 1 && g <= 64)
        fx_gran = g;
}

void effects_set_delay(int units)
{
    if (units >= 0 && units <= 500)
        fx_delay = units;
}

/* 1d22:002f random(n): 0..n. The multiply mixes up the halves of the 32-bit seed; kept as is. */
static int fx_random(int n)
{
    const uint32_t a = 0xDADAu * rnd_lo, b = 0xDADAu * rnd_hi;
    const uint16_t cx = (uint16_t)(0x0Cu * rnd_hi + (a & 0xFFFF));
    const uint32_t lo = (b & 0xFFFF) + 0xFEED;
    rnd_lo = (uint16_t)lo;
    rnd_hi = (uint16_t)((b >> 16) + cx + 0x0C + (lo >> 16));
    return (int)(((uint32_t)(rnd_hi & 0x7FFF) * (uint32_t)n) / 0x7FFF);
}

/* ------------------------------------------------------------------ drawing ---- */

static const pcx_image_t *pic;
static long drawn, blits; /* pixels and blits since the last step, for pacing */

/* 1a0d:0008, CGA version 1a0d:036e: copy the picture from (sx, sy) into the screen rectangle
 * x1..x2, y1..y2. It works in whole bytes: the columns become sx / 4 and x1 / 4 and the width
 * (x2 - x1 + 1) / 4, rounded down, so a strip "g + 1" pixels wide is g wide and a start at
 * x = 1 is x = 0. It doesn't clip; rows past the bottom would land in the invisible part of
 * video memory, so the port just skips them. */
static void blit(int sx, int sy, int x1, int y1, int x2, int y2)
{
    const int src = sx >> 2, dst = x1 >> 2, n = (x2 - x1 + 1) >> 2;
    for (int y = y1; y <= y2; y++) {
        const int py = sy + (y - y1);
        if (y < 0 || y >= CGA_H || py < 0 || py >= CGA_H)
            continue;
        for (int b = 0; b < n; b++) {
            if (dst + b < 0 || dst + b >= CGA_STRIDE || src + b >= CGA_STRIDE)
                continue;
            cga_vram[cga_offset((dst + b) * 4, y)] = pic->pixels[py * CGA_STRIDE + src + b];
        }
        drawn += n * 4;
    }
    blits++;
}

/* 1d10:0092: the delay after each step: `units` x 1/100 of 18 ticks. The original also took
 * as long as its drawing did, which is what paces the title's delay-0 dissolve. The port
 * charges an estimate of a 286's speed: 0.4 ms per blit (Genus's dispatch, the EMS page
 * check) plus 1 ms per 16,000 pixels copied. The library could stop on Esc, but only when
 * [0F44] is set, which this game never does. */
static void step(void)
{
    plat_dirty();
    const double ms = fx_delay * (18.0 * 65536.0 / 1193182.0 * 1000.0 / 100.0) + drawn / 16000.0 +
                      blits * 0.4;
    drawn = blits = 0;
    static double owed; /* sub-millisecond waits add up */
    owed += ms;
    if (owed >= 1.0) {
        plat_delay((int)owed);
        owed -= (int)owed;
    }
}

/* ------------------------------------------------------------------ the effects ---- */

#define W CGA_W
#define H CGA_H
#define R (CGA_W - 1) /* [bp-4]: right edge */
#define B (CGA_H - 1) /* [bp-6]: bottom edge */

static int imin(int a, int b)
{
    return a < b ? a : b;
}

/* Type 0 (1c34:000a): strips one after the other: 8 top down, 4 bottom up, 2 left to right,
 * 1 right to left. */
static void wipe(int v, int gr, int gc)
{
    if (v == 2) {
        for (int si = 0;; si += gc) {
            const int dx = imin(si, W - gc);
            blit(dx, 0, dx, 0, dx + gc, B);
            step();
            if (si >= W)
                break;
        }
    } else if (v == 1) {
        for (int si = gc;; si += gc) {
            si = imin(si, W);
            blit(W - si, 0, R + 1 - si, 0, R + 1 - si + gc, B);
            step();
            if (si >= W)
                break;
        }
    } else if (v == 4) {
        for (int si = gr + 1;; si += gr) {
            si = imin(si, H);
            blit(0, H - si, 0, B + 1 - si, R, B + 1 - si + gr);
            step();
            if (si >= H)
                break;
        }
    } else {
        for (int si = 0; si < H; si += gr) {
            const int dy = imin(si, H - gr - 1);
            blit(0, dy, 0, dy, R, dy + gr);
            step();
        }
    }
}

/* Type 1 (1c66:0002): from the middle outwards, 2 sideways, otherwise up and down. */
static void split(int v, int gr, int gc)
{
    if (v == 2) {
        for (int si = W / 2, di = W / 2 + gc;; si += gc, di += gc) {
            const int dx = imin(si, W - gc);
            blit(dx, 0, dx, 0, dx + gc, B);
            di = imin(di, W);
            blit(W - di, 0, R + 1 - di, 0, R + 1 - di + gc, B);
            step();
            if (si >= W - gc)
                break;
        }
    } else {
        for (int si = H / 2, di = H / 2 + 1;; di += gr, si += gr) {
            const int dy = imin(si, H - gr - 1);
            blit(0, dy, 0, dy, R, dy + gr);
            di = imin(di, H);
            blit(0, H - di, 0, B + 1 - di, R, B + 1 - di + gr);
            step();
            if (di >= H)
                break;
        }
    }
}

/* Type 3 (1c4d:000e): the picture slides in: 8 down from the top, 4 up from the bottom,
 * 2 in from the left, 1 in from the right. */
static void slide(int v, int gr, int gc)
{
    if (v == 4) {
        for (int si = gr;; si += gr) {
            const int dy = si < H ? si : H - 1;
            blit(0, 0, 0, B - dy, R, B);
            step();
            if (si >= H)
                break;
        }
    } else if (v == 2) {
        for (int si = gc;; si += gc) {
            si = imin(si, W);
            blit(W - si, 0, 0, 0, si - 1, B);
            step();
            if (si >= W)
                break;
        }
    } else if (v == 1) {
        for (int si = gc;; si += gc) {
            const int dx = si < W ? si : W - 1;
            blit(0, 0, R + 1 - dx, 0, R + 1, B);
            step();
            if (si >= W)
                break;
        }
    } else {
        for (int si = gr;; si += gr) {
            si = imin(si, H);
            blit(0, H - si, 0, 0, R, si - 1);
            step();
            if (si >= H)
                break;
        }
    }
}

/* Type 6 (1ca5:000e): column and row strips together, a cross growing from the middle; with
 * variant 2, closing in from all four edges instead. The routine keeps the column and the row
 * state on the stack and swaps them; here they're two structs. */
typedef struct { int si, di, lim; } band_t;

static void cross(int v, int gc)
{
    band_t c, r;
    const int inward = v == 2;
    if (inward) {
        r = (band_t){0, gc + 1, H - gc - 1};
        c = (band_t){0, gc, W / 2 - gc};
    } else {
        r = (band_t){H / 2, H / 2 + 1, H - gc - 1};
        c = (band_t){W / 2, W / 2 + gc, W - gc};
    }
    for (;;) {
        const int dx = imin(c.si, c.lim);
        blit(dx, 0, dx, 0, dx + gc, B);
        c.di = imin(c.di, W);
        blit(W - c.di, 0, R + 1 - c.di, 0, R + 1 - c.di + gc, B);
        const int dy = imin(r.si, r.lim);
        blit(0, dy, 0, dy, R, dy + gc);
        r.di = imin(r.di, H);
        blit(0, H - r.di, 0, B + 1 - r.di, R, B + 1 - r.di + gc);
        step();
        if (inward) {
            r.di += gc;
            r.si += gc;
            if (r.si >= H / 2) {
                r.di -= gc;
                r.si -= gc;
            }
        } else if (r.di < H) {
            r.di += gc;
            r.si += gc;
        }
        if (c.si >= c.lim)
            break;
        c.si += gc;
        c.di += gc;
    }
}

/* The cell grid (1cd0:000c) and one cell (1cd0:0039): g x g cells, the last row and column
 * pushed back inside the picture. */
typedef struct { int g, last_col, last_row; } grid_t;

static grid_t make_grid(int g)
{
    grid_t gr = {g, W / g - (W % g == 0), H / g - (H % g == 0)};
    return gr;
}

static void cell(const grid_t *gr, int col, int row)
{
    const int sx = imin(col * gr->g, W - gr->g), sy = imin(row * gr->g, H - gr->g);
    blit(sx, sy, sx, sy, sx + gr->g - 1, sy + gr->g - 1);
}

/* Type 7 (1cd0:0080): diagonals of cells, one per step, from a corner: 10 top left, 5 bottom
 * right, 6 bottom left, 9 top right. It runs (W + H) / g + 1 steps, a few more than there are
 * diagonals; the extra ones redraw edge cells. */
static void diagonal(int v, int gc)
{
    const grid_t gr = make_grid(gc);
    const int steps = (W + H) / gc + 1, lc = gr.last_col, lr = gr.last_row;
    int k = v == 5 || v == 9 ? lc : 0;
    for (int n = 0; n < steps; n++) {
        int si, di;
        if (v == 6) {
            si = k, di = lr;
            if (si > lc) {
                di -= si - lc;
                si = lc;
            }
            for (;; si--, di--) {
                cell(&gr, si, di);
                if (si == 0 || di == 0)
                    break;
            }
            k++;
        } else if (v == 9) {
            si = k, di = 0;
            if (si < 0) {
                di = -si;
                si = 0;
            }
            for (;; si++, di++) {
                cell(&gr, si, di);
                if (si == lc || di == lr)
                    break;
            }
            k--;
        } else if (v == 5) {
            si = k, di = lr;
            if (si < 0) {
                di += si;
                si = 0;
            }
            for (;; si++, di--) {
                cell(&gr, si, di);
                if (si == lc || di == 0)
                    break;
            }
            k--;
        } else {
            si = k, di = 0;
            if (si > lc) {
                di = si - lc;
                si = lc;
            }
            for (;; si--, di++) {
                cell(&gr, si, di);
                if (si == 0 || di == lr)
                    break;
            }
            k++;
        }
        step();
    }
}

/* Type 9 (1d05:0000): the cells in random order. The grid's cell size doubles until it has
 * fewer than [09BA] = 2048 cells. random(count) can give 0, which counts as 1, so the last
 * free cell in the table is only taken at the very end. A cell's column is shifted left by
 * one, the first going to the end. */
static void dissolve(int gc)
{
    grid_t gr = make_grid(gc);
    while ((gr.last_col + 1) * (gr.last_row + 1) >= 2048)
        gr = make_grid(gr.g * 2);
    const int cols = gr.last_col + 1, n = cols * (gr.last_row + 1);
    unsigned char *taken = calloc((size_t)n, 1);
    if (!taken)
        return;
    for (int count = n; count > 0; count--) {
        int r = fx_random(count);
        if (r == 0)
            r = 1;
        int i = 0;
        for (;; i++)
            if (!taken[i] && --r == 0)
                break;
        taken[i] = 1;
        int col = i % cols - 1;
        if (col < 0)
            col = gr.last_col;
        cell(&gr, col, i / cols);
        step();
    }
    free(taken);
}

void effects_show(const pcx_image_t *img, int variant)
{
    pic = img;
    drawn = blits = 0;
    const int gr = fx_gran;                        /* [bp-0E]: rows */
    const int gc = (fx_gran & 0xF8) ? fx_gran & 0xF8 : 8; /* [bp-10]: columns, whole bytes */
    switch (fx_type) {
    case 0: wipe(variant, gr, gc); break;
    case 1: split(variant, gr, gc); break;
    case 3: slide(variant, gr, gc); break;
    case 6: cross(variant, gc); break;
    case 7: diagonal(variant, gc); break;
    case 9: dissolve(gc); break;
    default: pcx_show(img); break; /* types 2, 4, 5 and 8 aren't used by the game */
    }
    plat_dirty();
}
