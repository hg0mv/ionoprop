/*
 * validate.h - az előrejelzés ellenőrzése valódi PSK Reporter spotokkal
 *
 * Minden spotnál kiszámoljuk, mit jósolt volna a program arra az útra,
 * frekvenciára és időre. A naplóba a paraméterektől független nyers
 * értékek kerülnek (MUF-arány, elnyelés), így a napló később más
 * paraméterekkel is újraértékelhető.
 *
 * Fontos korlát: a spotok csak azt mutatják, ami MŰKÖDÖTT. Abból, hogy
 * egy sávon nincs spot, nem tudjuk, hogy zárva volt (lehet, hogy csak
 * senki sem adott). Ezért a hangolás csak "engedni" tud a határokon,
 * szigorítani nem.
 */
#ifndef IONOPROP_VALIDATE_H
#define IONOPROP_VALIDATE_H

#include <stdio.h>
#include <time.h>
#include "data.h"
#include "model.h"
#include "predict.h"
#include "pskr.h"

#define STRONG_SNR (-10)   /* e fölött "erős" jel (SSB-vel is használható lenne) */

typedef struct {
    time_t t;
    int    band;
    double f;          /* MHz */
    int    snr;
    char   mode[12];
    double dist;       /* km */
    double ratio_raw;  /* f / MUF (szorzó nélkül) */
    double absorb;     /* D-réteg elnyelés, dB */
    int    polar_pen;
    int    flare;
    double conf;       /* mennyire mérésen alapult az előrejelzés */
} SpotEval;

/* Egy spot kiértékelése. A modell idejét a spot idejére állítja, majd vissza. */
void validate_eval(IonoModel *m, const PropData *d, const PskSpot *s, SpotEval *e);

/* Sporadikus E (vagy más, nem F2) terjedés valószínű: 20 m-en vagy feljebb,
 * 2300 km-nél rövidebb úton, jóval az F2 MUF fölött. 1 = igen */
int validate_is_es(const SpotEval *e);

/* A spot értékelése a mostani paraméterekkel (0..3) */
int validate_grade(const SpotEval *e);

/* ---- napló: ~/.cache/ionoprop/spots_eval.csv ---- */
int validate_log_append(const SpotEval *e, int n);
/* Betöltés 'since' óta. *out-ot a hívó szabadítja fel. Visszatérés: darab, -1 hiba */
int validate_log_load(time_t since, SpotEval **out);
/* Ha a napló túl nagy, csak az utolsó 'days' napot tartja meg */
void validate_log_trim(int days);

/* ---- statisztika ---- */
typedef struct {
    int    n, n_strong, n_ft8;
    int    cnt[NBANDS][4];       /* sávonként: hány spot esett az egyes értékelésekbe */
    double snr[NBANDS][4];       /* SNR összeg (átlaghoz) */
    int    tot[4];
    double tot_snr[4];
    int    strong_tot[4];
    int    zero_muf, zero_abs, zero_other;   /* a "zárva" jóslatok oka */
    int    n_es;                 /* rövid úton jóval a MUF fölött: nem F2 (Es, szórás...) */
    int    n_ground;             /* 100 km alatti (földi hullám) */
    int    n_long;               /* 2000 km-nél hosszabb utak (ezekből hangolunk MUF-ot) */
    PredictParams suggest;
    int    have_suggest;
    int    for_app;              /* 1 = az app ablakába (nincs parancssori tipp) */
    time_t t_first, t_last;
} ValReport;

void validate_report(const SpotEval *e, int n, ValReport *r);
void validate_print(const ValReport *r, FILE *out);

#endif
