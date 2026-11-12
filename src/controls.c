#include "controls.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "bind.h"
#include "cga.h"
#include "gamepad.h"
#include "platform.h"
#include "text.h"

/* The table: a row per action, then RESET TO DEFAULTS and DONE; the action rows' four cells
 * (KEY 1, KEY 2, BUTTON 1, BUTTON 2) start at these columns, 7 wide, with the marker before. */
enum { FIRST_ROW = 4, ROW_RESET = ACTIONS, ROW_DONE, ROWS, CELLS = 2 * BIND_SLOTS };
static const int cell_col[CELLS] = {9, 17, 25, 33};

static int screen_row(int r)
{
    return r < ACTIONS ? FIRST_ROW + r : FIRST_ROW + r + 1;
}

/* Cell c of action a: its text and colour (1 fixed or none, 2 set, 3 chosen). */
static void cell(int a, int c, bool chosen, bool waiting)
{
    const bool pad = c >= BIND_SLOTS;
    const int s = c % BIND_SLOTS;
    const char *name = pad ? bind_pad_name(bindings.pad[a][s]) : bind_key_name(bindings.key[a][s]);
    char text[8];
    snprintf(text, sizeof text, "%-7s", waiting ? "?" : name ? name : "-");
    const int color = chosen ? 3 : !name || bind_locked(a, pad, s) ? 1 : 2;
    draw_text(color, text, screen_row(a), cell_col[c]);
    draw_ascii(3, chosen ? ">" : " ", screen_row(a), cell_col[c] - 1);
}

static void help(const char *line1, const char *line2)
{
    char s[41];
    snprintf(s, sizeof s, "%-38s", line1);
    draw_text(1, s, 21, 1);
    snprintf(s, sizeof s, "%-38s", line2);
    draw_text(1, s, 22, 1);
}

static void draw(int row, int col, bool waiting)
{
    for (int a = 0; a < ACTIONS; a++) {
        draw_text(a == row ? 3 : 2, bind_action_name(a), screen_row(a), 1);
        for (int c = 0; c < CELLS; c++)
            cell(a, c, a == row && c == col, waiting && a == row && c == col);
    }
    static const char *const extra[] = {"RESET TO DEFAULTS", "DONE"};
    for (int r = ROW_RESET; r < ROWS; r++) {
        draw_ascii(3, r == row ? ">" : " ", screen_row(r), 0);
        draw_text(r == row ? 3 : 2, extra[r - ROW_RESET], screen_row(r), 1);
    }
    if (waiting)
        help(col < BIND_SLOTS ? "PRESS A KEY FOR " : "PRESS A BUTTON FOR ", "ESC OR START: CANCEL");
    else
        help("ENTER OR A: CHANGE   DEL OR X: CLEAR", "ESC OR B: DONE");
    if (waiting) /* the action's name after "PRESS A ... FOR " */
        draw_text(3, bind_action_name(row), 21, col < BIND_SLOTS ? 17 : 20);
    plat_dirty();
}

/* The next key or button, waiting for it. */
static void next_input(bool *pad, int *code)
{
    while (!plat_raw_take(pad, code))
        plat_idle();
}

/* What a key or button does on the table. */
enum { IN_NONE, IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_TAKE, IN_CLEAR, IN_DONE };

static int nav(bool pad, int code)
{
    if (pad) {
        switch (code) {
        case PAD_N_UP: case PAD_N_STICK_UP: return IN_UP;
        case PAD_N_DOWN: case PAD_N_STICK_DOWN: return IN_DOWN;
        case PAD_N_LEFT: case PAD_N_STICK_LEFT: return IN_LEFT;
        case PAD_N_RIGHT: case PAD_N_STICK_RIGHT: return IN_RIGHT;
        case PAD_N_A: return IN_TAKE;
        case PAD_N_X: return IN_CLEAR;
        case PAD_N_B: case PAD_N_BACK: case PAD_N_START: return IN_DONE;
        }
        return IN_NONE;
    }
    switch (code) {
    case SDL_SCANCODE_UP: return IN_UP;
    case SDL_SCANCODE_DOWN: return IN_DOWN;
    case SDL_SCANCODE_LEFT: return IN_LEFT;
    case SDL_SCANCODE_RIGHT: return IN_RIGHT;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: return IN_TAKE;
    case SDL_SCANCODE_DELETE: case SDL_SCANCODE_BACKSPACE: return IN_CLEAR;
    case SDL_SCANCODE_ESCAPE: return IN_DONE;
    }
    return IN_NONE;
}

/* Cell `col` of action `a` takes the next key (or button); Esc or Start cancels, and the
 * other kind is ignored. */
static void change(int a, int col)
{
    draw(a, col, true);
    const bool want_pad = col >= BIND_SLOTS;
    for (;;) {
        bool pad;
        int code;
        next_input(&pad, &code);
        if ((!pad && code == SDL_SCANCODE_ESCAPE) || (pad && code == PAD_N_START) || plat_quitting())
            return;
        if (pad != want_pad)
            continue;
        if (pad && code < PAD_BINDABLE) {
            bind_set_pad(a, col - BIND_SLOTS, code);
            return;
        }
        if (!pad && bind_key_name(code)) {
            bind_set_key(a, col, code);
            return;
        }
    }
}

void controls_screen(const gamedata_t *g)
{
    cga_set_mode(0, 0); /* as Config Keyboard: SetVideoMode(4) */
    text_set_high_font(g, FONT_NORMAL);
    draw_text(2, "CONTROLS", 0, 16);
    draw_text(1, "ACTION", 2, 1);
    draw_text(1, "KEYBOARD", 2, cell_col[0]);
    draw_text(1, "CONTROLLER", 2, cell_col[BIND_SLOTS]);
    draw_text(1, "THE LEFT STICK ALWAYS WALKS.", 23, 1);
    plat_raw_begin();
    int row = 0, col = 0;
    for (;;) {
        draw(row, col, false);
        bool pad;
        int code;
        next_input(&pad, &code);
        const int in = plat_quitting() ? IN_DONE : nav(pad, code);
        if (in == IN_UP)
            row = (row + ROWS - 1) % ROWS;
        else if (in == IN_DOWN)
            row = (row + 1) % ROWS;
        else if (in == IN_LEFT && row < ACTIONS)
            col = (col + CELLS - 1) % CELLS;
        else if (in == IN_RIGHT && row < ACTIONS)
            col = (col + 1) % CELLS;
        else if (in == IN_DONE || (in == IN_TAKE && row == ROW_DONE))
            break;
        else if (in == IN_TAKE && row == ROW_RESET)
            bind_defaults();
        else if (row < ACTIONS && !bind_locked(row, col >= BIND_SLOTS, col % BIND_SLOTS)) {
            if (in == IN_TAKE)
                change(row, col);
            else if (in == IN_CLEAR && col < BIND_SLOTS)
                bind_set_key(row, col, 0);
            else if (in == IN_CLEAR)
                bind_set_pad(row, col - BIND_SLOTS, -1);
        }
    }
    plat_raw_end();
    bind_save();
}
