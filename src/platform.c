#include "platform.h"

#include <SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bind.h"
#include "cga.h"
#include "crt.h"
#include "gamepad.h"
#include "settings.h"
#include "smooth.h"

/* The four configurable keys start at 0; keyconfig_load sets them. */
isr_keys_t isr_keys;
bool plat_isr_installed;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_Texture *crt_tex; /* display.crt: the picture doubled by 2xSaL, scaled smoothly */
static SDL_Texture *mask;    /* ... and the shadow mask over it, a texel per window pixel */
static int mask_w, mask_h;
static bool mask_failed;
static SDL_AudioDeviceID audio;
static SDL_atomic_t speaker_hz;
static bool quitting, dirty = true;
static Uint64 start_counter, perf_freq;
static int shot_no;
static int refresh_hz = 60; /* the display's, for smooth frames */

/* The clock also works without plat_init (--screenshot / --script run without a window). */
static void timer_init(void)
{
    if (!perf_freq) {
        perf_freq = SDL_GetPerformanceFrequency();
        start_counter = SDL_GetPerformanceCounter();
    }
}

/* BIOS keyboard buffer: what getch sees. Extended keys are stored as 0, scancode. */
static int keybuf[64];
static int key_head, key_tail;

void plat_push_key(int c)
{
    const int next = (key_tail + 1) % 64;
    if (next != key_head) {
        keybuf[key_tail] = c;
        key_tail = next;
    }
}

