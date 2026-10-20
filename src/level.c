#include "level.h"

#include <string.h>

#include "gfx.h"

static void add(list_t *l, int s, int col, int row)
{
    if (l->n < MAX_LIST) {
        l->r[l->n].s = (uint8_t)s;
        l->r[l->n].col = (uint8_t)col;
        l->r[l->n].row = (uint8_t)row;
        l->n++;
    }
}

/* Cells are visited row by row. For cell i, col is a byte column (4 per tile) and row a
 * scanline pair (8 per tile): exactly the values the game stores in its lists. */
#define CELL_COL(i) ((i) % LEVEL_COLS * 4)
#define CELL_ROW(i) ((i) / LEVEL_COLS * 8)

void level_draw(level_t *lv, const gamedata_t *g, int number)
{
    memset(lv, 0, sizeof *lv);
    lv->number = number;
    const uint8_t *m = g->levels[number];

    /* Pass 1: the vertical platforms start moving (state 1) unless a lever is in the level:
     * 16 (lever on) -> state 0, 17 (lever off) -> state 2 (stopped). */
    int vplat_state = 1;
    for (int i = 0; i < LEVEL_BYTES; i++) {
        const int b = m[i];
        if (b == 0x16)
            vplat_state = 0;
        if (b == 0x17)
            vplat_state = 2;
    }

    /* Pass 2: clear each cell with tile 0, then entities and most scenery. */
    for (int i = 0; i < LEVEL_BYTES; i++) {
        const int b = m[i], col = CELL_COL(i), row = CELL_ROW(i);
        blit_tile_buf1(tile_ptr(g, 0), row, col);
        if (b == 0x6E) {
            add(&lv->boulders, 0, col, row);
        } else if (b == 0x04) {
            add(&lv->vplat, vplat_state, col, row);
        } else if (b == 0x06) {
            add(&lv->hplat, 0, col, row);
        } else if (b == 0x49) {
            add(&lv->enemy49, 0, col, row);
        } else if (b == 0x46) {
            lv->has_turret = 1;
            lv->turret_col = col;
            lv->turret_row = row;
        } else if (b == 0x4A) {
            add(&lv->enemy4a, 0, col, row);
        } else if (b == 0x4B) {
            add(&lv->enemy4b, 0, col, row);
        } else if (b == 0x65) {
            lv->player_col = col;
            lv->player_row = row;
            blit_tile_buf1(tile_ptr(g, 0), row, col);
        } else {
            if (b == 0x5E) /* teleporter target, drawn as tile 01 */
                blit_tile_buf1(tile_ptr(g, 0x01), row, col);
            if (b == 0x55)
                add(&lv->code55, 0, col, row);
            if (b >= 0x56 && b <= 0x59) /* M A R S */
                blit_tile_buf1(tile_ptr(g, b), row, col);
            if (b == 0x6F) {
                lv->has_6f = 1;
                lv->col_6f = col;
                lv->row_6f = row;
                blit_tile_buf1(tile_ptr(g, 0x6F), row, col);
            } else if (b < 0x09 || (b > 0x65 && b < 0x6E) || b > 0x77 || b == 0x55) {
                blit_tile_buf1(tile_ptr(g, b), row, col);
            }
        }
    }

    /* Pass 3: the special tiles (touch effects, animation). The stars (74h-77h) are listed
     * but inactive and never drawn. */
    for (int i = 0; i < LEVEL_BYTES; i++) {
        const int b = m[i], col = CELL_COL(i), row = CELL_ROW(i);
        if ((b > 0x08 && b < 0x47) || (b > 0x55 && b < 0x5A) || b == 0x6E || b == 0x70 ||
            (b > 0x73 && b < 0x78)) {
            if (b != 0x6E && !(b >= 0x74 && b <= 0x77))
                blit_tile_buf1(tile_ptr(g, b), row, col);
            if (lv->special.n < MAX_LIST)
                lv->special_active[lv->special.n] = !(b >= 0x74 && b <= 0x78);
            add(&lv->special, b, col, row);
        }
    }

    /* Pass 4: dripping ceilings, 18 = left end, 19 = right end (s = width in bytes). */
    int drip_left = 0, drip_row = 0;
    for (int i = 0; i < LEVEL_BYTES; i++) {
        const int b = m[i], col = CELL_COL(i), row = CELL_ROW(i);
        if (b == 0x18) {
            drip_left = col;
            drip_row = row;
        } else if (b == 0x19) {
            add(&lv->drips, col - drip_left, drip_left, drip_row);
        }
    }
}

void level_animate_tiles(level_t *lv, const gamedata_t *g)
{
    for (int i = 0; i < lv->special.n; i++) {
        const rec_t *r = &lv->special.r[i];
        if (lv->special_active[i] && r->s > 0x33 && r->s < 0x47)
            blit_tile_buf1(tile_ptr(g, r->s + lv->anim_phase / TILE_BYTES), r->row, r->col);
    }
    lv->anim_phase += 0x80;
    if (lv->anim_phase > 0x100)
        lv->anim_phase = 0;
}

static void draw_list(const list_t *l, const gamedata_t *g, int tile)
{
    for (int i = 0; i < l->n; i++)
        blit_masked_buf2(tile_ptr(g, tile), l->r[i].row, l->r[i].col);
}

void level_draw_sprites_preview(const level_t *lv, const gamedata_t *g)
{
    draw_list(&lv->vplat, g, 0x04);
    draw_list(&lv->hplat, g, 0x06);
    draw_list(&lv->boulders, g, 0x6E);
    draw_list(&lv->enemy49, g, 0x49);
    draw_list(&lv->enemy4a, g, 0x4D);
    draw_list(&lv->enemy4b, g, 0x51);
    blit_masked_buf2(tile_ptr(g, 0x1B), lv->player_row, lv->player_col); /* facing right */
}
