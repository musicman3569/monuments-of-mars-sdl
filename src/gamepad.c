#include "gamepad.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "bind.h"
#include "platform.h"

#define MAX_PADS 8

static SDL_GameController *pads[MAX_PADS];
static bool yes_no, menu, raw;

/* The stick walks past half-way and stops below 3/8, so it doesn't flicker at the edge. */
#define STICK_ON 16384
#define STICK_OFF 12288

void pad_set_yes_no(bool on)
{
    yes_no = on;
}

void pad_set_menu(bool on)
{
    menu = on;
}

void pad_set_raw(bool on)
{
    raw = on;
}

bool pad_playing(void)
{
    return plat_isr_installed && !menu && !yes_no;
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

/* In game: the scancode each button pressed, or the action it pressed (+ 1), to let go of the
 * same whatever changed since (a Y/N question coming up while Back is held, say). */
static int sent[PAD_BUTTONS], sent_action[PAD_BUTTONS];

static void press(int b)
{
    const unsigned bit = 1u << b;
    const unsigned up = PAD_UP | PAD_STICK_UP, down = PAD_DOWN | PAD_STICK_DOWN;
    const unsigned left = PAD_LEFT | PAD_STICK_LEFT, right = PAD_RIGHT | PAD_STICK_RIGHT;
    if (!plat_isr_installed) { /* getch: menus, text screens, the high-score name */
        if (menu && (bit & (up | down | left | right))) { /* an arrow key */
            plat_push_key(0);
            plat_push_key(bit & up ? 0x48 : bit & down ? 0x50 : bit & left ? 0x4B : 0x4D);
        } else if (bit & (PAD_A | PAD_START))
            plat_push_key('\r');
        else if (bit & (PAD_B | PAD_BACK))
            plat_push_key(27);
        return;
    }
    int code = 0;
    if (menu) {
        code = bit & up ? 0x48 : bit & down ? 0x50 : bit & left ? 0x4B : bit & right ? 0x4D
             : bit == PAD_A ? 0x1C : bit & (PAD_B | PAD_BACK) ? 0x01 : bit == PAD_START ? SC_PAUSE_MENU : 0;
    } else if (yes_no && (bit & (PAD_A | PAD_START)))
        code = 0x15; /* Y */
    else if (yes_no && (bit & (PAD_B | PAD_BACK)))
        code = 0x31; /* N */
    else if (!yes_no) { /* playing: the stick walks, the buttons do what they're bound to */
        const int a = bit == PAD_STICK_LEFT ? ACT_LEFT : bit == PAD_STICK_RIGHT ? ACT_RIGHT : bind_pad_action(b);
        if (a >= 0) {
            bind_press(a);
            sent_action[b] = a + 1;
        }
    }
    if (code) {
        plat_isr_key(code);
        sent[b] = code;
    }
}

static void release(int b)
{
    if (sent_action[b])
        bind_release(sent_action[b] - 1);
    sent_action[b] = 0;
    if (sent[b] && plat_isr_installed)
        plat_isr_key(sent[b] | 0x80);
    sent[b] = 0;
}

void pad_apply(unsigned held)
{
    static unsigned prev;
    if (raw) { /* the controls screen: what was pressed, nothing else */
        for (int b = 0; b < PAD_BUTTONS; b++)
            if ((held & ~prev) & (1u << b))
                plat_raw_push(true, b);
        prev = held;
        return;
    }
    /* Outside the game the stick and d-pad do nothing but move through the main menu, and
     * holding them as a game starts walks; the stick's up and down are only for the menus. */
    if (!plat_isr_installed && !menu)
        held &= ~(unsigned)(PAD_LEFT | PAD_RIGHT | PAD_STICK_LEFT | PAD_STICK_RIGHT);
    if (!menu)
        held &= ~(unsigned)(PAD_STICK_UP | PAD_STICK_DOWN);
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
        {SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, PAD_L3}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK, PAD_R3},
    };
    static bool stick_left, stick_right, stick_up, stick_down, trig_l, trig_r;
    unsigned held = 0;
    int x = 0, y = 0, tl = 0, tr = 0; /* the stick pushed furthest wins */
    for (int i = 0; i < MAX_PADS; i++) {
        if (!pads[i])
            continue;
        for (size_t j = 0; j < sizeof buttons / sizeof buttons[0]; j++)
            if (SDL_GameControllerGetButton(pads[i], buttons[j].b))
                held |= buttons[j].bit;
        const int a = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTX);
        if (SDL_abs(a) > SDL_abs(x))
            x = a;
        const int b = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_LEFTY);
        if (SDL_abs(b) > SDL_abs(y))
            y = b;
        const int l = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        if (l > tl)
            tl = l;
        const int r = SDL_GameControllerGetAxis(pads[i], SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (r > tr)
            tr = r;
    }
    stick_left = x <= -STICK_ON || (stick_left && x < -STICK_OFF);
    stick_right = x >= STICK_ON || (stick_right && x > STICK_OFF);
    stick_up = y <= -STICK_ON || (stick_up && y < -STICK_OFF);
    stick_down = y >= STICK_ON || (stick_down && y > STICK_OFF);
    trig_l = tl >= STICK_ON || (trig_l && tl > STICK_OFF); /* the triggers as buttons */
    trig_r = tr >= STICK_ON || (trig_r && tr > STICK_OFF);
    held |= (stick_left ? PAD_STICK_LEFT : 0) | (stick_right ? PAD_STICK_RIGHT : 0) |
            (stick_up ? PAD_STICK_UP : 0) | (stick_down ? PAD_STICK_DOWN : 0) |
            (trig_l ? PAD_LT : 0) | (trig_r ? PAD_RT : 0);
    pad_apply(held);
}