/* SDL scancode -> PC/XT set 1 make code (0 = not a key the game sees). */
static int xt_scancode(SDL_Scancode s)
{
    static const struct { SDL_Scancode sdl; int xt; } map[] = {
        {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_1, 0x02}, {SDL_SCANCODE_2, 0x03},
        {SDL_SCANCODE_3, 0x04}, {SDL_SCANCODE_4, 0x05}, {SDL_SCANCODE_5, 0x06},
        {SDL_SCANCODE_6, 0x07}, {SDL_SCANCODE_7, 0x08}, {SDL_SCANCODE_8, 0x09},
        {SDL_SCANCODE_9, 0x0A}, {SDL_SCANCODE_0, 0x0B}, {SDL_SCANCODE_MINUS, 0x0C},
        {SDL_SCANCODE_EQUALS, 0x0D}, {SDL_SCANCODE_BACKSPACE, 0x0E}, {SDL_SCANCODE_TAB, 0x0F},
        {SDL_SCANCODE_Q, 0x10}, {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_E, 0x12},
        {SDL_SCANCODE_R, 0x13}, {SDL_SCANCODE_T, 0x14}, {SDL_SCANCODE_Y, 0x15},
        {SDL_SCANCODE_U, 0x16}, {SDL_SCANCODE_I, 0x17}, {SDL_SCANCODE_O, 0x18},
        {SDL_SCANCODE_P, 0x19}, {SDL_SCANCODE_LEFTBRACKET, 0x1A}, {SDL_SCANCODE_RIGHTBRACKET, 0x1B},
        {SDL_SCANCODE_RETURN, 0x1C}, {SDL_SCANCODE_KP_ENTER, 0x1C}, {SDL_SCANCODE_LCTRL, 0x1D},
        {SDL_SCANCODE_RCTRL, 0x1D}, {SDL_SCANCODE_A, 0x1E}, {SDL_SCANCODE_S, 0x1F},
        {SDL_SCANCODE_D, 0x20}, {SDL_SCANCODE_F, 0x21}, {SDL_SCANCODE_G, 0x22},
        {SDL_SCANCODE_H, 0x23}, {SDL_SCANCODE_J, 0x24}, {SDL_SCANCODE_K, 0x25},
        {SDL_SCANCODE_L, 0x26}, {SDL_SCANCODE_SEMICOLON, 0x27}, {SDL_SCANCODE_APOSTROPHE, 0x28},
        {SDL_SCANCODE_GRAVE, 0x29}, {SDL_SCANCODE_LSHIFT, 0x2A}, {SDL_SCANCODE_BACKSLASH, 0x2B},
        {SDL_SCANCODE_Z, 0x2C}, {SDL_SCANCODE_X, 0x2D}, {SDL_SCANCODE_C, 0x2E},
        {SDL_SCANCODE_V, 0x2F}, {SDL_SCANCODE_B, 0x30}, {SDL_SCANCODE_N, 0x31},
        {SDL_SCANCODE_M, 0x32}, {SDL_SCANCODE_COMMA, 0x33}, {SDL_SCANCODE_PERIOD, 0x34},
        {SDL_SCANCODE_SLASH, 0x35}, {SDL_SCANCODE_RSHIFT, 0x36}, {SDL_SCANCODE_LALT, 0x38},
        {SDL_SCANCODE_RALT, 0x38}, {SDL_SCANCODE_SPACE, 0x39}, {SDL_SCANCODE_CAPSLOCK, 0x3A},
        {SDL_SCANCODE_F1, 0x3B}, {SDL_SCANCODE_F2, 0x3C}, {SDL_SCANCODE_F3, 0x3D},
        {SDL_SCANCODE_F4, 0x3E}, {SDL_SCANCODE_F5, 0x3F}, {SDL_SCANCODE_F6, 0x40},
        {SDL_SCANCODE_F7, 0x41}, {SDL_SCANCODE_F8, 0x42}, {SDL_SCANCODE_F9, 0x43},
        {SDL_SCANCODE_F10, 0x44}, {SDL_SCANCODE_HOME, 0x47}, {SDL_SCANCODE_UP, 0x48},
        {SDL_SCANCODE_PAGEUP, 0x49}, {SDL_SCANCODE_LEFT, 0x4B}, {SDL_SCANCODE_RIGHT, 0x4D},
        {SDL_SCANCODE_END, 0x4F}, {SDL_SCANCODE_DOWN, 0x50}, {SDL_SCANCODE_PAGEDOWN, 0x51},
        {SDL_SCANCODE_INSERT, 0x52}, {SDL_SCANCODE_DELETE, 0x53},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (map[i].sdl == s)
            return map[i].xt;
    return 0;
}

/* input.quick_taps: the four held flags (jump, fire, left, right) and Enter, as seen since the
 * last frame started (pressed; changed at all) and what plat_frame_input_begin made up: for
 * how many more frames a tapped key stays held. Jump stays 2: UpdatePlayer ignores walking
 * and jumping on every third frame (the walk's pace), and a jump can't start twice. Fire stays
 * 1, or a shot that hits a wall at once could be followed by a second. */
static bool tapped[4], touched[4], forced[4];
static int hold_left[4];
static const int hold_frames[4] = {2, 1, 1, 1};
static bool enter_tapped, sc_touched, enter_forced;

static bool *held_flag(int i)
{
    bool *const f[4] = {&isr_keys.jump, &isr_keys.fire, &isr_keys.left, &isr_keys.right};
    return f[i];
}

static void keyboard_isr(int code);

void plat_isr_key(int code)
{
    bool before[4];
    for (int i = 0; i < 4; i++)
        before[i] = *held_flag(i);
    keyboard_isr(code);
    for (int i = 0; i < 4; i++)
        if (*held_flag(i) != before[i]) {
            touched[i] = true;
            tapped[i] |= *held_flag(i);
        }
    sc_touched = true;
    if (code == 0x1C)
        enter_tapped = true;
}

void plat_frame_input_begin(void)
{
    const bool on = settings.quick_taps && plat_isr_installed;
    for (int i = 0; i < 4; i++) {
        if (on && tapped[i] && !*held_flag(i))
            hold_left[i] = hold_frames[i];
        forced[i] = on && hold_left[i] > 0 && !*held_flag(i);
        if (forced[i]) {
            *held_flag(i) = true;
            hold_left[i]--;
        } else {
            hold_left[i] = 0;
        }
        tapped[i] = touched[i] = false;
    }
    /* Only Enter: the frame's other keys (F5's pause, the Y/N questions) wait for a key to
     * change, which a made-up press would confuse. */
    enter_forced = on && enter_tapped && isr_keys.scancode == 0x9C;
    if (enter_forced)
        isr_keys.scancode = 0x1C;
    enter_tapped = sc_touched = false;
}

void plat_forget_taps(void)
{
    for (int i = 0; i < 4; i++)
        tapped[i] = false;
    enter_tapped = false;
}

void plat_frame_input_end(void)
{
    for (int i = 0; i < 4; i++) {
        if (forced[i] && !touched[i])
            *held_flag(i) = false;
        if (touched[i]) /* pressed or let go meanwhile: the keyboard has it again */
            hold_left[i] = 0;
    }
    if (enter_forced && !sc_touched)
        isr_keys.scancode = 0x9C;
    for (int i = 0; i < 4; i++)
        forced[i] = false;
    enter_forced = false;
}

/* The game's INT 9 handler, 1054:0499. */
static void keyboard_isr(int code)
{
    isr_keys_t *k = &isr_keys;
    k->scancode = code;
    if (code == k->key_jump) k->jump = true;
    if (code == (k->key_jump | 0x80)) k->jump = false;
    if (code == k->key_fire) k->fire = true;
    if (code == (k->key_fire | 0x80)) k->fire = false;
    if (code == k->key_left || code == 0x4B) k->left = true;
    if (code == (k->key_left | 0x80) || code == 0xCB) k->left = false;
    if (code == k->key_right || code == 0x4D) k->right = true;
    if (code == (k->key_right | 0x80) || code == 0xCD) k->right = false;
}

void plat_set_fullscreen(bool on)
{
    settings.fullscreen = on && !PLAT_HANDHELD;
    if (!win || PLAT_HANDHELD)
        return;
    SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
    dirty = true;
}

void plat_request_quit(void)
{
    quitting = true;
}

/* The action each key pressed while playing (+ 1), and the keys whose own scancode went to the
 * game's ISR. */
static int key_action[SDL_NUM_SCANCODES];
static bool key_raw[SDL_NUM_SCANCODES];

/* plat_raw_begin / end: the keys and buttons pressed meanwhile. */
static bool raw_input;
static struct { bool pad; int code; } raw_queue[32];
static int raw_head, raw_tail;

void plat_raw_begin(void)
{
    raw_input = true;
    raw_head = raw_tail = 0;
    pad_set_raw(true);
}

void plat_raw_end(void)
{
    raw_input = false;
    pad_set_raw(false);
}

void plat_raw_push(bool pad, int code)
{
    const int next = (raw_tail + 1) % 32;
    if (next != raw_head) {
        raw_queue[raw_tail].pad = pad;
        raw_queue[raw_tail].code = code;
        raw_tail = next;
    }
}

static void capture(void);

bool plat_raw_take(bool *pad, int *code)
{
    if (!win) {
        capture();
        *pad = false;
        *code = SDL_SCANCODE_ESCAPE;
        return true;
    }
    if (quitting) {
        *pad = false;
        *code = SDL_SCANCODE_ESCAPE;
        return true;
    }
    plat_poll();
    if (raw_head == raw_tail)
        return false;
    *pad = raw_queue[raw_head].pad;
    *code = raw_queue[raw_head].code;
    raw_head = (raw_head + 1) % 32;
    return true;
}

static void on_key(const SDL_KeyboardEvent *e)
{
    const int xt = xt_scancode(e->keysym.scancode);
    if (e->type == SDL_KEYDOWN && e->keysym.scancode == SDL_SCANCODE_F12 && !e->repeat) {
        char path[32];
        snprintf(path, sizeof path, "shot%03d.png", ++shot_no);
        if (plat_screenshot(path) == 0)
            printf("wrote %s\n", path);
        return;
    }
    /* F11 or Alt+Enter: full screen on / off (the desktop's mode, so no mode switch) */
    if (e->type == SDL_KEYDOWN && (e->keysym.scancode == SDL_SCANCODE_F11 ||
                                   (e->keysym.scancode == SDL_SCANCODE_RETURN && (e->keysym.mod & KMOD_ALT)))) {
        if (!e->repeat)
            plat_set_fullscreen(!settings.fullscreen);
        return;
    }
    /* Alt+C: the CRT look on / off; the game doesn't see that C */
    if (e->type == SDL_KEYDOWN && e->keysym.scancode == SDL_SCANCODE_C && (e->keysym.mod & KMOD_ALT)) {
        if (!e->repeat) {
            settings.crt = !settings.crt && !PLAT_HANDHELD;
            printf("CRT look %s\n", settings.crt ? "on" : "off");
            dirty = true;
        }
        return;
    }
    if (e->type == SDL_KEYDOWN && e->keysym.scancode == SDL_SCANCODE_F4 && !e->repeat) {
        settings.smooth = !settings.smooth;
        printf("smooth movement %s\n", settings.smooth ? "on" : "off");
        plat_set_title();
        dirty = true;
        return;
    }
    const int sc = (int)e->keysym.scancode;
    if (sc <= 0 || sc >= SDL_NUM_SCANCODES)
        return;
    if (raw_input) {
        if (e->type == SDL_KEYDOWN && !e->repeat)
            plat_raw_push(false, sc);
        return;
    }
    /* The bindings (bind.h): while playing, a bound key presses its action's scancode, and
     * one whose own scancode is an action's does nothing; each key lets go of what it sent. */
    if (e->type == SDL_KEYUP && key_action[sc]) {
        bind_release(key_action[sc] - 1);
        key_action[sc] = 0;
        return;
    }
    if (e->type == SDL_KEYDOWN && pad_playing()) {
        const int a = bind_key_action(sc);
        if (a >= 0) {
            if (!key_action[sc]) {
                key_action[sc] = a + 1;
                bind_press(a);
            }
            return;
        }
        if (bind_reserved(xt))
            return;
    }
    if (!xt)
        return;
    if (e->type == SDL_KEYUP) {
        if (plat_isr_installed && (key_raw[sc] || !bind_reserved(xt)))
            plat_isr_key(xt | 0x80);
        key_raw[sc] = false;
        return;
    }
    if (plat_isr_installed) {
        plat_isr_key(xt);
        key_raw[sc] = true;
    }
    /* The game's ISR doesn't chain to the BIOS, so the buffer only fills while it's out. */
    if (plat_isr_installed)
        return;
    const SDL_Keycode sym = e->keysym.sym;
    if (sym >= 32 && sym < 127) {
        /* the BIOS's US layout: Shift and Caps Lock cancel out on letters */
        static const char plain[] = "`1234567890-=[]\\;',./", shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
        int c = (int)sym;
        const int shift = (e->keysym.mod & KMOD_SHIFT) != 0, caps = (e->keysym.mod & KMOD_CAPS) != 0;
        const char *k = strchr(plain, c);
        if (c >= 'a' && c <= 'z' && shift != caps)
            c -= 32;
        else if (shift && k)
            c = shifted[k - plain];
        plat_push_key(c);
    } else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
        plat_push_key('\r');
    } else if (sym == SDLK_ESCAPE) {
        plat_push_key(27);
    } else if (sym == SDLK_BACKSPACE) {
        plat_push_key(8);
    } else {
        plat_push_key(0);
        plat_push_key(xt);
    }
}

