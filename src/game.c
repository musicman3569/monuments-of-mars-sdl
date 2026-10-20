/* Playing the game. Each function is a port of the game routine named in its comment; the
 * state below mirrors the game's variables in its data segment (DS 2354 in MARS1), with their
 * addresses, since most of them are shared between routines.
 *
 * The original's quirks are kept: TouchTile can run several times in one frame (so holding
 * Enter on a lever can flip it and flip it back), a dead enemy can still kill on the frame
 * it's shot, the letters bonus pays 2000 for every letter flag still set, and so on. */
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gfx.h"
#include "hiscore.h"
#include "level.h"
#include "platform.h"
#include "text.h"

/* ------------------------------------------------------------------ state ---- */

static gamedata_t *gd;
static level_t lv;
static uint8_t backup[LEVEL_BYTES]; /* DS:8809, the level as it was when it started */

static int level;                  /* 05DB */
static int game_over;              /* 05D5 */
static int died;                   /* 05D6 */
static int sound_on = 1;           /* 05D7 */
static int shots = 5;              /* 05D8 (a byte) */
static int shots_at_start;         /* 0756 */
static unsigned long score;        /* 4871 */
static unsigned long score_at_start; /* 0757 */
static int level_changed;          /* CCE9: go back to the level start */
static int death_x, death_y;       /* 8803 / 8806: where RestartLevel draws the explosion */
static int has_card;               /* 05DA */
static int found;                  /* 0755: FindSpecialTileAt's last hit, an index in lv.special */

/* The player. The starting values are the data segment's initial ones; the game never resets
 * them, so they carry over from level to level. */
static int px, py;                 /* 05DC / 05DE: byte column, scanline pair */
static int step_ctr;               /* 05E0: walking moves on 2 frames of every 3 */
static int frame_first = 0x1B;     /* 05E1: first walking frame (1B right, 21 left) */
static int can_left = 1;           /* 05E6 */
static int can_right;              /* 05E7 */
static int facing_left;            /* 05E8 */
static int frame = 0x1A;           /* 05E9: the player's tile */
static int frame_end = 0x1E;       /* 05EA: 1F right, 25 left (1E until the first turn) */
static int fall_v = 1;             /* 05EB */
static int jump_t;                 /* 05EC */
static int flag_5ed;               /* 05ED: tested, never set */
static int in_air = 1;             /* 05EE: set and cleared, never tested */
static int jump_sub;               /* 05EF: jump_t counts up every 3 frames */
static int can_jump;               /* 05F0 */
static int on_ground;              /* 05F1 */
static int jump_over = 1;          /* 05A8: the jump has peaked or hit something */
static int big_hitbox;             /* C92E: 5 on the frame a jump starts, else 0 */

static int shot_active;            /* 05D9 */
static int shot_dir;               /* 05F2: 2 or -2 bytes per frame */
static uint8_t shot_col, shot_row; /* C8ED / C8EE */

static int vplat_tick, hplat_tick; /* 05E2, 05E4 */
static uint8_t vplat_saved[MAX_LIST]; /* CCDF: the platforms' states while a lever stops them */

static int e49_tick, e49_frame;    /* 05F6, 05F4 (frame in tile bytes, 0..180h) */
static int e4a_tick, e4a_frame;    /* 05F9, 05F7 */
static int e4b_tick, e4b_frame;    /* 05FC, 05FA */

static int drip_active;            /* 05A7 */
static int drip_col, drip_row;     /* 05FD / 05FF */

static int bolt_active;            /* C8AF */
static int bolt_dir;               /* 0605: 1 or -1 */
static uint8_t bolt_col;           /* 0608 */

static int got_m, got_a, got_r, got_s; /* 4946, 48B1, 8805, 8808 */
static int next_letter;            /* CCC8: 0, then 'm', 'a', 'r', 's' */
static int rings_done;             /* 05A6 */

static unsigned long rand_seed = 1; /* Turbo C's rand(); the game never seeds it */
static int scripted;               /* --script: Y/N questions answer yes */

#define K (isr_keys)
#define SC (isr_keys.scancode) /* [C8AA] */
#define ENTER 0x1C

/* ------------------------------------------------------------------ helpers ---- */

static int tc_rand(void)
{
    rand_seed = (rand_seed * 0x015A4E35UL + 1) & 0xFFFFFFFFUL;
    return (int)((rand_seed >> 16) & 0x7FFF);
}

static void delay(int ms)
{
    plat_delay(ms);
}

/* PlaySound (1054:0e34): sound() if sound is on. 7FFFh is used as "silent". */
static void play(int hz)
{
    if (sound_on)
        plat_sound(hz);
}

static void nosound(void)
{
    plat_nosound();
}

static const uint8_t *tile(int n)
{
    return tile_ptr(gd, n);
}

static uint8_t *map(void) /* the level in the level segment, which the game edits */
{
    return gd->levels[level];
}

static void wait_for_key(void) /* while ([C8AA] == 0) */
{
    while (SC == 0 && !plat_quitting())
        delay(1);
}

/* Background colour flashes: INT 10h AH=0Bh BH=0, set and immediately put back, so on real
 * hardware they're at most a flicker. */
static void set_background(int c) /* 1054:0918 */
{
    cga_cur_background = c & 15;
    plat_dirty();
}

static void flash(int c) /* 1054:08cb (1), 094f (0Eh), 099c (4), 09e9 (2) */
{
    set_background(c);
    set_background(0);
}

/* DrawScore (1054:0e53): add to the score and show it. */
static void draw_score(unsigned add)
{
    score += add;
    draw_number(1, 8, (long)score, 0, 0x21);
}

/* DrawShots (1054:088d). */
static void draw_shots(int col, int row, int n)
{
    draw_text(3, "SHOTS  ", row, col + 1);
    draw_number(1, 2, n, row, col + 8);
}

static void kill_player(int dy) /* the deadly cases all do this */
{
    died = 1;
    death_x = px;
    death_y = py + dy;
}

/* ------------------------------------------------------------------ sounds ---- */

static void sound_jump(void) /* 1054:068b */
{
    static const int f[] = {700, 500, 700, 900, 1500, 1900, 2200};
    for (int i = 0; i < 7; i++) {
        delay(8);
        play(f[i]);
    }
    nosound();
}

