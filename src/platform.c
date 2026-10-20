#include "platform.h"

#include <SDL.h>

#include <stdio.h>
#include <string.h>

#include "cga.h"

/* The four configurable keys start at 0; keyconfig_apply sets them. */
isr_keys_t isr_keys;
bool plat_isr_installed;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID audio;
static SDL_atomic_t speaker_hz;
static bool quitting, dirty = true;
static Uint64 start_counter, perf_freq;
static int shot_no;

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

static void push_key(int c)
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
    if (!xt)
        return;
    if (e->type == SDL_KEYUP) {
        if (plat_isr_installed)
            keyboard_isr(xt | 0x80);
        return;
    }
    if (plat_isr_installed)
        keyboard_isr(xt);
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
        push_key(c);
    } else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
        push_key('\r');
    } else if (sym == SDLK_ESCAPE) {
        push_key(27);
    } else if (sym == SDLK_BACKSPACE) {
        push_key(8);
    } else {
        push_key(0);
        push_key(xt);
    }
}

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    static double phase;
    (void)userdata;
    Sint16 *out = (Sint16 *)stream;
    const int n = len / (int)sizeof(Sint16);
    const int hz = SDL_AtomicGet(&speaker_hz);
    for (int i = 0; i < n; i++) {
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
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 600,
                           SDL_WINDOW_RESIZABLE);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CGA_W, CGA_H)
              : NULL;
    if (!tex) {
        snprintf(err, errlen, "can't open a window: %s", SDL_GetError());
        return -1;
    }
    SDL_RenderSetLogicalSize(ren, CGA_W, CGA_H);

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
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

void plat_dirty(void)
{
    dirty = true;
}

static void present(void)
{
    void *pixels;
    int pitch;
    if (SDL_LockTexture(tex, NULL, &pixels, &pitch) != 0)
        return;
    cga_to_argb(pixels, pitch / 4, cga_cur_colors());
    SDL_UnlockTexture(tex);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
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
    }
    /* The game draws straight into video memory, so redraw on every poll at most at 70 Hz. */
    static Uint64 last;
    const Uint64 now = SDL_GetPerformanceCounter();
    if (dirty || now - last > perf_freq / 70) {
        present();
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

void plat_sound(int hz)
{
    /* The game uses sound(7FFFh) as "quiet": 33 kHz, which a PC speaker can't reproduce but a
     * 44.1 kHz square wave would alias into an audible whine. */
    if (hz > 20000)
        SDL_AtomicSet(&speaker_hz, 0);
    else if (hz > 18) /* the PIT divisor can't go lower (Turbo C sound() computes 1193180 / hz) */
        SDL_AtomicSet(&speaker_hz, hz);
}

void plat_nosound(void)
{
    SDL_AtomicSet(&speaker_hz, 0);
}

int plat_screenshot(const char *path)
{
    return cga_write_png(path, cga_cur_colors(), 2);
}