/* plat_sfx_begin / end: the effect being written down, and the one playing (under the audio
 * lock). at is in samples from the effect's start; hz 0 is quiet. */
#define AUDIO_HZ 44100
#define MAX_SFX 512
typedef struct { int at, hz; } tone_t;
static tone_t sfx_rec[MAX_SFX], sfx_play[MAX_SFX];
static int n_rec, rec_at, n_play, play_next, play_pos;
static bool recording, play_kept;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    static double phase;
    (void)userdata;
    Sint16 *out = (Sint16 *)stream;
    const int n = len / (int)sizeof(Sint16);
    int hz = SDL_AtomicGet(&speaker_hz);
    for (int i = 0; i < n; i++) {
        if (n_play) { /* a sound effect: its tones take over the speaker until the last one */
            while (play_next < n_play && sfx_play[play_next].at <= play_pos)
                hz = sfx_play[play_next++].hz;
            play_pos++;
            if (play_next == n_play) {
                SDL_AtomicSet(&speaker_hz, hz);
                n_play = 0;
            }
        }
        if (hz <= 0) {
            out[i] = 0;
            continue;
        }
        phase += hz / 44100.0;
        if (phase >= 1.0)
            phase -= (int)phase;
        out[i] = phase < 0.5 ? 2500 : -2500;
    }
}

