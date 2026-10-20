/* The title and menu pictures (MARSn.CF1 / .CF2, PCX files).
 *
 * The game loads them with the Genus PCX Toolkit (1d2a:000e), shows them with a Genus PCX
 * Effects transition (1c09:0006, see effects.h) and frees them (1d2a:01e8). pcx_show draws
 * the picture at once. */
#ifndef MARS_PCX_H
#define MARS_PCX_H

#include <stddef.h>
#include <stdint.h>

#include "cga.h"

typedef struct {
    uint8_t pixels[CGA_H * CGA_STRIDE]; /* linear, 80 bytes per scanline, 2 bpp */
} pcx_image_t;

/* Decode a 320x200 2 bpp RLE PCX file. Returns 0, or -1 with a message in err. */
int pcx_load(const char *path, pcx_image_t *img, char *err, size_t errlen);

/* Copy the picture to video memory (interleaving the scanlines into CGA's two banks). */
void pcx_show(const pcx_image_t *img);

#endif
