/* Everything the port reads from the original game files.
 *
 * MARSn.EXE (LZEXE-packed or already unpacked) supplies the level maps, the game's font and
 * its strings; MARSn.IGS the tile set; MARSn.CF1 / .CF2 the title and menu pictures. Nothing
 * from the game is built into the port. */
#ifndef MARS_GAMEDATA_H
#define MARS_GAMEDATA_H

#include <stddef.h>
#include <stdint.h>

#define LEVELS 20
#define LEVEL_ROWS 12
#define LEVEL_COLS 20
#define LEVEL_BYTES (LEVEL_ROWS * LEVEL_COLS)

#define IGS_BYTES 0x413C  /* LoadIGS freads this much into DS:0766 */
#define TILE_BYTES 0x80
#define NUM_TILE_SLOTS (IGS_BYTES / TILE_BYTES)

#define FONT_NORMAL 0x0006 /* SetHighFont arguments: DS offsets of the INT 1Fh font table */
#define FONT_ALT 0x01A6

typedef struct {
    uint8_t *image;      /* the unpacked load image (no MZ header) */
    size_t image_len;
    size_t ds;           /* linear offset of the game's data segment (Ghidra 2354 in MARS1) */
    size_t level_seg;    /* linear offset of the level segment (Ghidra 2226 in MARS1) */

    /* The level maps, 20 x 12 x 20, copied out of the level segment. The game changes them in
     * place (switches, levers) and puts a level back from its copy at DS:8809 when it ends. */
    uint8_t levels[LEVELS][LEVEL_BYTES];

    /* What each level's computer terminal (code 70h) does, 1..4 (0 = nothing): the byte at
     * level segment + 0Ch + level (TouchTile, 1054:3170). */
    uint8_t terminal_action[LEVELS];

    uint8_t igs[IGS_BYTES]; /* the tile set; tile n at n * 0x80 (zero-padded like the fread) */

    int volume;          /* 1..4, from the EXE name */
    char base[16];       /* "MARS1" */
    char dir[1024];      /* directory of the EXE, with a trailing separator, or "" */
} gamedata_t;

int gamedata_load(gamedata_t *g, const char *exe_path, char *err, size_t errlen);
void gamedata_free(gamedata_t *g);

/* A byte of the game's data segment / a C string in it (NULL if out of range). */
const uint8_t *gamedata_ds(const gamedata_t *g, unsigned off, size_t len);

/* The first string in the data segment that starts with `prefix`, or NULL. */
const char *gamedata_find_string(const gamedata_t *g, const char *prefix);

/* The first string in the data segment that is exactly `s`, or NULL. */
const char *gamedata_find_exact(const gamedata_t *g, const char *s);

/* The string after `s` in the data segment (Turbo C lays string literals out one after the
 * other), or NULL. */
const char *gamedata_next_string(const gamedata_t *g, const char *s);

/* "MARS1.CF1" etc.: the path of a file next to the EXE, in whatever case it has there (files.h). */
void gamedata_path(const gamedata_t *g, const char *ext, char *out, size_t outlen);

static inline const uint8_t *tile_ptr(const gamedata_t *g, int tile)
{
    return &g->igs[(tile & 0xFF) < NUM_TILE_SLOTS ? (tile & 0xFF) * TILE_BYTES : 0];
}

#endif
