/*
 * appcore.c - felülettől független app-mag (lásd appcore.h)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

#include "../third_party/cJSON.h"
#include "appcore.h"
#include "data.h"
#include "parse.h"
#include "geo.h"
#include "model.h"
#include "predict.h"
#include "mapview.h"
#include "canvas.h"
#include "config.h"
#include "pskr.h"
#include "validate.h"
#include "spots.h"
#include "targets.h"
#include "i18n.h"
#include "alerts.h"
#include "meteor.h"

#define AC_VERSION     "0.9.1"
#define DEFAULT_LOC    "JN97mm"
#define MIN_CS         25
#define MAX_AGE_S      (3 * 3600)


struct AppCore {
    char       locator[12];
    double     qlat, qlon;
    int        first_start;

    PropData   data;
    int        loaded_mask;
    IonoModel  model;
    time_t     now;
    int        toff;

    Target     tg[TARGETS_MAX];
    int        n_default, n_custom, nt, sel;
    PathResult res[TARGETS_MAX];
    char       err[200];
    int        lang_saved;
    int        units_saved;    /* van-e mentett km/mi beállítás (különben a telefon országa dönt) */
    Forecast   fc;

    MapView    map;
    Canvas     cv;
    int        map_dirty;

    Pskr      *psk;
    char       psk_host[128];
    int        psk_port;
    SpotStore  spots;
    PskSpot    pbuf[1024];
    SpotEval   ebuf[1024];
};

/* ---------- segédfüggvények ---------- */

static void normalize_locator(const char *in, char *out, size_t len)
{
    size_t i;
    for (i = 0; in[i] && i + 1 < len; i++) {
        char ch = in[i];
        if (i < 2 || i >= 6) ch = (char)toupper((unsigned char)ch);
        if (i >= 4 && i < 6) ch = (char)tolower((unsigned char)ch);
        out[i] = ch;
    }
    out[i] = '\0';
}

static int valid_locator(const char *loc)
{
    double la, lo;
    size_t n = strlen(loc);
    return (n == 4 || n == 6 || n == 8) && locator_to_latlon(loc, &la, &lo) == 0;
}

static int cmp_dist(const void *x, const void *y)
{
    double a = ((const IonoStation *)x)->dist_km, b = ((const IonoStation *)y)->dist_km;
    return (a > b) - (a < b);
}

static void resort_stations(AppCore *a)
{
    for (int i = 0; i < a->data.count; i++)
        a->data.st[i].dist_km = distance_km(a->qlat, a->qlon, a->data.st[i].lat, a->data.st[i].lon);
    qsort(a->data.st, (size_t)a->data.count, sizeof(IonoStation), cmp_dist);
}

/* ---------- létrehozás ---------- */

AppCore *ac_new(const char *cache_dir, const char *config_dir)
{
    if (cache_dir && *cache_dir) setenv("XDG_CACHE_HOME", cache_dir, 1);
    if (config_dir && *config_dir) setenv("XDG_CONFIG_HOME", config_dir, 1);

    AppCore *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    if (cv_init(&a->cv, 16, 16) != 0) { free(a); return NULL; }
    if (spots_init(&a->spots) != 0) { cv_free(&a->cv); free(a); return NULL; }

    predict_params_default(&g_params);
    config_load(&g_params);

    char loc[16] = "";
    if (config_load_locator(loc, sizeof loc) && valid_locator(loc)) {
        normalize_locator(loc, a->locator, sizeof a->locator);
    } else {
        snprintf(a->locator, sizeof a->locator, "%s", DEFAULT_LOC);
        a->first_start = 1;
    }
    locator_to_latlon(a->locator, &a->qlat, &a->qlon);

    {
        /* nyelv: a mentett; ha nincs, a Kotlin-oldal a telefon nyelve szerint beállítja */
        char v[8];
        a->lang_saved = config_get("lang", v, sizeof v) && lang_from_name(v) >= 0;
        g_lang = a->lang_saved ? lang_from_name(v) : LANG_HU;
        a->units_saved = config_get("units", v, sizeof v);
        units_load();
    }
    a->n_default = predict_default_targets(a->tg, TARGETS_MAX - TARGETS_CUSTOM_MAX - 1);
    a->n_custom = targets_load_custom(&a->tg[a->n_default], TARGETS_CUSTOM_MAX);
    a->nt = a->n_default + a->n_custom;
    {
        char v[32];
        if (config_get("mode", v, sizeof v) && mode_from_name(v) >= 0) g_mode = mode_from_name(v);
    }
    a->map.mode = MAP_MUF;
    a->map.no_legend = 1;
    a->map_dirty = 1;
    a->now = time(NULL);
    model_init(&a->model, &a->data, a->now, MAX_AGE_S, MIN_CS);
    return a;
}