int plat_init(const char *title, char *err, size_t errlen)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        snprintf(err, errlen, "SDL_Init: %s", SDL_GetError());
        return -1;
    }
    pad_init();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (PLAT_HANDHELD) /* whatever mars.ini says (copied from a PC, say) */
        settings.crt = settings.fullscreen = false;
    /* the handheld's SDL scales the window to its 640 x 480 panel keeping its shape, so 640 x 400
     * is the picture doubled with black bars above and below it */
    win = PLAT_HANDHELD ? SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 400, 0)
                        : SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 600,
                                           SDL_WINDOW_RESIZABLE |
                                               (settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    /* No vsync: waiting for it inside the game's own delay() calls (its sound effects) would
     * stretch them. plat_poll paces the redraws instead; on Windows the compositor keeps a
     * window tear-free. */
    ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
    SDL_DisplayMode mode;
    if (win && SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(win), &mode) == 0 && mode.refresh_rate > 0)
        refresh_hz = mode.refresh_rate < 240 ? mode.refresh_rate : 240;
    tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CGA_W, CGA_H)
              : NULL;
    crt_tex = tex ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CRT_W, CRT_H)
                  : NULL;
    if (!crt_tex) {
        snprintf(err, errlen, "can't open a window: %s", SDL_GetError());
        return -1;
    }
    SDL_SetTextureScaleMode(crt_tex, SDL_ScaleModeLinear);
    SDL_ShowCursor(settings.fullscreen ? SDL_DISABLE : SDL_ENABLE); /* the game has no use for it */
    plat_set_title();

    SDL_AudioSpec want = {0}, have;
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio)
        SDL_PauseAudioDevice(audio, 0);

    timer_init();
    return 0;
}

