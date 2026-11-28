/*
 * predict.c - útvonal-számítás és sávjavaslat
 *
 * Egy útvonalnál:
 *  1. Az utat ugrásokra bontjuk (F2: max. kb. 3500 km/ugrás).
 *  2. Minden ugrás közepén (vezérlőpont) lekérdezzük a modellt.
 *  3. MUF ugrásonként: foF2 és az M(3000) tényező alapján, a valódi
 *     ugráshosszra átszámolva. Az út MUF-ja a legrosszabb ugrásé.
 *  4. D-réteg elnyelés a napállásból és a frekvenciából -> LUF.
 *  5. Napkitörés (röntgen) és geomágneses vihar (Kp) hatása.
 *  6. Sávonként értékelés 0..3.
 *
 * Az elnyelési határok tapasztalati értékek; a későbbiekben a PSK Reporter
 * valódi vételi adataival lehet őket pontosítani.
 */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "predict.h"
#include "i18n.h"
#include "solar.h"
#include "geo.h"

#define R_E 6371.0
#define MIN_ELEV_DEG 3.0
#define E_HEIGHT 110.0
#define D_HEIGHT 100.0
#define E_HOP_MAX 2000.0
#define NIGHT_ABSORB 0.1
/* elnyelési határok alapértékei (dB): e fölött egy, két fokozattal rosszabb,
 * ill. zárva. A beállítófájl (params.conf) felülírhatja őket. */
#define ABS_FAIR   12.0
#define ABS_POOR   25.0
#define ABS_CLOSED 50.0

PredictParams g_params = { 1.0, ABS_FAIR, ABS_POOR, ABS_CLOSED };

void predict_params_default(PredictParams *p)
{
    p->muf_scale  = 1.0;
    p->abs_fair   = ABS_FAIR;
    p->abs_poor   = ABS_POOR;
    p->abs_closed = ABS_CLOSED;
}

int g_mode = MODE_SSB;
int g_lang = LANG_HU;             /* i18n.h */
int g_units_mi = 0;   /* távolság mérföldben (i18n.h) */
const char *const MODE_NAMES[NMODES] = { "SSB", "CW", "FT8" };

/* Üzemmódonként mennyivel több elnyelést bír ki az összeköttetés (dB), és
 * mennyivel a MUF fölött jöhet még át (szórással, rövid nyitásokkal).
 * Az SSB az alap; a CW kb. 8 dB-lel, az FT8 kb. 16 dB-lel érzékenyebb. */
static const double MODE_ABS_DB[NMODES]  = { 0.0, 8.0, 16.0 };
static const double MODE_WEAK_MUF[NMODES] = { 1.15, 1.20, 1.30 };

double mode_abs_offset(int mode)
{
    return (mode >= 0 && mode < NMODES) ? MODE_ABS_DB[mode] : 0.0;
}

