/* Playing the game. Each function is a port of the game routine named in its comment; the
 * state below mirrors the game's variables in its data segment (DS 2354 in MARS1), with their
 * addresses, since most of them are shared between routines.
 *
 * The original's quirks are kept: TouchTile can run several times in one frame (so holding
 * Enter on a lever can flip it and flip it back), a dead enemy can still kill on the frame
 * it's shot, the letters bonus pays 2000 for every letter flag still set, and so on. */
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gamepad.h"
#include "gfx.h"
#include "hiscore.h"
#include "level.h"
#include "options.h"
#include "platform.h"
#include "settings.h"
#include "smooth.h"
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

static int scripted;               /* --script: Y/N questions answer yes */

/* game.physics = modern (see "modern physics" below). The rest of the game is shared, with a
 * few differences that only exist when this is set. */
static int modern;
static bool m_enter;               /* Enter went down at this step: one press, one use */
static int m_exit_cell = -1;       /* the map cell CheckLevelExit last saw the player in */
static void m_explosion(float x, float y);

#define K (isr_keys)
#define SC (isr_keys.scancode) /* [C8AA] */
#define ENTER 0x1C

/* ------------------------------------------------------------------ helpers ---- */

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

/* The original waits while ([C8AA] == 0), so letting go of a key (a break code) goes on too:
 * Enter held a moment too long on the rescue terminal let go of its message at once. The
 * port waits for a key pressed (a make code) instead. */
