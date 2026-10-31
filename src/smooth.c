#include "smooth.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"

int smooth_id;

/* The playfield's part of video memory: PresentFrame's two copies (gfx.c). */
#define PF_OFF 0x140
#define PF_LEN 0x1E78

#define MAX_OPS 96
#define MAX_OBJS 64
#define HIST 6           /* frames the average speed is taken over */
#define LAG_FRAMES 2.0   /* catch up once further behind than this many frames' movement */
#define CATCH_UP 2.0     /* ... at this many px per frame for each px past that */
#define SNAP_X 16
#define SNAP_Y 24
#define TICK_HZ (1193182.0 / 65536.0)

typedef struct {
    const uint8_t *tile;
    int row, col, id;
    uint64_t cut; /* the tile's bytes an opaque tile drawn later replaced (smooth_sprite_t) */
} op_t;

/* The frame being built: buffer 2 without its masked tiles, and the tiles. */
static uint8_t building_bg[CGA_VRAM_SIZE];
static op_t building[MAX_OPS];
static int n_building;

/* The last frame presented. */
static uint8_t shown_bg[CGA_VRAM_SIZE];   /* video memory with the playfield's tiles taken out */
static uint8_t shown_real[CGA_VRAM_SIZE]; /* video memory as PresentFrame left it */
static op_t shown[MAX_OPS];
static int n_shown;
static bool have_frame;

typedef struct {
    int id;
    float tx, ty;           /* where the game has it (screen px) */
    float sx, sy;           /* where it's shown */
    float hx[HIST + 1], hy[HIST + 1]; /* the game's positions, oldest first */
    int nh;
    bool seen;
} obj_t;

static obj_t objs[MAX_OBJS];
static int n_objs;
static double last_now;

void smooth_reset(void)
{
    n_objs = 0;
}

void smooth_frame_begin(void)
{
    /* CopyBackground has just copied the first 15999 bytes; the rest of buffer 2 stays */
    memcpy(building_bg, back2, CGA_VRAM_SIZE);
    n_building = 0;
    smooth_id = 0;
}

/* dst = (dst & mask) | pixels, as gfx.c's blit_masked (callers checked the range), leaving
 * out the bytes in `cut`. */
static void blit_masked(uint8_t *base, const uint8_t *t, int row, int col, uint64_t cut)
{
    uint8_t *d = base + row * CGA_STRIDE + col;
    for (int i = 0; i < 8; i++, t += 8, d += CGA_STRIDE)
        for (int b = 0; b < 4; b++) {
            if (!(cut >> (8 * i + b) & 1))
                d[b] = (uint8_t)((t[2 * b] & d[b]) | t[2 * b + 1]);
            if (!(cut >> (8 * i + 4 + b) & 1))
                d[CGA_BANK + b] = (uint8_t)((t[0x40 + 2 * b] & d[CGA_BANK + b]) | t[0x41 + 2 * b]);
        }
}

void smooth_opaque(const uint8_t *t, int row, int col)
{
    uint8_t *d = building_bg + row * CGA_STRIDE + col;
    for (int i = 0; i < 8; i++, t += 8, d += CGA_STRIDE)
        for (int b = 0; b < 4; b++) {
            d[b] = t[2 * b + 1];
            d[CGA_BANK + b] = t[0x41 + 2 * b];
        }
    /* It replaces what masked tiles drawn before showed there: UpdateBoulders erases each
     * boulder to move it (and the next one's erasing can clip it again), a pickup is erased
     * next to the player. Those tiles lose the bytes it covers; a whole one goes. */
    int n = 0;
    for (int k = 0; k < n_building; k++) {
        op_t *o = &building[k];
        /* by address: a tile at column 4Dh..4Fh runs on into the next row, as in the game */
        for (int i = 0; i < 8; i++)
            for (int b = 0; b < 4; b++) {
                const int off = (o->row + i - row) * CGA_STRIDE + o->col + b - col;
                if (off >= 0 && off % CGA_STRIDE < 4 && off / CGA_STRIDE < 8)
                    o->cut |= (uint64_t)0x11 << (8 * i + b);
            }
        if (o->cut != ~(uint64_t)0)
            building[n++] = *o;
    }
    n_building = n;
}

static bool transparent(const uint8_t *t)
{
    for (int i = 0; i < 128; i += 2)
        if (t[i] != 0xFF || t[i + 1] != 0)
            return false;
    return true;
}

void smooth_masked(const uint8_t *tile, int row, int col)
{
    const int id = smooth_id;
    smooth_id = 0;
    if (n_building < MAX_OPS && !transparent(tile)) /* tile 0: drawn only for its samples */
        building[n_building++] = (op_t){tile, row, col, id, 0};
}

static obj_t *find_obj(int id)
{
    for (int i = 0; i < n_objs; i++)
        if (objs[i].id == id)
            return &objs[i];
    return NULL;
}