void ac_free(AppCore *a)
{
    if (!a) return;
    ac_psk_stop(a);
    spots_free(&a->spots);
    cv_free(&a->cv);
    free(a);
}

/* ---------- beállítások ---------- */

int ac_set_locator(AppCore *a, const char *loc)
{
    char n[12];
    normalize_locator(loc, n, sizeof n);
    if (!valid_locator(n)) return -1;
    snprintf(a->locator, sizeof a->locator, "%s", n);
    locator_to_latlon(a->locator, &a->qlat, &a->qlon);
    config_save_locator(a->locator);
    a->first_start = 0;

    resort_stations(a);
    a->nt = a->n_default + a->n_custom;        /* a térképen kijelölt cél törlődik */
    if (a->sel >= a->nt) a->sel = 0;
    a->spots.n = 0;
    if (a->psk) {                              /* a környékbeli négyzetek változnak */
        char host[128];
        int port = a->psk_port;
        snprintf(host, sizeof host, "%s", a->psk_host);
        ac_psk_stop(a);
        ac_psk_start(a, host, port);
    }
    a->map_dirty = 1;
    return 0;
}

const char *ac_locator(const AppCore *a) { return a->locator; }
int ac_first_start(const AppCore *a) { return a->first_start; }

int ac_load_json(AppCore *a, const char *stations, const char *f107, const char *kp, const char *xray)
{
    int mask = 0;
    if (stations) {
        PropData *tmp = malloc(sizeof *tmp);       /* hibás adat ne írja felül a jót */
        if (tmp) {
            memset(tmp, 0, sizeof *tmp);
            if (parse_kc2g_stations(stations, tmp, a->qlat, a->qlon) > 0) {
                memcpy(a->data.st, tmp->st, sizeof tmp->st);
                a->data.count = tmp->count;
                a->data.have_stations = 1;
                mask |= 1;
            }
            free(tmp);
        }
    }
    SolarData sd = a->data.solar;
    if (f107 && parse_swpc_f107(f107, &sd) == 0) { a->data.have_sfi = 1; mask |= 2; }
    if (kp   && parse_swpc_kp(kp, &sd) == 0)     { a->data.have_kp = 1;  mask |= 4; }
    if (xray && parse_swpc_xray(xray, &sd) == 0) { a->data.have_xray = 1; mask |= 8; }
    a->data.solar = sd;
    a->loaded_mask |= mask;
    a->map_dirty = 1;
    return mask;
}

void ac_set_time_offset(AppCore *a, int hours)
{
    if (hours < 0) hours = 0;
    if (hours > FC_HOURS - 1) hours = FC_HOURS - 1;
    a->toff = hours;
}

void ac_select_target(AppCore *a, int idx)
{
    if (idx >= 0 && idx < a->nt) a->sel = idx;
}

