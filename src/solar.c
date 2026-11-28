/*
 * solar.c - a Nap helyzete és geomágneses szélesség
 *
 * Egyszerűsített csillagászati képletek (pontosság kb. 0,1-0,5 fok),
 * ionoszféra-számításhoz bőven elég.
 */
#include <math.h>
#include "solar.h"
#include "geo.h"

#define D2R(x) ((x) * M_PI / 180.0)
#define R2D(x) ((x) * 180.0 / M_PI)

/* A geomágneses északi pólus (dipólus, IGRF 2025 körül) */
#define GM_POLE_LAT 80.8
#define GM_POLE_LON (-72.6)

void sun_subsolar(time_t t, double *lat, double *lon)
{
    double n = (double)t / 86400.0 + 2440587.5 - 2451545.0;  /* napok J2000 óta */

    double L = fmod(280.460 + 0.9856474 * n, 360.0);          /* közepes hossz */
    double g = D2R(fmod(357.528 + 0.9856003 * n, 360.0));     /* közepes anomália */
    double lambda = D2R(L + 1.915 * sin(g) + 0.020 * sin(2 * g));
    double eps = D2R(23.439 - 0.0000004 * n);                 /* ekliptika ferdesége */

    double ra  = atan2(cos(eps) * sin(lambda), cos(lambda));  /* rektaszcenzió */
    double dec = asin(sin(eps) * sin(lambda));                /* deklináció */

    double gmst = fmod(18.697374558 + 24.06570982441908 * n, 24.0);  /* óra */

    *lat = R2D(dec);
    *lon = norm_lon(R2D(ra) - gmst * 15.0);
}

double sun_cos_zenith(double lat, double lon, time_t t)
{
    double slat, slon;
    sun_subsolar(t, &slat, &slon);
    return sin(D2R(lat)) * sin(D2R(slat)) +
           cos(D2R(lat)) * cos(D2R(slat)) * cos(D2R(lon - slon));
}

double geomag_lat(double lat, double lon)
{
    double s = sin(D2R(lat)) * sin(D2R(GM_POLE_LAT)) +
               cos(D2R(lat)) * cos(D2R(GM_POLE_LAT)) * cos(D2R(lon - GM_POLE_LON));
    return R2D(asin(s));
}
