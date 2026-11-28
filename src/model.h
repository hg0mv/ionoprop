/*
 * model.h - ionoszféra-modell: rétegparaméterek a Föld bármely pontjára
 *
 * Módszer (hasonló a KC2G-hez, de sokkal egyszerűbb):
 *  1. Alapmodell: foF2 a napfluxusból (SFI) és a Nap állásából.
 *  2. Minden friss ionoszondánál kiszámoljuk: mért / modell arány.
 *  3. Egy tetszőleges pontban ezeket az arányokat a közeli ionoszondákból
 *     súlyozva átlagoljuk (minél közelebb, annál nagyobb súly), és ezzel
 *     javítjuk az alapmodellt. Ahol nincs közeli ionoszonda, ott az
 *     alapmodell marad (kisebb pontossággal).
 */
#ifndef IONOPROP_MODEL_H
#define IONOPROP_MODEL_H

#include <time.h>
#include "data.h"

#define MODEL_MAX_REF 64

/* Rétegparaméterek egy pontban */
typedef struct {
    double fof2;      /* F2 kritikus frekvencia, MHz */
    double md;        /* M(3000)F2 tényező */
    double mufd;      /* MUF(3000) = fof2 * md, MHz */
    double hmf2;      /* F2 csúcsmagasság, km */
    double foe;       /* E réteg kritikus frekvencia, MHz */
    double cosx;      /* napzenit koszinusza (nappal > 0) */
    double near_km;   /* legközelebbi felhasznált ionoszonda távolsága */
    double conf;      /* mennyire támaszkodik mérésre: 0 = csak modell, 1 = mérés */
} IonoPoint;

/* Egy felhasznált ionoszonda-mérés */
typedef struct {
    double lat, lon;
    double r_f2;      /* ln(mért foF2 / modell foF2) */
    double md, hmf2;  /* NaN, ha nincs */
    double w;         /* megbízhatósági súly 0..1 */
    char   code[16];
} ModelRef;

typedef struct {
    ModelRef ref[MODEL_MAX_REF];
    int      nref;
    double   prior_r; /* általános korrekció ott, ahol nincs mérés */
    double   sfi;
    double   ssn;     /* napfoltszám (SFI-ből becsülve) */
    time_t   now;     /* az adatok ideje (most) */
    time_t   t;       /* kiértékelési idő (előrejelzésnél a jövő) */
    double   decay;   /* mennyire érvényes még a mért korrekció t-ben: 1..0 */
} IonoModel;

/* A modell felépítése a letöltött adatokból.
 * max_age_s: ennél régebbi mérést nem használ; min_cs: minimális megbízhatóság */
void model_init(IonoModel *m, const PropData *d, time_t now, int max_age_s, int min_cs);

/* Előrejelzés: a modell kiértékelési idejének beállítása.
 * Minél messzebb van t a mérésektől, annál inkább az alapmodell számít
 * (a mért eltérés kb. 5 óra alatt "felejtődik el"). */
void model_set_time(IonoModel *m, time_t t);

/* Rétegparaméterek egy pontban (m->t időpontra) */
void model_point(const IonoModel *m, double lat, double lon, IonoPoint *out);

/* Alapmodell foF2 (korrekció nélkül), adott helyen és időben */
double model_base_fof2(double lat, double lon, time_t t, double sfi);

/* E réteg kritikus frekvenciája (CCIR képlet) */
double model_foe(double cosx, double ssn);

#endif
