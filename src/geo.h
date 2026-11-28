/*
 * geo.h - földrajzi segédfüggvények
 */
#ifndef IONOPROP_GEO_H
#define IONOPROP_GEO_H

/* Maidenhead lokátor (4, 6 vagy 8 karakter) -> négyzet közepe.
 * Visszatérés: 0 = rendben, -1 = hibás lokátor */
int locator_to_latlon(const char *loc, double *lat, double *lon);

/* Lat/lon -> 6 karakteres lokátor (buf legalább 7 bájt) */
void latlon_to_locator(double lat, double lon, char *buf);

/* Nagykör-távolság km-ben */
double distance_km(double lat1, double lon1, double lat2, double lon2);

/* Irányszög (azimut) fokban, 0 = észak, 90 = kelet */
double bearing_deg(double lat1, double lon1, double lat2, double lon2);

/* Pont a nagykörön 1 és 2 között, frac = 0..1 arányban */
void gc_point(double lat1, double lon1, double lat2, double lon2, double frac,
              double *lat, double *lon);

/* Ugyanez a hosszú úton (long path): a nagykör "másik" fele, frac 0..1 */
void gc_point_lp(double lat1, double lon1, double lat2, double lon2, double frac,
                 double *lat, double *lon);

/* Hosszúság normalizálása -180..+180 közé (a KC2G 0..360-at ad) */
double norm_lon(double lon);

#endif
