/* The port's CRT look (display.crt: --crt, Alt+C), the same as realms-of-chaos-port's: after
 * RetroArch's 2xsal-level2-crt preset. Its first pass, guest(r)'s 2xSaL level 2, doubles the
 * picture with smoothed edges (done here on the CPU); its second scales that to the window with
 * linear filtering and multiplies it by Lottes' "stretched VGA" shadow mask (dotmask.slang, mask
 * style 3, maskDark 0.6, maskLight 1.4), which platform.c does with the renderer's scaling and a
 * blend mode. */
#ifndef MARS_CRT_H
#define MARS_CRT_H

#include "cga.h"

#include <stdint.h>

enum { CRT_W = CGA_W * 2, CRT_H = CGA_H * 2 };

/* The mask's two factors in gamma space. The shader multiplies in linear light, (c^2.2 * m)^(1 /
 * 2.2), which is c * m^(1 / 2.2): the same multiply on the colours as they are. */
#define CRT_MASK_DARK 0.7929f   /* 0.6 ^ (1 / 2.2) */
#define CRT_MASK_LIGHT 1.1653f  /* 1.4 ^ (1 / 2.2) */

/* 2xSaL level 2: the CGA_W x CGA_H picture `in` doubled into CRT_W x CRT_H `out`. */
void crt_2xsal(const uint32_t *in, uint32_t *out);

/* The colour channel (0 red, 1 green, 2 blue) the mask lets through brightly at pixel (x, y) of
 * the picture's place on the screen; the other two get CRT_MASK_DARK. */
int crt_mask_channel(int x, int y);

/* The whole look at 1:1, as if the window were CRT_W x CRT_H (the screenshots): `in` doubled
 * and masked into `out`. */
void crt_render(const uint32_t *in, uint32_t *out);

#endif