static void sound_drip(void) /* 1054:0724 */
{
    static const int f[] = {800, 1000, 1200, 1400, 1600};
    for (int i = 0; i < 5; i++) {
        delay(8);
        play(f[i]);
    }
    nosound();
}

static void sound_switch(void) /* 1054:0795 */
{
    static const int f[] = {200, 300, 400, 500, 600, 400};
    for (int i = 0; i < 6; i++) {
        delay(8);
        play(f[i]);
    }
    delay(8);
    nosound();
}

static void sound_pickup(void) /* 1054:0825 */
{
    static const int f[] = {200, 210, 220, 250};
    for (int i = 0; i < 4; i++) {
        delay(10);
        play(f[i]);
    }
    delay(10);
    nosound();
}

/* ------------------------------------------------------------------ the map's specials ---- */

/* Switches 0F-12 (Enter, or a shot): 0F/12 turn the force fields (37h) off, 10/11 on. Every
 * switch in the level flips, in the list and in the map. */
static void switches(int off)
{
    for (int i = 0; i < lv.special.n; i++) {
        rec_t *r = &lv.special.r[i];
        if (off ? r->s == 0x0F : r->s == 0x10) {
            r->s = off ? 0x10 : 0x0F;
            blit_tile_buf1(tile(r->s), r->row, r->col);
        } else if (off ? r->s == 0x12 : r->s == 0x11) {
            r->s = off ? 0x11 : 0x12;
            blit_tile_buf1(tile(r->s), r->row, r->col);
        }
        if (r->s == 0x37) {
            lv.special_active[i] = !off;
            if (off)
                blit_tile_buf1(tile(0), r->row, r->col);
        }
    }
    uint8_t *m = map();
    for (int i = 0; i < LEVEL_BYTES; i++) {
        if (m[i] == (off ? 0x0F : 0x10))
            m[i] = off ? 0x10 : 0x0F;
        if (m[i] == (off ? 0x12 : 0x11))
            m[i] = off ? 0x11 : 0x12;
    }
}

/* Lever 16 / 17 (Enter, or a shot): 16 -> 17 stops the vertical platforms, 17 -> 16 starts
 * them again in the direction they had. */
static void lever(int from)
{
    const int to = from == 0x16 ? 0x17 : 0x16;
    for (int i = 0; i < lv.special.n; i++) {
        rec_t *r = &lv.special.r[i];
        if (r->s == from) {
            r->s = (uint8_t)to;
            blit_tile_buf1(tile(to), r->row, r->col);
        }
    }
    for (int i = 0; i < lv.vplat.n; i++) {
        if (from == 0x16) {
            vplat_saved[i] = lv.vplat.r[i].s;
            lv.vplat.r[i].s = 2;
        } else {
            lv.vplat.r[i].s = vplat_saved[i];
        }
    }
    uint8_t *m = map();
    for (int i = 0; i < LEVEL_BYTES; i++)
        if (m[i] == from)
            m[i] = (uint8_t)to;
}

/* FindSpecialTileAt (1054:0fce): the first active special within 2 byte columns and 7 rows
 * of (y, x), handling the door (0D) and the switches on the way. Returns its code, or 0 for
 * none, a boulder, a closed door or a switch: the callers treat 0 as "a plain wall". */
static int find_special_at(uint8_t y, uint8_t x)
{
    for (int i = 0; i < lv.special.n; i++) {
        rec_t *r = &lv.special.r[i];
        if (!lv.special_active[i] || abs(r->col - x) >= 3 || abs(r->row - y) >= 8)
            continue;
        found = i;
        const int s = r->s;
        if (s == 0x6E || s == 0x14 || s == 0x15)
            return 0;
        if (s == 0x0D) { /* the door: opens if the player has the card */
            if (!has_card)
                return 0;
            draw_text(2, "     ", 0, 0x14);
            blit_tile_buf1(tile(0), r->row, r->col);
            blit_tile_buf2(tile(0), r->row, r->col);
            for (int t = 0x2F, k = 0; t <= 0x33; t++, k++) {
                blit_tile_screen(tile(t), r->row, r->col);
                play(k * 50 + 200);
                delay(70);
                play(0x7FFF);
            }
            has_card = 0;
            lv.special_active[found] = 0;
            return s;
        }
        if (s >= 0x0F && s <= 0x12) {
            if (SC == ENTER) {
                delay(200);
                sound_switch();
                switches(s == 0x0F || s == 0x12);
            }
            return 0;
        }
        return s;
    }
    return 0;
}

/* Remove the special FindSpecialTileAt found (a pickup). */
static void take_found(void)
{
    const rec_t *r = &lv.special.r[found];
    blit_tile_buf1(tile(0), r->row, r->col);
    lv.special_active[found] = 0;
}

/* 1054:58ab: the letters M-A-R-S. Each one pays 2000 while its flag is set; taken in order,
 * the last pays a 10000 bonus with a fanfare. */
static void letters(void)
{
    if (got_m) {
        draw_score(2000);
        if (next_letter == 0) {
            got_m = 0;
            next_letter = 'm';
            return;
        }
    }
    if (got_a) {
        draw_score(2000);
        if (next_letter == 'm') {
            got_a = 0;
            next_letter = 'a';
            return;
        }
    }
    if (got_r) {
        draw_score(2000);
        if (next_letter == 'a') {
            got_r = 0;
            next_letter = 'r';
            return;
        }
    }
    if (got_s) {
        draw_score(2000);
        if (next_letter == 'r') {
            next_letter = 's';
            got_s = 0;
            for (int i = 1; i < 25; i++)
                for (int j = 1; j < 14; j++) {
                    play(i * i * j);
                    delay(j + 1);
                }
            nosound();
            draw_score(10000);
        }
    }
}

/* TeleportTo5C (1054:4126), really "show block 5C": a block (tile 01) materialises at every
 * 5C cell, which becomes solid code 01. */
