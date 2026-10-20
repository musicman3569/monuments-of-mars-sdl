#include "pcx.h"

#include <stdio.h>
#include <string.h>

int pcx_load(const char *path, pcx_image_t *img, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "can't open %s", path);
        return -1;
    }
    static uint8_t buf[65536];
    const size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n < 128 || buf[0] != 0x0A || buf[2] != 1 || buf[3] != 2 || buf[65] != 1) {
        snprintf(err, errlen, "%s is not a 2 bpp RLE PCX file", path);
        return -1;
    }
    const int w = (buf[8] | buf[9] << 8) - (buf[4] | buf[5] << 8) + 1;
    const int h = (buf[10] | buf[11] << 8) - (buf[6] | buf[7] << 8) + 1;
    const int bpl = buf[66] | buf[67] << 8;
    if (w != CGA_W || h != CGA_H || bpl != CGA_STRIDE) {
        snprintf(err, errlen, "%s is %dx%d, expected 320x200", path, w, h);
        return -1;
    }
    size_t pos = 128, out = 0;
    memset(img->pixels, 0, sizeof img->pixels);
    while (out < sizeof img->pixels && pos < n) {
        const uint8_t b = buf[pos++];
        if (b >= 0xC0) {
            const int run = b & 0x3F;
            const uint8_t v = pos < n ? buf[pos++] : 0;
            for (int i = 0; i < run && out < sizeof img->pixels; i++)
                img->pixels[out++] = v;
        } else {
            img->pixels[out++] = b;
        }
    }
    return 0;
}

void pcx_show(const pcx_image_t *img)
{
    for (int y = 0; y < CGA_H; y++)
        memcpy(cga_vram + cga_offset(0, y), img->pixels + y * CGA_STRIDE, CGA_STRIDE);
}
