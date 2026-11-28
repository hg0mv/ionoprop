/*
 * parse.c - a letöltött JSON adatok feldolgozása (cJSON)
 *
 * A források néha szövegként adják a számokat (pl. "14.6"), néha
 * számként, néha null-t adnak - a json_num() mindhármat kezeli.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../third_party/cJSON.h"
#include "parse.h"
#include "geo.h"

/* ---------- segédfüggvények ---------- */

/* Napok száma 1970-01-01 óta (Howard Hinnant algoritmusa).
 * Így nem kell a timegm(), ami Windowson nincs. */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

time_t parse_iso_time(const char *s)
{
    int Y, M, D, h = 0, m = 0;
    double sec = 0;
    if (!s) return 0;
    /* a dátum és idő között 'T' vagy szóköz lehet */
    int n = sscanf(s, "%4d-%2d-%2d%*1[T ]%2d:%2d:%lf", &Y, &M, &D, &h, &m, &sec);
    if (n < 3) return 0;
    if (M < 1 || M > 12 || D < 1 || D > 31) return 0;
    return (time_t)(days_from_civil(Y, M, D) * 86400L + h * 3600L + m * 60L + (long)sec);
}

static double json_num(const cJSON *it)
{
    if (!it) return IONO_NAN;
    if (cJSON_IsNumber(it)) return it->valuedouble;
    if (cJSON_IsString(it) && it->valuestring && *it->valuestring) {
        char *end;
        double v = strtod(it->valuestring, &end);
        if (end != it->valuestring) return v;
    }
    return IONO_NAN;
}

static const char *json_str(const cJSON *obj, const char *key)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(it) && it->valuestring) ? it->valuestring : NULL;
}

static double get_num(const cJSON *obj, const char *key)
{
    return json_num(cJSON_GetObjectItemCaseSensitive(obj, key));
}

static cJSON *parse_array(const char *json, const char *what)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        fprintf(stderr, "Hibás JSON (%s)\n", what);
        return NULL;
    }
    if (!cJSON_IsArray(root)) {
        fprintf(stderr, "Váratlan formátum (%s): nem tömb\n", what);
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

/* ---------- KC2G ionoszonda-adatok ---------- */

static int cmp_dist(const void *a, const void *b)
{
    double da = ((const IonoStation *)a)->dist_km;
    double db = ((const IonoStation *)b)->dist_km;
    return (da > db) - (da < db);
}

int parse_kc2g_stations(const char *json, PropData *pd, double qth_lat, double qth_lon)
{
    cJSON *root = parse_array(json, "KC2G stations.json");
    if (!root) return -1;

    pd->count = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, root) {
        if (pd->count >= IONO_MAX_STATIONS) break;
        const cJSON *stn = cJSON_GetObjectItemCaseSensitive(e, "station");
        if (!cJSON_IsObject(stn)) continue;

        IonoStation s;
        memset(&s, 0, sizeof s);

        double lat = get_num(stn, "latitude");
        double lon = get_num(stn, "longitude");
        if (isnan(lat) || isnan(lon)) continue;   /* hely nélkül nem használható */
        s.lat = lat;
        s.lon = norm_lon(lon);

        const char *code = json_str(stn, "code");
        const char *name = json_str(stn, "name");
        snprintf(s.code, sizeof s.code, "%s", code ? code : "?");
        snprintf(s.name, sizeof s.name, "%s", name ? name : "?");

        s.time = parse_iso_time(json_str(e, "time"));
        s.fof2 = get_num(e, "fof2");
        s.mufd = get_num(e, "mufd");
        s.hmf2 = get_num(e, "hmf2");
        s.md   = get_num(e, "md");
        s.foes = get_num(e, "foes");
        if (!(s.foes > 0.5 && s.foes < 30)) s.foes = IONO_NAN;
        double cs = get_num(e, "cs");
        s.cs = isnan(cs) ? 0 : (int)lround(cs);

        /* ha nincs md, de van MUF és foF2, kiszámoljuk */
        if (isnan(s.md) && s.mufd > 0 && s.fof2 > 0)
            s.md = s.mufd / s.fof2;

        s.dist_km = distance_km(qth_lat, qth_lon, s.lat, s.lon);
        pd->st[pd->count++] = s;
    }
    cJSON_Delete(root);

    qsort(pd->st, (size_t)pd->count, sizeof(IonoStation), cmp_dist);
    pd->have_stations = pd->count > 0;
    return pd->count;
}

/* ---------- NOAA SWPC: 10,7 cm napfluxus ---------- */

