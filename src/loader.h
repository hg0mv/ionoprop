/*
 * loader.h - minden adatforrás letöltése és feldolgozása egy lépésben
 */
#ifndef IONOPROP_LOADER_H
#define IONOPROP_LOADER_H

#include "data.h"

/* Letölt (vagy a gyorsítótárból betölt) minden forrást a 'd'-be.
 * qlat/qlon: a QTH, az ionoszondák ehhez képest lesznek rendezve.
 * force != 0: letöltés akkor is, ha friss adat van.
 * Visszatérés: hány forrás sikerült (0..4). */
int load_all(PropData *d, double qlat, double qlon, int force);

#endif
