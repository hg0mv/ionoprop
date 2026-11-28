/*
 * geo.c - földrajzi segédfüggvények
 */
#include <math.h>
#include <ctype.h>
#include <string.h>

#include "geo.h"

#define EARTH_R_KM 6371.0
#define DEG2RAD(x) ((x) * M_PI / 180.0)
#define RAD2DEG(x) ((x) * 180.0 / M_PI)

int locator_to_latlon(const char *loc, double *lat, double *lon)
{
    size_t n = strlen(loc);
    if (n != 4 && n != 6 && n != 8) return -1;

    int A = toupper((unsigned char)loc[0]) - 'A';
    int B = toupper((unsigned char)loc[1]) - 'A';
    if (A < 0 || A > 17 || B < 0 || B > 17) return -1;
    if (!isdigit((unsigned char)loc[2]) || !isdigit((unsigned char)loc[3])) return -1;

    double lo = -180.0 + A * 20.0 + (loc[2] - '0') * 2.0;
    double la =  -90.0 + B * 10.0 + (loc[3] - '0') * 1.0;
    double w = 2.0, h = 1.0;   /* aktuális négyzet mérete */

    if (n >= 6) {
        int c = tolower((unsigned char)loc[4]) - 'a';
        int d = tolower((unsigned char)loc[5]) - 'a';
        if (c < 0 || c > 23 || d < 0 || d > 23) return -1;
        w /= 24.0; h /= 24.0;
        lo += c * w;
        la += d * h;
    }
    if (n == 8) {
        if (!isdigit((unsigned char)loc[6]) || !isdigit((unsigned char)loc[7])) return -1;
        w /= 10.0; h /= 10.0;
        lo += (loc[6] - '0') * w;
        la += (loc[7] - '0') * h;
    }
    *lon = lo + w / 2.0;
    *lat = la + h / 2.0;
    return 0;
}

void latlon_to_locator(double lat, double lon, char *buf)
{
    lon += 180.0;
    lat += 90.0;
    if (lon < 0) lon = 0;
    if (lon >= 360) lon = 359.9999;
    if (lat < 0) lat = 0;
    if (lat >= 180) lat = 179.9999;

    buf[0] = (char)('A' + (int)(lon / 20));
    buf[1] = (char)('A' + (int)(lat / 10));
    buf[2] = (char)('0' + (int)fmod(lon / 2, 10));
    buf[3] = (char)('0' + (int)fmod(lat, 10));
    buf[4] = (char)('a' + (int)(fmod(lon, 2) * 12));
    buf[5] = (char)('a' + (int)(fmod(lat, 1) * 24));
    buf[6] = '\0';
}

double distance_km(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = DEG2RAD(lat1), p2 = DEG2RAD(lat2);
    double dp = p2 - p1, dl = DEG2RAD(lon2 - lon1);
    double a = sin(dp / 2) * sin(dp / 2) +
               cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 2.0 * EARTH_R_KM * atan2(sqrt(a), sqrt(1.0 - a));
}

double bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = DEG2RAD(lat1), p2 = DEG2RAD(lat2);
    double dl = DEG2RAD(lon2 - lon1);
    double y = sin(dl) * cos(p2);
    double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double b = RAD2DEG(atan2(y, x));
    return fmod(b + 360.0, 360.0);
}

double norm_lon(double lon)
{
    while (lon > 180.0)   lon -= 360.0;
    while (lon <= -180.0) lon += 360.0;
    return lon;
}

void gc_point(double lat1, double lon1, double lat2, double lon2, double frac,
              double *lat, double *lon)
{
    double p1 = DEG2RAD(lat1), l1 = DEG2RAD(lon1);
    double p2 = DEG2RAD(lat2), l2 = DEG2RAD(lon2);
    double d = distance_km(lat1, lon1, lat2, lon2) / EARTH_R_KM;  /* szögtávolság */
    if (d < 1e-9) { *lat = lat1; *lon = lon1; return; }

    double a = sin((1.0 - frac) * d) / sin(d);
    double b = sin(frac * d) / sin(d);
    double x = a * cos(p1) * cos(l1) + b * cos(p2) * cos(l2);
    double y = a * cos(p1) * sin(l1) + b * cos(p2) * sin(l2);
    double z = a * sin(p1) + b * sin(p2);
    *lat = RAD2DEG(atan2(z, sqrt(x * x + y * y)));
    *lon = RAD2DEG(atan2(y, x));
}

void gc_point_lp(double lat1, double lon1, double lat2, double lon2, double frac,
                 double *lat, double *lon)
{
    double p1 = DEG2RAD(lat1), l1 = DEG2RAD(lon1);
    double p2 = DEG2RAD(lat2), l2 = DEG2RAD(lon2);
    double A[3] = { cos(p1) * cos(l1), cos(p1) * sin(l1), sin(p1) };
    double B[3] = { cos(p2) * cos(l2), cos(p2) * sin(l2), sin(p2) };
    double d = distance_km(lat1, lon1, lat2, lon2) / EARTH_R_KM;
    double U[3];
    if (d < 1e-9 || fabs(M_PI - d) < 1e-9) {
        /* egybeeső vagy egymással átellenes pontok: bármelyik irány jó, legyen észak */
        double n[3] = { -sin(p1) * cos(l1), -sin(p1) * sin(l1), cos(p1) };
        for (int i = 0; i < 3; i++) U[i] = n[i];
    } else {
        /* U: A-ból B felé mutató egységvektor; a hosszú út az ellenkező irányba indul */
        for (int i = 0; i < 3; i++) U[i] = -(B[i] - A[i] * cos(d)) / sin(d);
    }
    double th = frac * (2.0 * M_PI - d);
    double P[3];
    for (int i = 0; i < 3; i++) P[i] = A[i] * cos(th) + U[i] * sin(th);
    *lat = RAD2DEG(atan2(P[2], sqrt(P[0] * P[0] + P[1] * P[1])));
    *lon = RAD2DEG(atan2(P[1], P[0]));
}