static void wait_for_key(void)
{
    while ((SC == 0 || (SC & 0x80)) && !plat_quitting())
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

/* Enter used on a switch, lever or terminal. The original tests the last key ([C8AA]), so it
 * acts on every frame Enter is held; modern counts one use per press. */
static bool use_enter(void)
{
    if (!modern)
        return SC == ENTER;
    const bool e = m_enter;
    m_enter = false;
    return e;
}

/* The pause before a switch's sound, which keeps a held Enter from flipping it every frame;
 * modern has no need of it. */
static void debounce(void)
{
    if (!modern)
        delay(200);
}

/* ------------------------------------------------------------------ sounds ---- */

/* The effects stop the game while they play (they wait with delay()); with modern they are
 * handed to the audio thread instead. */
static void sfx_begin(void)
{
    if (modern)
        plat_sfx_begin();
}

static void sfx_end(void)
{
    if (modern)
        plat_sfx_end(false);
}

/* ... and one that plays to its end: the letters' fanfare, which a jump straight after the S
 * otherwise cut off (the original plays it with the game stopped). */
static void sfx_end_kept(void)
{
    if (modern)
        plat_sfx_end(true);
}

static void sound_jump(void) /* 1054:068b */
{
    static const int f[] = {700, 500, 700, 900, 1500, 1900, 2200};
    sfx_begin();
    for (int i = 0; i < 7; i++) {
        delay(8);
        play(f[i]);
    }
    nosound();
    sfx_end();
}

static void sound_drip(void) /* 1054:0724 */
{
    static const int f[] = {800, 1000, 1200, 1400, 1600};
    sfx_begin();
    for (int i = 0; i < 5; i++) {
        delay(8);
        play(f[i]);
    }
    nosound();
    sfx_end();
}

static void sound_switch(void) /* 1054:0795 */
{
    static const int f[] = {200, 300, 400, 500, 600, 400};
    sfx_begin();
    for (int i = 0; i < 6; i++) {
        delay(8);
        play(f[i]);
    }
    delay(8);
    nosound();
    sfx_end();
}

static void sound_pickup(void) /* 1054:0825 */
{
    static const int f[] = {200, 210, 220, 250};
    sfx_begin();
    for (int i = 0; i < 4; i++) {
        delay(10);
        play(f[i]);
    }
    delay(10);
    nosound();
    sfx_end();
}

static void sound_gem(void) /* in TouchTile */
{
    static const int f[] = {2000, 1500, 1750, 2000, 1500, 1250};
    sfx_begin();
    for (int i = 0; i < 6; i++) {
        delay(10);
        play(f[i]);
    }
    nosound();
    sfx_end();
}

static void sound_explosion(void) /* in EnemyExplosion */
{
    static const int f[] = {300, 250, 200, 250, 300, 250};
    sfx_begin();
    for (int i = 0; i < 6; i++) {
        delay(10);
        play(f[i]);
    }
    nosound();
    sfx_end();
}

static void sound_shot(void) /* in UpdatePlayerShot */
{
    static const int f[] = {900, 850, 800, 750, 800, 850};
    sfx_begin();
    for (int i = 0; i < 6; i++) {
        delay(10);
        play(f[i]);
    }
    nosound();
    sfx_end();
}

static void sound_bolt(void) /* in UpdateTurret */
{
    static const int f[] = {1200, 1350, 900, 850, 300, 650};
    sfx_begin();
    for (int i = 0; i < 6; i++) {
        delay(6);
        play(f[i]);
    }
    nosound();
    sfx_end();
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
            if (use_enter()) {
                debounce();
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
            sfx_begin();
            for (int i = 1; i < 25; i++)
                for (int j = 1; j < 14; j++) {
                    play(i * i * j);
                    delay(j + 1);
                }
            nosound();
            sfx_end_kept();
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

/* ShowRescueMessage (1054:0b18): a row of text per row of the screen from row 1 (a blank one),
 * the text from row 2; waits for a key. MARS2 and MARS3 have MARS1's text and rows; MARS4's
 * text is shorter but it draws 24 rows, the key prompt on the last (notes.md, "The volumes'
 * texts"). Colour 8 is a blank row. */
static void show_rescue_message(void)
{
    static const int colors1[20] = {8, 1, 1, 1, 8, 3, 3, 3, 8, 1, 1, 1, 8, 3, 3, 3, 3, 8, 1, 8};
    static const int colors4[24] = {8, 1, 1, 1, 8, 3, 3, 3, 8, 2, 2, 2, 8, 3, 8, 8, 8, 8, 8, 8, 8, 8, 8, 1};
    const int *colors = gd->volume == 4 ? colors4 : colors1, n = gd->volume == 4 ? 24 : 20;
    const char *s = gamedata_find_string(gd, " YOU HAVE DEACTIVATED");
    draw_text(8, "                                        ", 1, 0);
    for (int i = 1; i < n && s; i++, s = gamedata_next_string(gd, s))
        draw_text(colors[i], s, i + 1, 0);
    delay(200);
    SC = 0;
    wait_for_key();
    SC = 0;
}

/* 1054:0ca4: after the last level. MARS4 has an ending of its own ("A SUCCESSFUL MISSION!",
 * image 0122C there); the first and last lines are in the normal font, the rest the
 * alternate one. */
static void show_ending(void)
{
    typedef struct { int color, row, col; } at_t;
    static const at_t at1[18] = {
        {3, 1, 7}, {1, 3, 0}, {1, 4, 0}, {1, 5, 0}, {1, 6, 0}, {2, 8, 6}, {2, 9, 6},
        {2, 10, 6}, {2, 11, 6}, {1, 13, 0}, {1, 14, 0}, {1, 15, 0}, {1, 16, 0}, {1, 18, 0},
        {3, 19, 0}, {1, 21, 0}, {1, 22, 0}, {2, 24, 0},
    };
    static const at_t at4[19] = {
        {3, 1, 9}, {3, 3, 0}, {3, 4, 0}, {1, 6, 8}, {2, 8, 0}, {2, 9, 0}, {1, 10, 6},
        {1, 11, 6}, {1, 12, 6}, {1, 13, 6}, {2, 15, 0}, {2, 16, 0}, {2, 17, 0}, {2, 18, 0},
        {1, 20, 12}, {1, 21, 12}, {3, 22, 3}, {3, 23, 5}, {2, 24, 0},
    };
    const bool vol4 = gd->volume == 4;
    const at_t *at = vol4 ? at4 : at1;
    const int n = vol4 ? 19 : 18;
    cga_clear(); /* ClearScreen (1054:0471) */
    const char *s = gamedata_find_string(gd, vol4 ? "A SUCCESSFUL MISSION" : "YOUR JOURNEY CONTINUES");
    for (int i = 0; i < n && s; i++, s = gamedata_next_string(gd, s)) {
        text_set_high_font(gd, i == 0 || i == n - 1 ? FONT_NORMAL : FONT_ALT);
        draw_text(at[i].color, s, at[i].row, at[i].col);
    }
    text_set_high_font(gd, FONT_NORMAL);
    delay(200);
    SC = 0;
    wait_for_key();
    game_over = 1;
}

/* MARS2-4, code 81 (or 82) entered: the face etched in stone (image 0491D in MARS2, 04987 in
 * MARS4), once a level ([05A6], cleared by DrawLevel). Its 19 lines of text are on rows 1-2,
 * 4-6, 8-12, 14-18 and 20-23 in the alternate font; MARS4's text also has the blank rows
 * between, drawn in colour 0. Then the key prompt on row 24. MARS4 then takes the stone out
 * of the level's picture: tile 0 into back buffer 1 where DrawLevel saw the last 81
 * ([8A27] / [8A28]). */
static int face_shown; /* [05A6] */

static void show_face_message(void)
{
    static const struct { int row, color; } line[19] = {
        {1, 1}, {2, 1}, {4, 2}, {5, 2}, {6, 2}, {8, 3}, {9, 3}, {10, 3}, {11, 3}, {12, 3},
        {14, 2}, {15, 2}, {16, 2}, {17, 2}, {18, 2}, {20, 3}, {21, 3}, {22, 3}, {23, 3},
    };
    face_shown = 1;
    text_set_high_font(gd, FONT_ALT);
    const char *s = gamedata_find_string(gd, " YOU HAVE DISCOVERED");
    for (int k = 0; k < 19 && s; s = gamedata_next_string(gd, s)) {
        if (strspn(s, " ") == strlen(s)) { /* MARS4's blank rows, before the next line */
            draw_text(0, s, line[k].row - 1, 0);
            continue;
        }
        draw_text(line[k].color, s, line[k].row, 0);
        k++;
    }
    if (s)
        draw_text(1, s, 24, 0); /* PRESS ANY KEY TO CONTINUE. */
    delay(200);
    SC = 0;
    wait_for_key();
    SC = 0;
    text_set_high_font(gd, FONT_NORMAL);
    if (gd->volume == 4) {
        const uint8_t *m = map();
        for (int i = LEVEL_BYTES - 1; i >= 0; i--)
            if (m[i] == 0x81) {
                blit_tile_buf1(tile(0), i / LEVEL_COLS * 8, i % LEVEL_COLS * 4);
                break;
            }
    }
}

/* TouchTile (1054:2b08): what touching special `code` does. */
static void touch_tile(int code)
{
    switch (code) {
    case 0x09: /* force-field switch, off */
    case 0x0A: /* ... and on */
        if (!use_enter())
            break;
        debounce();
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
        if (!use_enter())
            break;
        debounce();
        sound_switch();
        lever(code);
        break;
    case 0x34: case 0x37: case 0x3D: case 0x40: case 0x46:
        kill_player(0);
        break;
    case 0x43: /* the energy rings kill 2 rows higher (for RestartLevel's explosion) */
        kill_player(-2);
        break;
    case 0x3A: /* gem */
        take_found();
        sound_gem();
        draw_score(1000);
        break;
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
        if (!use_enter())
            break;
        debounce();
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
        smooth_id = 0x100 + i;
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
        smooth_id = 0x200 + i;
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
    if (modern) {
        m_explosion((float)(col * 4), (float)(row * 2));
        return;
    }
    sound_explosion();
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
        smooth_id = 0x300 + (w->kind - 0x49) * 0x40 + i;
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
        const int which = plat_rand() % lv.drips.n;
        const int off = plat_rand() % (lv.drips.r[which].s + 1);
        drip_col = lv.drips.r[which].col + off;
        drip_row = lv.drips.r[which].row;
        drip_active = 1;
        sound_drip();
        return;
    }
    const int row = drip_row++;
    smooth_id = 0x400;
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
            sound_bolt();
        }
        return;
    }
    if (bolt_dir == 1) {
        smooth_id = 0x401;
        blit_masked_buf2(tile(0x2A), lv.turret_row, bolt_col);
    }
    const long at = DS_BACK1 + (lv.turret_row + 4) * CGA_STRIDE + bolt_col + bolt_dir;
    const uint8_t a = ds_peek(at), b = ds_peek(at + 1);
    bolt_col = (uint8_t)(bolt_col + bolt_dir);
    if (bolt_dir == -1) {
        smooth_id = 0x401;
        blit_masked_buf2(tile(0x2A), lv.turret_row, bolt_col);
    }
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
        smooth_id = 0x500 + i;
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

/* The shot has met something in the level's picture, in map cell `cell`: switches and levers
 * flip, the turret's generator blows up, and the shot stops or flies on. */
static void shot_hit(uint8_t *cell)
{
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
        sound_shot();
    }
    if (!shot_active)
        return;
    if (shot_dir == 2) {
        smooth_id = 0x402;
        blit_masked_buf2(tile(0x0B), shot_row, shot_col);
    }
    const long at = DS_BACK1 + (shot_row + 4) * CGA_STRIDE + shot_col + shot_dir;
    const uint8_t a = ds_peek(at), b = ds_peek(at + 1);
    shot_col = (uint8_t)(shot_col + shot_dir);
    if (shot_col > 0x4E) {
        end_shot();
        return;
    }
    if (shot_dir == -2) {
        smooth_id = 0x402;
        blit_masked_buf2(tile(0x0B), shot_row, shot_col);
    }
    if (!a && !b)
        return;
    shot_hit(&map()[(shot_row + 3) / 8 * LEVEL_COLS + shot_col / 4]);
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
    smooth_id = 1;
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
    face_shown = 0;
}

/* CheckLevelExit (1054:43d2): the map cell the player stands in. */
static void check_level_exit(void)
{
    uint8_t *cell = &map()[(py >> 3) * LEVEL_COLS + ((px + 2) >> 2)];
    const int b = *cell;
    /* The original shows the rescue message on every frame the player is on its cell, but
     * leaves it within a frame or two; modern steps are smaller, so it shows on entering. */
    const int entered = !modern || cell - map() != m_exit_cell;
    if (modern)
        m_exit_cell = (int)(cell - map());
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
    } else if (b == 0x81 || b == 0x82) { /* MARS1: the rescue message; MARS2-4: the stone face */
        if (gd->volume == 1 ? entered : !face_shown) {
            if (gd->volume == 1)
                show_rescue_message();
            else
                show_face_message();
        }
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

/* ------------------------------------------------------------------ modern physics ---- */

/* game.physics = modern (port enhancement). The original moves things a whole step per frame,
 * 18.2 frames a second: 4 pixels across (a byte of video memory) and 2 down. Its collision
 * tests read bytes of the frame being drawn. Here the same things move at the same speeds,
 * but in pixels and fractions of a pixel, M_HZ fixed steps a second, testing the level's
 * picture pixel by pixel with the original's test points. The drawing is separate: it
 * happens as often as the display can show a new picture, between the last two steps.
 *
 * Speeds are in pixels per original frame (TICK_HZ a second); a step is M_TICK frames. Each
 * thing keeps its position in floats; the original's byte columns and rows (px, py, the
 * lists' col / row) are kept rounded from them, for the code shared with the original. */
#define M_HZ 120.0
#define TICK_HZ (1193182.0 / 65536.0)
#define M_TICK ((float)(TICK_HZ / M_HZ))

#define M_WALK (8.0f / 3)  /* 4 px on 2 frames of 3 */
#define M_VPLAT 1.0f       /* 2 px every 2nd frame */
#define M_HPLAT (4.0f / 3) /* 4 px every 3rd frame */
#define M_DRIP 2.0f
#define M_BOLT 4.0f
#define M_SHOT 8.0f
#define M_FALL 2.0f        /* boulders */
#define M_JUMP_TOP 96.0f   /* the jump's own push ends after 96 frames (JumpT 32) */
#define M_MAX_X (0x4C * 4)
/* The original clamps py to 54h before its gravity, which then carries it up to 4 rows on:
 * at the bottom of an open shaft the player stays at 54h-58h, and the exits there are in the
 * map's last row, reached at 58h. */
#define M_MAX_Y (0x58 * 2)
#define M_STEP_UP 5        /* the most the feet climb in a step (the original: a row a frame) */

typedef struct {
    float x, y;   /* px; y in scanlines of the back buffers */
    float ox, oy; /* at the step before, to draw in between */
} mpos_t;

static mpos_t m_pl, m_shot, m_bolt, m_drip;
static mpos_t m_vp[MAX_LIST], m_hp[MAX_LIST], m_bo[MAX_LIST], m_wk[3][MAX_LIST];
static float m_air;      /* frames since the feet left the ground: gravity's speed (FallV) */
static float m_jump;     /* frames since the jump started: its push (JumpT) */
static float m_walked;   /* px walked, for the walking frames */
static float m_tiles;    /* frames since the animated tiles last changed */
static float m_anim[3];  /* frames, for the walkers' animation */
static int m_wframe[3];
static int m_support;    /* what the player stands on (an m_at result) */
static int m_wall_l, m_wall_r, m_ceiling; /* the player's last wall and ceiling tests */
/* A jump needs a fresh press (the original jumps again on landing while it's held, which at
 * 120 steps a second under a low ceiling is a new jump, and its sound, every step). A press
 * shortly before landing still counts: m_jump_press is how many frames it has left. */
#define M_JUMP_BUFFER 2.0f
static bool m_jump_was_held;
static float m_jump_press;
/* So does a shot (the original fires again as soon as a shot ends while fire is held), with
 * the same 2 frames' grace for a press just before the last shot ends. */
static bool m_fire_was_held;
static float m_fire_press;

/* Explosions: the original draws its 5 tiles over each other for one frame; here they play. */
typedef struct { float x, y, t; } fx_t;
static fx_t m_fx[8];
static int m_nfx;

static void m_explosion(float x, float y)
{
    sound_explosion();
    if (m_nfx < (int)(sizeof m_fx / sizeof m_fx[0]))
        m_fx[m_nfx++] = (fx_t){x, y, 0};
}

static int to_col(float x)
{
    return (int)floorf(x / 4 + 0.5f);
}

static int to_row(float y)
{
    return (int)floorf(y / 2 + 0.5f);
}

static void m_sync_player(void)
{
    px = to_col(m_pl.x);
    py = to_row(m_pl.y);
}

/* The original's |a - b| < n tests, on byte columns (cols) and rows: the same threshold
 * halfway between the distances it lets through and the first it doesn't. */
static bool near(float dx, float dy, int cols, int rows)
{
    return fabsf(dx) < cols * 4 - 2 && fabsf(dy) < rows * 2 - 1;
}

/* ------------------------------------------- the world, pixel by pixel */

enum { W_BG = 1, W_PLAT = 2, W_BOULDER = 4, W_PLAYER = 8, W_SOLID = W_BG | W_PLAT | W_BOULDER,
       W_PICKUPS_OPEN = 16, /* the level, except the pickups' pixels (only the player's tests) */
       W_BODY = W_SOLID | W_PICKUPS_OPEN, /* what the player can't move through */
       W_SPECIALS_OPEN = 32, /* ... nor the walk-through specials' pixels (the step-up's test) */
       W_CONTROLS_OPEN = 64 }; /* ... nor the switches', levers' and terminals' (m_wall, m_ground) */
#define HIT_BG 1
#define HIT_VPLAT 0x100
#define HIT_HPLAT 0x200
#define HIT_BOULDER 0x300
#define HIT_PLAYER 0x400
static int m_self; /* the thing asking, which doesn't see itself */

/* The colour of pixel (lx, ly) of tile n, 0 where it has none. */
static int tile_pixel(int n, int lx, int ly)
{
    const uint8_t *row = tile(n) + (ly & 1) * 0x40 + (ly >> 1) * 8;
    return (row[2 * (lx >> 2) + 1] >> (6 - 2 * (lx & 3))) & 3;
}

/* The things the player picks up by touching them: the key card, +3 shots, a gem, the letters
 * (and a hidden gem, which has no pixels until the terminal turns it into one). */
static bool is_pickup(int s)
{
    return s == 0x0C || s == 0x0E || s == 0x3A || (s >= 0x56 && s <= 0x59) || s == 0x5F;
}

/* The specials FindSpecialTileAt answers with their code, which the step-up doesn't climb:
 * all but the boulders, the surface, the door and the field switches (0F-12). */
static bool is_walk_through(int s)
{
    return s != 0x6E && s != 0x14 && s != 0x15 && s != 0x0D && !(s >= 0x0F && s <= 0x12);
}

/* The specials used with Enter where the player stands: the switches, levers and terminals. */
static bool is_control(int s)
{
    return s == 0x09 || s == 0x0A || (s >= 0x0F && s <= 0x12) || s == 0x16 || s == 0x17 || s == 0x70;
}

/* Scanline ly of tile n has a pixel. */
static bool m_tile_row_has_pixels(int n, int ly)
{
    for (int lx = 0; lx < 16; lx++)
        if (tile_pixel(n, lx, ly))
            return true;
    return false;
}

/* Pixel (x, y) is in a special still in the level that `kind` picks. */
static bool m_in_special(int x, int y, bool (*kind)(int))
{
    for (int i = 0; i < lv.special.n; i++) {
        const rec_t *r = &lv.special.r[i];
        if (lv.special_active[i] && kind(r->s) && x >= r->col * 4 && x < r->col * 4 + 16 &&
            y >= r->row * 2 && y < r->row * 2 + 16)
            return true;
    }
    return false;
}

static bool in_sprite(const mpos_t *p, int n, int x, int y)
{
    const int lx = x - (int)floorf(p->x), ly = y - (int)floorf(p->y);
    return lx >= 0 && lx < 16 && ly >= 0 && ly < 16 && tile_pixel(n, lx, ly);
}

/* A pixel of tile n at p overlaps one of the player's picture. The original's tests are boxes
 * around the two (3 byte columns and 8 rows for the walkers, 5 rows more on the frame a jump
 * starts), which kill a tile short of the picture: under a hovering robot, a jump is death
 * 26 px below it. */
static bool m_touches_player(int n, const mpos_t *p)
{
    const int x0 = (int)floorf(m_pl.x), y0 = (int)floorf(m_pl.y);
    const int dx = (int)floorf(p->x) - x0, dy = (int)floorf(p->y) - y0;
    if (abs(dx) >= 16 || abs(dy) >= 16)
        return false;
    for (int y = dy > 0 ? dy : 0; y < 16 && y - dy < 16; y++)
        for (int x = dx > 0 ? dx : 0; x < 16 && x - dx < 16; x++)
            if (tile_pixel(frame, x, y) && tile_pixel(n, x - dx, y - dy))
                return true;
    return false;
}

/* What is at pixel (x, y) of the playfield, of the kinds in `what`: 0 for nothing, else one
 * of the HIT_ values (plus the thing's index). A pixel that isn't black is solid, as the
 * original's bytes are; off the sides and below the level is wall. */
static int m_at(int x, int y, int what)
{
    if (x < 0 || x >= CGA_W || y >= LEVEL_ROWS * 16)
        return HIT_BG;
    if (y < 0)
        return 0;
    if ((what & W_BG) && cga_get_in(back1, x, y) && !((what & W_PICKUPS_OPEN) && m_in_special(x, y, is_pickup)) &&
        !((what & W_SPECIALS_OPEN) && m_in_special(x, y, is_walk_through)) &&
        !((what & W_CONTROLS_OPEN) && m_in_special(x, y, is_control)))
        return HIT_BG;
    if (what & W_PLAT) {
        for (int i = 0; i < lv.vplat.n; i++)
            if (in_sprite(&m_vp[i], 0x04, x, y))
                return HIT_VPLAT + i;
        for (int i = 0; i < lv.hplat.n; i++)
            if (in_sprite(&m_hp[i], 0x04, x, y))
                return HIT_HPLAT + i;
    }
    if (what & W_BOULDER)
        for (int i = 0; i < lv.boulders.n; i++)
            if (HIT_BOULDER + i != m_self && in_sprite(&m_bo[i], 0x6E, x, y))
                return HIT_BOULDER + i;
    if (what & W_PLAYER) {
        const int x0 = (int)floorf(m_pl.x), y0 = (int)floorf(m_pl.y);
        if (x >= x0 + 4 && x < x0 + 12 && y >= y0 && y < y0 + 16)
            return HIT_PLAYER;
    }
    return 0;
}

/* The first thing in the box from (x0, y0) to (x1, y1), corners included. */
static int m_box(int x0, int y0, int x1, int y1, int what)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const int h = m_at(x, y, what);
            if (h)
                return h;
        }
    return 0;
}