void plat_shutdown(void)
{
    if (audio)
        SDL_CloseAudioDevice(audio);
    if (mask)
        SDL_DestroyTexture(mask);
    SDL_DestroyTexture(crt_tex);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

void plat_dirty(void)
{
    dirty = true;
}

/* display.smooth: the moving tiles as textures, made when first drawn in these colours. */
#define MAX_TILE_TEX 160
static struct { const uint8_t *tile; uint64_t cut; SDL_Texture *tex; } tile_tex[MAX_TILE_TEX];
static int n_tile_tex;
static rgb_t tile_tex_colors[4];

static SDL_Texture *texture_of(const uint8_t *tile, uint64_t cut, const rgb_t *pal)
{
    if (memcmp(pal, tile_tex_colors, sizeof tile_tex_colors) != 0 || n_tile_tex == MAX_TILE_TEX) {
        for (int i = 0; i < n_tile_tex; i++)
            SDL_DestroyTexture(tile_tex[i].tex);
        n_tile_tex = 0;
        memcpy(tile_tex_colors, pal, sizeof tile_tex_colors);
    }
    for (int i = 0; i < n_tile_tex; i++)
        if (tile_tex[i].tile == tile && tile_tex[i].cut == cut)
            return tile_tex[i].tex;
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (!t)
        return NULL;
    uint32_t px[16 * 16];
    smooth_tile_argb(tile, cut, pal, px);
    SDL_UpdateTexture(t, NULL, px, 16 * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    tile_tex[n_tile_tex].tile = tile;
    tile_tex[n_tile_tex].cut = cut;
    tile_tex[n_tile_tex++].tex = t;
    return t;
}

/* Where the picture goes: as large as the window allows, keeping its shape, with black bars (what
 * SDL_RenderSetLogicalSize did), in the output's own pixels so the CRT mask lines up with them. */
static SDL_Rect view_rect(void)
{
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    SDL_Rect r = {0, 0, w, h};
    if ((long)w * CGA_H > (long)h * CGA_W)
        r.w = (int)((long)h * CGA_W / CGA_H);
    else
        r.h = (int)((long)w * CGA_H / CGA_W);
    r.x = (w - r.w) / 2;
    r.y = (h - r.h) / 2;
    return r;
}

/* The shadow mask for a picture of w x h pixels, made when that size changes. It's drawn over
 * the picture with the blend 2 * mask * picture, so each texel holds half the factor: a factor
 * over 1 (the bright channel's) can't be stored, its half can. */
static bool make_mask(int w, int h)
{
    if (mask_failed)
        return false;
    if (mask && w == mask_w && h == mask_h)
        return true;
    if (mask)
        SDL_DestroyTexture(mask);
    mask = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    uint32_t *px = malloc((size_t)w * h * sizeof *px);
    const SDL_BlendMode twice = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_SRC_COLOR, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    if (!mask || !px || SDL_SetTextureBlendMode(mask, twice) != 0) {
        fprintf(stderr, "mars: the CRT mask can't be drawn (%s); 2xSaL alone\n", SDL_GetError());
        mask_failed = true;
        free(px);
        return false;
    }
    const uint32_t lit = (uint32_t)(CRT_MASK_LIGHT / 2 * 255 + 0.5f);
    const uint32_t dark = (uint32_t)(CRT_MASK_DARK / 2 * 255 + 0.5f);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const int c = crt_mask_channel(x, y);
            px[y * w + x] = 0xFF000000u | (c == 0 ? lit : dark) << 16 | (c == 1 ? lit : dark) << 8 |
                            (c == 2 ? lit : dark);
        }
    SDL_UpdateTexture(mask, NULL, px, w * (int)sizeof *px);
    free(px);
    mask_w = w;
    mask_h = h;
    return true;
}

/* display.crt with display.smooth: 2xSaL needs the whole picture, so the gliding tiles are drawn
 * into it here, at the nearest pixel, inside the playfield. */
static void draw_sprite_argb(uint32_t *frame, const smooth_sprite_t *s, const rgb_t *pal)
{
    uint32_t px[16 * 16];
    smooth_tile_argb(s->tile, s->cut, pal, px);
    const int x0 = (int)floorf(s->x + 0.5f), y0 = (int)floorf(s->y + 0.5f);
    for (int y = 0; y < 16; y++) {
        if (y0 + y < SMOOTH_PLAYFIELD_Y || y0 + y >= CGA_H)
            continue;
        for (int x = 0; x < 16; x++)
            if (x0 + x >= 0 && x0 + x < CGA_W && px[y * 16 + x] >> 24)
                frame[(y0 + y) * CGA_W + x0 + x] = px[y * 16 + x];
    }
}

/* Draw the window. Returns true if it showed a smooth frame (which changes on its own). */
static bool present(void)
{
    if (!ren) /* no window (--screenshot / --script) */
        return false;
    static uint8_t vram[CGA_VRAM_SIZE];
    const smooth_sprite_t *spr = NULL;
    int n = 0;
    const double now = (double)(SDL_GetPerformanceCounter() - start_counter) / (double)perf_freq;
    const bool smooth = settings.smooth && plat_isr_installed && smooth_frame(now, vram, &spr, &n);
    const rgb_t *pal = cga_cur_colors();
    const SDL_Rect r = view_rect();
    if (settings.crt) {
        /* 2xSaL only when the picture changed: the window is redrawn more often than that */
        static uint32_t frame[CGA_W * CGA_H], shown[CGA_W * CGA_H], big[CRT_W * CRT_H];
        static bool have_shown;
        if (smooth)
            cga_buf_to_argb(vram, frame, CGA_W, pal);
        else
            cga_to_argb(frame, CGA_W, pal);
        for (int i = 0; i < n; i++)
            draw_sprite_argb(frame, &spr[i], pal);
        if (!have_shown || memcmp(frame, shown, sizeof frame) != 0) {
            crt_2xsal(frame, big);
            SDL_UpdateTexture(crt_tex, NULL, big, CRT_W * sizeof *big);
            memcpy(shown, frame, sizeof frame);
            have_shown = true;
        }
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, crt_tex, NULL, &r);
        if (make_mask(r.w, r.h))
            SDL_RenderCopy(ren, mask, NULL, &r);
        SDL_RenderPresent(ren);
        return smooth;
    }
    void *pixels;
    int pitch;
    if (SDL_LockTexture(tex, NULL, &pixels, &pitch) != 0)
        return false;
    if (smooth)
        cga_buf_to_argb(vram, pixels, pitch / 4, pal);
    else
        cga_to_argb(pixels, pitch / 4, pal);
    SDL_UnlockTexture(tex);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, &r);
    if (smooth) {
        const float sx = (float)r.w / CGA_W, sy = (float)r.h / CGA_H;
        const int top = (int)(SMOOTH_PLAYFIELD_Y * sy + 0.5f);
        const SDL_Rect playfield = {r.x, r.y + top, r.w, r.h - top};
        SDL_RenderSetClipRect(ren, &playfield);
        for (int i = 0; i < n; i++) {
            SDL_Texture *t = texture_of(spr[i].tile, spr[i].cut, pal);
            const SDL_FRect at = {r.x + spr[i].x * sx, r.y + spr[i].y * sy, 16 * sx, 16 * sy};
            if (t)
                SDL_RenderCopyF(ren, t, NULL, &at);
        }
        SDL_RenderSetClipRect(ren, NULL);
    }
    SDL_RenderPresent(ren);
    return smooth;
}