int ac_target_from_map(AppCore *a, int w, int h, int x, int y)
{
    MapView tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.w = w;
    tmp.h = h;
    double lat, lon;
    if (w <= 0 || h <= 0 || screen_to_map(&tmp, x, y, &lat, &lon) != 0) return -1;

    int idx = a->n_default + a->n_custom;
    Target *t = &a->tg[idx];
    memset(t, 0, sizeof *t);
    t->lat = lat;
    t->lon = lon;
    char loc[8];
    latlon_to_locator(lat, lon, loc);
    t->nvis = distance_km(a->qlat, a->qlon, lat, lon) < 300.0;
    snprintf(t->name, sizeof t->name, TR("Kijelölt: %s", "Picked: %s"), loc);
    a->nt = idx + 1;
    a->sel = idx;
    return a->sel;
}

/* ---------- üzemmód, saját célterületek, hosszú út ---------- */

void ac_set_mode(AppCore *a, int mode)
{
    if (mode < 0 || mode >= NMODES) return;
    g_mode = mode;
    config_set("mode", MODE_NAMES[mode]);
    a->map_dirty = 1;
    a->map.cover_valid = 0;
}

const char *ac_last_error(const AppCore *a) { return a->err; }

void ac_set_units(AppCore *a, int mi)
{
    units_set(mi);
    a->units_saved = 1;
}

const char *ac_help_text(void) { return help_text(); }

void ac_set_lang(AppCore *a, int lang)
{
    if (lang != LANG_HU && lang != LANG_EN) return;
    g_lang = lang;
    config_set("lang", lang_name(lang));
    a->lang_saved = 1;
    /* a beépített célterületek neve a nyelvtől függ */
    Target tmp[TARGETS_MAX];
    int n = predict_default_targets(tmp, a->n_default);
    for (int i = 0; i < n; i++) {
        int lp = a->tg[i].lp;
        a->tg[i] = tmp[i];
        a->tg[i].lp = lp;
    }
    a->map_dirty = 1;
}

static int add_custom(AppCore *a, const Target *t)
{
    if (a->n_custom >= TARGETS_CUSTOM_MAX) {
        snprintf(a->err, sizeof a->err, TR("Legfeljebb %d saját célterület lehet - előbb törölj egyet.",
                                           "At most %d own targets - delete one first."), TARGETS_CUSTOM_MAX);
        return -1;
    }
    int idx = a->n_default + a->n_custom;
    a->tg[idx] = *t;
    a->tg[idx].nvis = 0;
    a->n_custom++;
    a->nt = a->n_default + a->n_custom;         /* a kijelölt pont törlődik */
    a->sel = idx;
    if (targets_save_custom(&a->tg[a->n_default], a->n_custom) != 0)
        snprintf(a->err, sizeof a->err, "%s", TR("Nem tudtam elmenteni.", "Could not save."));
    return idx;
}

int ac_add_target(AppCore *a, const char *input)
{
    a->err[0] = '\0';
    Target t;
    if (target_resolve(input, &t, a->err, sizeof a->err) != 0) return -1;
    return add_custom(a, &t);
}

int ac_save_picked(AppCore *a)
{
    a->err[0] = '\0';
    int idx = a->n_default + a->n_custom;
    if (a->nt <= idx) { snprintf(a->err, sizeof a->err, "%s", TR("Nincs kijelölt pont.", "No picked point.")); return -1; }
    Target t = a->tg[idx];
    {
        const char *pre = TR("Kijelölt: ", "Picked: ");
        size_t pl = strlen(pre);
        if (strncmp(t.name, pre, pl) == 0) memmove(t.name, t.name + pl, strlen(t.name + pl) + 1);
    }
    return add_custom(a, &t);
}

int ac_remove_target(AppCore *a, int idx)
{
    if (idx < a->n_default || idx >= a->n_default + a->n_custom) return -1;
    int last = a->n_default + a->n_custom - 1;
    for (int k = idx; k < last; k++) a->tg[k] = a->tg[k + 1];
    a->n_custom--;
    a->nt = a->n_default + a->n_custom;
    if (a->sel >= a->nt) a->sel = a->nt - 1;
    if (a->sel < 0) a->sel = 0;
    targets_save_custom(&a->tg[a->n_default], a->n_custom);
    return 0;
}

