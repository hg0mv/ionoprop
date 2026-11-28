/*
 * config.h - hangolt paraméterek mentése / betöltése
 *
 * Fájl: ~/.config/ionoprop/params.conf (egyszerű kulcs = érték sorok)
 */
#ifndef IONOPROP_CONFIG_H
#define IONOPROP_CONFIG_H

#include <stddef.h>
#include "predict.h"

/* A beállítófájl útvonala (a könyvtárat létrehozza, ha kell) */
int config_path(char *buf, size_t len);

/* Betöltés a g_params-ba. Visszatérés: 1 = betöltve, 0 = nincs fájl, -1 = hiba */
int config_load(PredictParams *p);

/* Mentés. Visszatérés: 0 = rendben */
int config_save(const PredictParams *p, const char *comment);

/* Saját QTH lokátor: ~/.config/ionoprop/settings.conf
 * load: 1 = van mentett, 0 = nincs; save: 0 = rendben */
int config_load_locator(char *buf, size_t len);
int config_save_locator(const char *loc);

/* Általános beállítás a settings.conf-ban ("kulcs = érték").
 * get: 1 = megvan, 0 = nincs; set: 0 = rendben (value NULL = törlés) */
int config_get(const char *key, char *buf, size_t len);
int config_set(const char *key, const char *value);

#endif