/* One of the original's byte tests: anything in the 4 pixels from (x, y). */
static int m_byte(int x, int y, int what)
{
    return m_box(x, y, x + 3, y, what);
}

/* The colour at pixel (x, y) of buffer 2 when the platforms sample it: the level with the
 * boulders drawn over it (DrawBoulders comes first in the frame). */
static int m_colour(int x, int y)
{
    for (int i = 0; i < lv.boulders.n; i++) {
        const int lx = x - (int)floorf(m_bo[i].x), ly = y - (int)floorf(m_bo[i].y);
        if (lx >= 0 && lx < 16 && ly >= 0 && ly < 16 && tile_pixel(0x6E, lx, ly))
            return tile_pixel(0x6E, lx, ly);
    }
    return cga_get_in(back1, x, y);
}

/* ... and its "FFh" tests, the platforms' turning marks: 4 pixels of colour 3, which a
 * boulder's top also has (the level 13 platform turns on the stack of boulders under it). */
static bool m_ff(int x, int y)
{
    for (int i = 0; i < 4; i++)
        if (x + i < 0 || x + i >= CGA_W || y < 0 || y >= PLAYFIELD_LINES || m_colour(x + i, y) != 3)
            return false;
    return true;
}

/* A switch, lever or terminal is ground only at its top, its first two scanlines with pixels:
 * the player stands on one landed on from above, but falls past its insides. */