static void show_blocks_5c(void)
{
    uint8_t *m = map();
    for (int i = 0; i < LEVEL_BYTES; i++) {
        if (m[i] != 0x5C)
            continue;
        const int row = i / LEVEL_COLS * 8, col = i % LEVEL_COLS * 4;
        play(200);
        delay(10);
        play(0x7FFF);
        for (int t = 0x29; t >= 0x27; t--) {
            blit_tile_screen(tile(t), row, col);
            delay(45);
        }
        blit_tile_screen(tile(1), row, col);
        blit_tile_buf1(tile(1), row, col);
        blit_tile_buf2(tile(1), row, col);
        m[i] = 1;
    }
}

/* TeleportTo5E (1054:427c), really "hide block 5E": the blocks at 5E cells (drawn as tile 01)
 * dissolve and the cells become empty. */
static void hide_blocks_5e(void)
{
    uint8_t *m = map();
    for (int i = 0; i < LEVEL_BYTES; i++) {
        if (m[i] != 0x5E)
            continue;
        const int row = i / LEVEL_COLS * 8, col = i % LEVEL_COLS * 4;
        play(200);
        delay(10);
        play(0x7FFF);
        for (int t = 0x27; t <= 0x29; t++) {
            blit_tile_screen(tile(t), row, col);
            delay(45);
        }
        blit_tile_screen(tile(0), row, col);
        blit_tile_buf1(tile(0), row, col);
        blit_tile_buf2(tile(0), row, col);
        m[i] = 0;
    }
}

/* 1054:0e86: the invisible 5F cells turn into gems (3A). */
static void reveal_gems(void)
{
    uint8_t *m = map();
    for (int i = 0; i < LEVEL_BYTES; i++) {
        if (m[i] != 0x5F)
            continue;
        m[i] = 0x3A;
        if (lv.special.n >= MAX_LIST)
            continue;
        rec_t *r = &lv.special.r[lv.special.n];
        r->s = 0x3A;
        r->col = (uint8_t)(i % LEVEL_COLS * 4);
        r->row = (uint8_t)(i / LEVEL_COLS * 8);
        for (int t = 0x71; t <= 0x73; t++) {
            blit_tile_screen(tile(t), r->row, r->col);
            delay(50);
        }
        blit_tile_screen(tile(0x3A), r->row, r->col);
        lv.special_active[lv.special.n++] = 1;
        play(200);
        delay(40);
        play(0x7FFF);
    }
}

/* 1054:1a3c: the energy rings (code 55) flicker out, leaving a freed astronaut (tile 1A,
 * which looks like the player). Once per level. */
static void deactivate_rings(void)
{
    if (rings_done)
        return;
    rings_done = 1;
    for (int i = 0; i < lv.code55.n; i++) {
        const int row = lv.code55.r[i].row, col = lv.code55.r[i].col;
        for (int k = 0; k < 50; k++) {
            blit_tile_screen(tile(0x55), row, col);
            play(200);
            delay(50 - k);
            play(1000 - k * 10);
            play(500);
            nosound();
            blit_tile_screen(tile(0x1A), row, col);
            delay(50 - k);
            play(k * 20 + 1000);
        }
        blit_tile_screen(tile(0x7F), row, col);
        blit_tile_buf1(tile(0x1A), row, col);
        nosound();
    }
}

/* ShowRescueMessage (1054:0b18): 20 lines of text over the screen; waits for a key. */
static void show_rescue_message(void)
{
    static const int colors[20] = {8, 1, 1, 1, 8, 3, 3, 3, 8, 1, 1, 1, 8, 3, 3, 3, 3, 8, 1, 8};
    const char *s = gamedata_find_string(gd, " YOU HAVE DEACTIVATED");
    draw_text(8, "                                        ", 1, 0);
    for (int i = 1; i < 20 && s; i++, s = gamedata_next_string(gd, s))
        draw_text(colors[i], s, i + 1, 0);
    delay(200);
    SC = 0;
    wait_for_key();
    SC = 0;
}

/* 1054:0ca4: after the last level. */
static void show_ending(void)
{
    static const struct { int color, row, col; } at[18] = {
        {3, 1, 7}, {1, 3, 0}, {1, 4, 0}, {1, 5, 0}, {1, 6, 0}, {2, 8, 6}, {2, 9, 6},
        {2, 10, 6}, {2, 11, 6}, {1, 13, 0}, {1, 14, 0}, {1, 15, 0}, {1, 16, 0}, {1, 18, 0},
        {3, 19, 0}, {1, 21, 0}, {1, 22, 0}, {2, 24, 0},
    };
    cga_clear(); /* ClearScreen (1054:0471) */
    const char *s = gamedata_find_string(gd, "YOUR JOURNEY CONTINUES");
    for (int i = 0; i < 18 && s; i++, s = gamedata_next_string(gd, s)) {
        text_set_high_font(gd, i == 0 || i == 17 ? FONT_NORMAL : FONT_ALT);
        draw_text(at[i].color, s, at[i].row, at[i].col);
    }
    text_set_high_font(gd, FONT_NORMAL);
    delay(200);
    SC = 0;
    wait_for_key();
    game_over = 1;
}