void smooth_present(void)
{
    memcpy(shown_real, cga_vram, CGA_VRAM_SIZE);
    memcpy(shown_bg, cga_vram, CGA_VRAM_SIZE);
    memcpy(shown_bg + PF_OFF, building_bg, PF_LEN);
    memcpy(shown_bg + CGA_BANK + PF_OFF, building_bg + CGA_BANK, PF_LEN);
    memcpy(shown, building, sizeof building[0] * (size_t)n_building);
    n_shown = n_building;
    have_frame = true;

    for (int i = 0; i < n_objs; i++)
        objs[i].seen = false;
    for (int i = 0; i < n_shown; i++) {
        const op_t *o = &shown[i];
        if (!o->id)
            continue;
        const float x = (float)(o->col * 4), y = (float)(o->row * 2 + SMOOTH_PLAYFIELD_Y);
        obj_t *b = find_obj(o->id);
        if (b && fabsf(x - b->tx) <= SNAP_X && fabsf(y - b->ty) <= SNAP_Y && !b->seen) {
            if (b->nh == HIST + 1) {
                memmove(b->hx, b->hx + 1, sizeof b->hx[0] * HIST);
                memmove(b->hy, b->hy + 1, sizeof b->hy[0] * HIST);
                b->nh--;
            }
        } else {
            if (!b) {
                if (n_objs == MAX_OBJS)
                    continue;
                b = &objs[n_objs++];
            }
            b->id = o->id;
            b->sx = x;
            b->sy = y;
            b->nh = 0;
        }
        b->tx = x;
        b->ty = y;
        b->hx[b->nh] = x;
        b->hy[b->nh] = y;
        b->nh++;
        b->seen = true;
    }
    /* Things not drawn this frame are gone (a drip that landed, a shot that hit). */
    int n = 0;
    for (int i = 0; i < n_objs; i++)
        if (objs[i].seen)
            objs[n++] = objs[i];
    n_objs = n;
}

/* One axis of an object: move `shown` towards `target` for dt seconds. */
static float chase(float shown, float target, const float *hist, int nh, double dt)
{
    float moved = 0;
    for (int i = 1; i < nh; i++)
        moved += fabsf(hist[i] - hist[i - 1]);
    const float avg = moved / HIST; /* px per frame; a fresh start counts as standing */
    const float d = target - shown, dist = fabsf(d);
    float speed = avg;
    if (dist > avg * LAG_FRAMES)
        speed += (float)CATCH_UP * (dist - avg * (float)LAG_FRAMES);
    const float step = (float)(speed * TICK_HZ * dt);
    if (step >= dist)
        return target;
    return shown + (d > 0 ? step : -step);
}

bool smooth_frame(double now, uint8_t vram[CGA_VRAM_SIZE], const smooth_sprite_t **sprites, int *n)
{
    static smooth_sprite_t out[MAX_OPS];
    double dt = now - last_now;
    last_now = now;
    if (dt < 0 || dt > 0.1)
        dt = dt < 0 ? 0 : 0.1;
    if (!have_frame || memcmp(cga_vram + PF_OFF, shown_real + PF_OFF, PF_LEN) != 0 ||
        memcmp(cga_vram + CGA_BANK + PF_OFF, shown_real + CGA_BANK + PF_OFF, PF_LEN) != 0)
        return false;

    for (int i = 0; i < n_objs; i++) {
        obj_t *b = &objs[i];
        b->sx = chase(b->sx, b->tx, b->hx, b->nh, dt);
        b->sy = chase(b->sy, b->ty, b->hy, b->nh, dt);
    }
    /* The status line and anything else outside the playfield come from video memory now. */
    memcpy(vram, cga_vram, CGA_VRAM_SIZE);
    memcpy(vram + PF_OFF, shown_bg + PF_OFF, PF_LEN);
    memcpy(vram + CGA_BANK + PF_OFF, shown_bg + CGA_BANK + PF_OFF, PF_LEN);
    for (int i = 0; i < n_shown; i++) {
        const op_t *o = &shown[i];
        const obj_t *b = o->id ? find_obj(o->id) : NULL;
        out[i].tile = o->tile;
        out[i].cut = o->cut;
        out[i].id = o->id;
        out[i].x = b ? b->sx : (float)(o->col * 4);
        out[i].y = b ? b->sy : (float)(o->row * 2 + SMOOTH_PLAYFIELD_Y);
    }
    *sprites = out;
    *n = n_shown;
    return true;
}

void smooth_tile_argb(const uint8_t *t, uint64_t cut, const rgb_t *pal, uint32_t out[16 * 16])
{
    for (int y = 0; y < 16; y++) {
        const uint8_t *row = t + (y & 1) * 0x40 + (y >> 1) * 8;
        for (int x = 0; x < 16; x++) {
            if (cut >> ((y >> 1) * 8 + (y & 1) * 4 + (x >> 2)) & 1) {
                out[y * 16 + x] = 0;
                continue;
            }
            const int shift = 6 - 2 * (x & 3);
            const int mask = (row[2 * (x >> 2)] >> shift) & 3, pix = (row[2 * (x >> 2) + 1] >> shift) & 3;
            const rgb_t c = pal[pix];
            /* (dst & mask) | pix: a pixel that keeps what's under it and adds nothing is see-through */
            out[y * 16 + x] = mask && !pix ? 0
                                           : 0xFF000000u | (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b;
        }
    }
}

int smooth_check(void)
{
    static uint8_t buf[CGA_VRAM_SIZE];
    memcpy(buf, building_bg, CGA_VRAM_SIZE);
    for (int i = 0; i < n_building; i++)
        blit_masked(buf, building[i].tile, building[i].row, building[i].col, building[i].cut);
    int diff = 0;
    for (int i = 0; i < CGA_VRAM_SIZE; i++)
        diff += buf[i] != back2[i];
    return diff;
}
