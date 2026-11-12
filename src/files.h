/* Port extra: the game's files on the host, names in any case, and where the port and the game
 * are.
 *
 * DOS doesn't care about case, so a copy of the game may have MARS1.HS or mars1.hs, whatever the
 * original asked for. On Linux (case-sensitive) the port looks for a file in any case before
 * making a new one.
 *
 * The port's folder is where mars.exe (or mars) is, except in the Linux AppImage, where the
 * program runs from a read-only folder that comes and goes: there it's the AppImage's own
 * folder. mars.ini and gamecontrollerdb.txt are kept there, and the game is looked for there. */
#ifndef MARS_FILES_H
#define MARS_FILES_H

#include <stdbool.h>
#include <stddef.h>

/* The path of the file `name` in the folder `dir` ("" for the current folder; with or without
 * a separator at the end): the name as it is in the folder, in whatever case, or as given if it
 * isn't there. */
void file_path(char *out, size_t n, const char *dir, const char *name);

/* The file exists and can be read. */
bool file_exists(const char *path);

/* The port's folder, with a separator at the end ("" if it can't be told). */
void port_dir(char *out, size_t n);

/* Where a volume is looked for when the command line doesn't name one, in this order: the
 * current folder, the port's folder and a "game" folder in it. Returns how many (at most 3),
 * each with a separator at the end or "" for the current folder. */
enum { GAME_PLACES = 3 };
int game_places(char places[GAME_PLACES][1024]);

#endif