/* TouchTile (1054:2b08): what touching special `code` does. */
static void touch_tile(int code)
{
    switch (code) {
    case 0x09: /* force-field switch, off */
    case 0x0A: /* ... and on */
        if (SC != ENTER)
            break;
        delay(200);
        sound_switch();
        for (int i = 0; i < lv.special.n; i++) {
            rec_t *r = &lv.special.r[i];
            if (r->s == code) {
                r->s = code == 0x09 ? 0x0A : 0x09;
                blit_tile_buf1(tile(r->s), r->row, r->col);
            }
            if (r->s == 0x34) {
                lv.special_active[i] = code == 0x0A;
                if (code == 0x09)
                    blit_tile_buf1(tile(0), r->row, r->col);
            }
        }
        break;
    case 0x0C: /* the key card */
        take_found();
        has_card = 1;
        sound_pickup();
        draw_text(2, "CARD!", 0, 0x14);
        draw_score(5000);
        break;
    case 0x0E: /* three shots */
        take_found();
        sound_pickup();
        draw_score(250);
        shots = (shots + 3) & 0xFF;
        draw_shots(9, 0, shots);
        break;
    case 0x16:
    case 0x17:
        if (SC != ENTER)
            break;
        delay(200);
        sound_switch();
        lever(code);
        break;
    case 0x34: case 0x37: case 0x3D: case 0x40: case 0x46:
        kill_player(0);
        break;
    case 0x43: /* the energy rings kill 2 rows higher (for RestartLevel's explosion) */
        kill_player(-2);
        break;
    case 0x3A: { /* gem */
        static const int f[] = {2000, 1500, 1750, 2000, 1500, 1250};
        take_found();
        for (int i = 0; i < 6; i++) {
            delay(10);
            play(f[i]);
        }
        nosound();
        draw_score(1000);
        break;
    }
    case 0x56:
        take_found();
        got_m = 1;
        sound_pickup();
        letters();
        break;
    case 0x57:
        take_found();
        got_a = 1;
        sound_pickup();
        letters();
        break;
    case 0x58:
        take_found();
        sound_pickup();
        got_r = 1;
        letters();
        break;
    case 0x59:
        take_found();
        got_s = 1;
        sound_pickup();
        letters();
        break;
    case 0x70: /* computer terminal */
        if (SC != ENTER)
            break;
        delay(200);
        sound_switch();
        switch (gd->terminal_action[level]) {
        case 1:
            flash(1);
            show_blocks_5c();
            break;
        case 2:
            flash(0x0E);
            hide_blocks_5e();
            break;
        case 3:
            flash(2);
            reveal_gems();
            break;
        case 4:
            flash(2);
            flash(0x0E);
            hide_blocks_5e();
            deactivate_rings();
            show_rescue_message();
            draw_score(50000);
            break;
        }
        break;
    }
}

/* ------------------------------------------------------------------ moving things ---- */

/* UpdateVPlatforms (1054:38a9): state 1 up, 0 down, 2 stopped; one row every other frame,
 * turning at a solid colour-3 byte (FFh) or the top. */
static void update_vplatforms(void)
{
    if (++vplat_tick == 2)
        vplat_tick = 0;
    for (int i = 0; i < lv.vplat.n; i++) {
        rec_t *r = &lv.vplat.r[i];
        blit_vplat(tile(0x04), r->row, r->col);
        if (r->s == 1 && vplat_tick == 0) {
            if (probe[0] == 0xFF || r->row < 8)
                r->s = 0;
            else
                r->row--;
        } else if (r->s == 0 && vplat_tick == 0) {
            if (probe[1] == 0xFF || probe[2] == 0xFF)
                r->s = 1;
            else
                r->row++;
        }
    }
}

/* UpdateHPlatforms (1054:398e): state 1 left, 0 right; a byte every 3rd frame, carrying the
 * player when they stand on it (8 rows above) unless a wall is in the way. */
static void update_hplatforms(void)
{
    if (++hplat_tick == 3)
        hplat_tick = 0;
    for (int i = 0; i < lv.hplat.n; i++) {
        rec_t *r = &lv.hplat.r[i];
        blit_hplat(tile(0x04), r->row, r->col);
        const int riding = px - r->col < 3 && px - r->col > -3 && py - r->row == -8;
        if (r->s == 1 && hplat_tick == 0) {
            if (probe[4] == 0xFF || r->col == 0) {
                r->s = 0;
            } else {
                if (riding) {
                    blit_masked_buf2_collide(tile(0), py, px); /* only for the wall samples */
                    if (!collide.wall_left)
                        px--;
                }
                r->col--;
            }
        } else if (r->s == 0 && hplat_tick == 0) {
            if (probe[5] == 0xFF || r->col > 0x4B) {
                r->s = 1;
            } else {
                if (riding) {
                    blit_masked_buf2_collide(tile(0), py, px);
                    if (!collide.wall_right)
                        px++;
                }
                r->col++;
            }
        }
    }
}

/* EnemyExplosion (1054:2a5b). */
static void enemy_explosion(int col, int row)
{
    static const int f[] = {300, 250, 200, 250, 300, 250};
    for (int i = 0; i < 6; i++) {
        delay(10);
        play(f[i]);
    }
    nosound();
    for (int t = 0x2A; t <= 0x2E; t++)
        blit_masked_buf2(tile(t), row, col);
}

/* The three walkers (UpdateEnemies49 / 4A / 4B, 1054:4fe1 / 51db / 53e7) are one routine
 * with different tiles, speeds, wall tests and hit boxes. State 1 walks left, 0 right, 2 is
 * dead. A turn skips the hit tests for that frame. */
typedef struct {
    list_t *list;
    int *tick, *frame;
    int tick_len;            /* moves on 1 frame of this many */
    int first_tile;
    void (*blit)(const uint8_t *, int, int);
    int kind;                /* 0x49, 0x4A, 0x4B: which turning rule */
    int shot_dx, shot_dy;    /* a shot hits within these */
    unsigned points;
} walker_t;

static void update_walkers(const walker_t *w)
{
    if (++*w->tick == w->tick_len)
        *w->tick = 0;
    if (*w->tick == 0)
        *w->frame += 0x80;
    if (*w->frame == 0x200)
        *w->frame = 0;
    for (int i = 0; i < w->list->n; i++) {
        rec_t *r = &w->list->r[i];
        if (r->s == 2)
            continue;
        w->blit(tile(w->first_tile + *w->frame / TILE_BYTES), r->row, r->col);
        int turn;
        if (r->s == 1) {
            if (w->kind == 0x4A)
                turn = probe[0] != 0 || r->col == 0;
            else /* 49 hangs from the ceiling, 4B walks on the floor: turn at an edge or a wall */
                turn = probe[0] == 0 || probe[2] != 0;
            if (turn) {
                r->s = 0;
                continue;
            }
            if (*w->tick == 0)
                r->col--;
        } else if (r->s == 0) {
            if (w->kind == 0x4A)
                turn = probe[1] != 0 || r->col > 0x4B;
            else
                turn = probe[1] == 0 || probe[3] != 0;
            if (turn) {
                r->s = 1;
                continue;
            }
            if (*w->tick == 0)
                r->col++;
        }
        if (shot_active && abs(r->col - shot_col) < w->shot_dx &&
            abs(r->row - shot_row) < w->shot_dy) {
            r->s = 2;
            shot_active = 0;
            enemy_explosion(r->col, r->row);
            draw_score(w->points);
        }
        if (w->kind == 0x4B ? abs(r->col - px) < 2 && abs(r->row - py) < 8
                            : abs(r->col - px) < 3 && abs(r->row - py) < big_hitbox + 8)
            kill_player(0);
    }
}

