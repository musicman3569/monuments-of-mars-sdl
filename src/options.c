#include "options.h"

#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "settings.h"
#include "text.h"

void menu_box(const char *title, const char *const *lines, int n, int sel, int width)
{
    const int col = (40 - width) / 2, row = 1 + (24 - (n + 5)) / 2;
    char s[41];
    for (int r = 0; r < n + 5; r++) {
        memset(s, r == 0 || r == n + 4 ? '*' : ' ', (size_t)width);
        s[0] = s[width - 1] = '*';
        s[width] = 0;
        draw_text(1, s, row + r, col);
    }
    draw_text(0x0F, title, row + 1, col + (width - (int)strlen(title)) / 2);
    for (int i = 0; i < n; i++) {
        draw_ascii(0x0F, i == sel ? ">" : " ", row + 3 + i, col + 2);
        draw_text(i == sel ? 0x0F : 2, lines[i], row + 3 + i, col + 4);
    }
    plat_dirty();
}

/* The settings switched in the menu, saved as it closes. */
static void save(const bool *changed, int n)
{
    static const char *const keys[] = {"game.physics", "display.smooth", "display.crt", "display.fullscreen",
                                       "input.quick_taps"};
    const char *list[5];
    int k = 0;
    for (int i = 0; i < n; i++)
        if (changed[i])
            list[k++] = keys[i];
    settings_save(list, k);
}

int options_menu(menu_key_fn key, bool *physics_changed)
{
    enum { PHYSICS, SMOOTH, CRT, FULLSCREEN, QUICK_TAPS, BACK, N };
    bool changed[BACK] = {false};
    int sel = 0;
    for (;;) {
        char text[N][24];
        const char *lines[N];
        const struct { const char *name; int on; } opt[BACK] = {
            {"PHYSICS", settings.physics == PHYSICS_MODERN}, {"SMOOTH MOVEMENT", settings.smooth},
            {"CRT FILTER", settings.crt}, {"FULL SCREEN", settings.fullscreen},
            {"QUICK TAPS FIX", settings.quick_taps},
        };
        for (int i = 0; i < BACK; i++) {
            snprintf(text[i], sizeof text[i], "%-15s%8s", opt[i].name,
                     i == PHYSICS ? (opt[i].on ? "MODERN" : "CLASSIC") : opt[i].on ? "ON" : "OFF");
            lines[i] = text[i];
        }
        lines[BACK] = "BACK";
        menu_box("OPTIONS", lines, N, sel, 29);
        const int k = key();
        if (k == 0x48)
            sel = (sel + N - 1) % N;
        else if (k == 0x50)
            sel = (sel + 1) % N;
        else if (k == SC_PAUSE_MENU || k == 0x01 || (k == 0x1C && sel == BACK)) {
            save(changed, BACK);
            return k == SC_PAUSE_MENU;
        } else if (k == 0x1C || k == 0x4B || k == 0x4D) {
            changed[sel] = true;
            switch (sel) {
            case PHYSICS:
                settings.physics = settings.physics == PHYSICS_MODERN ? PHYSICS_CLASSIC : PHYSICS_MODERN;
                *physics_changed = true;
                plat_set_title();
                break;
            case SMOOTH:
                settings.smooth = !settings.smooth;
                plat_set_title();
                break;
            case CRT:
                settings.crt = !settings.crt;
                break;
            case FULLSCREEN:
                plat_set_fullscreen(!settings.fullscreen);
                break;
            case QUICK_TAPS:
                settings.quick_taps = !settings.quick_taps;
                break;
            }
            plat_dirty();
        }
    }
}
