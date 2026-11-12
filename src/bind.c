#include "bind.h"

#include <SDL.h>
#include <ctype.h>
#include <string.h>

#include "gamepad.h"
#include "platform.h"
#include "settings.h"

bindings_t bindings;

/* The keyboard bindings mars.ini (or the Controls screen) set, which MARSn.KBD's doesn't
 * replace. */
static bool key_from_ini[ACTIONS];

static const struct { const char *name, *ini; } actions[ACTIONS] = {
    {"LEFT", "left"}, {"RIGHT", "right"}, {"JUMP", "jump"}, {"FIRE", "fire"},
    {"USE", "use"}, {"MENU", "menu"}, {"SAVE", "save"}, {"RESTORE", "restore"},
    {"QUIT", "quit"}, {"PAUSE", "pause"}, {"SOUND", "sound"}, {"GIVE UP", "give_up"},
};

/* The keys that can be bound, by the names shown and written. F4, F11 and F12 are the port's
 * own (smooth movement, full screen, screenshot). */
static const struct { SDL_Scancode sc; const char *name; } keys[] = {
    {SDL_SCANCODE_A, "A"}, {SDL_SCANCODE_B, "B"}, {SDL_SCANCODE_C, "C"}, {SDL_SCANCODE_D, "D"},
    {SDL_SCANCODE_E, "E"}, {SDL_SCANCODE_F, "F"}, {SDL_SCANCODE_G, "G"}, {SDL_SCANCODE_H, "H"},
    {SDL_SCANCODE_I, "I"}, {SDL_SCANCODE_J, "J"}, {SDL_SCANCODE_K, "K"}, {SDL_SCANCODE_L, "L"},
    {SDL_SCANCODE_M, "M"}, {SDL_SCANCODE_N, "N"}, {SDL_SCANCODE_O, "O"}, {SDL_SCANCODE_P, "P"},
    {SDL_SCANCODE_Q, "Q"}, {SDL_SCANCODE_R, "R"}, {SDL_SCANCODE_S, "S"}, {SDL_SCANCODE_T, "T"},
    {SDL_SCANCODE_U, "U"}, {SDL_SCANCODE_V, "V"}, {SDL_SCANCODE_W, "W"}, {SDL_SCANCODE_X, "X"},
    {SDL_SCANCODE_Y, "Y"}, {SDL_SCANCODE_Z, "Z"},
    {SDL_SCANCODE_1, "1"}, {SDL_SCANCODE_2, "2"}, {SDL_SCANCODE_3, "3"}, {SDL_SCANCODE_4, "4"},
    {SDL_SCANCODE_5, "5"}, {SDL_SCANCODE_6, "6"}, {SDL_SCANCODE_7, "7"}, {SDL_SCANCODE_8, "8"},
    {SDL_SCANCODE_9, "9"}, {SDL_SCANCODE_0, "0"},
    {SDL_SCANCODE_RETURN, "ENTER"}, {SDL_SCANCODE_ESCAPE, "ESC"}, {SDL_SCANCODE_BACKSPACE, "BKSP"},
    {SDL_SCANCODE_TAB, "TAB"}, {SDL_SCANCODE_SPACE, "SPACE"}, {SDL_SCANCODE_MINUS, "-"},
    {SDL_SCANCODE_EQUALS, "="}, {SDL_SCANCODE_LEFTBRACKET, "["}, {SDL_SCANCODE_RIGHTBRACKET, "]"},
    {SDL_SCANCODE_BACKSLASH, "\\"}, {SDL_SCANCODE_SEMICOLON, ";"}, {SDL_SCANCODE_APOSTROPHE, "'"},
    {SDL_SCANCODE_GRAVE, "`"}, {SDL_SCANCODE_COMMA, ","}, {SDL_SCANCODE_PERIOD, "."},
    {SDL_SCANCODE_SLASH, "/"}, {SDL_SCANCODE_CAPSLOCK, "CAPS"},
    {SDL_SCANCODE_F1, "F1"}, {SDL_SCANCODE_F2, "F2"}, {SDL_SCANCODE_F3, "F3"}, {SDL_SCANCODE_F5, "F5"},
    {SDL_SCANCODE_F6, "F6"}, {SDL_SCANCODE_F7, "F7"}, {SDL_SCANCODE_F8, "F8"}, {SDL_SCANCODE_F9, "F9"},
    {SDL_SCANCODE_F10, "F10"},
    {SDL_SCANCODE_INSERT, "INS"}, {SDL_SCANCODE_HOME, "HOME"}, {SDL_SCANCODE_PAGEUP, "PGUP"},
    {SDL_SCANCODE_DELETE, "DEL"}, {SDL_SCANCODE_END, "END"}, {SDL_SCANCODE_PAGEDOWN, "PGDN"},
    {SDL_SCANCODE_RIGHT, "RIGHT"}, {SDL_SCANCODE_LEFT, "LEFT"}, {SDL_SCANCODE_DOWN, "DOWN"},
    {SDL_SCANCODE_UP, "UP"},
    {SDL_SCANCODE_KP_DIVIDE, "KP/"}, {SDL_SCANCODE_KP_MULTIPLY, "KP*"}, {SDL_SCANCODE_KP_MINUS, "KP-"},
    {SDL_SCANCODE_KP_PLUS, "KP+"}, {SDL_SCANCODE_KP_ENTER, "KPENTER"}, {SDL_SCANCODE_KP_1, "KP1"},
    {SDL_SCANCODE_KP_2, "KP2"}, {SDL_SCANCODE_KP_3, "KP3"}, {SDL_SCANCODE_KP_4, "KP4"},
    {SDL_SCANCODE_KP_5, "KP5"}, {SDL_SCANCODE_KP_6, "KP6"}, {SDL_SCANCODE_KP_7, "KP7"},
    {SDL_SCANCODE_KP_8, "KP8"}, {SDL_SCANCODE_KP_9, "KP9"}, {SDL_SCANCODE_KP_0, "KP0"},
    {SDL_SCANCODE_KP_PERIOD, "KP."},
    {SDL_SCANCODE_LCTRL, "LCTRL"}, {SDL_SCANCODE_LSHIFT, "LSHIFT"}, {SDL_SCANCODE_LALT, "LALT"},
    {SDL_SCANCODE_RCTRL, "RCTRL"}, {SDL_SCANCODE_RSHIFT, "RSHIFT"}, {SDL_SCANCODE_RALT, "RALT"},
};

