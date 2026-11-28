/*
 * parse.h - a letöltött JSON adatok feldolgozása
 */
#ifndef IONOPROP_PARSE_H
#define IONOPROP_PARSE_H

#include <time.h>
#include "data.h"

/* "2026-10-31T17:15:00", "2026-10-31 17:15:00Z", "...00.000Z" -> UTC time_t
 * Hiba esetén 0. */
time_t parse_iso_time(const char *s);

/* KC2G stations.json -> pd->st[], távolság szerint rendezve a QTH-tól.
 * Visszatérés: beolvasott állomások száma, vagy -1 hiba esetén. */
int parse_kc2g_stations(const char *json, PropData *pd, double qth_lat, double qth_lon);

/* NOAA SWPC fájlok -> pd->solar. Visszatérés: 0 = rendben, -1 = hiba. */
int parse_swpc_f107(const char *json, SolarData *sd);
int parse_swpc_kp(const char *json, SolarData *sd);
/* Kp-előrejelzés (3 órás szakaszok) a sd->kpf tömbökbe */
int parse_swpc_kp_forecast(const char *json, SolarData *sd);
int parse_swpc_xray(const char *json, SolarData *sd);

/* Röntgenfluxus (W/m^2) -> fler-osztály, pl. 2.4e-6 -> "C2.4" */
void xray_flux_class(double flux, char *buf, size_t len);

#endif