int ac_toggle_lp(AppCore *a, int idx)
{
    if (idx < 0 || idx >= a->nt || a->tg[idx].nvis) return -1;
    a->tg[idx].lp = !a->tg[idx].lp;
    return a->tg[idx].lp;
}

int ac_load_kp_forecast(AppCore *a, const char *json)
{
    if (!json) return 0;
    SolarData sd = a->data.solar;
    if (parse_swpc_kp_forecast(json, &sd) != 0) return 0;
    a->data.solar = sd;
    a->data.have_kpf = 1;
    a->map_dirty = 1;
    return 1;
}

/* ---------- számítás ---------- */

void ac_recompute(AppCore *a)
{
    a->now = time(NULL);
    model_init(&a->model, &a->data, a->now, MAX_AGE_S, MIN_CS);
    model_set_time(&a->model, a->now + a->toff * 3600);
    for (int i = 0; i < a->nt; i++)
        predict_path(&a->model, &a->data, a->qlat, a->qlon, &a->tg[i], &a->res[i]);
    predict_forecast(&a->model, &a->data, a->qlat, a->qlon, &a->tg[a->sel],
                     a->now - a->now % 3600, &a->fc);
    a->map_dirty = 1;
}

/* volt-e valódi vétel a célterületen az adott sávon az utóbbi 30 percben.
 * 0 = nem, 1 = csak gyenge (FT8-szintű), 2 = erős jel is (>= STRONG_SNR) */
static int spot_on_target(const AppCore *a, const Target *t, int band)
{
    return spots_on_target(&a->spots, a->qlat, a->qlon, t, band, time(NULL));
}

static cJSON *num_or_null(double v, int have)
{
    return (have && !isnan(v)) ? cJSON_CreateNumber(v) : cJSON_CreateNull();
}

/* szám kerekítve (kisebb JSON) */
static cJSON *num1(double v)
{
    if (isnan(v) || isinf(v)) return cJSON_CreateNull();
    return cJSON_CreateNumber(round(v * 100.0) / 100.0);
}