int parse_swpc_f107(const char *json, SolarData *sd)
{
    cJSON *root = parse_array(json, "SWPC f107_cm_flux.json");
    if (!root) return -1;

    time_t best_t = 0, mean_t = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, root) {
        time_t t = parse_iso_time(json_str(e, "time_tag"));
        double flux = get_num(e, "flux");
        double mean = get_num(e, "ninety_day_mean");
        if (t > best_t && flux > 0) {
            best_t = t;
            sd->sfi = flux;
            sd->sfi_time = t;
        }
        if (t > mean_t && mean > 0) {
            mean_t = t;
            sd->sfi_90day = mean;
        }
    }
    cJSON_Delete(root);
    return best_t ? 0 : -1;
}

/* ---------- NOAA SWPC: Kp-index ---------- */

int parse_swpc_kp(const char *json, SolarData *sd)
{
    cJSON *root = parse_array(json, "SWPC planetary_k_index_1m.json");
    if (!root) return -1;

    time_t best_t = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, root) {
        time_t t = parse_iso_time(json_str(e, "time_tag"));
        double kp = get_num(e, "estimated_kp");
        if (isnan(kp)) kp = get_num(e, "kp_index");
        if (t > best_t && kp >= 0) {
            best_t = t;
            sd->kp = kp;
            sd->kp_time = t;
        }
    }
    cJSON_Delete(root);
    return best_t ? 0 : -1;
}

/* ---------- NOAA SWPC: Kp-előrejelzés ----------
 * Kétféle formátum létezik (2026 tavaszán váltott a NOAA):
 *   régi: [["time_tag","kp","observed","noaa_scale"], ["2026-11-11 00:00:00","2.33","observed",null], ...]
 *   új:   [{"time_tag":"2026-11-11T00:00:00","kp":2.33,"observed":"observed",...}, ...]
 * Mindkettőt elfogadjuk. */
int parse_swpc_kp_forecast(const char *json, SolarData *sd)
{
    cJSON *root = parse_array(json, "SWPC noaa-planetary-k-index-forecast.json");
    if (!root) return -1;
    sd->nkpf = 0;
    int col_t = -1, col_kp = -1;
    const cJSON *e;
    cJSON_ArrayForEach(e, root) {
        time_t t = 0;
        double kp = IONO_NAN;
        if (cJSON_IsArray(e)) {
            if (col_t < 0) {                     /* fejléc sor */
                int i = 0;
                const cJSON *h;
                cJSON_ArrayForEach(h, e) {
                    if (cJSON_IsString(h) && strcmp(h->valuestring, "time_tag") == 0) col_t = i;
                    if (cJSON_IsString(h) && strcmp(h->valuestring, "kp") == 0) col_kp = i;
                    i++;
                }
                if (col_t < 0 || col_kp < 0) break;
                continue;
            }
            const cJSON *ct = cJSON_GetArrayItem(e, col_t), *ck = cJSON_GetArrayItem(e, col_kp);
            t = parse_iso_time(cJSON_IsString(ct) ? ct->valuestring : NULL);
            kp = json_num(ck);
        } else if (cJSON_IsObject(e)) {
            t = parse_iso_time(json_str(e, "time_tag"));
            kp = get_num(e, "kp");
            if (isnan(kp)) kp = get_num(e, "kp_index");
        }
        if (!t || isnan(kp) || kp < 0 || kp > 9.5) continue;
        if (sd->nkpf < KPF_MAX) {
            sd->kpf_t[sd->nkpf] = t;
            sd->kpf[sd->nkpf] = kp;
            sd->nkpf++;
        }
    }
    cJSON_Delete(root);
    return sd->nkpf > 0 ? 0 : -1;
}

/* ---------- NOAA SWPC: GOES röntgen ---------- */

void xray_flux_class(double flux, char *buf, size_t len)
{
    static const char cls[] = "ABCMX";
    if (flux <= 0) { snprintf(buf, len, "?"); return; }
    int i = (int)floor(log10(flux)) + 8;      /* 1e-8 -> A ... 1e-4 -> X */
    if (i < 0) i = 0;
    if (i > 4) i = 4;
    double v = flux / pow(10.0, i - 8);
    snprintf(buf, len, "%c%.1f", cls[i], v);
}

int parse_swpc_xray(const char *json, SolarData *sd)
{
    cJSON *root = parse_array(json, "SWPC xrays-6-hour.json");
    if (!root) return -1;

    time_t best_t = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, root) {
        const char *energy = json_str(e, "energy");
        if (!energy || strcmp(energy, "0.1-0.8nm") != 0) continue;  /* hosszú csatorna */
        time_t t = parse_iso_time(json_str(e, "time_tag"));
        double flux = get_num(e, "flux");
        if (t > best_t && flux > 0) {
            best_t = t;
            sd->xray_flux = flux;
            sd->xray_time = t;
        }
    }
    cJSON_Delete(root);
    if (!best_t) return -1;
    xray_flux_class(sd->xray_flux, sd->xray_class, sizeof sd->xray_class);
    return 0;
}
