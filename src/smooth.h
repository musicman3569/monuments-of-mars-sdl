/* The display layer for display.smooth (port enhancement): moving things glide instead of
 * jumping 4 pixels at a time, drawn at the monitor's refresh rate. Only the picture changes:
 * the game runs exactly as before, and the CGA buffers stay what the original had.
 *
 * Every frame the game copies back buffer 1 (the level) into back buffer 2, draws the moving
 * things into it with the masked blitters, and copies it to the screen (gfx.h). gfx.c reports
 * each of those steps here, so this module knows buffer 2 without the masked tiles (the
 * background) and the list of tiles drawn on it, in order. game.c tags the tiles that belong
 * to something that moves (`smooth_id`); the window then shows the background with the tiles
 * on top, the tagged ones at a position that follows the game's.
 *
 * How they follow (tuned with a simulation of the game's step patterns): the game moves
 * things on some frames and not others (the player walks on 2 frames of 3, the robots on 1
 * of 2, the ceiling crawlers on 1 of 3), so gliding from one step to the next would still stop
 * between steps. Instead the shown position chases the game's at the thing's average speed
 * over the last 6 frames, plus a catch-up term once it falls more than 2 frames' worth behind:
 * steady movement shows at an even speed (within about a third), it never overshoots where the
 * game has it, and it trails by about one frame (55 ms), as plain interpolation would. Moves
 * of more than 16 px across or 24 down (a restart, a new drip) jump. */
#ifndef MARS_SMOOTH_H
#define MARS_SMOOTH_H

#include <stdbool.h>
#include <stdint.h>

#include "cga.h"

/* game.c: the id of the next masked tile drawn into back buffer 2: what it belongs to, the
 * same from frame to frame (0 = it doesn't move; the next tile drawn resets it to 0). */
extern int smooth_id;

/* Forget where things were shown, so they appear where the game has them (a level started). */
void smooth_reset(void);

/* gfx.c: CopyBackground started a frame; a tile was drawn opaque / masked into back buffer 2
 * (in range); PresentFrame copied the frame to the screen. */
void smooth_frame_begin(void);
void smooth_opaque(const uint8_t *tile, int row, int col);
void smooth_masked(const uint8_t *tile, int row, int col);
void smooth_present(void);

/* A tile to draw over the background, at screen pixel x, y (its top-left; fractional while
 * gliding). Only the playfield (screen rows 8..199) should show it. */
typedef struct {
    const uint8_t *tile;
    uint64_t cut; /* bytes of the tile not to draw: bit 8 * pair + 4 * odd + byte, where pair
                     is its row of scanline pairs (0..7), odd its second scanline, byte 0..3 */
    float x, y;
    int id;       /* smooth_id (0 = it doesn't move) */
} smooth_sprite_t;

#define SMOOTH_PLAYFIELD_Y 8

/* What the window shows at time `now` (seconds): the screen as video memory with the
 * playfield's moving tiles taken out (into `vram`), and the tiles to draw over it. Returns
 * false when there's no such picture (no game frame yet, or something has drawn on the
 * playfield since, such as a banner or an animation): then show video memory as it is. */
bool smooth_frame(double now, uint8_t vram[CGA_VRAM_SIZE], const smooth_sprite_t **sprites, int *n);

/* A tile as 16x16 0xAARRGGBB pixels in `pal`'s colours, transparent where it shows what's
 * under it and in the bytes in `cut`. */
void smooth_tile_argb(const uint8_t *tile, uint64_t cut, const rgb_t *pal, uint32_t out[16 * 16]);

/* For tests: the background with every tile drawn at the game's position, compared with back
 * buffer 2 as the game left it. Returns how many bytes differ (0 = the scene is complete). */
int smooth_check(void);

#endif
