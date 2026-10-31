#include "gamepad.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"

#define MAX_PADS 8

static SDL_GameController *pads[MAX_PADS];
static bool yes_no;

/* The stick walks past half-way and stops below 3/8, so it doesn't flicker at the edge. */
#define STICK_ON 16384
#define STICK_OFF 12288

void pad_set_yes_no(bool on)
{
    yes_no = on;
}

/* --- controllers coming and going ---------------------------------------------------------- */

static int find_pad(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id)
            return i;
    return -1;
}

static void open_pad(int device)
{
    if (!SDL_IsGameController(device) || find_pad(SDL_JoystickGetDeviceInstanceID(device)) >= 0)
        return;
    for (int i = 0; i < MAX_PADS; i++)
        if (!pads[i]) {
            pads[i] = SDL_GameControllerOpen(device);
            if (pads[i])
                printf("controller: %s\n", SDL_GameControllerName(pads[i]));
            return;
        }
}

static void close_pad(SDL_JoystickID id)
{
    const int i = find_pad(id);
    if (i >= 0) {
        SDL_GameControllerClose(pads[i]);
        pads[i] = NULL;
    }
}

void pad_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "mars: no controller support: %s\n", SDL_GetError());
        return;
    }
    char *base = SDL_GetBasePath();
    if (base) {
        char path[1024];
        snprintf(path, sizeof path, "%sgamecontrollerdb.txt", base);
        SDL_free(base);
        SDL_RWops *rw = SDL_RWFromFile(path, "rb");
        if (rw) {
            const int n = SDL_GameControllerAddMappingsFromRW(rw, 1);
            if (n > 0)
                printf("controller mappings: %d from %s\n", n, path);
        }
    }
    /* Controllers already plugged in arrive as device-added events too. */
}

void pad_event(const SDL_Event *ev)
{
    if (ev->type == SDL_CONTROLLERDEVICEADDED)
        open_pad(ev->cdevice.which);
    else if (ev->type == SDL_CONTROLLERDEVICEREMOVED)
        close_pad(ev->cdevice.which);
}

/* --- buttons to keys ----------------------------------------------------------------------- */

/* In game: the scancode each button pressed, to release the same key whatever changed since
 * (a Y/N question coming up while Back is held, say). */
static int sent[PAD_BUTTONS];

static void press(int b)
{
    const unsigned bit = 1u << b;
    if (!plat_isr_installed) { /* getch: menus, text screens, the high-score name */
        if (bit & (PAD_A | PAD_START))
            plat_push_key('\r');
        else if (bit & (PAD_B | PAD_BACK))
            plat_push_key(27);
        return;
    }
    int code = 0;
    if (yes_no && (bit & (PAD_A | PAD_START)))
        code = 0x15; /* Y */
    else if (yes_no && (bit & (PAD_B | PAD_BACK)))
        code = 0x31; /* N */
    else if (bit == PAD_LEFT)
        code = 0x4B; /* the arrows always walk */
    else if (bit == PAD_RIGHT)
        code = 0x4D;
    else if (bit == PAD_A)
        code = isr_keys.key_jump; /* 0 with an empty MARSn.KBD: no jump, as on the keyboard */
    else if (bit == PAD_X)
        code = isr_keys.key_fire;
    else if (bit & (PAD_B | PAD_Y))
        code = 0x1C; /* Enter */
    else if (bit == PAD_START)
        code = 0x3F; /* F5 */
    else if (bit == PAD_BACK)
        code = 0x01; /* Esc */
    else if (bit == PAD_LB)
        code = 0x1F; /* S */
    else if (bit == PAD_RB)
        code = 0x13; /* R */
    if (code) {
        plat_isr_key(code);
        sent[b] = code;
    }
}

static void release(int b)
{
    if (sent[b] && plat_isr_installed)
        plat_isr_key(sent[b] | 0x80);
    sent[b] = 0;
}

void pad_apply(unsigned held)
{
    static unsigned prev;
    /* Outside the game the stick does nothing, and holding it as a game starts walks. */
    if (!plat_isr_installed)
        held &= ~(unsigned)(PAD_LEFT | PAD_RIGHT);
    const unsigned changed = held ^ prev;
    prev = held;
    for (int b = 0; b < PAD_BUTTONS; b++)
        if (changed & (1u << b)) {
            if (held & (1u << b))
                press(b);
            else
                release(b);
        }
}

void pad_update(void)
{
    static const struct { SDL_GameControllerButton b; unsigned bit; } buttons[] = {
        {SDL_CONTROLLER_BUTTON_A, PAD_A}, {SDL_CONTROLLER_BUTTON_B, PAD_B},
        {SDL_CONTROLLER_BUTTON_X, PAD_X}, {SDL_CONTROLLER_BUTTON_Y, PAD_Y},
        {SDL_CONTROLLER_BUTTON_START, PAD_START}, {SDL_CONTROLLER_BUTTON_BACK, PAD_BACK},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_LB}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_RB},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT},
    };
    static bool stick_left, stick_right;
    unsigned held = 0;
    int x = 0; /* the stick pushed furthest wins */
    for (int i = 0; i < MAX_PADS; i++) {
        if (!pads[i])
            continue;
        for (size_t j = 0; j < sizeof buttons / sizeof buttons[0]; j++)
            if (SDL_GameControllerGetButton(pads[i], buttons[j].b))
                held |= buttons[j].bit;
        const int a = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTX);
        if (SDL_abs(a) > SDL_abs(x))
            x = a;
    }
    stick_left = x <= -STICK_ON || (stick_left && x < -STICK_OFF);
    stick_right = x >= STICK_ON || (stick_right && x > STICK_OFF);
    if (stick_left)
        held |= PAD_LEFT;
    if (stick_right)
        held |= PAD_RIGHT;
    pad_apply(held);
}