int mode_from_name(const char *s)
{
    if (!s) return -1;
    for (int i = 0; i < NMODES; i++) {
        const char *a = s, *b = MODE_NAMES[i];
        while (*a && *b && toupper((unsigned char)*a) == *b) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

int predict_grade_mode(double ratio_raw, double A, int polar_pen, int flare, int mode)
{
    if (flare) return 0;
    if (mode < 0 || mode >= NMODES) mode = MODE_SSB;
    double ratio = ratio_raw / g_params.muf_scale;
    double off = MODE_ABS_DB[mode];
    int g = ratio <= 0.85 ? 3 : ratio <= 1.0 ? 2 : ratio <= MODE_WEAK_MUF[mode] ? 1 : 0;
    if (A >= g_params.abs_closed + off) g = 0;
    else if (A >= g_params.abs_poor + off) g -= 2;
    else if (A >= g_params.abs_fair + off) g -= 1;
    g -= polar_pen;
    return g < 0 ? 0 : g > 3 ? 3 : g;
}

int predict_grade(double ratio_raw, double A, int polar_pen, int flare)
{
    return predict_grade_mode(ratio_raw, A, polar_pen, flare, g_mode);
}
/* napkitörés hatása csak M1 (1e-5 W/m2) felett */
#define FLARE_MIN_FLUX 1e-5

const Band BANDS[NBANDS] = {
    {"160m", 1.84}, {"80m", 3.6}, {"60m", 5.36}, {"40m", 7.1}, {"30m", 10.12},
    {"20m", 14.1},  {"17m", 18.1}, {"15m", 21.2}, {"12m", 24.9}, {"11m", 27.3},
    {"10m", 28.5},  {"8m", 40.68},  {"6m", 50.313},
};

static const struct {
    const char *name, *name_en;
    double lat, lon;
    int nvis;
} DEFAULT_TARGETS[] = {
    {"Közeli (NVIS 0-300km)", "Local (NVIS 0-300km)",  0,     0, 1},
    {"Németország", "Germany",           50.1,   8.7, 0},
    {"Anglia", "England",                51.5,  -0.1, 0},
    {"Skandinávia", "Scandinavia",           59.3,  18.1, 0},
    {"Spanyolország", "Spain",         40.4,  -3.7, 0},
    {"Oroszország (Moszkva)", "Russia (Moscow)", 55.8,  37.6, 0},
    {"Közel-Kelet", "Middle East",           32.1,  34.8, 0},
    {"Kanári-szigetek", "Canary Islands",       28.1, -15.4, 0},
    {"USA keleti part", "USA East Coast",       40.7, -74.0, 0},
    {"USA nyugati part", "USA West Coast",      34.0,-118.2, 0},
    {"Brazília", "Brazil",             -23.5, -46.6, 0},
    {"Dél-Afrika", "South Africa",           -26.2,  28.0, 0},
    {"India", "India",                 28.6,  77.2, 0},
    {"Japán", "Japan",                 35.7, 139.7, 0},
    {"Ausztrália", "Australia",           -33.9, 151.2, 0},
};

int predict_default_targets(Target *t, int max)
{
    int n = (int)(sizeof DEFAULT_TARGETS / sizeof DEFAULT_TARGETS[0]);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        memset(&t[i], 0, sizeof t[i]);
        snprintf(t[i].name, sizeof t[i].name, "%s", TR(DEFAULT_TARGETS[i].name, DEFAULT_TARGETS[i].name_en));
        t[i].lat = DEFAULT_TARGETS[i].lat;
        t[i].lon = DEFAULT_TARGETS[i].lon;
        t[i].nvis = DEFAULT_TARGETS[i].nvis;
    }
    return n;
}

/* ---------- geometria ---------- */

double hop_sec(double hop_km, double h_km)
{
    double th = hop_km / (2.0 * R_E);
    double horiz = R_E * sin(th);
    double vert  = (R_E + h_km) - R_E * cos(th);
    double t = horiz / vert;
    return sqrt(1.0 + t * t);
}

static double elev_deg(double hop_km, double h_km)
{
    double th = hop_km / (2.0 * R_E);
    if (th < 1e-6) return 90.0;
    return atan((cos(th) - R_E / (R_E + h_km)) / sin(th)) * 180.0 / M_PI;
}

double hop_max_km(double h_km)
{
    double lo = 100, hi = 8000;
    for (int i = 0; i < 40; i++) {              /* felezéses keresés */
        double mid = 0.5 * (lo + hi);
        if (elev_deg(mid, h_km) >= MIN_ELEV_DEG) lo = mid; else hi = mid;
    }
    return lo;
}

double hop_muf(double hop_km, double fof2, double md, double h_km)
{
    /* Úgy számolunk, hogy 3000 km-en pontosan a mért MUF(3000) jöjjön ki,
     * 0 km-en (függőleges) pedig foF2. */
    double s  = hop_sec(hop_km, h_km);
    double s3 = hop_sec(3000.0, h_km);
    return fof2 * (1.0 + (md - 1.0) * (s - 1.0) / (s3 - 1.0));
}

/* ---------- D-réteg elnyelés ---------- */

/* Napsugárzás okozta ionizáció egy pontban (0 éjjel) */
static double absorb_index(double cosx, double ssn)
{
    if (cosx > 1) cosx = 1;
    double chi = acos(cosx < -1 ? -1 : cosx);           /* radián */
    double c = cos(0.881 * chi);                         /* kb. 102 fokig nappal */
    double day = c > 0 ? pow(c, 1.3) : 0;
    /* éjjel is marad egy kis elnyelés (ez sok ugráson összeadódik) */
    return (1.0 + 0.0037 * ssn) * (day + NIGHT_ABSORB);
}

/* Elnyelés egy ugráson, dB (egyszerűsített ITU-R P.533 jellegű képlet) */
static double hop_absorb(double f, double hop_km, double I)
{
    double sec = hop_sec(hop_km, D_HEIGHT);
    if (sec > 4.0) sec = 4.0;
    return 677.2 * sec * I / (pow(f + 1.2, 1.98) + 10.2);
}

/* Teljes elnyelés az összes ugráson */
static double path_absorb(double f, double hop, const double *I, int nh)
{
    double A = 0;
    for (int k = 0; k < nh; k++) A += hop_absorb(f, hop, I[k]);
    return A;
}

/* ---------- útvonal ---------- */

void predict_path(const IonoModel *m, const PropData *d,
                  double qlat, double qlon, const Target *t, PathResult *r)
{
    memset(r, 0, sizeof *r);
    double tlat = t->lat, tlon = t->lon;
    double dist;

    if (t->nvis) {
        tlat = qlat; tlon = qlon;
        dist = 150.0;          /* jellemző NVIS távolság */
        r->bearing = 0;
    } else {
        dist = distance_km(qlat, qlon, tlat, tlon);
        r->bearing = bearing_deg(qlat, qlon, tlat, tlon);
        if (t->lp) {                                /* hosszú út: a Föld másik oldalán */
            dist = 2.0 * M_PI * 6371.0 - dist;
            r->bearing = fmod(r->bearing + 180.0, 360.0);
        }
    }
    r->dist_km = dist;

    /* pont az úton: frac 0..1 (NVIS-nél mindig a saját hely) */
    #define PATH_PT(frac, la, lo) target_path_point(qlat, qlon, t, (frac), (la), (lo))

    /* ugrások száma az út közepének hmF2 értéke alapján */
    IonoPoint mid;
    double la, lo;
    PATH_PT(0.5, &la, &lo);
    model_point(m, la, lo, &mid);
    double hmax = hop_max_km(mid.hmf2);
    int hops = (int)ceil(dist / hmax);
    if (hops < 1) hops = 1;
    double hop = dist / hops;
    r->hops = hops;
    r->hop_km = hop;

    /* F2 MUF: a legrosszabb vezérlőpont */
    double muf_f2 = 1e9, conf = 1.0;
    for (int k = 0; k < hops; k++) {
        IonoPoint p;
        PATH_PT((k + 0.5) / hops, &la, &lo);
        model_point(m, la, lo, &p);
        double mu = hop_muf(hop, p.fof2, p.md, p.hmf2);
        if (mu < muf_f2) muf_f2 = mu;
        if (p.conf < conf) conf = p.conf;
    }
    r->muf_f2 = muf_f2;
    r->conf = conf;

    /* E réteg MUF (nappal fontos, max. 2000 km-es ugrásokkal) */
    int ehops = (int)ceil(dist / E_HOP_MAX);
    if (ehops < 1) ehops = 1;
    double ehop = dist / ehops, muf_e = 1e9;
    for (int k = 0; k < ehops; k++) {
        IonoPoint p;
        PATH_PT((k + 0.5) / ehops, &la, &lo);
        model_point(m, la, lo, &p);
        double mu = p.foe * hop_sec(ehop, E_HEIGHT);
        if (mu < muf_e) muf_e = mu;
    }
    r->muf_e = muf_e;
    r->muf_raw = muf_f2 > muf_e ? muf_f2 : muf_e;
    r->muf = r->muf_raw * g_params.muf_scale;
    r->fot = 0.85 * r->muf;

    /* elnyelési tényező ugrásonként: a két ugrásvég átlaga */
    double Ihop[64];
    int nh = hops > 64 ? 64 : hops;
    for (int k = 0; k < nh; k++) {
        double a1, o1, a2, o2;
        PATH_PT((double)k / hops, &a1, &o1);
        PATH_PT((double)(k + 1) / hops, &a2, &o2);
        Ihop[k] = 0.5 * (absorb_index(sun_cos_zenith(a1, o1, m->t), m->ssn) +
                         absorb_index(sun_cos_zenith(a2, o2, m->t), m->ssn));
    }

    /* napkitörés: "legmagasabb érintett frekvencia" (NOAA D-RAP közelítés) */
    double haf = 0;
    /* csak a jelenre (egy órán belül) - a kitörés nem jelezhető előre */
    if (d->have_xray && d->solar.xray_flux >= FLARE_MIN_FLUX &&
        labs((long)(m->t - m->now)) < 3600)
        haf = 10.0 * log10(d->solar.xray_flux) + 65.0;
    /* a nappali ugrásvégeken a HAF alatti frekvenciák elnémulnak */
    r->haf_eff = 0;
    if (haf > 0) {
        for (int k = 0; k <= hops; k++) {
            double a1, o1;
            PATH_PT((double)k / hops, &a1, &o1);
            double c = sun_cos_zenith(a1, o1, m->t);
            if (c > 0 && haf * pow(c, 0.75) > r->haf_eff) r->haf_eff = haf * pow(c, 0.75);
        }
    }

    /* sarki fény övezete: a Kp-től függ, hol kezdődik */
    double maxml = 0;
    int nsteps = t->nvis ? 1 : (int)(dist / 300.0) + 2;
    for (int k = 0; k <= nsteps; k++) {
        PATH_PT((double)k / nsteps, &la, &lo);
        double ml = fabs(geomag_lat(la, lo));
        if (ml > maxml) maxml = ml;
    }
    int polar_pen = 0;
    double kp = kp_at(d, m->now, m->t);
    if (!isnan(kp) && kp >= 4.0 && maxml > 66.0 - 2.0 * kp) {
        r->polar = 1;
        polar_pen = kp >= 6.0 ? 2 : 1;
    }
    r->kp = kp;
    /* előre jelzett geomágneses vihar: a közepes és magas szélességen az F2
     * réteg "elvékonyodik" (negatív viharfázis), a MUF csökken. A mostani
     * állapotot az ionoszondák már mérik, ezért csak a jövőbeli, a mostaninál
     * erősebb vihart vesszük figyelembe. */
    if (!isnan(kp) && m->t > m->now + 5400 && kp >= 5.0 && maxml > 45.0) {
        double cur = d->have_kp ? d->solar.kp : 0;
        double extra = kp - (cur > 4.0 ? cur : 4.0);
        if (extra > 0) {
            double f = 1.0 - 0.06 * extra;
            if (f < 0.7) f = 0.7;
            r->muf *= f;
            r->muf_raw *= f;
            r->muf_f2 *= f;
            r->fot = 0.85 * r->muf;
        }
    }
    r->polar_pen = polar_pen;
    r->nh = nh > 8 ? 8 : nh;
    for (int k = 0; k < r->nh; k++) r->ihop[k] = Ihop[k];

    /* LUF: a legkisebb frekvencia, ahol az elnyelés még elviselhető */
    /* az elnyelés a frekvenciával csökken, ezért felezéses kereséssel */
    r->luf = 0;
    double lufA = g_params.abs_poor + mode_abs_offset(g_mode);
    if (path_absorb(1.5, hop, Ihop, nh) < lufA) {
        r->luf = 1.5;
    } else if (path_absorb(30.0, hop, Ihop, nh) < lufA) {
        double lo = 1.5, hi = 30.0;
        for (int it = 0; it < 12; it++) {
            double mid = 0.5 * (lo + hi);
            if (path_absorb(mid, hop, Ihop, nh) < lufA) hi = mid; else lo = mid;
        }
        r->luf = hi;
    }

    /* sávok értékelése */
    for (int b = 0; b < NBANDS; b++) {
        double ratio, A;
        r->grade[b] = predict_at_freq(r, BANDS[b].f, &ratio, &A);
        r->absorb[b] = A;
        if (r->haf_eff > 0 && BANDS[b].f < r->haf_eff) r->flare = 1;
    }
    #undef PATH_PT
}

void target_path_point(double qlat, double qlon, const Target *t, double frac,
                       double *lat, double *lon)
{
    if (t->nvis) { *lat = qlat; *lon = qlon; return; }
    if (t->lp) gc_point_lp(qlat, qlon, t->lat, t->lon, frac, lat, lon);
    else       gc_point(qlat, qlon, t->lat, t->lon, frac, lat, lon);
}

int sun_rise_set(double lat, double lon, time_t t, time_t *rise, time_t *set)
{
    const double h0 = -0.01454;                      /* sin(-0,833°) */
    /* a helyi (nap)idő szerinti éjfél */
    long off = (long)lround(lon / 15.0 * 3600.0);
    time_t local = t + off;
    time_t mid = local - (local % 86400 + 86400) % 86400 - off;
    *rise = *set = 0;
    double prev = sun_cos_zenith(lat, lon, mid) - h0;
    int up = 0, down = 0;
    for (int m = 2; m <= 24 * 60; m += 2) {
        time_t tt = mid + m * 60;
        double v = sun_cos_zenith(lat, lon, tt) - h0;
        if (prev <= 0 && v > 0 && !*rise) {
            *rise = tt - (time_t)(120.0 * v / (v - prev));   /* lineáris közelítés */
            up = 1;
        }
        if (prev > 0 && v <= 0 && !*set) {
            *set = tt - (time_t)(120.0 * v / (v - prev));
            down = 1;
        }
        prev = v;
    }
    if (up || down) return 0;
    return sun_cos_zenith(lat, lon, mid + 43200) > h0 ? 1 : -1;
}

double es_muf(double foes, double d_km)
{
    if (isnan(foes) || foes <= 0) return NAN;
    if (d_km > ES_MAX_HOP_KM) d_km = ES_MAX_HOP_KM;
    return foes * hop_sec(d_km, ES_HEIGHT);
}

double kp_at(const PropData *d, time_t now, time_t t)
{
    double cur = d->have_kp ? d->solar.kp : NAN;
    /* a közeljövőre a mért érték a jobb */
    if (!d->have_kpf || t < now + 5400) return cur;
    for (int i = 0; i < d->solar.nkpf; i++)
        if (t >= d->solar.kpf_t[i] && t < d->solar.kpf_t[i] + 3 * 3600) return d->solar.kpf[i];
    return cur;
}

int predict_at_freq(const PathResult *r, double f, double *ratio_raw, double *absorb_db)
{
    double A = 2.0 * (r->hops - 1);                 /* földvisszaverődés */
    for (int k = 0; k < r->nh; k++) A += hop_absorb(f, r->hop_km, r->ihop[k]);
    /* 8-nál több ugrásnál az utolsó tényezővel számolunk tovább (nagyon ritka) */
    for (int k = r->nh; k < r->hops; k++) A += hop_absorb(f, r->hop_km, r->ihop[r->nh - 1]);
    double ratio = f / r->muf_raw;
    if (ratio_raw) *ratio_raw = ratio;
    if (absorb_db) *absorb_db = A;
    int flare = r->haf_eff > 0 && f < r->haf_eff;
    return predict_grade(ratio, A, r->polar_pen, flare);
}

void predict_forecast(IonoModel *m, const PropData *d, double qlat, double qlon,
                      const Target *t, time_t t0, Forecast *fc)
{
    time_t keep = m->t;
    fc->t0 = t0;
    for (int h = 0; h < FC_HOURS; h++) {
        PathResult r;
        model_set_time(m, t0 + h * 3600 + 1800);   /* az óra közepére */
        predict_path(m, d, qlat, qlon, t, &r);
        fc->muf[h] = r.muf;
        fc->luf[h] = r.luf;
        fc->kp[h] = r.kp;
        for (int b = 0; b < NBANDS; b++) fc->grade[h][b] = r.grade[b];
    }
    model_set_time(m, keep);
}
