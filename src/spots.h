/*
 * spots.h - a beérkezett valódi vételek (PSK Reporter) tárolása és
 *           felhasználása: célterület-jelzés, sporadikus E, térképrajz
 *
 * Közös rész az asztali programnak (gui.c) és az Android-magnak (appcore.c).
 */
#ifndef IONOPROP_SPOTS_H
#define IONOPROP_SPOTS_H

#include <time.h>
#include "canvas.h"
#include "data.h"
#include "mapview.h"
#include "model.h"
#include "predict.h"
#include "pskr.h"
#include "validate.h"

#define SPOT_MAX        20000
#define SPOT_KEEP       (60 * 60)  /* ennyi ideig tartjuk meg */
#define SPOT_SHOW       (30 * 60)  /* ennyi ideig látszik egy spot */
#define SPOT_TARGET_KM  800.0      /* a távoli vég ennyin belül legyen a célhoz */
#define SPOT_MINPAIR    2          /* ennyi különböző állomáspár kell egy jelzéshez */

typedef struct {
    time_t   t;
    int      band, snr;
    double   flat, flon;           /* távoli vég */
    double   mlat, mlon;           /* az út közepe (Es-nél itt a "felhő") */
    double   dist;
    unsigned pair;                 /* adó+vevő hívójel lenyomata */
    int      es;                   /* 1 = sporadikus E (validate_is_es) */
} RecentSpot;

typedef struct {
    RecentSpot *s;
    int         n;
} SpotStore;

int  spots_init(SpotStore *st);                 /* 0 = rendben */
void spots_free(SpotStore *st);

/* Új spotok felvétele: kiértékeli őket a "most" modellel (ev[] kimenet, a
 * naplóhoz), a régieket kidobja. A modell idejét visszaállítja. */
void spots_add(SpotStore *st, IonoModel *m, const PropData *d,
               const PskSpot *in, int n, SpotEval *ev);

/* Az utóbbi SPOT_SHOW másodperc spotjainak száma */
int  spots_recent(const SpotStore *st, time_t now);

/* Volt-e valódi vétel a célterületen az adott sávon:
 * 0 = nem, 1 = csak gyenge (FT8-szintű), 2 = erős is (>= STRONG_SNR) */
int  spots_on_target(const SpotStore *st, double qlat, double qlon,
                     const Target *t, int band, time_t now);

/* Sporadikus E most: sávonként hány különböző állomáspár vette Es-sel
 * (utóbbi SPOT_SHOW). Nyitásnak SPOT_MINPAIR pártól számít. */
typedef struct {
    int pairs[NBANDS];
    int best_snr[NBANDS];
} EsNow;
void spots_es_now(const SpotStore *st, time_t now, EsNow *es);

/* Sporadikus E összesítve: ionoszonda-mérés (foEs) a közelben + valódi vételek */
#define ES_STATION_KM 1200.0       /* ennyin belüli ionoszondát nézünk */
#define ES_MAX_AGE    (90 * 60)    /* az Es gyorsan változik: csak friss mérés */
typedef struct {
    int    have_foes;
    double foes;                   /* a környék legnagyobb friss foEs-értéke, MHz */
    char   foes_name[64];          /* melyik ionoszonda */
    double foes_km;                /* milyen messze van */
    int    foes_age_min;           /* hány perce mérte */
    double es_muf;                 /* Es-MUF 2000 km-es úton, MHz (NaN, ha nincs foEs) */
    EsNow  live;                   /* valódi vételek */
} EsStatus;
void es_status(const PropData *d, const SpotStore *st, double qlat, double qlon,
               time_t now, EsStatus *es);

/* Spotok a térképre:
 *   sávos térképen: a távoli végek (fehér pont) + Es-felhők (lila kör)
 *   MUF/foF2 térképen: csak az Es-felhők, 10 m-től felfelé
 * scale: a pontok mérete (1 = asztali) */
void spots_draw(Canvas *c, const MapView *mv, const SpotStore *st, time_t now, double scale);

#endif
