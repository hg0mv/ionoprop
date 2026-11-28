/*
 * mapview.h - világtérkép: MUF/foF2 színtérkép, sávonkénti lefedettség,
 *             éjszakai árnyék, ionoszondák, útvonal
 */
#ifndef IONOPROP_MAPVIEW_H
#define IONOPROP_MAPVIEW_H

#include <time.h>
#include <stdint.h>
#include "canvas.h"
#include "data.h"
#include "model.h"
#include "predict.h"

#define MAP_LAT_TOP   80.0
#define MAP_LAT_BOT  -60.0
#define GRID_STEP     3.0
#define GRID_NX     120          /* 360 / 3 */
#define GRID_NY      47          /* (80 + 60) / 3, felfelé kerekítve */

/* Térkép módok: MAP_MUF, MAP_FOF2, vagy 0..NBANDS-1 = adott sáv lefedettsége */
enum { MAP_MUF = -2, MAP_FOF2 = -1 };

typedef struct {
    int    x, y, w, h;                     /* helye a vásznon */
    int    mode;
    int    no_legend;                      /* 1 = a jelmagyarázatot a hívó rajzolja */
    float  muf[GRID_NY][GRID_NX];          /* MUF(3000), MHz */
    float  fof2[GRID_NY][GRID_NX];
    float  cosx[GRID_NY][GRID_NX];         /* napzenit koszinusza */
    uint8_t grade[GRID_NY][GRID_NX][NBANDS]; /* QTH -> pont útvonal értékelése */
    time_t layer_t, cover_t;               /* mikorra számoltuk */
    int    cover_valid;
} MapView;

/* Rácsértékek számítása a modell aktuális idejére (m->t).
 * with_coverage: a sávonkénti lefedettséget is (lassabb, kb. 5600 útvonal). */
void map_compute(MapView *mv, const IonoModel *m, const PropData *d,
                 double qlat, double qlon, int with_coverage);

/* Térkép kirajzolása. sel: kijelölt cél (NULL = nincs) */
void map_draw(Canvas *c, const MapView *mv, const PropData *d, const IonoModel *m,
              double qlat, double qlon, const Target *sel);

/* Átváltás képernyő és földrajzi koordináták között */
void map_to_screen(const MapView *mv, double lat, double lon, double *x, double *y);
int  screen_to_map(const MapView *mv, int x, int y, double *lat, double *lon);

/* Színskálák */
Color muf_color(double mhz);
Color grade_color(int g);

#endif
