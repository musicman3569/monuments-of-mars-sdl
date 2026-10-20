#include "screens.h"

#include <stdio.h>
#include <string.h>

#include "cga.h"
#include "gfx.h"
#include "platform.h"
#include "text.h"

/* Find `pat` (-1 = any byte) in the image; returns its offset or -1. */
static long find(const gamedata_t *g, const int *pat, size_t n, size_t from, size_t to)
{
    if (to > g->image_len)
        to = g->image_len;
    for (size_t i = from; i + n <= to; i++) {
        size_t k = 0;
        while (k < n && (pat[k] < 0 || g->image[i + k] == pat[k]))
            k++;
        if (k == n)
            return (long)i;
    }
    return -1;
}

static uint16_t rd16(const gamedata_t *g, size_t at)
{
    return (uint16_t)(g->image[at] | g->image[at + 1] << 8);
}

/* Where the game's routines are in this EXE (image offsets). */
typedef struct {
    int found;
    size_t code;                 /* the game's code segment (Ghidra 1054) */
    size_t blit_screen;          /* BlitTile_Screen: 1b72, 1bcc in MARS4 */
    size_t notice;               /* ShowSpecialNotice */
    size_t story, instructions, ordering;
} addrs_t;

static addrs_t A;
static const gamedata_t *A_for;

/* MainMenu: cmp byte [key],lo / je +7 / cmp byte [key],up / jne +4 / push cs / call rel16 */
static size_t menu_call(const gamedata_t *g, int lo, int up)
{
    const int pat[] = {0x80, 0x3E, -1, -1, lo, 0x74, 0x07, 0x80, 0x3E, -1, -1, up, 0x75, 0x04,
                       0x0E, 0xE8};
    const long at = find(g, pat, 16, 0, g->image_len);
    if (at < 0)
        return 0;
    return (size_t)(at + 18) + (int16_t)rd16(g, (size_t)at + 16);
}

static int find_addrs(const gamedata_t *g)
{
    if (A_for == g)
        return A.found ? 0 : -1;
    A_for = g;
    memset(&A, 0, sizeof A);
    /* SetHighFont, at 0001 of the code segment in every volume */
    static const int set_high_font[] = {0x55, 0x8B, 0xEC, 0x1E, 0xB8, -1, -1, 0x8E, 0xD8, 0xFA,
                                        0x8B, 0x46, 0x06, 0x33, 0xDB, 0x8E, 0xC3, 0xBB, 0x7C};
    static const int blit_screen[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x1E, 0xB8, -1, -1,
                                      0x8E, 0xD8, 0xC4, 0x1E, -1, -1, 0x8C, 0x46, 0xFC, 0x89,
                                      0x5E, 0xFA, 0x8A, 0x46, 0x0A, 0x04, 0x08};
    const long shf = find(g, set_high_font, 19, 0, g->image_len);
    const long bs = find(g, blit_screen, 27, 0, g->image_len);
    const char *title = gamedata_find_exact(g, "*** SPECIAL NOTICE ***");
    if (shf < 1 || bs < 0 || !title)
        return -1;
    A.code = (size_t)shf - 1;
    A.blit_screen = (size_t)bs;
    /* ShowSpecialNotice: the routine that loads the notice's title (push ds / mov ax,title /
     * push ax), backed up to its prologue push ds / mov ax,DSEG / mov ds,ax */
    const size_t t = (size_t)((const uint8_t *)title - g->image) - g->ds;
    const int load_title[] = {0x1E, 0xB8, (int)(t & 0xFF), (int)(t >> 8), 0x50};
    const long lt = find(g, load_title, 5, A.code, A.code + 0x10000);
    if (lt < 0)
        return -1;
    for (long i = lt; i > lt - 0x40 && i >= 0; i--)
        if (g->image[i] == 0x1E && g->image[i + 1] == 0xB8 && g->image[i + 4] == 0x8E &&
            g->image[i + 5] == 0xD8) {
            A.notice = (size_t)i;
            break;
        }
    A.story = menu_call(g, 'S', 's');
    A.instructions = menu_call(g, 'I', 'i');
    A.ordering = menu_call(g, 'o', 'O');
    A.found = A.notice && A.story && A.instructions && A.ordering;
    return A.found ? 0 : -1;
}

/* ------------------------------------------------------------------ the interpreter ---- */

#define TAG_CS 0x10000 /* stack slots that hold CS / DS rather than a value */
#define TAG_DS 0x20000