/* The controller's buttons by PAD_ number (gamepad.h); the stick isn't bound. The handheld's
 * own names for its shoulder buttons and Select. */
static const char *const pad_names[PAD_BINDABLE] = {
#ifdef MARS_MIYOO
    "A", "B", "X", "Y", "START", "SELECT", "L1", "R1", "D-LEFT", "D-RIGHT", "D-UP", "D-DOWN",
    "L2", "R2", "L3", "R3",
#else
    "A", "B", "X", "Y", "START", "BACK", "LB", "RB", "D-LEFT", "D-RIGHT", "D-UP", "D-DOWN",
    "LT", "RT", "L3", "R3",
#endif
};

const char *bind_action_name(int a)
{
    return actions[a].name;
}

const char *bind_key_name(int key)
{
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if ((int)keys[i].sc == key)
            return keys[i].name;
    return NULL;
}

const char *bind_pad_name(int button)
{
    return button >= 0 && button < PAD_BINDABLE ? pad_names[button] : NULL;
}

bool bind_locked(int a, bool pad, int slot)
{
    (void)pad;
    return a == ACT_MENU && slot == 0;
}

void bind_defaults(void)
{
    static const int k[ACTIONS][BIND_SLOTS] = {
        {SDL_SCANCODE_LEFT, SDL_SCANCODE_LALT}, {SDL_SCANCODE_RIGHT, SDL_SCANCODE_LCTRL},
        {SDL_SCANCODE_SPACE, 0}, {SDL_SCANCODE_LSHIFT, 0},
        {SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER}, {SDL_SCANCODE_ESCAPE, 0},
        {SDL_SCANCODE_S, 0}, {SDL_SCANCODE_R, 0}, {SDL_SCANCODE_Q, 0},
        {SDL_SCANCODE_F5, 0}, {SDL_SCANCODE_F1, 0}, {SDL_SCANCODE_F6, 0},
    };
    static const int p[ACTIONS][BIND_SLOTS] = {
        {PAD_N_LEFT, -1}, {PAD_N_RIGHT, -1}, {PAD_N_A, -1}, {PAD_N_X, -1}, {PAD_N_B, PAD_N_Y},
        {PAD_N_START, -1}, {PAD_N_LB, -1}, {PAD_N_RB, -1}, {PAD_N_BACK, -1}, {-1, -1}, {-1, -1},
        {-1, -1},
    };
    /* The handheld's buttons are labelled the Nintendo way round (A right, B below, Y left, X
     * above), and its SDL names them by label, so the same places do the same: B jumps, Y fires,
     * A or X uses. */
    static const int handheld[ACTIONS][BIND_SLOTS] = {
        {PAD_N_LEFT, -1}, {PAD_N_RIGHT, -1}, {PAD_N_B, -1}, {PAD_N_Y, -1}, {PAD_N_A, PAD_N_X},
        {PAD_N_START, -1}, {PAD_N_LB, -1}, {PAD_N_RB, -1}, {PAD_N_BACK, -1}, {-1, -1}, {-1, -1},
        {-1, -1},
    };
    memcpy(bindings.key, k, sizeof k);
    memcpy(bindings.pad, PLAT_HANDHELD ? handheld : p, sizeof p);
    memset(key_from_ini, 0, sizeof key_from_ini);
}

