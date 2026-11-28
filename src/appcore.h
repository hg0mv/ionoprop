/*
 * appcore.h - felülettől független app-mag (az Android-app ezt használja JNI-n át)
 *
 * Minden számítás itt történik; a felület csak:
 *   - letölti a JSON-fájlokat és átadja (ac_load_json),
 *   - lekéri az eredményeket JSON-ban (ac_state_json),
 *   - lekéri a térképet egy RGBA pufferbe (ac_render_map).
 *
 * A függvényeket egyetlen szálról kell hívni (az Android-appban egy
 * háttérszálról). A PSK Reporter-gyűjtő saját szálon fut, de az ac_psk_poll
 * hívásig nem nyúl az app-mag állapotához.
 */
#ifndef IONOPROP_APPCORE_H
#define IONOPROP_APPCORE_H

#include <stdint.h>

typedef struct AppCore AppCore;

/* cache_dir / config_dir: ide kerül a napló és a beállítások (Androidon az
 * app saját könyvtárai). NULL esetén az alapértelmezett ~/.cache, ~/.config. */
AppCore *ac_new(const char *cache_dir, const char *config_dir);
void     ac_free(AppCore *a);

/* Saját QTH. Visszatérés: 0 rendben, -1 hibás lokátor. A lokátort elmenti. */
int  ac_set_locator(AppCore *a, const char *loc);
const char *ac_locator(const AppCore *a);
int  ac_first_start(const AppCore *a);        /* 1 = még nincs mentett lokátor */

/* Letöltött adatok átadása (bármelyik lehet NULL = nincs).
 * Visszatérés: bitmaszk, mi töltődött be: 1 ionoszondák, 2 SFI, 4 Kp, 8 röntgen */
int  ac_load_json(AppCore *a, const char *stations, const char *f107,
                  const char *kp, const char *xray);

/* NOAA Kp-előrejelzés (noaa-planetary-k-index-forecast.json). 1 = sikerült */
int  ac_load_kp_forecast(AppCore *a, const char *json);

/* Időeltolás órában (0..23) és a kijelölt cél */
void ac_set_time_offset(AppCore *a, int hours);
void ac_select_target(AppCore *a, int idx);
/* Cél a térképre koppintva: (x, y) egy w x h méretű térképen */
int  ac_target_from_map(AppCore *a, int w, int h, int x, int y);

/* Üzemmód (0 SSB, 1 CW, 2 FT8) - elmenti; utána ac_recompute kell */
void ac_set_mode(AppCore *a, int mode);

/* Saját célterület lokátorral, hívójellel vagy prefixszel. Visszatérés: az
 * új cél sorszáma (ki is jelöli), vagy -1 (az ok: ac_last_error) */
int  ac_add_target(AppCore *a, const char *input);
/* A térképen kijelölt pont mentése saját célterületként */
int  ac_save_picked(AppCore *a);
/* Saját célterület törlése. 0 = rendben */
int  ac_remove_target(AppCore *a, int idx);
/* Hosszú út ki/be a célnál. Visszatérés: az új állapot (0/1), -1 hiba */
int  ac_toggle_lp(AppCore *a, int idx);
const char *ac_last_error(const AppCore *a);

/* Nyelv: 0 = magyar, 1 = angol (elmenti). Az első indításkor a
 * beállított, vagy ha nincs, a lang_hint (a telefon nyelve) számít. */
void ac_set_lang(AppCore *a, int lang);
/* km / mérföld (settings.conf "units"), és a magyarázatok szövege */
void ac_set_units(AppCore *a, int mi);
const char *ac_help_text(void);

/* Riasztás sávnyitáskor (alerts.h). ac_check_alerts: az ac_recompute
 * után hívandó; JSON {"enabled":bool,"new":[{"key","text"},...]},
 * a hívó szabadítja fel free()-vel. */
void  ac_set_alerts(AppCore *a, int on);
char *ac_check_alerts(AppCore *a);

/* Újraszámolás (modell, célok, 24 órás előrejelzés) */
void ac_recompute(AppCore *a);

/* Minden eredmény egy JSON-szövegben (a hívó szabadítja fel free()-vel) */
char *ac_state_json(AppCore *a);

/* Térkép kirajzolása. mode: -2 MUF, -1 foF2, 0..9 sáv lefedettség.
 * out: w*h pixel, stride: sorhossz pixelben.
 * argb = 1: 0xAARRGGBB egészek (Java int[], Bitmap.setPixels)
 * argb = 0: Android ARGB_8888 bitmap-memória (R,G,B,A bájtok)
 * Visszatérés: 0 rendben */
int  ac_render_map(AppCore *a, uint32_t *out, int w, int h, int stride, int mode,
                   int show_spots, int argb);

/* PSK Reporter (valódi vételek) */
int  ac_psk_start(AppCore *a, const char *host, int port);
void ac_psk_stop(AppCore *a);
int  ac_psk_poll(AppCore *a);                 /* új spotok száma */

/* Ellenőrzési jelentés a naplóból (days napra). A hívó szabadítja fel. */
char *ac_report_text(AppCore *a, int days);
/* A jelentés javaslatának mentése. 0 = mentve, 1 = kevés adat, -1 = hiba */
int  ac_save_suggestion(AppCore *a, int days);

#endif
