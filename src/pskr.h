/*
 * pskr.h - PSK Reporter MQTT-gyűjtő
 *
 * A QTH körüli lokátornégyzetekre iratkozik fel (adóként és vevőként is),
 * így minden olyan spot beérkezik, amelynek egyik vége a közelben van.
 * A háttérben fut (libmosquitto saját szála), a program pskr_poll()-lal
 * veszi át az új spotokat.
 *
 * Fordítás libmosquitto nélkül: -DIONOPROP_NO_PSK (ekkor a gyűjtő nem
 * indul el, de a pskr_parse() használható).
 */
#ifndef IONOPROP_PSKR_H
#define IONOPROP_PSKR_H

#include <time.h>

#define PSKR_HOST "mqtt.pskreporter.info"
#define PSKR_PORT 1883

typedef struct {
    time_t t;
    double f_mhz;
    int    band;          /* index a BANDS[] tömbben, -1 ha nem HF amatőrsáv */
    int    snr;           /* dB */
    char   mode[12];
    char   sc[16], rc[16];      /* adó, vevő hívójele */
    double slat, slon, rlat, rlon;
    /* a pskr_classify() tölti ki: */
    double nlat, nlon;    /* a QTH-hoz közeli vég */
    double flat, flon;    /* a távoli vég */
    double dist_km;       /* a két vég távolsága */
    int    local_tx;      /* 1 = a közeli vég az adó */
} PskSpot;

/* Egy MQTT üzenet (JSON) feldolgozása. sq: sorszám (duplikátumszűréshez).
 * Visszatérés: 0 = rendben, -1 = hibás vagy nem HF amatőrsáv */
int pskr_parse(const char *json, PskSpot *s, long long *sq);

/* Frekvencia (MHz) -> sáv index, -1 ha nem a 10 HF sáv egyike */
int pskr_band(double f_mhz);

/* Ennél rövidebb úton a földi hullám a jellemző, nem az ionoszféra */
#define PSKR_MIN_KM 100.0

/* Közeli/távoli vég meghatározása. Visszatérés: 0 = a spot releváns
 * (legalább az egyik vége radius_km-en belül van a QTH-tól, és az út
 * legalább PSKR_MIN_KM hosszú), -1 = nem */
int pskr_classify(PskSpot *s, double qlat, double qlon, double radius_km);

/* ---- élő gyűjtés ---- */
typedef struct Pskr Pskr;

/* Indítás. host NULL = PSKR_HOST. Visszatérés: NULL hiba esetén. */
Pskr *pskr_start(const char *host, int port, double qlat, double qlon, double radius_km);

/* Az azóta beérkezett spotok átvétele (max darab). Visszatérés: darabszám */
int pskr_poll(Pskr *p, PskSpot *out, int max);

/* Állapot szövegesen (kapcsolat, darabszám) */
void pskr_status(Pskr *p, char *buf, int len);
int  pskr_connected(Pskr *p);

void pskr_stop(Pskr *p);

#endif