void plat_set_title(void)
{
    if (win)
        SDL_SetWindowTitle(win, settings.physics == PHYSICS_MODERN ? "Monuments of Mars (modern)"
                                : settings.smooth                  ? "Monuments of Mars (smooth)"
                                                                   : "Monuments of Mars");
}

void plat_poll(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            quitting = true;
        else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
            on_key(&e.key);
        else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_EXPOSED)
            dirty = true;
        else
            pad_event(&e);
    }
    pad_update();
    /* The game draws straight into video memory, so redraw on every poll at most at 70 Hz;
     * smooth frames as often as the monitor shows them (up to 240 Hz, 120 with the CRT look). */
    static Uint64 last;
    static bool smooth_shown;
    const Uint64 now = SDL_GetPerformanceCounter();
    if (dirty || now - last >= perf_freq / (smooth_shown ? (Uint64)plat_refresh_hz() : 70)) {
        smooth_shown = present();
        last = now;
        dirty = false;
    }
}

bool plat_quitting(void)
{
    return quitting;
}

/* Without a window (--show), each wait for a key writes the next screenshot instead. */
static const char *capture_base;
static int capture_no;

void plat_capture_waits(const char *base)
{
    capture_base = base;
}

static void capture(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s-%d.png", capture_base ? capture_base : "wait", ++capture_no);
    if (plat_screenshot(path) == 0)
        printf("wrote %s\n", path);
}

