/*
 * loader.c - minden adatforrás letöltése és feldolgozása
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader.h"
#include "fetch.h"
#include "parse.h"

typedef int (*parse_fn)(const char *json, void *ctx);

static double g_qlat, g_qlon;

static int load_source(const char *title, const char *url, const char *fname,
                       int max_age, int force, parse_fn fn, void *ctx)
{
    char path[600];
    if (fetch_cached(url, fname, max_age, force, path, sizeof path) < 0) {
        fprintf(stderr, "%s: nincs adat.\n", title);
        return -1;
    }
    char *json = read_file(path, NULL);
    if (!json) {
        fprintf(stderr, "%s: nem tudom olvasni: %s\n", title, path);
        return -1;
    }
    int ok = fn(json, ctx);
    free(json);
    if (ok < 0) fprintf(stderr, "%s: nem sikerült feldolgozni.\n", title);
    return ok < 0 ? -1 : 0;
}

static int wrap_kc2g(const char *j, void *c)
{
    return parse_kc2g_stations(j, (PropData *)c, g_qlat, g_qlon);
}
static int wrap_f107(const char *j, void *c) { return parse_swpc_f107(j, (SolarData *)c); }
static int wrap_kp(const char *j, void *c)   { return parse_swpc_kp(j, (SolarData *)c); }
static int wrap_xray(const char *j, void *c) { return parse_swpc_xray(j, (SolarData *)c); }
static int wrap_kpf(const char *j, void *c)  { return parse_swpc_kp_forecast(j, (SolarData *)c); }

int load_all(PropData *d, double qlat, double qlon, int force)
{
    g_qlat = qlat;
    g_qlon = qlon;
    memset(d, 0, sizeof *d);

    if (fetch_init() != 0) {
        fprintf(stderr, "A letöltő indítása nem sikerült.\n");
        return 0;
    }
    d->have_stations = load_source("Ionoszondák", URL_KC2G_STATIONS, "kc2g_stations.json",
                                   CACHE_KC2G_SEC, force, wrap_kc2g, d) == 0;
    d->have_sfi  = load_source("SFI", URL_SWPC_F107, "swpc_f107.json",
                               CACHE_SWPC_SEC, force, wrap_f107, &d->solar) == 0;
    d->have_kp   = load_source("Kp", URL_SWPC_KP, "swpc_kp.json",
                               CACHE_SWPC_SEC, force, wrap_kp, &d->solar) == 0;
    d->have_xray = load_source("Röntgen", URL_SWPC_XRAY, "swpc_xray.json",
                               CACHE_SWPC_SEC, force, wrap_xray, &d->solar) == 0;
    d->have_kpf  = load_source("Kp-előrejelzés", URL_SWPC_KPF, "swpc_kpf.json",
                               CACHE_KPF_SEC, force, wrap_kpf, &d->solar) == 0;
    fetch_cleanup();

    return d->have_stations + d->have_sfi + d->have_kp + d->have_xray;
}