static bool m_control_top(int x0, int x1, int y)
{
    for (int i = 0; i < lv.special.n; i++) {
        const rec_t *r = &lv.special.r[i];
        const int tx = r->col * 4, ty = r->row * 2;
        if (!lv.special_active[i] || !is_control(r->s) || y < ty || y >= ty + 16 || x1 < tx || x0 >= tx + 16)
            continue;
        int top = 0;
        while (top < 16 && !m_tile_row_has_pixels(r->s, top))
            top++;
        if (y - ty > top + 1)
            continue;
        for (int x = x0 > tx ? x0 : tx; x <= x1 && x < tx + 16; x++)
            if (tile_pixel(r->s, x - tx, y - ty))
                return true;
    }
    return false;
}

/* The player's test points (BlitMasked_Buf2_Collide's), for the sprite at (x, y): the feet
 * cover x+4..x+11, the ground is the scanline under them, the head's room the one above.
 * Unlike the original, a switch, lever or terminal is ground only at its top (m_control_top):
 * falling past the level 15 terminal, the original lands on its desk, half in the wall above. */
static int m_ground(int x, int y)
{
    const int h = m_box(x + 4, y + 16, x + 11, y + 16, W_BODY | W_CONTROLS_OPEN);
    return h || !m_control_top(x + 4, x + 11, y + 16) ? h : HIT_BG;
}

static int m_head(int x, int y)
{
    return m_box(x + 4, y - 1, x + 11, y - 1, W_BODY);
}

/* ------------------------------------------- the player */

/* A wall at column x for the player at height y: scanlines 4-10 of the sprite, but a switch,
 * lever or terminal only where the original samples, the scanline 10 below its row. The
 * original steps a byte at a time past what's above that line, which is how the player gets
 * by the level 19 switch's button to the hole beside it. */
static int m_wall(int x, float y)
{
    const int y0 = (int)floorf(y);
    const int h = m_box(x, y0 + 4, x, y0 + 10, W_BODY | W_CONTROLS_OPEN);
    const int wy = to_row(y) * 2 + 10;
    return h || !m_in_special(x, wy, is_control) ? h : m_at(x, wy, W_BODY);
}

/* Something the player stands on moved by (dx, dy): go with it, unless a wall or the ceiling
 * is in the way. */
static void m_carry(int id, float dx, float dy)
{
    if (!on_ground || m_support != id)
        return;
    if ((dx < 0 && !can_left) || (dx > 0 && !can_right))
        dx = 0;
    if (dy < 0 && m_ceiling)
        dy = 0;
    m_pl.x = fminf(fmaxf(m_pl.x + dx, 0), M_MAX_X);
    m_pl.y += dy;
}

/* UpdateVPlatforms: state 1 up, 0 down, 2 stopped, turning at a mark or the top. The marks
 * are tested at the platform's row, as the original moves it: an even scanline, so the odd
 * ones of a ledge's underside (the level 13 platform's top) don't turn it early. */
static void m_update_vplatforms(void)
{
    for (int i = 0; i < lv.vplat.n; i++) {
        rec_t *r = &lv.vplat.r[i];
        mpos_t *p = &m_vp[i];
        const int x = (int)floorf(p->x), y = to_row(p->y) * 2;
        float dy = 0;
        if (r->s == 1) {
            if (m_ff(x, y - 28) || p->y < 16)
                r->s = 0;
            else
                dy = -M_VPLAT * M_TICK;
        } else if (r->s == 0) {
            if (m_ff(x + 4, y + 22) || m_ff(x + 8, y + 22))
                r->s = 1;
            else
                dy = M_VPLAT * M_TICK;
        }
        p->y += dy;
        r->row = (uint8_t)to_row(p->y);
        m_carry(HIT_VPLAT + i, 0, dy);
    }
}

