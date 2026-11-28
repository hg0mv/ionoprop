/*
 * targets.h - saját célterületek: felvétel lokátorral, hívójellel vagy
 *             prefixszel, mentés a targets.conf fájlba
 */
#ifndef IONOPROP_TARGETS_H
#define IONOPROP_TARGETS_H

#include <stddef.h>
#include "predict.h"

#define TARGETS_MAX        40     /* összesen (beépített + saját + kijelölt) */
#define TARGETS_CUSTOM_MAX 10     /* ennyi saját célterület lehet */

/* Bevitel értelmezése: Maidenhead lokátor (pl. "PM95", "FN20xr"),
 * hívójel (pl. "JA1ABC") vagy prefix (pl. "VK", "ZS6", "PY").
 * Siker: 0, és out->name / lat / lon kitöltve. Hiba: -1, err-ben az ok. */
int target_resolve(const char *input, Target *out, char *err, size_t errlen);

/* Mentett saját célterületek (a beállítások mellett: targets.conf).
 * load: a beolvasott darabszám; save: 0 = rendben */
int targets_load_custom(Target *t, int max);
int targets_save_custom(const Target *t, int n);

/* Prefix-táblázat (kb. 130 DXCC-terület): a leghosszabb illeszkedő prefix.
 * Visszatérés: 1 = megvan, 0 = nincs */
int prefix_lookup(const char *call, char *name, size_t namelen, double *lat, double *lon);

#endif
