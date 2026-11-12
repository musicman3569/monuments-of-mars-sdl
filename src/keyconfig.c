#include "keyconfig.h"

#include <stdio.h>

#include "bind.h"
#include "platform.h"

/* ApplyKeyConfig (1054:6a94)'s setups. PC/XT scancodes: 1D Ctrl, 38 Alt, 2A left Shift, 36 right
 * Shift, 39 Space, 52 Ins, 53 Del, 43 F9, 44 F10, 4B / 4D the arrows (which the ISR always
 * accepts for walking). */
static void seed(int c)
{
    static const struct { char c; unsigned char left, right, jump, fire; } setups[] = {
        {'A', 0x1D, 0x38, 0x36, 0x39}, {'B', 0x1D, 0x38, 0x39, 0x36},
        {'C', 0x38, 0x1D, 0x2A, 0x39}, {'D', 0x38, 0x1D, 0x39, 0x2A},
        {'E', 0x52, 0x53, 0x38, 0x39}, {'F', 0x43, 0x44, 0x36, 0x39},
        {'G', 0x4B, 0x4D, 0x1D, 0x38}, {'X', 0x4B, 0x4D, 0x39, 0x2A},
        {'a', 0x1D, 0x38, 0x36, 0x39}, /* the only lower-case letter it takes */
    };
    for (size_t i = 0; i < sizeof setups / sizeof setups[0]; i++)
        if (setups[i].c == (char)c)
            bind_seed_setup(setups[i].left, setups[i].right, setups[i].jump, setups[i].fire);
    /* any other letter leaves the keys as they were */
}

void keyconfig_load(const gamedata_t *g)
{
    isr_keys.key_left = 0x4B;  /* [C8EC] */
    isr_keys.key_right = 0x4D; /* [C92D] */
    isr_keys.key_jump = 0x39;  /* [C8AE] */
    isr_keys.key_fire = 0x2A;  /* [8A25] */
    char path[1100];
    gamedata_path(g, "KBD", path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        seed('D');
        return;
    }
    seed(fgetc(f) & 0xFF);
    fclose(f);
}