char *ac_state_json(AppCore *a)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "version", AC_VERSION);
    cJSON_AddStringToObject(r, "locator", a->locator);
    cJSON_AddItemToObject(r, "lat", num1(a->qlat));
    cJSON_AddItemToObject(r, "lon", num1(a->qlon));
    cJSON_AddBoolToObject(r, "first_start", a->first_start);
    cJSON_AddNumberToObject(r, "now", (double)a->now);
    cJSON_AddNumberToObject(r, "t", (double)a->model.t);
    cJSON_AddNumberToObject(r, "toff", a->toff);
    cJSON_AddNumberToObject(r, "loaded", a->loaded_mask);
    cJSON_AddNumberToObject(r, "nref", a->model.nref);

    /* bandnevek */
    cJSON *bn = cJSON_AddArrayToObject(r, "bands");
    for (int b = 0; b < NBANDS; b++) cJSON_AddItemToArray(bn, cJSON_CreateString(BANDS[b].name));

    /* napaktivitás */
    const PropData *d = &a->data;
    cJSON *so = cJSON_AddObjectToObject(r, "solar");
    cJSON_AddItemToObject(so, "sfi", num_or_null(d->solar.sfi, d->have_sfi));
    cJSON_AddItemToObject(so, "sfi90", num_or_null(d->solar.sfi_90day, d->have_sfi && d->solar.sfi_90day > 0));
    cJSON_AddItemToObject(so, "kp", num_or_null(d->solar.kp, d->have_kp));
    if (d->have_xray) {
        cJSON_AddStringToObject(so, "xray", d->solar.xray_class);
        cJSON_AddNumberToObject(so, "xray_flux", d->solar.xray_flux);
    } else {
        cJSON_AddNullToObject(so, "xray");
    }

    /* rétegek a QTH fölött */
    IonoPoint q;
    model_point(&a->model, a->qlat, a->qlon, &q);
    cJSON *ly = cJSON_AddObjectToObject(r, "layers");
    cJSON_AddItemToObject(ly, "fof2", num1(q.fof2));
    cJSON_AddItemToObject(ly, "hmf2", num1(q.hmf2));
    cJSON_AddItemToObject(ly, "mufd", num1(q.mufd));
    cJSON_AddItemToObject(ly, "foe", num1(q.foe));
    cJSON_AddItemToObject(ly, "cosx", num1(q.cosx));
    cJSON_AddItemToObject(ly, "conf", num1(q.conf));
    cJSON_AddStringToObject(ly, "day", q.cosx > 0.2 ? TR("nappal", "day")
                                       : q.cosx > -0.05 ? TR("napkelte/nyugta", "sunrise/sunset")
                                       : q.cosx > -0.3 ? TR("szürkület", "twilight") : TR("éjszaka", "night"));

    /* javaslat: csoportonként (közeli / Európa / DX) a legjobb sávok */
    const char *gname[3] = { TR("Közeli", "Local"), TR("Európa", "Europe"), "DX" };
    int gsum[3][NBANDS] = {{0}}, gcnt[3] = {0};
    for (int i = 0; i < a->n_default; i++) {
        int g = a->tg[i].nvis ? 0 : a->res[i].dist_km < 4000 ? 1 : 2;
        gcnt[g]++;
        for (int b = 0; b < NBANDS; b++) gsum[g][b] += a->res[i].grade[b];
    }
    cJSON *rec = cJSON_AddArrayToObject(r, "recommend");
    for (int g = 0; g < 3; g++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "group", gname[g]);
        cJSON *bl = cJSON_AddArrayToObject(o, "bands");
        int shown = 0;
        for (int lvl = 3; lvl >= 2 && shown < 3; lvl--)
            for (int b = NBANDS - 1; b >= 0 && shown < 3; b--) {
                double avg = gcnt[g] ? (double)gsum[g][b] / gcnt[g] : 0;
                int a2 = avg >= 2.5 ? 3 : avg >= 1.75 ? 2 : 0;
                if (a2 != lvl) continue;
                cJSON *e = cJSON_CreateObject();
                cJSON_AddStringToObject(e, "name", BANDS[b].name);
                cJSON_AddNumberToObject(e, "grade", lvl);
                cJSON_AddItemToArray(bl, e);
                shown++;
            }
        if (!shown) {
            int best = -1;
            double bavg = 0.75;
            for (int b = 0; b < NBANDS; b++) {
                double avg = gcnt[g] ? (double)gsum[g][b] / gcnt[g] : 0;
                if (avg >= bavg) { bavg = avg; best = b; }
            }
            if (best >= 0) cJSON_AddStringToObject(o, "weak", BANDS[best].name);
        }
        cJSON_AddItemToArray(rec, o);
    }

    /* célterületek */
    int show_spots = a->psk && a->toff == 0;
    cJSON *ta = cJSON_AddArrayToObject(r, "targets");
    for (int i = 0; i < a->nt; i++) {
        const PathResult *p = &a->res[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", a->tg[i].name);
        cJSON_AddBoolToObject(o, "nvis", a->tg[i].nvis);
        cJSON_AddBoolToObject(o, "custom", i >= a->n_default && i < a->n_default + a->n_custom);
        cJSON_AddBoolToObject(o, "picked", i == a->n_default + a->n_custom);
        cJSON_AddBoolToObject(o, "lp", a->tg[i].lp);
        cJSON_AddItemToObject(o, "dist", num1(p->dist_km));
        cJSON_AddItemToObject(o, "bearing", num1(p->bearing));
        cJSON_AddItemToObject(o, "muf", num1(p->muf));
        cJSON_AddItemToObject(o, "luf", num1(p->luf));
        cJSON_AddNumberToObject(o, "hops", p->hops);
        cJSON_AddBoolToObject(o, "polar", p->polar);
        cJSON_AddBoolToObject(o, "flare", p->flare);
        cJSON_AddItemToObject(o, "conf", num1(p->conf));
        cJSON *gr = cJSON_AddArrayToObject(o, "grades");
        cJSON *sp = cJSON_AddArrayToObject(o, "spots");
        for (int b = 0; b < NBANDS; b++) {
            cJSON_AddItemToArray(gr, cJSON_CreateNumber(p->grade[b]));
            cJSON_AddItemToArray(sp, cJSON_CreateNumber(show_spots ? spot_on_target(a, &a->tg[i], b) : 0));
        }
        cJSON_AddItemToArray(ta, o);
    }
    cJSON_AddNumberToObject(r, "sel", a->sel);
    cJSON_AddStringToObject(r, "mode", MODE_NAMES[g_mode]);
    cJSON_AddStringToObject(r, "lang", lang_name(g_lang));
    cJSON_AddBoolToObject(r, "lang_saved", a->lang_saved);
    cJSON_AddBoolToObject(r, "alerts", alerts_enabled());
    cJSON_AddStringToObject(r, "units", dist_unit());
    cJSON_AddBoolToObject(r, "units_saved", a->units_saved);
    cJSON_AddNumberToObject(r, "mode_idx", g_mode);

    /* napkelte / napnyugta (UTC másodperc) a QTH-n és a kijelölt célnál */
    {
        cJSON *sun = cJSON_AddObjectToObject(r, "sun");
        time_t t = a->now + a->toff * 3600, rs, st;
        int k = sun_rise_set(a->qlat, a->qlon, t, &rs, &st);
        cJSON_AddNumberToObject(sun, "qth_state", k);
        cJSON_AddNumberToObject(sun, "qth_rise", (double)rs);
        cJSON_AddNumberToObject(sun, "qth_set", (double)st);
        const Target *tg = &a->tg[a->sel];
        if (!tg->nvis) {
            k = sun_rise_set(tg->lat, tg->lon, t, &rs, &st);
            cJSON_AddNumberToObject(sun, "tg_state", k);
            cJSON_AddNumberToObject(sun, "tg_rise", (double)rs);
            cJSON_AddNumberToObject(sun, "tg_set", (double)st);
        }
    }

    /* 24 órás előrejelzés a kijelölt célra */
    cJSON *fc = cJSON_AddObjectToObject(r, "forecast");
    cJSON_AddNumberToObject(fc, "t0", (double)a->fc.t0);
    cJSON *fg = cJSON_AddArrayToObject(fc, "grades");
    cJSON *fm = cJSON_AddArrayToObject(fc, "muf");
    cJSON *fl = cJSON_AddArrayToObject(fc, "luf");
    cJSON *fk = cJSON_AddArrayToObject(fc, "kp");
    cJSON_AddBoolToObject(fc, "kp_forecast", a->data.have_kpf);
    for (int h = 0; h < FC_HOURS; h++) {
        cJSON *row = cJSON_CreateArray();
        for (int b = 0; b < NBANDS; b++) cJSON_AddItemToArray(row, cJSON_CreateNumber(a->fc.grade[h][b]));
        cJSON_AddItemToArray(fg, row);
        cJSON_AddItemToArray(fm, num1(a->fc.muf[h]));
        cJSON_AddItemToArray(fl, num1(a->fc.luf[h]));
        cJSON_AddItemToArray(fk, num1(a->fc.kp[h]));
    }

    /* PSK Reporter */
    cJSON *ps = cJSON_AddObjectToObject(r, "psk");
    cJSON_AddBoolToObject(ps, "on", a->psk != NULL);
    cJSON_AddBoolToObject(ps, "connected", pskr_connected(a->psk));
    char st[256];
    pskr_status(a->psk, st, sizeof st);
    cJSON_AddStringToObject(ps, "status", st);
    time_t now = time(NULL);
    cJSON_AddNumberToObject(ps, "spots30", spots_recent(&a->spots, now));

    /* sporadikus E: közeli ionoszonda (foEs) + valódi vételek */
    EsStatus es;
    es_status(&a->data, a->psk ? &a->spots : NULL, a->qlat, a->qlon, now, &es);
    cJSON *eo = cJSON_AddObjectToObject(r, "es");
    if (es.have_foes) {
        cJSON_AddItemToObject(eo, "foes", num1(es.foes));
        cJSON_AddStringToObject(eo, "station", es.foes_name);
        cJSON_AddNumberToObject(eo, "km", round(es.foes_km));
        cJSON_AddNumberToObject(eo, "age", es.foes_age_min);
        cJSON_AddItemToObject(eo, "muf", num1(es.es_muf));
    }
    cJSON *ep = cJSON_AddArrayToObject(eo, "pairs");
    for (int b = 0; b < NBANDS; b++) cJSON_AddItemToArray(ep, cJSON_CreateNumber(es.live.pairs[b]));
    cJSON_AddNumberToObject(eo, "minpairs", SPOT_MINPAIR);

    /* meteorraj: az aktív vagy a következő */
    {
        MeteorStatus ms;
        cJSON *mo = cJSON_AddObjectToObject(r, "meteor");
        char line[160];
        if (meteor_status(now + a->toff * 3600, a->qlat, a->qlon, &ms)) {
            meteor_line(&ms, line, sizeof line);
            cJSON_AddStringToObject(mo, "text", line);
            cJSON_AddStringToObject(mo, "name", meteor_name(ms.sh));
            cJSON_AddBoolToObject(mo, "active", ms.active);
            cJSON_AddItemToObject(mo, "days", num1(ms.days_to_peak));
            cJSON_AddNumberToObject(mo, "zhr", ms.sh->zhr);
        } else {
            meteor_line(NULL, line, sizeof line);
            cJSON_AddStringToObject(mo, "text", line);
            cJSON_AddBoolToObject(mo, "active", 0);
        }
    }

    PredictParams def;
    predict_params_default(&def);
    cJSON_AddBoolToObject(r, "tuned", memcmp(&def, &g_params, sizeof def) != 0);

    char *out = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return out;
}

