/*
 * data.h - IonoProp közös adatszerkezetek
 *
 * Itt vannak azok a struktúrák, amelyekbe a letöltött adatok kerülnek.
 * A későbbi modulok (számítás, grafikus felület) ezeket fogják használni.
 */
#ifndef IONOPROP_DATA_H
#define IONOPROP_DATA_H

#include <time.h>
#include <math.h>

#define IONO_MAX_STATIONS 256

/* Hiányzó érték jelölése (pl. ha az ionoszonda nem mérte).
 * Vizsgálat: isnan(x) */
#define IONO_NAN NAN

/* Egy ionoszonda legutóbbi mérése (KC2G stations.json) */
typedef struct {
    char   code[16];     /* URSI kód, pl. "PQ052" (Průhonice) */
    char   name[64];     /* állomás neve */
    double lat;          /* földrajzi szélesség, fok */
    double lon;          /* földrajzi hosszúság, fok (-180..+180) */
    time_t time;         /* mérés ideje (UTC) */
    double fof2;         /* F2 réteg kritikus frekvenciája, MHz */
    double mufd;         /* MUF(3000) F2, MHz */
    double hmf2;         /* F2 réteg csúcsmagassága, km */
    double md;           /* M(3000) tényező = MUF/foF2 (ha van) */
    double foes;         /* sporadikus E kritikus frekvencia, MHz (NaN, ha nincs) */
    int    cs;           /* megbízhatóság 0..100, -1 = kézzel kiértékelt */
    double dist_km;      /* távolság a saját QTH-tól (számolt) */
} IonoStation;

/* Napaktivitás és geomágneses adatok (NOAA SWPC) */
typedef struct {
    /* 10,7 cm-es napfluxus (SFI) */
    double sfi;              /* legutóbbi mért érték, sfu */
    double sfi_90day;        /* 90 napos átlag */
    time_t sfi_time;

    /* Planetáris K-index (1 perces becslés) */
    double kp;
    time_t kp_time;

    /* GOES röntgenfluxus, 0.1-0.8 nm */
    double xray_flux;        /* W/m^2 */
    char   xray_class[8];    /* pl. "C2.4" */
    time_t xray_time;

    /* Kp-előrejelzés (NOAA, 3 órás szakaszok, kb. 3 nap) */
#define KPF_MAX 96
    time_t kpf_t[KPF_MAX];   /* a szakasz kezdete (UTC) */
    double kpf[KPF_MAX];
    int    nkpf;
} SolarData;

typedef struct {
    IonoStation st[IONO_MAX_STATIONS];
    int         count;
    SolarData   solar;
    int         have_stations; /* 1, ha sikerült betölteni */
    int         have_sfi, have_kp, have_xray, have_kpf;
} PropData;

#endif