static const walker_t walkers[3] = {
    {&lv.enemy49, &e49_tick, &e49_frame, 3, 0x49, blit_enemy49, 0x49, 3, 5, 500},
    {&lv.enemy4a, &e4a_tick, &e4a_frame, 2, 0x4D, blit_enemy4a, 0x4A, 4, 7, 500},
    {&lv.enemy4b, &e4b_tick, &e4b_frame, 2, 0x51, blit_enemy4b, 0x4B, 4, 7, 250},
};

/* UpdateDrips (1054:55d9): one drop at a time, from a random byte of a random 18..19 range,
 * falling a row per frame until it lands on something. */
static void update_drips(void)
{
    if (lv.drips.n == 0)
        return;
    if (!drip_active) {
        const int which = tc_rand() % lv.drips.n;
        const int off = tc_rand() % (lv.drips.r[which].s + 1);
        drip_col = lv.drips.r[which].col + off;
        drip_row = lv.drips.r[which].row;
        drip_active = 1;
        sound_drip();
        return;
    }
    const int row = drip_row++;
    blit_drip(tile(0x26), row, drip_col);
    if (abs(drip_col - px) < 2 && abs(drip_row - py) < big_hitbox + 8)
        kill_player(0);
    else if (probe[0] || probe[1])
        drip_active = 0;
}

/* UpdateTurret (1054:56c9, "UpdateEntity46"): when the player is within 6 rows, fire a bolt
 * (tile 2A) their way, a byte per frame, until it hits a wall in back buffer 1, the edge, or
 * them. */
static void update_turret(void)
{
    if (!lv.has_turret)
        return;
    if (!bolt_active) {
        if (abs(lv.turret_row - py) < 7) {
            bolt_dir = px < lv.turret_col ? -1 : 1;
            bolt_active = 1;
            bolt_col = (uint8_t)(lv.turret_col + bolt_dir * 3);
            static const int f[] = {1200, 1350, 900, 850, 300, 650};
            for (int i = 0; i < 6; i++) {
                delay(6);
                play(f[i]);
            }
            nosound();
        }
        return;
    }
    if (bolt_dir == 1)
        blit_masked_buf2(tile(0x2A), lv.turret_row, bolt_col);
    const long at = DS_BACK1 + (lv.turret_row + 4) * CGA_STRIDE + bolt_col + bolt_dir;
    const uint8_t a = ds_peek(at), b = ds_peek(at + 1);
    bolt_col = (uint8_t)(bolt_col + bolt_dir);
    if (bolt_dir == -1)
        blit_masked_buf2(tile(0x2A), lv.turret_row, bolt_col);
    if (!a && !b && bolt_col != 0 && bolt_col < 0x4E) {
        if (abs(bolt_col - px) < 3 && abs(lv.turret_row - py) < 4) {
            bolt_active = 0;
            kill_player(0);
        }
    } else {
        bolt_active = 0;
    }
}

/* DrawBoulders (1054:5ba4): drawn before everything else moves, so they collide. */
static void draw_boulders(void)
{
    for (int i = 0; i < lv.boulders.n; i++)
        blit_boulder(tile(0x6E), lv.boulders.r[i].row, lv.boulders.r[i].col);
}

/* UpdateBoulders (1054:5978): fall; slide off an edge; get pushed by the player (recognised
 * by the bytes of the player's sprite next to it); stop shots and bolts. */
static void update_boulders(void)
{
    for (int i = 0; i < lv.boulders.n; i++) {
        rec_t *r = &lv.boulders.r[i];
        blit_tile_buf2(tile(0), r->row, r->col);
        blit_boulder(tile(0), r->row, r->col); /* tile 0 draws nothing: just the samples */
        const uint8_t below_l = probe[0], below_r = probe[1], left = probe[2], right = probe[3];
        if (!below_l && !below_r) {
            r->row++;
        } else {
            if (below_l && !below_r && r->col <= 0x4B) {
                r->row++;
                r->col++;
            } else if (!below_l && below_r && r->col != 0) {
                r->row++;
                r->col--;
            } else if ((right == 0x0B || right == 0x02 || right == 0xFF) && left == 0) {
                /* the player on the right pushes it left */
                if (r->col != 0)
                    r->col--;
            } else if ((left == 0x80 || left == 0xE0 || left == 0xFF) && right == 0) {
                /* ... or from the left */
                if (r->col < 0x4C)
                    r->col++;
            } else {
                goto drawn;
            }
            play(1300);
            nosound();
        }
    drawn:
        blit_boulder(tile(0x6E), r->row, r->col);
        if (shot_active && abs(r->col - shot_col) < 4 && abs(r->row - shot_row) < 8)
            shot_active = 0;
        if (bolt_active && abs(r->col - bolt_col) < 4 && abs(r->row - lv.turret_row) < 8)
            bolt_active = 0;
    }
}

/* 1054:74bf: the stars (74-77) are drawn over everything, every frame. */
static void draw_stars(void)
{
    for (int i = 0; i < lv.special.n; i++) {
        const rec_t *r = &lv.special.r[i];
        if (r->s > 0x73 && r->s < 0x78)
            blit_masked_buf2(tile(r->s), r->row, r->col);
    }
}

/* ------------------------------------------------------------------ the player ---- */

static void end_shot(void)
{
    shot_active = 0;
    shot_dir = 0;
}

/* UpdatePlayerShot (1054:45d3). The shot looks at back buffer 1 (the level's picture) half a
 * tile down from its top; when there's something there it looks the map cell up to decide. */