bool plat_kbhit(void)
{
    if (!win)
        return false;
    plat_poll();
    return key_head != key_tail || quitting;
}

bool plat_key_pressed(void)
{
    if (!win) {
        capture();
        return true;
    }
    return plat_kbhit();
}

int plat_getch(void)
{
    if (!win) {
        capture();
        return 27;
    }
    while (key_head == key_tail) {
        if (quitting)
            return 27;
        plat_poll();
        SDL_Delay(5);
    }
    const int c = keybuf[key_head];
    key_head = (key_head + 1) % 64;
    return c;
}

void plat_delay(int ms)
{
    if (recording) { /* a sound effect's pause between tones */
        rec_at += AUDIO_HZ * (ms > 0 ? ms : 0) / 1000;
        return;
    }
    timer_init();
    const Uint64 until = SDL_GetPerformanceCounter() + perf_freq * (Uint64)(ms > 0 ? ms : 0) / 1000;
    do {
        plat_poll();
        const Uint64 now = SDL_GetPerformanceCounter();
        if (now >= until || quitting)
            break;
        const Uint64 left_ms = (until - now) * 1000 / perf_freq;
        SDL_Delay(left_ms > 5 ? 5 : (Uint32)left_ms);
    } while (1);
}

int plat_rand(void)
{
    static unsigned long seed = 1;
    seed = (seed * 0x015A4E35UL + 1) & 0xFFFFFFFFUL;
    return (int)((seed >> 16) & 0x7FFF);
}

