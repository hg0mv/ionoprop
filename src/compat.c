/*
 * compat.c - Linux / Windows különbségek (lásd compat.h)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif

#include "compat.h"

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static int mkdir1(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

int make_dirs(const char *path)
{
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s", path);
    size_t n = strlen(tmp);
    while (n > 1 && (tmp[n - 1] == '/' || tmp[n - 1] == '\\')) tmp[--n] = '\0';

    /* a köztes szinteket megpróbáljuk létrehozni; a hibát (pl. "C:" vagy
     * már létező könyvtár) itt nem nézzük, csak a végén, hogy megvan-e */
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = '\0';
            if (!is_dir(tmp)) mkdir1(tmp);
            *p = c;
        }
    }
    if (!is_dir(tmp) && mkdir1(tmp) != 0 && errno != EEXIST) return -1;
    return is_dir(tmp) ? 0 : -1;
}

int user_dir(int kind, char *buf, size_t len)
{
    const char *xdg = getenv(kind == USERDIR_CACHE ? "XDG_CACHE_HOME" : "XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(buf, len, "%s/ionoprop", xdg);
    } else {
#ifdef _WIN32
        const char *base = getenv(kind == USERDIR_CACHE ? "LOCALAPPDATA" : "APPDATA");
        if (!base || !*base) base = getenv("USERPROFILE");
        if (!base || !*base) return -1;
        if (kind == USERDIR_CACHE) snprintf(buf, len, "%s\\IonoProp\\cache", base);
        else                       snprintf(buf, len, "%s\\IonoProp", base);
#else
        const char *home = getenv("HOME");
        if (home && *home)
            snprintf(buf, len, "%s/%s/ionoprop", home, kind == USERDIR_CACHE ? ".cache" : ".config");
        else if (kind == USERDIR_CACHE)
            snprintf(buf, len, "/tmp/ionoprop");
        else
            return -1;
#endif
    }
    if (make_dirs(buf) != 0) {
        fprintf(stderr, "Nem tudom létrehozni a könyvtárat: %s (%s)\n", buf, strerror(errno));
        return -1;
    }
    return 0;
}

int replace_file(const char *from, const char *to)
{
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(from, to);
#endif
}

void console_init(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
#endif
}
