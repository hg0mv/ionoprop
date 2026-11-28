/*
 * compat.h - ami Linuxon és Windowson másképp megy (fájlok, könyvtárak, konzol)
 *
 * A windows.h-t szándékosan nem húzzuk be ide, mert sok rövid nevet
 * (near, far, small...) makróként definiál.
 */
#ifndef IONOPROP_COMPAT_H
#define IONOPROP_COMPAT_H

#include <stddef.h>

enum { USERDIR_CACHE = 0, USERDIR_CONFIG = 1 };

/* A program saját könyvtára (létrehozza, ha kell):
 *   Linux:   gyorsítótár  $XDG_CACHE_HOME/ionoprop  vagy ~/.cache/ionoprop
 *            beállítások  $XDG_CONFIG_HOME/ionoprop vagy ~/.config/ionoprop
 *   Windows: gyorsítótár  %LOCALAPPDATA%\IonoProp\cache
 *            beállítások  %APPDATA%\IonoProp
 * (Windowson is érvényes az XDG_* változó, ha be van állítva - teszteléshez.)
 * Visszatérés: 0 rendben, -1 hiba. */
int user_dir(int kind, char *buf, size_t len);

/* Könyvtár létrehozása a szülőkkel együtt. 0 = rendben vagy már megvolt. */
int make_dirs(const char *path);

/* Fájl átnevezése úgy, hogy a meglévő célt felülírja (Windowson a
 * rename() ezt nem teszi meg). 0 = rendben. */
int replace_file(const char *from, const char *to);

/* Konzol előkészítése: Windowson UTF-8 kiírás és ANSI-színek. */
void console_init(void);

#endif