/* ---------- riasztás ---------- */

void ac_set_alerts(AppCore *a, int on) { (void)a; alerts_set_enabled(on); }

char *ac_check_alerts(AppCore *a)
{
    cJSON *r = cJSON_CreateObject();
    int on = alerts_enabled();
    cJSON_AddBoolToObject(r, "enabled", on);
    cJSON *arr = cJSON_AddArrayToObject(r, "new");
    /* csak a jelen időpontra van értelme (az idősávval nézett jövőre nem) */
    if (on && a->toff == 0 && a->loaded_mask) {
        time_t now = time(NULL);
        EsStatus es;
        es_status(&a->data, a->psk ? &a->spots : NULL, a->qlat, a->qlon, now, &es);
        Alert cur[ALERT_MAX], nw[ALERT_MAX];
        int n = alerts_collect(a->tg, a->res, a->nt, a->n_default, a->n_custom, &es, cur, ALERT_MAX);
        n = alerts_add_meteor(cur, n, ALERT_MAX, now, a->qlat, a->qlon);
        int k = alerts_new(cur, n, now, NULL, nw, ALERT_MAX);
        for (int i = 0; i < k; i++) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "key", nw[i].key);
            cJSON_AddStringToObject(o, "text", nw[i].text);
            cJSON_AddItemToArray(arr, o);
        }
    }
    char *out = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return out;
}

