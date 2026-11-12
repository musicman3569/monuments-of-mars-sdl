/* The game's files on the host (see files.h). */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* opendir, strcasecmp */
#endif
#include "files.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#include <strings.h>
#endif

/* dir/name, or "" if it doesn't fit */
static void join(char *out, size_t n, const char *dir, const char *name)
{
    const size_t len = strlen(dir), name_len = strlen(name);
    const size_t sep = len && dir[len - 1] != '/' && dir[len - 1] != '\\';
    if (len + sep + name_len >= n) {
        *out = '\0';
        return;
    }
    memcpy(out, dir, len);
    if (sep)
        out[len] = '/';
    memcpy(out + len + sep, name, name_len + 1);
}

bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

void file_path(char *out, size_t n, const char *dir, const char *name)
{
    join(out, n, dir, name);
#ifndef _WIN32
    if (file_exists(out))
        return;
    DIR *d = opendir(*dir ? dir : ".");
    if (!d)
        return;
    for (const struct dirent *e; (e = readdir(d)) != NULL;)
        if (!strcasecmp(e->d_name, name)) {
            join(out, n, dir, e->d_name);
            break;
        }
    closedir(d);
#endif
}

void port_dir(char *out, size_t n)
{
    /* the AppImage's runtime names the AppImage itself in $APPIMAGE */
    const char *appimage = getenv("APPIMAGE");
    const char *slash = appimage ? strrchr(appimage, '/') : NULL;
    if (slash) {
        snprintf(out, n, "%.*s", (int)(slash + 1 - appimage), appimage);
        return;
    }
    char *base = SDL_GetBasePath();
    snprintf(out, n, "%s", base ? base : "");
    SDL_free(base);
}

int game_places(char places[GAME_PLACES][1024])
{
    int count = 0;
    places[count++][0] = '\0';
    char dir[1000];
    port_dir(dir, sizeof dir);
    if (*dir) {
        snprintf(places[count++], sizeof places[0], "%s", dir);
        snprintf(places[count++], sizeof places[0], "%sgame/", dir);
    }
    return count;
}
