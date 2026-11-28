/*
 * alerts.h - riasztás sávnyitáskor
 *
 * Két lépés:
 *  1. alerts_collect(): melyik feltétel teljesül most (pl. "Es 6 m-en",
 *     "15 m nyitva JA felé"). Minden feltételnek van egy kulcsa.
 *  2. alerts_new(): ezek közül melyik ÚJ, vagyis az előző ellenőrzéskor
 *     még nem teljesült, és 3 órán belül nem szóltunk miatta.
 *     Az állapot a beállítások könyvtárában van (alerts.state), így a
 *     program újraindítása után sem jelez ugyanarra.
 *
 * Szabályok:
 *  - sporadikus E 10 / 8 / 6 m-en (valódi vételek vagy a közeli ionoszonda)
 *  - F2 nyitás 8 vagy 6 m-en bármelyik célterület felé
 *  - saját (★) célterület felé 20-10 m között legalább közepes sáv
 */
#ifndef IONOPROP_ALERTS_H
#define IONOPROP_ALERTS_H

#include <time.h>
#include "predict.h"
#include "spots.h"

#define ALERT_MAX       24
#define ALERT_COOLDOWN  (3 * 3600)    /* ugyanarra legfeljebb 3 óránként */

typedef struct {
    char key[64];      /* pl. "es:6m", "f2:6m", "tg:Japán:15m" */
    char text[160];    /* ember által olvasható, az aktuális nyelven */
} Alert;

/* Be van-e kapcsolva (settings.conf: alerts=1/0, alapból be) */
int  alerts_enabled(void);
void alerts_set_enabled(int on);

/* A most teljesülő feltételek. tg/res: az összes cél (nt darab),
 * ebből [first_custom, first_custom + n_custom) a saját. es lehet NULL. */
int alerts_collect(const Target *tg, const PathResult *res, int nt,
                   int first_custom, int n_custom, const EsStatus *es,
                   Alert *out, int max);

/* Meteorraj csúcsa (a csúcs előtti és utáni napon): egyszer szól rajonként.
 * Az alerts_collect() után hívandó, ugyanabba a listába ír. */
int alerts_add_meteor(Alert *out, int n, int max, time_t now, double lat, double lon);

/* Az újak kiválogatása (lásd fent) és az állapot mentése.
 * state_path: NULL = a szokásos hely. Ha még nincs állapotfájl (első
 * futás), csak megjegyzi a mostaniakat, és nem jelez. */
int alerts_new(const Alert *cur, int n, time_t now, const char *state_path,
               Alert *out, int max);

/* Az újak egy szövegben (sorvégekkel), az értesítéshez */
void alerts_join(const Alert *a, int n, char *buf, size_t len);

#endif