/* ---------- térkép ---------- */

static void draw_spots(AppCore *a)
{
    spots_draw(&a->cv, &a->map, &a->spots, time(NULL), a->map.w / 1000.0 + 0.4);
}

int ac_render_map(AppCore *a, uint32_t *out, int w, int h, int stride, int mode,
                  int show_spots, int argb)
{
    if (w <= 0 || h <= 0 || !out) return -1;
    if (mode < MAP_MUF || mode >= NBANDS) mode = MAP_MUF;
    if (a->cv.w != w || a->cv.h != h)
        if (cv_resize(&a->cv, w, h) != 0) return -1;
    a->map.x = 0;
    a->map.y = 0;
    a->map.w = w;
    a->map.h = h;
    a->map.mode = mode;

    if (a->map_dirty) {
        map_compute(&a->map, &a->model, &a->data, a->qlat, a->qlon, mode >= 0);
        if (mode < 0) a->map.cover_valid = 0;
        a->map_dirty = 0;
    } else if (mode >= 0 && !a->map.cover_valid) {
        map_compute(&a->map, &a->model, &a->data, a->qlat, a->qlon, 1);
    }

    map_draw(&a->cv, &a->map, &a->data, &a->model, a->qlat, a->qlon, &a->tg[a->sel]);
    if (show_spots && a->toff == 0 && a->psk) draw_spots(a);

    for (int y = 0; y < h; y++) {
        const uint32_t *src = &a->cv.px[(size_t)y * w];
        uint32_t *dst = &out[(size_t)y * stride];
        if (argb) {                        /* 0x00RRGGBB -> 0xFFRRGGBB */
            for (int x = 0; x < w; x++) dst[x] = 0xFF000000u | src[x];
        } else {                           /* -> memóriában R,G,B,A = 0xAABBGGRR */
            for (int x = 0; x < w; x++) {
                uint32_t p = src[x];
                dst[x] = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
            }
        }
    }
    return 0;
}