static void update_player_shot(void)
{
    if (K.fire && !shot_active && shots) {
        shot_active = 1;
        shot_dir = frame_first == 0x1B ? 2 : -2;
        shot_col = (uint8_t)(shot_dir == -2 ? px : px + 2);
        shot_row = (uint8_t)py;
        shots--;
        draw_shots(9, 0, shots);
        static const int f[] = {900, 850, 800, 750, 800, 850};
        for (int i = 0; i < 6; i++) {
            delay(10);
            play(f[i]);
        }
        nosound();
    }
    if (!shot_active)
        return;
    if (shot_dir == 2)
        blit_masked_buf2(tile(0x0B), shot_row, shot_col);
    const long at = DS_BACK1 + (shot_row + 4) * CGA_STRIDE + shot_col + shot_dir;
    const uint8_t a = ds_peek(at), b = ds_peek(at + 1);
    shot_col = (uint8_t)(shot_col + shot_dir);
    if (shot_col > 0x4E) {
        end_shot();
        return;
    }
    if (shot_dir == -2)
        blit_masked_buf2(tile(0x0B), shot_row, shot_col);
    if (!a && !b)
        return;

    uint8_t *cell = &map()[(shot_row + 3) / 8 * LEVEL_COLS + shot_col / 4];
    switch (*cell) {
    /* the shot flies on through these */
    case 0x04: case 0x06: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0E:
        return;
    case 0x0F: case 0x12:
        switches(1);
        break;
    case 0x10: case 0x11:
        switches(0);
        break;
    case 0x16: /* a lever only turns when shot from the side it leans */
        if (shot_dir == 2)
            lever(0x16);
        break;
    case 0x17:
        if (shot_dir == -2)
            lever(0x17);
        break;
    case 0x6F: /* the turret's generator: destroys it */
        *cell = 0;
        lv.has_turret = 0;
        blit_tile_buf1(tile(0), lv.row_6f, lv.col_6f);
        blit_tile_buf1(tile(0x6C), lv.turret_row, lv.turret_col);
        enemy_explosion(lv.col_6f, lv.row_6f);
        enemy_explosion(lv.col_6f, lv.row_6f);
        for (int i = 0; i < lv.special.n; i++)
            if (lv.special_active[i] && lv.special.r[i].s == 0x46)
                lv.special_active[i] = 0;
        end_shot();
        draw_score(1500);
        return;
    default:
        /* 01-03, 05, 07, 08, 0D, 13-15, 18, 19, 43, 46, 5E, 66-6D and 78-7E stop it; the
         * rest let it through */
        if ((*cell >= 0x01 && *cell <= 0x19) || *cell == 0x43 || *cell == 0x46 ||
            *cell == 0x5E || (*cell >= 0x66 && *cell <= 0x6D) || (*cell >= 0x78 && *cell <= 0x7E))
            break;
        return;
    }
    end_shot();
}

/* Floor division by 2, as SAR does. */
static int sar1(int v)
{
    return v >= 0 ? v / 2 : -((1 - v) / 2);
}

/* UpdatePlayer (1054:3d43). */
static void update_player(void)
{
    if (++step_ctr == 3)
        step_ctr = 0;
    if ((K.left || K.right || K.jump) && step_ctr != 0) {
        if (K.left && !K.right && can_left) {
            if (px % 3 != 0 && on_ground) { /* footsteps */
                play(1000); play(400); play(1000); play(400); play(0x7FFF);
            }
            if (!facing_left) {
                facing_left = 1;
                frame_first = 0x21;
                frame = 0x20;
                frame_end = 0x25;
            } else {
                if (on_ground)
                    frame++;
                if (px > 0)
                    px--;
            }
        } else if (K.right && !K.left && can_right) {
            if (px % 3 != 0 && on_ground) {
                play(1000); play(400); play(1000); play(400); play(0x7FFF);
            }
            if (facing_left) {
                facing_left = 0;
                frame_first = 0x1B;
                frame = 0x1A;
                frame_end = 0x1F;
            } else {
                if (on_ground)
                    frame++;
                if (px < 0x4C)
                    px++;
            }
        }
        if (K.jump && !jump_over && can_jump && on_ground) {
            on_ground = 0;
            can_jump = 0;
            big_hitbox = 5;
            sound_jump();
        }
    }
    if (frame > frame_end - 1)
        frame = frame_first;

    update_vplatforms();
    update_hplatforms();

    if (!can_jump && !on_ground) { /* in the air */
        in_air = 1;
        frame = facing_left ? 0x25 : 0x1F;
        if (!flag_5ed) {
            const int c = find_special_at((uint8_t)py, (uint8_t)px);
            touch_tile(c);
            if (!collide.ceiling_l && !collide.ceiling_r &&
                (c == 0 || c == 0x16 || c == 0x17 || c == 0x0A || c == 0x09) && !jump_over) {
                if (++jump_sub == 3) {
                    jump_sub = 0;
                    jump_t++;
                }
                if (jump_t >> 1 == 0x10) { /* the top of the jump */
                    frame = frame_first;
                    jump_over = 1;
                    jump_t = 0;
                } else {
                    py -= sar1(9 - jump_t); /* rising, then (once jump_t > 9) falling */
                }
            } else {
                jump_over = 1;
                jump_sub = 0;
            }
        }
    }
    if (py < 0)
        py = 0;
    if (py > 0x54)
        py = 0x54;

    update_player_shot();
    blit_masked_buf2_collide(tile(frame), py, px);
    draw_stars();

    if (!collide.wall_left) {
        can_left = 1;
    } else {
        const int c = find_special_at((uint8_t)py, (uint8_t)(px - 1));
        touch_tile(c);
        if (c == 0)
            can_left = 0;
    }
    if (!collide.wall_right) {
        can_right = 1;
    } else {
        const int c = find_special_at((uint8_t)py, (uint8_t)(px + 1));
        touch_tile(c);
        if (c == 0)
            can_right = 0;
    }
    if (!collide.ground_l && !collide.ground_r) { /* falling */
        py += fall_v >> 1;
        if (fall_v < 8)
            fall_v++;
        on_ground = 0;
    } else {
        touch_tile(find_special_at((uint8_t)py, (uint8_t)px));
        if (!on_ground) {
            on_ground = 1;
            can_jump = 1;
        }
        fall_v = 1;
        jump_over = 0;
        jump_t = 0;
        in_air = 0;
    }
    flag_5ed = 0;
    if (collide.sunk_l || collide.sunk_r) { /* feet in the floor: step up */
        const int c = find_special_at((uint8_t)py, (uint8_t)px);
        touch_tile(c);
        if (!collide.ceiling_l && !collide.ceiling_r) {
            if (c == 0)
                py--;
            on_ground = 1;
            can_jump = 1;
            fall_v = 1;
            jump_t = 0;
        }
    }
}