bool plat_caps_lock(void)
{
    return (SDL_GetModState() & KMOD_CAPS) != 0;
}

unsigned plat_ticks(void)
{
    timer_init();
    /* the PIT's 1193182 Hz / 65536 */
    return (unsigned)((SDL_GetPerformanceCounter() - start_counter) * 1193182 / 65536 / perf_freq);
}

static void record(int hz)
{
    if (n_rec < MAX_SFX)
        sfx_rec[n_rec++] = (tone_t){rec_at, hz};
}

void plat_sound(int hz)
{
    /* The game uses sound(7FFFh) as "quiet": 33 kHz, which a PC speaker can't reproduce but a
     * 44.1 kHz square wave would alias into an audible whine. */
    if (hz > 20000)
        hz = 0;
    else if (hz <= 18) /* the PIT divisor can't go lower (Turbo C sound() computes 1193180 / hz) */
        return;
    if (recording)
        record(hz);
    else
        SDL_AtomicSet(&speaker_hz, hz);
}

void plat_nosound(void)
{
    if (recording)
        record(0);
    else
        SDL_AtomicSet(&speaker_hz, 0);
}

void plat_sfx_begin(void)
{
    recording = true;
    n_rec = rec_at = 0;
}

void plat_sfx_end(bool keep)
{
    recording = false;
    if (!audio || !n_rec)
        return;
    SDL_LockAudioDevice(audio);
    if (!(n_play && play_kept)) {
        memcpy(sfx_play, sfx_rec, sizeof sfx_rec[0] * (size_t)n_rec);
        n_play = n_rec;
        play_next = play_pos = 0;
        play_kept = keep;
    }
    SDL_UnlockAudioDevice(audio);
}

double plat_seconds(void)
{
    timer_init();
    return (double)(SDL_GetPerformanceCounter() - start_counter) / (double)perf_freq;
}

/* With the CRT look, at most 120: its 2xSaL takes about 4 ms a picture. */
int plat_refresh_hz(void)
{
    return settings.crt && refresh_hz > 120 ? 120 : refresh_hz;
}

void plat_idle(void)
{
    plat_poll();
    SDL_Delay(1);
}

/* At 640 x 400 either way; with the CRT look, as a window that size shows it. */
int plat_screenshot(const char *path)
{
    if (settings.crt) {
        static uint32_t frame[CGA_W * CGA_H], big[CRT_W * CRT_H];
        cga_to_argb(frame, CGA_W, cga_cur_colors());
        crt_render(frame, big);
        return png_write_argb(path, big, CRT_W, CRT_H);
    }
    return cga_write_png(path, cga_cur_colors(), 2);
}
