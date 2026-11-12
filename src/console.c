#include "console.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <stdbool.h>
#include <windows.h>
#include <stdio.h>

/* stdout / stderr that go nowhere (no console of its own): to the parent's console, if any. */
void console_attach(void)
{
    const bool out = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN;
    const bool err = GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_UNKNOWN;
    if ((!out && !err) || !AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    if (out)
        freopen("CONOUT$", "w", stdout);
    if (err)
        freopen("CONOUT$", "w", stderr);
}
#else
void console_attach(void)
{
}
#endif