/* ------------------------------------------------------------------ levels ---- */

/* DrawLevel (1054:31f3): the picture and the lists, and the per-level flags it resets. */
static void draw_level(void)
{
    level_draw(&lv, gd, level);
    px = lv.player_col;
    py = lv.player_row;
    jump_over = 1;
    drip_active = 0;
    shot_active = 0;
    bolt_active = 0;
    has_card = 0;
    got_m = got_a = got_r = got_s = 0;
    rings_done = 0;
    next_letter = 0;
}

/* CheckLevelExit (1054:43d2): the map cell the player stands in. */
static void check_level_exit(void)
{
    uint8_t *cell = &map()[(py >> 3) * LEVEL_COLS + ((px + 2) >> 2)];
    const int b = *cell;
    if (b == 0x64) { /* the exit */
        level_changed = 1;
        draw_score((unsigned)level * 100 + 2000);
        memcpy(map(), backup, LEVEL_BYTES);
        level++;
        score_at_start = score;
        shots_at_start = shots;
        if (level == LEVELS) {
            game_over = 1;
            for (int i = 0; i < 16; i++) {
                set_background(i);
                play(i * 30 % 480 + 400);
                delay(10);
                nosound();
            }
            delay(300);
            set_background(0);
            sound_pickup();
            blit_tile_screen(tile(0), py - 4, px);
            blit_tile_screen(tile(0x7F), py, px);
            draw_text(1, "******************", 10, 11);
            draw_text(1, "*                *", 11, 11);
            draw_text(1, "*                *", 12, 11);
            draw_text(1, "******************", 13, 11);
            draw_text(2, "CONGRATULATIONS!", 11, 12);
            draw_text(2, " PRESS ANY KEY.", 12, 12);
            SC = 0;
            delay(200);
            wait_for_key();
            show_ending();
        }
    } else if (b == 0x5B) { /* trigger plates: used up when stepped on */
        flash(0x0E);
        show_blocks_5c();
        *cell = 0;
    } else if (b == 0x5D) {
        flash(1);
        hide_blocks_5e();
        *cell = 0;
    } else if (b == 0x81 || b == 0x82) {
        show_rescue_message();
    }
}

/* RestartLevel (1054:5bf2): after dying. */
static void restart_level(void)
{
    int f = 0;
    for (int t = 0x2A; t <= 0x2E; t++) {
        play(f + 300); delay(10);
        play(f + 250); delay(10);
        play(f + 200); delay(10);
        play(f + 250);
        blit_tile_screen(tile(t), death_y, death_x);
        delay(10);
        play(f + 300); delay(10);
        play(f + 250);
        f -= 10;
        nosound();
        blit_tile_screen(tile(t), death_y, death_x);
    }
    level_changed = 1;
    shots = shots_at_start;
    score = score_at_start;
    draw_shots(9, 0, shots);
    draw_text(0, "       ", 0, 0x21);
    draw_score(0);
    memcpy(map(), backup, LEVEL_BYTES);
}

/* CheatLevelSkip (1054:5d20): Caps Lock on, PgDn = next level, ` = previous. */
static void cheat_level_skip(void)
{
    if (!plat_caps_lock())
        return;
    if (SC == 0x51 && level < LEVELS - 1) {
        memcpy(map(), backup, LEVEL_BYTES);
        level++;
        level_changed = 1;
        shots_at_start = shots;
        score_at_start = score;
    } else if (SC == 0x29 && level != 0) {
        memcpy(map(), backup, LEVEL_BYTES);
        level--;
        level_changed = 1;
    }
}

/* ------------------------------------------------------------------ main's loop ---- */

static void banner(int color, int row, const char *const *lines, int n)
{
    for (int i = 0; i < n; i++)
        draw_text(color, lines[i], row + i, 0x0E);
}

/* SaveGame (1054:7960): MARSn.SV1 holds the level and the shots it started with, as bytes;
 * MARSn.SV2 the score it started with, as text ("%lu\n", text mode). */
static void save_game(void)
{
    char path[1100];
    gamedata_path(gd, "SV1", path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fputc(level, f);
    fputc(shots_at_start, f);
    fclose(f);
    gamedata_path(gd, "SV2", path, sizeof path);
    f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "%lu\r\n", score_at_start);
    fclose(f);
}

