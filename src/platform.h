/*
 * platform.h - ablak, billentyűzet, egér: a rendszerfüggő rész
 *
 * A grafikus felület (gui.c) mindent egy pixeltömbbe rajzol (canvas.c),
 * a platformréteg csak megnyitja az ablakot, kirakja a kész képet és
 * továbbítja az eseményeket. Két változat van:
 *   platform_x11.c   - Linux (X11)
 *   platform_win32.c - Windows (Win32 API)
 */
#ifndef IONOPROP_PLATFORM_H
#define IONOPROP_PLATFORM_H

#include <stdint.h>

enum { EV_NONE = 0, EV_KEY, EV_CLICK, EV_RESIZE, EV_EXPOSE, EV_QUIT };

/* Különleges billentyűk. A többi billentyű kódja maga a karakter
 * (pl. 'm', 'M', '1', '-'), a szerkesztéshez a kis/nagybetű megmarad. */
enum {
    K_LEFT = 0x110000, K_RIGHT, K_UP, K_DOWN, K_HOME,
    K_ESC, K_ENTER, K_BACKSPACE, K_DELETE, K_F11
};

typedef struct {
    int type;          /* EV_* */
    int key;           /* EV_KEY: K_* vagy a karakter kódja */
    int x, y, button;  /* EV_CLICK: 1 = bal, 3 = jobb gomb, 4/5 = görgő fel/le */
    int w, h;          /* EV_RESIZE: az új méret */
} PlatEvent;

/* Ablak megnyitása. Visszatérés: 0 rendben, -1 hiba (az okát kiírja). */
int  plat_open(const char *title_utf8, int w, int h, int min_w, int min_h);

/* Nagyítás: a rendszer kijelző-beállítása (Windowson pl. 150% = 1,5).
 * Linuxon az IONOPROP_SCALE változó, ha nincs megadva: 1, kis képernyőn
 * (pl. Raspberry Pi 1024x600 vagy 800x480) kisebb, hogy az ablak elférjen. Az események koordinátái és méretei logikai képpontban
 * jönnek, a kirakott kép viszont fizikai méretű (logikai * nagyítás). */
double plat_scale(void);

/* A kész kép kirakása (0xRRGGBB pixelek, sorfolytonosan, w*h, fizikai méret). */
void plat_present(const uint32_t *px, int w, int h);

/* A következő esemény, ha van (1), különben 0. Nem vár. */
int  plat_next_event(PlatEvent *ev);

/* Várakozás eseményre legfeljebb ms ezredmásodpercig. */
void plat_wait(int ms);

/* Teljes képernyő be/ki. Az ablak megnyitása előtt is hívható (akkor
 * már teljes képernyőn nyílik). Teljes képernyőn az egérmutató néhány
 * másodperc mozdulatlanság után eltűnik (érintőképernyős kijelzőhöz). */
void plat_set_fullscreen(int on);
int  plat_fullscreen(void);

/* Rövid figyelmeztető hang (riasztás) */
void plat_beep(void);

void plat_close(void);

#endif
