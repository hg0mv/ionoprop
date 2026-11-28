/*
 * meteor.h - meteorraj-naptár a meteorszóráshoz (MS: 6 m, 2 m)
 *
 * A nagy meteorrajok aktív időszaka, csúcsa és várható óránkénti
 * meteorszáma (ZHR) az IMO naptára alapján. A csúcsot a Nap ekliptikai
 * hosszúsága (λ☉, J2000) adja meg, ahogy az IMO naptárában is: ebből
 * bármelyik évre kiszámolható a csúcs időpontja, órára pontosan.
 * A rádiáns (ahonnan a meteorok jönnek) magasságából kiszámolja, mely
 * órákban jó a meteorszórás a QTH-ról: akkor, ha a rádiáns fent van.
 */
#ifndef IONOPROP_METEOR_H
#define IONOPROP_METEOR_H

#include <stddef.h>
#include <time.h>

typedef struct {
    const char *code;            /* IMO-kód, pl. "GEM" */
    const char *name_hu, *name_en;
    int peak_month, peak_day;    /* a csúcs napja nagyjából (a számolás kezdőpontja) */
    double sollong;              /* a csúcs: a Nap hosszúsága, fok (IMO) */
    int before, after;           /* aktív: a csúcs előtt / után ennyi nap */
    int zhr;                     /* óránkénti meteorszám a csúcson */
    double ra, dec;              /* rádiáns, fok */
    int daytime;                 /* nappali raj: csak rádióval figyelhető */
} Shower;

typedef struct {
    int    valid;                /* van mit mutatni */
    int    idx;                  /* a raj indexe */
    const Shower *sh;
    int    active;               /* most aktív */
    double days_to_peak;         /* + a csúcsig, - a csúcs óta (nap) */
    time_t peak;                 /* a csúcs időpontja (UTC) */
    time_t now;                  /* amire számoltunk */
    int    alt_now;              /* a rádiáns magassága most, fok */
    int    best_from, best_to;   /* a rádiáns >= 25° ezekben az UTC-órákban (-1: soha) */
} MeteorStatus;

int meteor_count(void);
const Shower *meteor_get(int i);

/* A most legfontosabb raj: az aktívak közül a legerősebb, ha nincs aktív,
 * a következő (60 napon belül). Visszatérés: 1, ha van mit mutatni. */
int meteor_status(time_t t, double lat, double lon, MeteorStatus *st);

/* A raj neve az aktuális nyelven */
const char *meteor_name(const Shower *sh);

/* Egysoros szöveg, pl. "Geminidák: csúcs 2 nap múlva, ZHR 150, rádiáns fent 18-09 UTC" */
void meteor_line(const MeteorStatus *st, char *buf, size_t len);

/* A csúcs időpontja egy adott évben (a Nap hosszúságából) */
time_t meteor_peak_time(const Shower *sh, int year);

/* A rádiáns magassága (fok) egy adott időben és helyen */
double meteor_radiant_alt(const Shower *sh, time_t t, double lat, double lon);

#endif
