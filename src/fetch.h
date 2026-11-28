/*
 * fetch.h - adatok letöltése HTTP-n (libcurl), helyi gyorsítótárral
 */
#ifndef IONOPROP_FETCH_H
#define IONOPROP_FETCH_H

/* Letöltési források */
#define URL_KC2G_STATIONS "https://prop.kc2g.com/api/stations.json"
#define URL_SWPC_F107     "https://services.swpc.noaa.gov/json/f107_cm_flux.json"
#define URL_SWPC_KP       "https://services.swpc.noaa.gov/json/planetary_k_index_1m.json"
#define URL_SWPC_XRAY     "https://services.swpc.noaa.gov/json/goes/primary/xrays-6-hour.json"
#define URL_SWPC_KPF      "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json"

/* Mennyi ideig érvényes egy letöltött fájl (másodperc).
 * A KC2G 15 percenként frissít, és kéri, hogy ne terheljük a szerverét. */
#define CACHE_KC2G_SEC  (15 * 60)
#define CACHE_SWPC_SEC  (5 * 60)
#define CACHE_KPF_SEC   (60 * 60)

/* Globális inicializálás / lezárás (egyszer a program elején/végén) */
int  fetch_init(void);
void fetch_cleanup(void);

/* A gyorsítótár könyvtára: ~/.cache/ionoprop (létrehozza, ha kell).
 * Visszaadja a teljes útvonalat a 'buf'-ba. */
int cache_dir(char *buf, size_t len);

/*
 * Letölt egy URL-t a gyorsítótárba 'fname' néven, ha a meglévő fájl
 * régebbi, mint 'max_age' másodperc (vagy 'force' != 0).
 * Ha a letöltés nem sikerül, de van régi fájl, azt használja.
 *
 * out_path: ide kerül a fájl teljes útvonala
 * Visszatérés: 0 = friss letöltés, 1 = gyorsítótárból, -1 = hiba
 */
int fetch_cached(const char *url, const char *fname, int max_age, int force,
                 char *out_path, size_t out_len);

/* Fájl beolvasása memóriába (a hívó szabadítja fel free()-vel) */
char *read_file(const char *path, size_t *len_out);

#endif