/* UpdateHPlatforms: state 1 left, 0 right, turning at a mark or the edge. */
static void m_update_hplatforms(void)
{
    for (int i = 0; i < lv.hplat.n; i++) {
        rec_t *r = &lv.hplat.r[i];
        mpos_t *p = &m_hp[i];
        const int x = (int)floorf(p->x), y = (int)floorf(p->y);
        float dx = 0;
        if (r->s == 1) {
            if (m_ff(x - 4, y + 2) || p->x <= 0)
                r->s = 0;
            else
                dx = -M_HPLAT * M_TICK;
        } else if (r->s == 0) {
            if (m_ff(x + 16, y + 2) || p->x > 0x4B * 4)
                r->s = 1;
            else
                dx = M_HPLAT * M_TICK;
        }
        p->x += dx;
        r->col = (uint8_t)to_col(p->x);
        m_carry(HIT_HPLAT + i, dx, 0);
    }
}

/* The boulder the player is walking into, if it can be pushed along: nothing on its other
 * side. */
static mpos_t *m_pushable(int dir)
{
    const int hit = dir < 0 ? m_wall_l : m_wall_r;
    if (hit < HIT_BOULDER || hit >= HIT_BOULDER + lv.boulders.n)
        return NULL;
    mpos_t *p = &m_bo[hit - HIT_BOULDER];
    const int x = (int)floorf(p->x), y = (int)floorf(p->y);
    m_self = hit;
    const bool blocked = dir < 0 ? p->x <= 0 || m_box(x - 1, y + 3, x - 1, y + 12, W_SOLID)
                                 : p->x >= M_MAX_X || m_box(x + 16, y + 3, x + 16, y + 12, W_SOLID);
    m_self = 0;
    return blocked ? NULL : p;
}

/* Move the player down (d > 0) or up, a scanline at a time: down stops on the ground, up at
 * the ceiling, which ends the jump. */
static void m_move_player_y(float d)
{
    const float to = m_pl.y + d;
    const int x = (int)floorf(m_pl.x), end = (int)floorf(to);
    for (int r = (int)floorf(m_pl.y); r < end; r++)
        if (m_ground(x, r)) {
            m_pl.y = (float)r;
            return;
        }
    for (int r = (int)floorf(m_pl.y); r > end; r--)
        if (m_head(x, r)) {
            m_pl.y = (float)r;
            jump_over = 1;
            return;
        }
    m_pl.y = to;
}

/* UpdatePlayerShot: in front of the shot, half a tile down, the level's picture. */
static void m_update_shot(void)
{
    if (K.fire && !m_fire_was_held)
        m_fire_press = M_JUMP_BUFFER;
    m_fire_was_held = K.fire;
    if (m_fire_press > 0 && !shot_active && shots) {
        m_fire_press = 0;
        shot_active = 1;
        shot_dir = facing_left ? -2 : 2;
        m_shot.x = m_shot.ox = facing_left ? m_pl.x : m_pl.x + 8;
        m_shot.y = m_shot.oy = floorf(m_pl.y);
        shots--;
        draw_shots(9, 0, shots);
        sound_shot();
    }
    if (m_fire_press > 0)
        m_fire_press -= M_TICK;
    if (!shot_active)
        return;
    /* the 8 pixels from the middle of the shot forwards, nearest first */
    const int x = (int)floorf(m_shot.x), y = (int)m_shot.y;
    int hit_x = -1;
    for (int k = 0; k < 8 && hit_x < 0; k++) {
        const int at = shot_dir > 0 ? x + 8 + k : x + 7 - k;
        if (m_at(at, y + 8, W_BG))
            hit_x = at;
    }
    m_shot.x += (shot_dir > 0 ? M_SHOT : -M_SHOT) * M_TICK;
    shot_col = (uint8_t)to_col(m_shot.x);
    shot_row = (uint8_t)to_row(m_shot.y);
    if (m_shot.x < 0 || m_shot.x > 0x4E * 4) {
        end_shot();
        return;
    }
    if (hit_x >= 0 && hit_x < CGA_W)
        shot_hit(&map()[(y / 2 + 3) / 8 * LEVEL_COLS + hit_x / 16]);
}

static bool lets_jump_on(int c) /* the specials a jump carries on through (and pickups) */
{
    return c == 0 || c == 0x16 || c == 0x17 || c == 0x0A || c == 0x09 || is_pickup(c);
}

/* The pickups the player overlaps, by FindSpecialTileAt's reach (3 byte columns, 8 rows): with
 * their pixels open, a fall no longer lands on one to take it. */
static void m_take_pickups(void)
{
    for (int i = 0; i < lv.special.n; i++) {
        const rec_t *r = &lv.special.r[i];
        if (lv.special_active[i] && is_pickup(r->s) && r->s != 0x5F && abs(r->col - px) < 3 &&
            abs(r->row - py) < 8) {
            found = i;
            touch_tile(r->s);
        }
    }
}

/* UpdatePlayer. */
static void m_update_player(void)
{
    const int dir = K.left && !K.right ? -1 : K.right && !K.left ? 1 : 0;
    if (dir) {
        if ((dir < 0) != (facing_left != 0)) { /* turn (the original spends a frame on it) */
            facing_left = dir < 0;
            frame_first = facing_left ? 0x21 : 0x1B;
            frame_end = facing_left ? 0x25 : 0x1F;
            frame = frame_first - 1;
            m_walked = 0;
        }
        const float dx = dir * M_WALK * M_TICK;
        const bool free = dir < 0 ? can_left : can_right;
        mpos_t *pushed = free ? NULL : m_pushable(dir);
        if (free || pushed) {
            m_pl.x = fminf(fmaxf(m_pl.x + dx, 0), M_MAX_X);
            if (pushed) {
                /* Kept right against the player (its edge at their wall test's column): moved
                 * apart by the same fraction, the two would round to whole pixels on different
                 * steps, and the boulder's edge would reach into the feet and lift them. */
                pushed->x = m_pl.x + (dir < 0 ? -12 : 12);
                play(1300);
                nosound();
            }
            if (on_ground) {
                m_walked += fabsf(dx);
                frame = frame_first + (int)(m_walked / 4) % 4;
            }
        }
    }
    if (K.jump && !m_jump_was_held)
        m_jump_press = M_JUMP_BUFFER;
    m_jump_was_held = K.jump;
    if (m_jump_press > 0 && !jump_over && can_jump && on_ground) {
        on_ground = 0;
        can_jump = 0;
        m_jump = 0;
        m_air = 0;
        m_jump_press = 0;
        sound_jump();
    }
    if (m_jump_press > 0)
        m_jump_press -= M_TICK;
    if (frame > frame_end - 1) /* the jump's picture, back on the ground: standing */
        frame = frame_first;
    m_update_vplatforms();
    m_update_hplatforms();

    bool pushing = false; /* the jump's push, which falls off as it goes */
    if (!can_jump && !on_ground) {
        frame = facing_left ? 0x25 : 0x1F;
        m_sync_player();
        const int c = find_special_at((uint8_t)py, (uint8_t)px);
        touch_tile(c);
        if (!m_ceiling && lets_jump_on(c) && !jump_over) {
            m_jump += M_TICK;
            if (m_jump >= M_JUMP_TOP) {
                frame = frame_first;
                jump_over = 1;
            } else {
                pushing = true;
            }
        } else {
            jump_over = 1;
        }
    }
    /* Rows per frame down. Gravity is FallV >> 1, FallV counting up a frame at a time to 8;
     * the jump's push is (9 - JumpT) >> 1, JumpT counting up every 3 frames. These are the
     * straight lines through the original's steps, which reach the same height (28 px). */
    float v = 0;
    if (!on_ground)
        v += fminf(4, m_air / 2);
    if (pushing)
        v -= 13.0f / 3 - m_jump / 6;
    m_move_player_y(v * 2 * M_TICK);
    m_pl.y = fminf(fmaxf(m_pl.y, 0), M_MAX_Y);

    m_update_shot();

    int x = (int)floorf(m_pl.x), y = (int)floorf(m_pl.y);
    m_wall_l = m_wall(x + 3, m_pl.y);
    m_wall_r = m_wall(x + 12, m_pl.y);
    m_ceiling = m_head(x, y);
    m_sync_player();
    m_take_pickups();
    if (!m_wall_l) {
        can_left = 1;
    } else {
        const int c = find_special_at((uint8_t)py, (uint8_t)(px - 1));
        touch_tile(c);
        if (c == 0)
            can_left = 0;
    }
    if (!m_wall_r) {
        can_right = 1;
    } else {
        const int c = find_special_at((uint8_t)py, (uint8_t)(px + 1));
        touch_tile(c);
        if (c == 0)
            can_right = 0;
    }

    int ground = m_ground(x, y);
    if (!ground && on_ground && can_jump) /* walking down a slope or a step: keep to it */
        for (int k = 1; k <= M_STEP_UP && !ground; k++)
            if ((ground = m_ground(x, y + k)) != 0) {
                y += k;
                m_pl.y = (float)y;
                m_sync_player();
            }
    if (!ground) { /* falling */
        on_ground = 0;
        m_air += M_TICK;
        m_support = 0;
    } else {
        touch_tile(find_special_at((uint8_t)py, (uint8_t)px));
        if (!on_ground) {
            on_ground = 1;
            can_jump = 1;
        }
        m_air = 0;
        jump_over = 0;
        m_jump = 0;
        m_support = ground;
    }
    if (m_box(x + 4, y + 11, x + 11, y + 15, W_BODY | W_CONTROLS_OPEN)) { /* feet in the floor: step up */
        const int c = find_special_at((uint8_t)py, (uint8_t)px);
        touch_tile(c);
        if (!m_ceiling) {
            /* Not onto a terminal, a lever and the like: the original finds the special (and
             * doesn't climb) as soon as its feet reach it, but at the byte column rounded, a
             * pixel test reaches it first. */
            for (int k = 0; c == 0 && k < M_STEP_UP && !m_head(x, y) &&
                            m_box(x + 4, y + 11, x + 11, y + 15, W_BODY | W_SPECIALS_OPEN); k++)
                m_pl.y = (float)--y;
            on_ground = 1;
            can_jump = 1;
            m_air = 0;
            m_jump = 0;
            m_support = m_ground(x, y);
            m_sync_player();
        }
    }
    /* Standing on a platform: take its fraction of a pixel, so the two move as one. */
    if (on_ground && m_support >= HIT_VPLAT && m_support < HIT_BOULDER) {
        const mpos_t *p = m_support < HIT_HPLAT ? &m_vp[m_support - HIT_VPLAT] : &m_hp[m_support - HIT_HPLAT];
        m_pl.y = p->y + (float)(y - (int)floorf(p->y));
        if (!dir)
            m_pl.x = p->x + roundf(m_pl.x - p->x);
    }
}

