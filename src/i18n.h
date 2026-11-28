/*
 * i18n.h - kétnyelvű felület (magyar / angol)
 *
 * A szövegek a helyükön, párban szerepelnek: TR("Javaslat", "Suggestion").
 * A nyelv a settings.conf "lang" kulcsából jön; ha nincs, a rendszeréből
 * (magyar rendszeren magyar, máshol angol).
 */
#ifndef IONOPROP_I18N_H
#define IONOPROP_I18N_H

enum { LANG_HU = 0, LANG_EN = 1 };
extern int g_lang;

#define TR(hu, en) (g_lang == LANG_EN ? (en) : (hu))

/* "hu" / "en" -> LANG_*, ismeretlen: -1 */
int lang_from_name(const char *s);
const char *lang_name(int lang);

/* a rendszer nyelve (LANG / Windows felhasználói nyelv) */
int lang_system(void);

/* beállítás betöltése: settings.conf "lang", különben a rendszeré */
void lang_load(void);

/* Távolság: km vagy mérföld (settings.conf "units" = km / mi).
 * A g_units_mi a predict.c-ben van, mint a g_lang. */
extern int g_units_mi;
void units_load(void);
void units_set(int mi);              /* és mentés */
double dist_out(double km);          /* km -> a választott egység */
const char *dist_unit(void);         /* "km" vagy "mi" */

/* Magyarázat a kijelzett értékekhez (SFI, Kp, MUF, ...), az aktuális
 * nyelven. Sorok '\n'-nel, a sor elején a fogalom, utána " - " és a leírás. */
const char *help_text(void);

#endif
