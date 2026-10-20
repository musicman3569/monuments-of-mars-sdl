#include "gamedata.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzexe.h"

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = buf ? (size_t)n : 0;
    return buf;
}

/* DrawLevel (1054:31f3) reads the map with
 *     mov ax,LEVELSEG / mov es,ax / mov al,es:[bx+0025h]
 * and starts, like every far function in the game, with push ds / mov ax,DSEG / mov ds,ax.
 * Segment values in the image are relative to it (the port doesn't apply relocations). */
static int find_segments(gamedata_t *g, char *err, size_t errlen)
{
    static const uint8_t read_map[] = {0x8E, 0xC0, 0x26, 0x8A, 0x87, 0x25, 0x00};
    const uint8_t *im = g->image;
    size_t at = 0;
    for (size_t i = 3; i + sizeof read_map <= g->image_len; i++)
        if (im[i - 3] == 0xB8 && !memcmp(im + i, read_map, sizeof read_map)) {
            at = i - 3;
            break;
        }
    if (!at) {
        snprintf(err, errlen, "can't find the level data in the EXE (is this Monuments of Mars?)");
        return -1;
    }
    g->level_seg = (size_t)(im[at + 1] | im[at + 2] << 8) * 16;

    /* back up to DrawLevel's prologue for the data segment */
    for (size_t i = at; i >= 6 && i > at - 0x600; i--)
        if (im[i - 6] == 0x1E && im[i - 5] == 0xB8 && im[i - 2] == 0x8E && im[i - 1] == 0xD8) {
            g->ds = (size_t)(im[i - 4] | im[i - 3] << 8) * 16;
            break;
        }
    if (!g->ds || g->ds >= g->image_len || g->level_seg + 0x25 + LEVELS * LEVEL_BYTES > g->image_len) {
        snprintf(err, errlen, "can't find the data segment in the EXE");
        return -1;
    }
    for (int i = 0; i < LEVELS; i++)
        memcpy(g->levels[i], im + g->level_seg + 0x25 + (size_t)i * LEVEL_BYTES, LEVEL_BYTES);
    return 0;
}

static void split_path(gamedata_t *g, const char *exe_path)
{
    const char *name = exe_path;
    for (const char *p = exe_path; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':')
            name = p + 1;
    snprintf(g->dir, sizeof g->dir, "%.*s", (int)(name - exe_path), exe_path);
    size_t n = 0;
    while (name[n] && name[n] != '.' && n + 1 < sizeof g->base) {
        g->base[n] = (char)toupper((unsigned char)name[n]);
        n++;
    }
    g->base[n] = 0;
    g->volume = n && isdigit((unsigned char)g->base[n - 1]) ? g->base[n - 1] - '0' : 1;
}

void gamedata_path(const gamedata_t *g, const char *ext, char *out, size_t outlen)
{
    snprintf(out, outlen, "%s%s.%s", g->dir, g->base, ext);
}

int gamedata_load(gamedata_t *g, const char *exe_path, char *err, size_t errlen)
{
    memset(g, 0, sizeof *g);
    split_path(g, exe_path);

    size_t len;
    uint8_t *exe = read_file(exe_path, &len);
    if (!exe) {
        snprintf(err, errlen, "can't read %s", exe_path);
        return -1;
    }
    if (len < 0x20 || exe[0] != 'M' || exe[1] != 'Z') {
        snprintf(err, errlen, "%s is not a DOS executable", exe_path);
        free(exe);
        return -1;
    }
    if (lzexe_is_packed(exe, len)) {
        uint32_t entry;
        if (lzexe_unpack(exe, len, &g->image, &g->image_len, &entry, err, errlen) != 0) {
            free(exe);
            return -1;
        }
        free(exe);
    } else {
        const size_t hdr = (size_t)(exe[8] | exe[9] << 8) * 16;
        if (hdr >= len) {
            snprintf(err, errlen, "%s has a bad MZ header", exe_path);
            free(exe);
            return -1;
        }
        g->image_len = len - hdr;
        g->image = malloc(g->image_len);
        if (!g->image) {
            free(exe);
            snprintf(err, errlen, "out of memory");
            return -1;
        }
        memcpy(g->image, exe + hdr, g->image_len);
        free(exe);
    }
    if (find_segments(g, err, errlen) != 0) {
        gamedata_free(g);
        return -1;
    }

    /* LoadIGS (1054:3b5f): fopen(name, "rb"), fread(DS:0766, 0x413C, 1, f). MARS1.IGS is 100
     * bytes short of that; the rest of the buffer stays zero. */
    char path[1100];
    gamedata_path(g, "IGS", path, sizeof path);
    size_t igs_len;
    uint8_t *igs = read_file(path, &igs_len);
    if (!igs) {
        snprintf(err, errlen, "can't read %s", path);
        gamedata_free(g);
        return -1;
    }
    memcpy(g->igs, igs, igs_len < IGS_BYTES ? igs_len : IGS_BYTES);
    free(igs);
    return 0;
}

void gamedata_free(gamedata_t *g)
{
    free(g->image);
    g->image = NULL;
}

const uint8_t *gamedata_ds(const gamedata_t *g, unsigned off, size_t len)
{
    return g->ds + off + len <= g->image_len ? g->image + g->ds + off : NULL;
}

const char *gamedata_find_string(const gamedata_t *g, const char *prefix)
{
    const size_t n = strlen(prefix);
    const size_t end = g->ds + 0x10000 < g->image_len ? g->ds + 0x10000 : g->image_len;
    for (size_t i = g->ds; i + n < end; i++)
        if ((i == g->ds || g->image[i - 1] == 0) && !memcmp(g->image + i, prefix, n))
            return (const char *)g->image + i;
    return NULL;
}