/* RestoreGame (1054:79fa): nothing happens without a saved game. */
static void restore_game(void)
{
    char path[1100];
    gamedata_path(gd, "SV1", path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    level = fgetc(f) & 0xFF; /* bytes: a short file gives FFh */
    shots_at_start = fgetc(f) & 0xFF;
    fclose(f);
    gamedata_path(gd, "SV2", path, sizeof path);
    f = fopen(path, "rb");
    if (f) { /* the original would fscanf a NULL FILE here */
        unsigned long v;
        if (fscanf(f, "%lu", &v) == 1)
            score_at_start = v & 0xFFFFFFFFUL;
        fclose(f);
    }
    score = score_at_start;
    shots = shots_at_start;
    draw_text(0, "        ", 0, 0x20);
}

/* "ARE YOU SURE (Y/N)" boxes for S and R. Then printf("?") puts a question mark at the BIOS
 * cursor, just after "(Y/N) ", with DOS's teletype output (ROM font, colour 3). */
static int ask_yes_no(const char *what)
{
    static const char *const box[] = {"**************", "*            *", "*            *",
                                      "*            *", "**************"};
    for (int i = 0; i < 5; i++)
        draw_text(1, box[i], 11 + i, 0x0D);
    draw_text(2, what, 12, 0x0E);
    draw_text(2, "ARE YOU SURE", 13, 0x0E);
    draw_text(2, "   (Y/N) ", 14, 0x0E);
    draw_text(3, "?", 14, 0x17);
    if (scripted)
        return 1;
    for (;;) {
        if (SC == 0x15)
            return 1;
        if (SC == 0x31 || plat_quitting())
            return 0;
        delay(1);
    }
}

/* The keys main handles at the top of each frame. Returns 1 to quit the game, 2 to skip the
 * rest of the frame. */
static int frame_keys(void)
{
    if (SC == 0x10 || SC == 0x01) { /* Q / Esc */
        static const char *const box[] = {"************", "*          *", "*          *", "************"};
        banner(1, 10, box, 4);
        draw_text(2, "  PAUSED  ", 11, 0x0F);
        draw_text(2, "QUIT (Y/N)", 12, 0x0F);
        for (;;) {
            if (SC == 0x15 || plat_quitting()) {
                nosound();
                return 1;
            }
            if (SC == 0x31)
                break;
            delay(1);
        }
    }
    if (SC == 0x1F && ask_yes_no("  SAVING..  ")) /* S */
        save_game();
    if (SC == 0x13) { /* R: either way, the rest of the frame is skipped */
        if (ask_yes_no(" RESTORING.")) {
            memcpy(map(), backup, LEVEL_BYTES);
            restore_game();
            level_changed = 1;
            draw_score(0);
        }
        return 2;
    }
    if (SC == 0x40) { /* F6: give up */
        death_x = px;
        death_y = py;
        died = 1;
    }
    if (SC == 0x3F) { /* F5: pause */
        delay(300);
        while (SC != 0x3F && !plat_quitting())
            delay(1);
        delay(300);
    }
    if (SC == 0x3B) { /* F1: sound */
        static const char *const box[] = {"************", "*          *", "************"};
        banner(1, 11, box, 3);
        draw_text(2, sound_on ? "SOUND OFF." : "SOUND  ON.", 12, 0x0F);
        sound_on = !sound_on;
        delay(551);
    }
    return 0;
}

static void init(gamedata_t *g, int first_level, int start_shots)
{
    gd = g;
    K.left = K.right = K.jump = K.fire = false;
    game_over = 0;
    level = first_level;
    shots = shots_at_start = start_shots;
    score = score_at_start = 0;
    cga_set_mode(0, 0); /* SetVideoMode(4) */
    text_set_high_font(g, FONT_NORMAL);
    draw_text(3, "     ", 0, 0x14);
    draw_score(0);
}

static void start_level(void)
{
    draw_text(3, "LEVEL    ", 0, 0);
    draw_text(3, "SCORE", 0, 0x1B);
    memcpy(backup, map(), LEVEL_BYTES);
    draw_number(1, 2, level + 1, 0, 7);
    draw_text(2, "     ", 0, 0x14); /* DrawLevel's first act: clear "CARD!" */
    draw_level();
    level_changed = 0;
    draw_shots(9, 0, shots);
}

/* The part of the frame before the timer wait. Returns 0 to go on, 1 if the game ended. */
static int frame_update(void)
{
    copy_background();
    level_animate_tiles(&lv, gd);
    draw_boulders();
    for (int i = 0; i < 3; i++)
        update_walkers(&walkers[i]);
    update_drips();
    if (big_hitbox)
        big_hitbox = 0;
    update_player();
    if (game_over)
        return 1;
    update_boulders();
    update_turret();
    present_frame();
    check_level_exit();
    return 0;
}

/* A --script token: "<frames><keys>", keys from l r j f e (left, right, jump, fire, Enter). */
static const char *script_step(const char *p, int *frames)
{
    while (*p == ' ' || *p == ',')
        p++;
    if (!*p)
        return NULL;
    *frames = (int)strtol(p, (char **)&p, 10);
    K.left = K.right = K.jump = K.fire = false;
    SC = 0;
    for (; *p && *p != ' ' && *p != ','; p++) {
        if (*p == 'l') { K.left = true; SC = 0x4B; }
        if (*p == 'r') { K.right = true; SC = 0x4D; }
        if (*p == 'j') { K.jump = true; SC = K.key_jump; }
        if (*p == 'f') { K.fire = true; SC = K.key_fire; }
        if (*p == 'e') SC = ENTER;
        if (*p == 'S') SC = 0x1F; /* save, answering Y */
        if (*p == 'R') SC = 0x13; /* restore, answering Y */
    }
    return p;
}

/* The level start and frame loop (1054:7c1a-806e). With a script, the keys come from it
 * instead of the keyboard and frames don't wait for the timer. */
static void run(gamedata_t *g, int first_level, int start_shots, const char *script)
{
    init(g, first_level, start_shots);
    plat_isr_installed = !script;
    scripted = script != NULL;
    unsigned last = plat_ticks();
    int frames = 0, total = 0, deaths = 0;
    const char *p = script;
    while (!plat_quitting()) {
        if (level >= LEVELS) /* after the last level: the original draws "LEVEL 21" first */
            break;
        start_level();
        while (!plat_quitting()) {
            if (script) {
                while (frames == 0 && p)
                    p = script_step(p, &frames);
                if (!p && frames == 0)
                    goto quit;
                frames--;
                total++;
            }
            const int k = frame_keys();
            if (k == 1)
                goto quit;
            if (k == 2) {
                if (level_changed)
                    break;
                continue;
            }
            if (frame_update())
                goto quit;
            if (!script) { /* wait for the timer ISR's tick ([CCD1]) */
                while (plat_ticks() == last && !plat_quitting())
                    delay(1);
                last = plat_ticks();
            }
            if (died) {
                died = 0;
                deaths++;
                flash(4); /* FlashBackground */
                restart_level();
            }
            cheat_level_skip();
            if (level_changed)
                break;
        }
    }
quit:
    if (script)
        printf("frames %d: level %d, x %d, y %d, shots %d, score %lu, deaths %d%s\n", total,
               level + 1, px, py, shots, score, deaths, game_over ? ", game over" : "");
    /* back to the menu loop: put the level back, then CheckHighScore */
    if (level < LEVELS)
        memcpy(map(), backup, LEVEL_BYTES);
    nosound();
    plat_isr_installed = false;
    if (!script && !plat_quitting())
        hiscore_check(g, score);
}

void game_play(gamedata_t *g, int first_level, int start_shots)
{
    run(g, first_level, start_shots, NULL);
}

void game_run_script(gamedata_t *g, int first_level, int start_shots, const char *script)
{
    run(g, first_level, start_shots, script);
}