/* ---------- PSK Reporter ---------- */

int ac_psk_start(AppCore *a, const char *host, int port)
{
    if (a->psk) return 0;
    snprintf(a->psk_host, sizeof a->psk_host, "%s", host && *host ? host : PSKR_HOST);
    a->psk_port = port > 0 ? port : PSKR_PORT;
    validate_log_trim(14);
    a->psk = pskr_start(a->psk_host, a->psk_port, a->qlat, a->qlon, 300.0);
    return a->psk ? 0 : -1;
}

void ac_psk_stop(AppCore *a)
{
    if (!a->psk) return;
    pskr_stop(a->psk);
    a->psk = NULL;
}

int ac_psk_poll(AppCore *a)
{
    if (!a->psk) return 0;
    int n = pskr_poll(a->psk, a->pbuf, 1024);
    if (n <= 0) return 0;
    spots_add(&a->spots, &a->model, &a->data, a->pbuf, n, a->ebuf);
    validate_log_append(a->ebuf, n);
    return n;
}

/* ---------- ellenőrzési jelentés ---------- */

char *ac_report_text(AppCore *a, int days)
{
    (void)a;
    SpotEval *e = NULL;
    int n = validate_log_load(time(NULL) - (time_t)days * 86400, &e);
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    if (!f) { free(e); return NULL; }
    if (n <= 0) {
        fprintf(f, TR("Még nincs gyűjtött spot az utolsó\n%d napból.\n\n"
                      "Hagyd futni az appot bekapcsolt\nPSK Reporterrel, és a spotok\n"
                      "automatikusan gyűlnek.\n",
                      "No spots collected in the last\n%d days yet.\n\n"
                      "Keep the app running with PSK\nReporter on, and spots are\n"
                      "collected automatically.\n"), days);
    } else {
        ValReport r;
        validate_report(e, n, &r);
        r.for_app = 1;
        validate_print(&r, f);
    }
    fclose(f);
    free(e);
    return buf;
}

int ac_save_suggestion(AppCore *a, int days)
{
    SpotEval *e = NULL;
    int n = validate_log_load(time(NULL) - (time_t)days * 86400, &e);
    if (n <= 0) { free(e); return 1; }
    ValReport r;
    validate_report(e, n, &r);
    free(e);
    if (!r.have_suggest) return 1;
    char note[96];
    snprintf(note, sizeof note, "%d spot alapján (%s), Android", r.n, a->locator);
    if (config_save(&r.suggest, note) != 0) return -1;
    g_params = r.suggest;
    a->map_dirty = 1;
    return 0;
}
