/*
 * predict.h - útvonal-számítás és sávjavaslat
 */
#ifndef IONOPROP_PREDICT_H
#define IONOPROP_PREDICT_H

#include "data.h"
#include "model.h"

#define NBANDS 13

/* sávok sorszáma a BANDS[] tömbben */
enum { B160, B80, B60, B40, B30, B20, B17, B15, B12, B11, B10, B8, B6 };

typedef struct {
    const char *name;   /* pl. "20m" */
    double      f;      /* jellemző frekvencia, MHz */
} Band;

extern const Band BANDS[NBANDS];

/* Célterület */
typedef struct {
    char   name[32];
    double lat, lon;
    int    nvis;        /* 1 = közeli (0-300 km) összeköttetés a saját környéken */
    int    lp;          /* 1 = hosszú út (long path): a nagykör másik fele */
} Target;

/* Pont a QTH és a cél közötti úton (frac 0..1), a rövid vagy a hosszú úton */
void target_path_point(double qlat, double qlon, const Target *t, double frac,
                       double *lat, double *lon);

/* Értékelés: 0 = zárva, 1 = gyenge/esetleges, 2 = közepes, 3 = jó */
typedef struct {
    double dist_km, bearing;
    int    hops;          /* F2 ugrások száma */
    double hop_km;        /* egy ugrás hossza */
    double muf;           /* az útvonal legnagyobb használható frekvenciája, MHz */
    double muf_f2, muf_e; /* F2 és E réteg szerinti MUF */
    double fot;           /* optimális frekvencia (0,85 * MUF) */
    double luf;           /* legkisebb használható frekvencia (elnyelés miatt) */
    int    polar;         /* 1 = a sarki fény övezetén megy át (zavart) */
    int    flare;         /* 1 = napkitörés miatti elnyelés */
    double conf;          /* mennyire mérésen alapul: 0..1 */
    double absorb[NBANDS];/* D-réteg elnyelés, dB */
    int    grade[NBANDS];

    /* a tetszőleges frekvencián való értékeléshez (predict_at_freq) */
    double muf_raw;       /* MUF a hangoló szorzó nélkül */
    int    polar_pen;     /* sarki zavar miatti levonás (0..2) */
    double kp;            /* a számításhoz használt Kp (NaN, ha nincs) */
    double haf_eff;       /* e frekvencia alatt napkitörés miatt zárva (0 = nincs) */
    int    nh;            /* ugrások száma az elnyeléshez (max. 8) */
    double ihop[8];       /* elnyelési tényező ugrásonként */
} PathResult;

/* Hangolható paraméterek (a PSK Reporter-es ellenőrzéssel állíthatók be) */
typedef struct {
    double muf_scale;     /* a számolt MUF szorzója (alap 1,0) */
    double abs_fair;      /* elnyelés (dB) e fölött egy fokozattal rosszabb */
    double abs_poor;      /* e fölött két fokozattal rosszabb (és ez a LUF határa) */
    double abs_closed;    /* e fölött zárva */
} PredictParams;

extern PredictParams g_params;
void predict_params_default(PredictParams *p);

/* Üzemmód: a gyengébb jelekkel is működő üzemmódokkal (CW, FT8) több
 * elnyelés és a MUF-ot kissé meghaladó frekvencia is belefér. */
enum { MODE_SSB = 0, MODE_CW, MODE_FT8, NMODES };
extern int g_mode;                          /* a választott üzemmód (alap: SSB) */
extern const char *const MODE_NAMES[NMODES];
double mode_abs_offset(int mode);           /* dB */
int    mode_from_name(const char *s);       /* "FT8" -> MODE_FT8, -1 ha ismeretlen */

/* Értékelés 0..3 a nyers MUF-arányból (f / muf_raw), az elnyelésből (dB)
 * és a zavarokból - a g_params és a g_mode alapján. */
int predict_grade(double ratio_raw, double absorb_db, int polar_pen, int flare);
int predict_grade_mode(double ratio_raw, double absorb_db, int polar_pen, int flare, int mode);

/* Egy már kiszámolt útvonal értékelése tetszőleges frekvencián (MHz).
 * Kimenet: nyers MUF-arány és elnyelés; visszatérés: értékelés 0..3 */
int predict_at_freq(const PathResult *r, double f_mhz, double *ratio_raw, double *absorb_db);

/* 24 órás előrejelzés egy célra, óránként */
#define FC_HOURS 24
typedef struct {
    time_t t0;                      /* az első óra kezdete */
    int    grade[FC_HOURS][NBANDS];
    double muf[FC_HOURS], luf[FC_HOURS];
    double kp[FC_HOURS];            /* Kp (a mért, vagy a NOAA-előrejelzés) */
} Forecast;

/* Sporadikus E: egy ugrás MUF-ja d_km-es úton a foEs-ből (az Es-felhő kb.
 * 105 km magasan van, egy ugrás legfeljebb kb. 2300 km). NaN, ha nincs foEs. */
#define ES_HEIGHT     105.0
#define ES_MAX_HOP_KM 2300.0
double es_muf(double foes, double d_km);

/* Kp a t időpontra: 1,5 órán belül a mért érték, utána a NOAA-előrejelzés
 * (ha van). NaN, ha egyik sincs. */
double kp_at(const PropData *d, time_t now, time_t t);

/* A modell idejét óránként állítja, a végén visszaállítja */
void predict_forecast(IonoModel *m, const PropData *d, double qlat, double qlon,
                      const Target *t, time_t t0, Forecast *fc);

/* Napkelte és napnyugta (UTC) az adott ponton, a t időpont helyi napján
 * (0,833°-os, azaz a látszó napkorong felső széle szerint).
 * Visszatérés: 0 = van mindkettő, 1 = egész nap fenn a Nap, -1 = egész nap lent */
int sun_rise_set(double lat, double lon, time_t t, time_t *rise, time_t *set);

/* Beépített célterületek listája */
int predict_default_targets(Target *t, int max);

/* Egy útvonal kiszámítása a saját QTH-tól a célig */
void predict_path(const IonoModel *m, const PropData *d,
                  double qlat, double qlon, const Target *t, PathResult *r);

/* Geometria (a tesztekhez is): beesési szög szekánsa egy ugrásnál */
double hop_sec(double hop_km, double h_km);
/* Legnagyobb ugráshossz adott magasságnál, min. 3 fokos kisugárzási szöggel */
double hop_max_km(double h_km);
/* MUF egy ugrásra a vezérlőpont adataiból */
double hop_muf(double hop_km, double fof2, double md, double h_km);

#endif
