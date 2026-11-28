/*
 * solar.h - a Nap helyzete és geomágneses szélesség
 */
#ifndef IONOPROP_SOLAR_H
#define IONOPROP_SOLAR_H

#include <time.h>

/* A szubszoláris pont (ahol a Nap a zenitben áll) adott UTC időben */
void sun_subsolar(time_t t, double *lat, double *lon);

/* A napzenit-szög koszinusza egy pontban: 1 = Nap a zenitben,
 * 0 = napnyugta/napkelte, negatív = éjszaka */
double sun_cos_zenith(double lat, double lon, time_t t);

/* Geomágneses szélesség (dipólus közelítés), fok */
double geomag_lat(double lat, double lon);

#endif