/* ------------------------------------------- everything else */

/* UpdateEnemies49 / 4A / 4B. The tests are the original's bytes, on the level and the
 * boulders; a turn takes the step, as the original's takes the frame. */
static void m_update_walkers(int k)
{
    const walker_t *w = &walkers[k];
    m_anim[k] += M_TICK;
    m_wframe[k] = (int)(m_anim[k] / (float)w->tick_len) % 4;
    for (int i = 0; i < w->list->n; i++) {
        rec_t *r = &w->list->r[i];
        mpos_t *p = &m_wk[k][i];
        if (r->s == 2)
            continue;
        const int x = (int)floorf(p->x), y = (int)floorf(p->y), what = W_BG | W_BOULDER;
        int probe_l, probe_r, wall_l, wall_r;
        if (w->kind == 0x49) { /* the ceiling ahead; a wall ahead */
            probe_l = m_byte(x - 4, y - 2, what);
            probe_r = m_byte(x + 16, y - 2, what);
            wall_l = m_byte(x - 4, y + 2, what);
            wall_r = m_byte(x + 16, y + 2, what);
        } else if (w->kind == 0x4A) { /* its own top row */
            probe_l = m_byte(x, y + 2, what);
            probe_r = m_byte(x + 12, y + 2, what);
            wall_l = wall_r = 0;
        } else { /* the floor under it; a wall ahead */
            probe_l = m_byte(x, y + 16, what);
            probe_r = m_byte(x + 12, y + 16, what);
            wall_l = m_byte(x - 4, y, what);
            wall_r = m_byte(x + 16, y, what);
        }
        const float dx = 4.0f / (float)w->tick_len * M_TICK;
        if (r->s == 1) {
            if (w->kind == 0x4A ? probe_l || p->x <= 0 : !probe_l || wall_l) {
                r->s = 0;
                continue;
            }
            p->x -= dx;
        } else {
            if (w->kind == 0x4A ? probe_r || p->x > 0x4B * 4 : !probe_r || wall_r) {
                r->s = 1;
                continue;
            }
            p->x += dx;
        }
        r->col = (uint8_t)to_col(p->x);
        if (shot_active && near(p->x - m_shot.x, p->y - m_shot.y, w->shot_dx, w->shot_dy)) {
            r->s = 2;
            shot_active = 0;
            m_explosion(p->x, p->y);
            draw_score(w->points);
        }
        if (m_touches_player(w->first_tile + m_wframe[k], p))
            kill_player(0);
    }
}

/* UpdateDrips. */
static void m_update_drips(void)
{
    if (lv.drips.n == 0)
        return;
    if (!drip_active) {
        const int which = plat_rand() % lv.drips.n;
        const int off = plat_rand() % (lv.drips.r[which].s + 1);
        drip_col = lv.drips.r[which].col + off;
        drip_row = lv.drips.r[which].row;
        m_drip.x = m_drip.ox = (float)(drip_col * 4);
        m_drip.y = m_drip.oy = (float)(drip_row * 2);
        drip_active = 1;
        sound_drip();
        return;
    }
    m_drip.y += M_DRIP * M_TICK;
    drip_row = to_row(m_drip.y);
    const int x = (int)floorf(m_drip.x), y = (int)floorf(m_drip.y);
    if (m_touches_player(0x26, &m_drip))
        kill_player(0);
    else if (m_box(x + 4, (y & ~1) + 16, x + 11, (y & ~1) + 16, W_BG | W_BOULDER)) /* as boulders */
        drip_active = 0;
}

/* UpdateTurret: the bolt flies until its middle meets the level, an edge, or the player. */
static void m_update_turret(void)
{
    if (!lv.has_turret)
        return;
    const float ty = (float)(lv.turret_row * 2);
    if (!bolt_active) {
        if (fabsf(ty - m_pl.y) < 7 * 2 - 1) {
            bolt_dir = m_pl.x < (float)(lv.turret_col * 4) ? -1 : 1;
            bolt_active = 1;
            m_bolt.x = m_bolt.ox = (float)((lv.turret_col + bolt_dir * 3) * 4);
            m_bolt.y = m_bolt.oy = ty;
            sound_bolt();
        }
        return;
    }
    const int x = (int)floorf(m_bolt.x), y = (int)ty;
    const int ahead = m_box(x + 4, y + 8, x + 11, y + 8, W_BG);
    m_bolt.x += (float)bolt_dir * M_BOLT * M_TICK;
    bolt_col = (uint8_t)to_col(m_bolt.x);
    if (!ahead && m_bolt.x > 0 && m_bolt.x < 0x4E * 4) {
        if (near(m_bolt.x - m_pl.x, ty - m_pl.y, 3, 4)) {
            bolt_active = 0;
            kill_player(0);
        }
    } else {
        bolt_active = 0;
    }
}

