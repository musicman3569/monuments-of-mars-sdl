/* A level: DrawLevel (1054:31f3) and the lists it builds. */
#ifndef MARS_LEVEL_H
#define MARS_LEVEL_H

#include <stdint.h>

#include "gamedata.h"

#define MAX_LIST 64

/* The game's 3-byte records: state or code, byte column, row (scanline pairs). */
typedef struct {
    uint8_t s, col, row;
} rec_t;

typedef struct {
    rec_t r[MAX_LIST];
    int n;
} list_t;

typedef struct {
    int number; /* 0-based, [05DB] */

    int player_col, player_row; /* code 65, [05DC] / [05DE] */

    list_t vplat;     /* code 04, DS:C8B0, count [0607]; s = state (0 down, 1 up, 2 stopped) */
    list_t hplat;     /* code 06, DS:89E9, count [CCDE] */
    list_t boulders;  /* code 6E, DS:C930, count [CCCF] */
    list_t enemy49;   /* code 49, DS:4875, count [CCD3] */
    list_t enemy4a;   /* code 4A, DS:48B2, count [CCD4] */
    list_t enemy4b;   /* code 4B, DS:48EE, count [CCD5] */
    list_t code55;    /* code 55, DS:C8EF, count [CCD0] */
    list_t special;   /* DS:C9A8 (s = map code), count [C92F] */
    uint8_t special_active[MAX_LIST]; /* DS:CC00 */
    list_t drips;     /* 18..19 pairs, DS:4947 (s = width), count [CCCD] */

    int has_turret, turret_col, turret_row; /* code 46: [0754], [CCDA], [CCDB] */
    int has_6f, col_6f, row_6f;             /* code 6F: [C8AB], [C8AC] */

    int anim_phase; /* AnimateTiles' [CCEA]: 0, 80h, 100h */
} level_t;

/* DrawLevel: draw level `number`'s static picture into back buffer 1 and build the lists. */
void level_draw(level_t *lv, const gamedata_t *g, int number);

/* AnimateTiles (1054:3823): redraw the active animated tiles (codes 34h-46h) in back buffer 1
 * with the next of their 3 frames. Once per game frame. */
void level_animate_tiles(level_t *lv, const gamedata_t *g);

#endif