/* A setup key's scancode to the key: Ctrl and Alt as the left ones. */
static int key_of_xt(int xt)
{
    static const struct { int xt; SDL_Scancode sc; } m[] = {
        {0x1D, SDL_SCANCODE_LCTRL}, {0x38, SDL_SCANCODE_LALT}, {0x2A, SDL_SCANCODE_LSHIFT},
        {0x36, SDL_SCANCODE_RSHIFT}, {0x39, SDL_SCANCODE_SPACE}, {0x52, SDL_SCANCODE_INSERT},
        {0x53, SDL_SCANCODE_DELETE}, {0x43, SDL_SCANCODE_F9}, {0x44, SDL_SCANCODE_F10},
        {0x4B, SDL_SCANCODE_LEFT}, {0x4D, SDL_SCANCODE_RIGHT},
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++)
        if (m[i].xt == xt)
            return (int)m[i].sc;
    return 0;
}

void bind_seed_setup(int left, int right, int jump, int fire)
{
    /* the arrows always walked; the setup's keys go beside them */
    const int k[4][BIND_SLOTS] = {
        {SDL_SCANCODE_LEFT, left == 0x4B ? 0 : key_of_xt(left)},
        {SDL_SCANCODE_RIGHT, right == 0x4D ? 0 : key_of_xt(right)},
        {key_of_xt(jump), 0},
        {key_of_xt(fire), 0},
    };
    for (int a = ACT_LEFT; a <= ACT_FIRE; a++) {
        if (key_from_ini[a])
            continue;
        for (int s = 0; s < BIND_SLOTS; s++)
            bindings.key[a][s] = 0;
        for (int s = 0; s < BIND_SLOTS; s++)
            if (k[a][s])
                bind_set_key(a, s, k[a][s]);
    }
}

void bind_set_key(int a, int slot, int key)
{
    if (key)
        for (int b = 0; b < ACTIONS; b++)
            for (int s = 0; s < BIND_SLOTS; s++)
                if (bindings.key[b][s] == key && !bind_locked(b, false, s))
                    bindings.key[b][s] = 0;
    bindings.key[a][slot] = key;
}

void bind_set_pad(int a, int slot, int button)
{
    if (button >= 0)
        for (int b = 0; b < ACTIONS; b++)
            for (int s = 0; s < BIND_SLOTS; s++)
                if (bindings.pad[b][s] == button && !bind_locked(b, true, s))
                    bindings.pad[b][s] = -1;
    bindings.pad[a][slot] = button;
}

/* --- mars.ini ------------------------------------------------------------------------------- */

/* "keys.jump" / "pad.jump": the action, and whether it's the controller's; -1 if neither. */
static int ini_action(const char *key, bool *pad)
{
    const char *name;
    if (!strncmp(key, "keys.", 5)) {
        *pad = false;
        name = key + 5;
    } else if (!strncmp(key, "pad.", 4)) {
        *pad = true;
        name = key + 4;
    } else {
        return -1;
    }
    for (int a = 0; a < ACTIONS; a++)
        if (!strcmp(name, actions[a].ini))
            return a;
    return -1;
}

bool bind_is_key(const char *key)
{
    bool pad;
    return ini_action(key, &pad) >= 0;
}

static bool same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return false;
    return !*a && !*b;
}

int bind_ini_set(const char *key, const char *value, char *err, size_t errlen)
{
    bool pad;
    const int a = ini_action(key, &pad);
    if (a < 0) {
        snprintf(err, errlen, "unknown setting '%s'", key);
        return -1;
    }
    int got[BIND_SLOTS] = {pad ? -1 : 0, pad ? -1 : 0}, n = 0;
    char buf[64];
    snprintf(buf, sizeof buf, "%s", value);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok == ' ' || *tok == '\t')
            tok++;
        char *e = tok + strlen(tok);
        while (e > tok && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        if (!*tok || !strcmp(tok, "-")) {
            n++;
            continue;
        }
        int v = pad ? -1 : 0;
        if (pad) {
            for (int b = 0; b < PAD_BINDABLE; b++)
                if (same_name(tok, pad_names[b]))
                    v = b;
        } else {
            for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
                if (same_name(tok, keys[i].name))
                    v = (int)keys[i].sc;
        }
        if (v == (pad ? -1 : 0)) {
            snprintf(err, errlen, "%s: no %s called '%s'", key, pad ? "button" : "key", tok);
            return -1;
        }
        if (bind_locked(a, pad, 0) && v == (pad ? bindings.pad[a][0] : bindings.key[a][0]))
            continue; /* the fixed one (Esc, Start), which needn't be written */
        if (n < BIND_SLOTS)
            got[n] = v;
        n++;
    }
    /* the values go into the slots that aren't fixed, in order */
    for (int s = 0, i = 0; s < BIND_SLOTS; s++) {
        if (bind_locked(a, pad, s))
            continue;
        if (pad)
            bind_set_pad(a, s, got[i++]);
        else
            bind_set_key(a, s, got[i++]);
    }
    if (!pad)
        key_from_ini[a] = true;
    return 0;
}