/* UpdateBoulders: fall, or slide off an edge (pushing is the player's, m_push). They land on
 * the level, platforms, other boulders and the player, and stop shots and bolts. */
static void m_update_boulders(void)
{
    for (int i = 0; i < lv.boulders.n; i++) {
        rec_t *r = &lv.boulders.r[i];
        mpos_t *p = &m_bo[i];
        /* The original only ever tests the even scanline under a boulder (it moves 2 at a
         * time), and levels count on that: boulders fall past letters and gems that a test
         * of every scanline would catch them on. So between two even scanlines this tests the
         * one it left. */
        const int x = (int)floorf(p->x), y = ((int)floorf(p->y) & ~1) + 16;
        m_self = HIT_BOULDER + i;
        const int below_l = m_byte(x, y, W_SOLID | W_PLAYER), below_r = m_byte(x + 12, y, W_SOLID | W_PLAYER);
        m_self = 0;
        if (!below_l && !below_r) {
            p->y += M_FALL * M_TICK;
        } else if (below_l && !below_r && p->x <= 0x4B * 4) {
            p->x += 2 * M_FALL * M_TICK;
            p->y += M_FALL * M_TICK;
        } else if (!below_l && below_r && p->x > 0) {
            p->x -= 2 * M_FALL * M_TICK;
            p->y += M_FALL * M_TICK;
        }
        r->col = (uint8_t)to_col(p->x);
        r->row = (uint8_t)to_row(p->y);
        if (shot_active && near(p->x - m_shot.x, p->y - m_shot.y, 4, 8))
            shot_active = 0;
        if (bolt_active && near(p->x - m_bolt.x, p->y - m_bolt.y, 4, 8))
            bolt_active = 0;
    }
}

static void m_start_level(void)
{
    m_pl.x = m_pl.ox = (float)(px * 4);
    m_pl.y = m_pl.oy = (float)(py * 2);
    const struct { list_t *l; mpos_t *p; } lists[] = {
        {&lv.vplat, m_vp}, {&lv.hplat, m_hp}, {&lv.boulders, m_bo},
        {&lv.enemy49, m_wk[0]}, {&lv.enemy4a, m_wk[1]}, {&lv.enemy4b, m_wk[2]},
    };
    for (size_t k = 0; k < sizeof lists / sizeof lists[0]; k++)
        for (int i = 0; i < lists[k].l->n; i++) {
            mpos_t *p = &lists[k].p[i];
            p->x = p->ox = (float)(lists[k].l->r[i].col * 4);
            p->y = p->oy = (float)(lists[k].l->r[i].row * 2);
        }
    m_air = m_jump = m_tiles = m_jump_press = m_fire_press = 0;
    m_jump_was_held = K.jump; /* a jump or shot held through a restart doesn't count */
    m_fire_was_held = K.fire;
    m_support = m_wall_l = m_wall_r = m_ceiling = 0;
    /* all four walking pictures from the start (the original's frame_end is 1E until the
     * first turn, which leaves out 1E and, with the picture going back to standing past the
     * last, would show 1B twice as long) */
    frame_first = facing_left ? 0x21 : 0x1B;
    frame_end = facing_left ? 0x25 : 0x1F;
    m_nfx = 0;
    m_exit_cell = -1;
}

/* One step: the original's frame (frame_update), in the same order. Returns 1 if the game
 * ended. */
static int m_step(void)
{
    mpos_t *const all[] = {&m_pl, &m_shot, &m_bolt, &m_drip};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        all[i]->ox = all[i]->x;
        all[i]->oy = all[i]->y;
    }
    mpos_t *const lists[] = {m_vp, m_hp, m_bo, m_wk[0], m_wk[1], m_wk[2]};
    for (size_t k = 0; k < sizeof lists / sizeof lists[0]; k++)
        for (int i = 0; i < MAX_LIST; i++) {
            lists[k][i].ox = lists[k][i].x;
            lists[k][i].oy = lists[k][i].y;
        }

    if ((m_tiles += M_TICK) >= 1) {
        m_tiles -= 1;
        level_animate_tiles(&lv, gd);
    }
    for (int k = 0; k < 3; k++)
        m_update_walkers(k);
    m_update_drips();
    m_update_player();
    if (game_over)
        return 1;
    m_update_boulders();
    m_update_turret();
    int n = 0;
    for (int i = 0; i < m_nfx; i++)
        if ((m_fx[i].t += M_TICK) < 5)
            m_fx[n++] = m_fx[i];
    m_nfx = n;
    m_sync_player();
    check_level_exit();
    return 0;
}

/* Draw a thing `a` of the way from its last step to this one (straight there after a jump
 * across the screen, such as a restart). */
static void m_draw(uint8_t *buf, int n, const mpos_t *p, float a)
{
    float x = p->x, y = p->y;
    if (fabsf(p->x - p->ox) < 16 && fabsf(p->y - p->oy) < 16) {
        x = p->ox + (p->x - p->ox) * a;
        y = p->oy + (p->y - p->oy) * a;
    }
    blit_masked_px(buf, tile(n), (int)floorf(x), (int)floorf(y));
}

/* The frame, in the original's drawing order, into the playfield of video memory; but the
 * stars first, behind everything that moves (the original draws them over the player). */
static void m_render(float a)
{
    static uint8_t buf[CGA_VRAM_SIZE];
    memcpy(buf, back1, sizeof buf);
    for (int i = 0; i < lv.special.n; i++) {
        const rec_t *r = &lv.special.r[i];
        if (r->s > 0x73 && r->s < 0x78)
            blit_masked_px(buf, tile(r->s), r->col * 4, r->row * 2);
    }
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < walkers[k].list->n; i++)
            if (walkers[k].list->r[i].s != 2)
                m_draw(buf, walkers[k].first_tile + m_wframe[k], &m_wk[k][i], a);
    if (drip_active)
        m_draw(buf, 0x26, &m_drip, a);
    for (int i = 0; i < lv.vplat.n; i++)
        m_draw(buf, 0x04, &m_vp[i], a);
    for (int i = 0; i < lv.hplat.n; i++)
        m_draw(buf, 0x04, &m_hp[i], a);
    if (shot_active)
        m_draw(buf, 0x0B, &m_shot, a);
    m_draw(buf, frame, &m_pl, a);
    for (int i = 0; i < lv.boulders.n; i++)
        m_draw(buf, 0x6E, &m_bo[i], a);
    if (bolt_active && lv.has_turret) /* the original draws it in UpdateTurret, which stops with the turret */
        m_draw(buf, 0x2A, &m_bolt, a);
    for (int i = 0; i < m_nfx; i++)
        blit_masked_px(buf, tile(0x2A + (int)m_fx[i].t), (int)floorf(m_fx[i].x), (int)floorf(m_fx[i].y));
    memcpy(cga_vram + 0x140, buf, 0x1E78);
    memcpy(cga_vram + CGA_BANK + 0x140, buf + CGA_BANK, 0x1E78);
    plat_dirty();
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
    int yes = 0;
    pad_set_yes_no(true);
    for (;;) {
        if (SC == 0x15) {
            yes = 1;
            break;
        }
        if (SC == 0x31 || plat_quitting())
            break;
        delay(1);
    }
    pad_set_yes_no(false);
    return yes;
}

/* ------------------------------------------- the pause menu (port extra) */

/* The options changed game.physics: the level carries on in the other frame loop. */
static int physics_switch;

/* The next key pressed (a make code), waiting for it; Esc once the window closes. */
static int menu_key(void)
{
    int last = SC;
    for (;;) {
        if (plat_quitting())
            return 0x01;
        delay(1);
        if (SC != last) {
            last = SC;
            if (!(SC & 0x80))
                return SC;
        }
    }
}