static int run(const gamedata_t *g, size_t ip, char *err, size_t errlen)
{
    long stack[64];
    int sp = 0;
    unsigned ax = 0;
#define PUSH(v) do { if (sp == 64) goto overflow; stack[sp++] = (v); } while (0)
#define POP() (sp > 0 ? stack[--sp] : -1)
    for (int steps = 0; steps < 100000; steps++) {
        if (ip + 6 > g->image_len)
            break;
        const uint8_t *p = g->image + ip;
        switch (p[0]) {
        case 0x50: PUSH(ax); ip++; continue;                       /* push ax */
        case 0x1E: PUSH(TAG_DS); ip++; continue;                   /* push ds */
        case 0x0E: PUSH(TAG_CS); ip++; continue;                   /* push cs */
        case 0x55: case 0x56: case 0x57: PUSH(0); ip++; continue;  /* push bp / si / di */
        case 0x1F: case 0x5D: case 0x5E: case 0x5F: (void)POP(); ip++; continue; /* pops */
        case 0xB0: ax = (ax & 0xFF00) | p[1]; ip += 2; continue;   /* mov al,ib */
        case 0xB4: ax = (ax & 0x00FF) | p[1] << 8; ip += 2; continue; /* mov ah,ib */
        case 0xB8: ax = rd16(g, ip + 1); ip += 3; continue;        /* mov ax,iw */
        case 0xCB: return 0;                                       /* retf */
        case 0x8E:
            if (p[1] == 0xD8) { ip += 2; continue; }               /* mov ds,ax */
            break;
        case 0x8B:
            if (p[1] == 0xEC) { ip += 2; continue; }               /* mov bp,sp */
            break;
        case 0x83:
            if (p[1] == 0xC4 && !(p[2] & 1)) {                     /* add sp,ib */
                for (int i = 0; i < p[2] / 2; i++)
                    (void)POP();
                ip += 3;
                continue;
            }
            break;
        case 0x44:
            if (p[1] == 0x44) { (void)POP(); ip += 2; continue; } /* inc sp / inc sp */
            break;
        case 0x9A:                                                 /* call far: the runtime */
            ip += 5;
            if (g->image[ip] == 0x44 && g->image[ip + 1] == 0x44)
                plat_delay((int)(sp > 0 ? stack[sp - 1] & 0xFFFF : 0)); /* delay(ms) */
            else
                ax = (unsigned)plat_getch();                       /* getch() */
            continue;
        case 0xE8: {                                               /* push cs / call near */
            const size_t t = ip + 3 + (size_t)(int16_t)rd16(g, ip + 1);
            ip += 3;
            if (POP() != TAG_CS)
                break;
            if (t == A.code + 0x0028) { /* DrawText(color, s, row, col): it pops them */
                const long color = POP(), off = POP(), seg = POP(), row = POP(), col = POP();
                const uint8_t *s = seg == TAG_DS ? gamedata_ds(g, (unsigned)off & 0xFFFF, 1) : NULL;
                if (!s)
                    break;
                draw_text((int)color & 0xFF, (const char *)s, (int)row & 0xFF, (int)col & 0xFF);
            } else if (t == A.code + 0x0001) { /* SetHighFont(s): the caller pops */
                text_set_high_font(g, (unsigned)(sp > 0 ? stack[sp - 1] : 0) & 0xFFFF);
            } else if (t == A.code + 0x0471) { /* ClearScreen */
                cga_clear();
            } else if (t == A.code + 0x0A36) { /* SetVideoMode(4) */
                cga_set_mode(0, 0);
            } else if (t == A.code + 0x0AA7) { /* SetVideoModePal1Bright(4) */
                cga_set_mode(1, 1);
            } else if (t == A.blit_screen) { /* BlitTile_Screen(tile, row, col): it pops them */
                const long tile = POP(), seg = POP(), row = POP(), col = POP();
                if (seg != TAG_DS || (tile & 0xFFFF) < 0x766)
                    break;
                blit_tile_screen(tile_ptr(g, (int)(((tile & 0xFFFF) - 0x766) / TILE_BYTES)),
                                 (int)row & 0xFF, (int)col & 0xFF);
            } else if (t == A.notice) {
                show_special_notice(g);
            } else {
                snprintf(err, errlen, "unexpected call to %zx", t - A.code);
                return -1;
            }
            plat_dirty();
            continue;
        }
        default:
            break;
        }
        snprintf(err, errlen, "can't run the instruction at %zx (%02x %02x %02x)", ip - A.code,
                 g->image[ip], g->image[ip + 1], g->image[ip + 2]);
        return -1;
    }
    snprintf(err, errlen, "ran off the routine");
    return -1;
overflow:
    snprintf(err, errlen, "stack overflow at %zx", ip - A.code);
    return -1;
#undef PUSH
#undef POP
}

static int run_screen(const gamedata_t *g, const size_t *entry, const char *name)
{
    char err[200];
    if (find_addrs(g) != 0) {
        fprintf(stderr, "mars: can't find %s in the EXE\n", name);
        return -1;
    }
    if (run(g, *entry, err, sizeof err) != 0) {
        fprintf(stderr, "mars: %s: %s\n", name, err);
        return -1;
    }
    return 0;
}

int screen_story(const gamedata_t *g)
{
    return run_screen(g, &A.story, "ShowStory");
}

int screen_instructions(const gamedata_t *g)
{
    return run_screen(g, &A.instructions, "ShowInstructions");
}

int screen_ordering(const gamedata_t *g)
{
    return run_screen(g, &A.ordering, "ShowOrderingInfo");
}

void show_special_notice(const gamedata_t *g)
{
    /* the nine strings follow each other from "*** SPECIAL NOTICE ***" */
    static const struct { int color, row, col; } at[9] = {
        {1, 0, 9}, {3, 3, 0}, {3, 5, 0}, {3, 7, 0}, {3, 9, 0}, {3, 11, 0}, {3, 13, 0},
        {1, 17, 0}, {2, 24, 7},
    };
    cga_clear(); /* ClearScreen */
    text_set_high_font(g, FONT_ALT);
    const char *s = gamedata_find_exact(g, "*** SPECIAL NOTICE ***");
    for (int i = 0; i < 9 && s; i++, s = gamedata_next_string(g, s)) {
        if (i > 0 && plat_kbhit()) /* a key cuts it short */
            break;
        if (i == 8)
            text_set_high_font(g, FONT_NORMAL);
        draw_text(at[i].color, s, at[i].row, at[i].col);
        plat_dirty();
        if (i == 8)
            while (!plat_key_pressed()) /* the key stays in the buffer */
                plat_delay(5);
    }
}