bool bind_ini_value(const char *key, char *out, size_t len)
{
    bool pad;
    const int a = ini_action(key, &pad);
    if (a < 0)
        return false;
    const char *n[BIND_SLOTS];
    for (int s = 0; s < BIND_SLOTS; s++) {
        n[s] = pad ? bind_pad_name(bindings.pad[a][s]) : bind_key_name(bindings.key[a][s]);
        if (!n[s])
            n[s] = "-";
    }
    snprintf(out, len, "%s, %s", n[0], n[1]);
    return true;
}

void bind_write(FILE *f)
{
    char key[32], value[64];
    fprintf(f, "\n[keys]\n"
               "; the keyboard's two keys for each of the game's actions (- for none), as the main\n"
               "; menu's Controls screen sets them. The first of menu is always Esc\n");
    for (int a = 0; a < ACTIONS; a++) {
        snprintf(key, sizeof key, "keys.%s", actions[a].ini);
        bind_ini_value(key, value, sizeof value);
        fprintf(f, "%s = %s\n", actions[a].ini, value);
    }
    static const char pad_help[] =
#ifdef MARS_MIYOO
        "; the handheld's two buttons for each action: A B X Y START SELECT L1 R1 L2 R2\n"
        "; D-LEFT D-RIGHT D-UP D-DOWN. The first of menu is always START\n";
#else
        "; the controller's two buttons for each action: A B X Y START BACK LB RB LT RT L3 R3\n"
        "; D-LEFT D-RIGHT D-UP D-DOWN (Xbox names). The first of menu is always START, and\n"
        "; the left stick always walks\n";
#endif
    fprintf(f, "\n[pad]\n%s", pad_help);
    for (int a = 0; a < ACTIONS; a++) {
        snprintf(key, sizeof key, "pad.%s", actions[a].ini);
        bind_ini_value(key, value, sizeof value);
        fprintf(f, "%s = %s\n", actions[a].ini, value);
    }
}

void bind_save(void)
{
    static char names[2 * ACTIONS][32];
    const char *list[2 * ACTIONS];
    for (int a = 0; a < ACTIONS; a++) {
        snprintf(names[a], sizeof names[a], "keys.%s", actions[a].ini);
        snprintf(names[ACTIONS + a], sizeof names[0], "pad.%s", actions[a].ini);
        key_from_ini[a] = true;
    }
    for (int i = 0; i < 2 * ACTIONS; i++)
        list[i] = names[i];
    settings_save(list, 2 * ACTIONS);
}

/* --- while playing --------------------------------------------------------------------------- */

int bind_key_action(int key)
{
    for (int a = 0; a < ACTIONS; a++)
        for (int s = 0; s < BIND_SLOTS; s++)
            if (bindings.key[a][s] == key)
                return a;
    return -1;
}

int bind_pad_action(int button)
{
    for (int a = 0; a < ACTIONS; a++)
        for (int s = 0; s < BIND_SLOTS; s++)
            if (bindings.pad[a][s] == button)
                return a;
    return -1;
}

int bind_code(int a)
{
    static const int codes[ACTIONS] = {0x4B, 0x4D, 0, 0, 0x1C, SC_PAUSE_MENU, 0x1F, 0x13, 0x10, 0x3F, 0x3B, 0x40};
    return a == ACT_JUMP ? isr_keys.key_jump : a == ACT_FIRE ? isr_keys.key_fire : codes[a];
}

bool bind_reserved(int xt)
{
    if (xt == 0x01) /* Esc, the pause menu's own */
        return true;
    for (int a = 0; a < ACTIONS; a++)
        if (bind_code(a) == xt)
            return true;
    return false;
}

static int held[ACTIONS], sent[ACTIONS];

void bind_press(int a)
{
    if (held[a]++ == 0 && plat_isr_installed) {
        sent[a] = bind_code(a);
        if (sent[a])
            plat_isr_key(sent[a]);
    }
}

void bind_release(int a)
{
    if (held[a] > 0 && --held[a] == 0) {
        if (sent[a] && plat_isr_installed)
            plat_isr_key(sent[a] | 0x80);
        sent[a] = 0;
    }
}