/* The controller's Start or Esc: Continue, Options, Quit to Menu (as Q then Y does) or Quit Game.
 * The game stands still meanwhile, and the screen comes back as it was. Returns 1 to quit. */
static int pause_menu(void)
{
    static const char *const items[] = {"CONTINUE", "OPTIONS", "QUIT TO MENU", "QUIT GAME"};
    static uint8_t saved[CGA_VRAM_SIZE];
    memcpy(saved, cga_vram, sizeof saved);
    nosound();
    pad_set_menu(true);
    int sel = 0, quit = 0;
    for (;;) {
        menu_box("PAUSED", items, 4, sel, 18);
        const int k = menu_key();
        if (k == 0x48)
            sel = (sel + 3) % 4;
        else if (k == 0x50)
            sel = (sel + 1) % 4;
        else if (k == 0x01 || k == SC_PAUSE_MENU || (k == ENTER && sel == 0))
            break;
        else if (k == ENTER && sel == 1) {
            bool physics_changed = false;
            const int close = options_menu(menu_key, &physics_changed);
            if (physics_changed)
                physics_switch = 1;
            memcpy(cga_vram, saved, sizeof saved);
            if (close)
                break;
        } else if (k == ENTER) {
            if (sel == 3)
                plat_request_quit();
            quit = 1;
            break;
        }
    }
    memcpy(cga_vram, saved, sizeof saved);
    plat_dirty();
    pad_set_menu(false);
    plat_forget_taps();
    SC = 0; /* the key that closed it is not the game's */
    return quit;
}

/* The keys main handles at the top of each frame. Returns 1 to quit the game, 2 to skip the
 * rest of the frame. */
static int frame_keys(void)
{
    if ((SC == SC_PAUSE_MENU || SC == 0x01) && pause_menu()) /* Start, or Esc (the port's) */
        return 1;
    if (SC == 0x10) { /* Q (and the original's Esc, which opens the pause menu instead) */
        static const char *const box[] = {"************", "*          *", "*          *", "************"};
        banner(1, 10, box, 4);
        draw_text(2, "  PAUSED  ", 11, 0x0F);
        draw_text(2, "QUIT (Y/N)", 12, 0x0F);
        pad_set_yes_no(true);
        for (;;) {
            if (SC == 0x15 || plat_quitting()) {
                pad_set_yes_no(false);
                nosound();
                return 1;
            }
            if (SC == 0x31)
                break;
            delay(1);
        }
        pad_set_yes_no(false);
    }
    if (SC == 0x1F && ask_yes_no("  SAVING..  ")) /* S */
        save_game();
    if (SC == 0x13) { /* R: either way, the rest of the frame is skipped */
        if (ask_yes_no(" RESTORING.")) {
            memcpy(map(), backup, LEVEL_BYTES);
            restore_game();
            smooth_reset();
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
    smooth_reset();
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

/* A script's frame being counted out in steps; kept across restarts, or dying at every step
 * would never finish one. */
static float script_frames;

/* The options switched game.physics: the other frame loop carries on from what both keep in
 * the original's variables (the player's, the moving things' and the level's); what only one
 * tracks (the shot, the drip, the bolt, a fall) starts afresh. */
static void switch_physics(void)
{
    physics_switch = 0;
    modern = settings.physics == PHYSICS_MODERN;
    end_shot();
    drip_active = bolt_active = 0;
    fall_v = 1;
    jump_t = jump_sub = 0;
    smooth_reset();
}

/* game.physics = modern: a level's frame loop, M_HZ fixed steps a second whatever the
 * display does, drawn whenever the display can show a new picture. A script's frame counts
 * stay the original's frames (several steps each). Returns 1 to quit, 0 when the level
 * changes, 2 when the options switched to classic physics. */
static int run_level_modern(const char *script, const char **p, int *frames, int *total, int *deaths)
{
    m_start_level();
    double last = plat_seconds(), acc = 0, next_frame = 0;
    int last_sc = SC;
    for (;;) {
        if (plat_quitting())
            return 1;
        int steps = 1;
        if (!script) {
            const double now = plat_seconds();
            double dt = now - last;
            last = now;
            if (dt > 0.1) /* the window was held still: carry on rather than catch up */
                dt = 1 / M_HZ;
            acc += dt;
            steps = (int)(acc * M_HZ);
            acc -= steps / M_HZ;
        }
        for (int i = 0; i < steps; i++) {
            if (script) {
                while (*frames == 0 && *p)
                    *p = script_step(*p, frames);
                if (!*p && *frames == 0) {
                    m_render(1); /* for --screenshot */
                    return 1;
                }
            }
            plat_frame_input_begin();
            m_enter = SC == ENTER && last_sc != ENTER;
            last_sc = SC;
            const int k = frame_keys();
            if (k == 1)
                return 1;
            if (physics_switch) {
                plat_frame_input_end();
                switch_physics();
                return 2;
            }
            const double t0 = plat_seconds();
            if (k == 0 && m_step())
                return 1;
            plat_frame_input_end();
            if (script && (script_frames += M_TICK) >= 1) {
                script_frames -= 1;
                (*frames)--;
                (*total)++;
            }
            if (died) {
                died = 0;
                (*deaths)++;
                flash(4); /* FlashBackground */
                restart_level();
            }
            cheat_level_skip();
            if (level_changed)
                return 0;
            if (!script && plat_seconds() - t0 > 0.05) { /* a door opening, a message: no catching up */
                last = plat_seconds();
                acc = 0;
                break;
            }
        }
        if (script)
            continue;
        const double now = plat_seconds(), period = 1.0 / plat_refresh_hz();
        if (now >= next_frame) {
            m_render((float)(acc * M_HZ));
            next_frame = now - next_frame < period ? next_frame + period : now + period;
            plat_poll();
        } else {
            plat_idle();
        }
    }
}

/* The level start and frame loop (1054:7c1a-806e). With a script, the keys come from it
 * instead of the keyboard and frames don't wait for the timer. */
static void run(gamedata_t *g, int first_level, int start_shots, const char *script)
{
    init(g, first_level, start_shots);
    plat_isr_installed = !script;
    scripted = script != NULL;
    modern = settings.physics == PHYSICS_MODERN;
    script_frames = 0;
    unsigned last = plat_ticks();
    int frames = 0, total = 0, deaths = 0, scene_bad = 0;
    const char *p = script;
    while (!plat_quitting()) {
        if (level >= LEVELS) /* after the last level: the original draws "LEVEL 21" first */
            break;
        start_level();
        while (!plat_quitting()) {
            if (modern) {
                const int r = run_level_modern(script, &p, &frames, &total, &deaths);
                if (r == 1)
                    goto quit;
                if (r == 0)
                    break;
                continue; /* switched to classic */
            }
            if (script) {
                while (frames == 0 && p)
                    p = script_step(p, &frames);
                if (!p && frames == 0)
                    goto quit;
                frames--;
                total++;
            }
            plat_frame_input_begin();
            const int k = frame_keys();
            if (k == 1)
                goto quit;
            if (physics_switch) {
                plat_frame_input_end();
                switch_physics();
                continue;
            }
            if (k == 2) {
                plat_frame_input_end();
                if (level_changed)
                    break;
                continue;
            }
            if (frame_update())
                goto quit;
            plat_frame_input_end();
            if (script && smooth_check() != 0) /* the display layer saw the whole frame */
                scene_bad++;
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
                smooth_reset();
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
    if (script && scene_bad)
        printf("smooth: the scene missed part of buffer 2 on %d frames\n", scene_bad);
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
