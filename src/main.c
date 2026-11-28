/*
 * IonoProp - ionoszféra-állapot és sávjavaslat rádióamatőröknek
 *
 *   - ionoszonda-mérések (foF2, MUF(3000), hmF2) a KC2G / GIRO hálózatból
 *   - napfluxus (SFI), Kp-index, röntgenfluxus a NOAA SWPC-től
 *   - rétegek állapota a QTH fölött, MUF/LUF és sávjavaslat célterületekre
 *
 * Használat:  ionoprop [-l LOKÁTOR] [-n DB] [-a ÓRA] [-c MIN] [-T LOK] [-f] [-s] [-C]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#ifdef _WIN32
#include <getopt.h>
#endif

#include "data.h"
#include "loader.h"
#include "parse.h"
#include "geo.h"
#include "model.h"
#include "predict.h"
#include "config.h"
#include "pskr.h"
#include "validate.h"
#include "compat.h"
#include "fetch.h"
#include "targets.h"
#include "config.h"
#include <signal.h>

#define VERSION "0.9.1"
#define DEFAULT_LOCATOR "JN97mm"   /* Budapest */

typedef struct {
    char   locator[12];
    double lat, lon;
    int    show_n;      /* hány ionoszondát listázzon */
    int    max_age_h;   /* ennél régebbi mérést nem mutat */
    int    min_cs;      /* minimális megbízhatóság (a kézi, -1 mindig jó) */
    int    force;       /* gyorsítótár figyelmen kívül hagyása */
    int    stations_only; /* csak az adatok, számítás nélkül */
    int    color;       /* színes kiírás */
    Target extra[8];    /* -T kapcsolóval megadott saját célok */
    int    n_extra;
    char   forecast[16];/* -F: 24 órás előrejelzés erre a célra (sorszám vagy lokátor) */
    int    live_min;    /* -P: élő PSK Reporter gyűjtés ennyi percig */
    int    val_days;    /* -V: kiértékelés a naplóból, ennyi napra visszamenőleg */
    int    write_cfg;   /* -W: a javasolt paraméterek mentése */
    char   mqtt_host[128];
    int    mqtt_port;
    double radius;      /* -R: a QTH körüli körzet a spotokhoz, km */
} Options;

static PropData g_data;   /* nagy struktúra, ezért nem a veremben */

static void usage(const char *prog)
{
    printf("IonoProp %s - ionoszféra-állapot és sávjavaslat\n\n", VERSION);
    printf("Használat: %s [kapcsolók]\n\n", prog);
    printf("  -l LOKÁTOR  saját QTH lokátor (alap: a grafikus programban beállított, vagy %s)\n",
           DEFAULT_LOCATOR);
    printf("  -n DB       ennyi legközelebbi ionoszondát mutat (alap: 8)\n");
    printf("  -a ÓRA      ennél régebbi mérést nem mutat (alap: 6)\n");
    printf("  -c MIN      minimális megbízhatóság 0-100 (alap: 25)\n");
    printf("  -T CÉL      célterület hozzáadása: lokátor, hívójel vagy prefix (többször is)\n");
    printf("  -u MÓD      üzemmód: SSB, CW vagy FT8 (alap: a grafikus programban választott)\n");
    printf("  -F CÉL      24 órás előrejelzés: a cél sorszáma (-F list) vagy lokátora\n");
    printf("\n  Ellenőrzés valódi vételekkel (PSK Reporter):\n");
    printf("  -P PERC     élő gyűjtés ennyi percig (Ctrl+C-vel előbb is leáll), utána kiértékelés\n");
    printf("  -V NAP      kiértékelés az eddig gyűjtött naplóból (ennyi napra visszamenőleg)\n");
    printf("  -W          a javasolt paraméterek mentése (-P vagy -V mellé)\n");
    printf("  -R KM       a QTH körüli körzet a spotokhoz (alap: 300 km)\n");
    printf("  -M HOST[:PORT]  MQTT szerver (alap: %s:%d)\n\n", PSKR_HOST, PSKR_PORT);
    printf("  -s          csak az adatok, sávjavaslat nélkül\n");
    printf("  -C          színek kikapcsolása\n");
    printf("  -f          letöltés akkor is, ha friss adat van a gyorsítótárban\n");
    printf("  -h          ez a súgó\n");
}

static void fmt_age(time_t t, char *buf, size_t len)
{
    if (!t) { snprintf(buf, len, "?"); return; }
    long m = (long)(time(NULL) - t) / 60;
    if (m < 0) m = 0;
    if (m < 120) snprintf(buf, len, "%ld p", m);
    else         snprintf(buf, len, "%ld ó", m / 60);
}

static void fmt_val(double v, const char *fmt, char *buf, size_t len)
{
    if (isnan(v) || v <= 0) snprintf(buf, len, "-");
    else snprintf(buf, len, fmt, v);
}

/* Oszlop kiírása 'width' karakter szélesen. UTF-8 betűket számol, nem
 * bájtokat, így az ékezetes szövegek sem csúsztatják el a táblázatot.
 * right != 0: jobbra igazítás. A túl hosszú szöveget levágja. */
static void put_col(const char *s, int width, int right)
{
    int chars = 0;
    const char *p = s, *cut = NULL;
    while (*p) {
        if (chars == width) { cut = p; break; }
        p++;
        while ((*p & 0xC0) == 0x80) p++;     /* UTF-8 folytató bájtok */
        chars++;
    }
    int bytes = (int)((cut ? cut : p) - s);
    int pad = width - chars;
    if (right) printf("%*s%.*s ", pad, "", bytes, s);
    else       printf("%.*s%*s ", bytes, s, pad, "");
}

static const char *sfi_text(double sfi)
{
    if (sfi < 80)  return "alacsony - a felső sávok (15-10 m) ritkán nyitnak";
    if (sfi < 120) return "közepes";
    if (sfi < 180) return "jó - a felső sávok nappal nyitnak";
    return "nagyon jó - 10 m is rendszeresen nyit";
}

static const char *kp_text(double kp)
{
    if (kp < 3) return "nyugodt";
    if (kp < 4) return "enyhén zavart";
    if (kp < 5) return "zavart - a sarki utak gyengülnek";
    return "geomágneses VIHAR - gyenge HF terjedés";
}

static const char *xray_text(const char *cls)
{
    switch (cls[0]) {
    case 'M': return "napkitörés! nappali oldalon rövid HF-elnémulás lehet";
    case 'X': return "ERŐS napkitörés! nappali oldalon HF-elnémulás";
    default:  return "nyugodt";
    }
}

static void print_report(const Options *o, const PropData *d)
{
    char tbuf[64];
    time_t now = time(NULL);
    strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M UTC", gmtime(&now));

    printf("\n=== IonoProp %s ===  %s   QTH: %s (%.2f, %.2f)\n\n",
           VERSION, tbuf, o->locator, o->lat, o->lon);

    printf("--- Napaktivitás (NOAA SWPC) ---\n");
    if (d->have_sfi)
        printf("  SFI (10,7 cm):  %.0f sfu  (90 napos átlag: %.0f)  %s\n",
               d->solar.sfi, d->solar.sfi_90day, sfi_text(d->solar.sfi));
    else
        printf("  SFI:            nincs adat\n");
    if (d->have_kp)
        printf("  Kp-index:       %.2f   %s\n", d->solar.kp, kp_text(d->solar.kp));
    else
        printf("  Kp-index:       nincs adat\n");
    if (d->have_xray)
        printf("  Röntgen (GOES): %s (%.1e W/m²)   %s\n", d->solar.xray_class,
               d->solar.xray_flux, xray_text(d->solar.xray_class));
    else
        printf("  Röntgen:        nincs adat\n");

    printf("\n--- Legközelebbi ionoszondák (KC2G / GIRO) ---\n");
    if (!d->have_stations) {
        printf("  nincs adat\n\n");
        return;
    }
    /* oszlopok: szöveg, szélesség, jobbra igazítás */
    static const struct { const char *h; int w, r; } col[] = {
        {"Kód", 6, 0}, {"Állomás", 20, 0}, {"km", 6, 1}, {"irány", 5, 1},
        {"kor", 6, 1}, {"foF2", 6, 1}, {"MUF3000", 7, 1}, {"hmF2", 5, 1}, {"cs", 4, 1},
    };
    printf("  ");
    for (size_t k = 0; k < sizeof col / sizeof col[0]; k++)
        put_col(col[k].h, col[k].w, col[k].r);
    printf("\n");

    int shown = 0, skipped = 0;
    for (int i = 0; i < d->count && shown < o->show_n; i++) {
        const IonoStation *s = &d->st[i];
        long age_s = s->time ? (long)(now - s->time) : -1;
        int cs_ok = (s->cs == -1) || (s->cs >= o->min_cs);

        if (age_s < 0 || age_s > o->max_age_h * 3600L || !cs_ok) {
            skipped++;
            continue;
        }
        char v[9][64];
        snprintf(v[0], sizeof v[0], "%s", s->code);
        snprintf(v[1], sizeof v[1], "%s", s->name);
        snprintf(v[2], sizeof v[2], "%.0f", s->dist_km);
        snprintf(v[3], sizeof v[3], "%.0f°", bearing_deg(o->lat, o->lon, s->lat, s->lon));
        fmt_age(s->time, v[4], sizeof v[4]);
        fmt_val(s->fof2, "%.2f", v[5], sizeof v[5]);
        fmt_val(s->mufd, "%.2f", v[6], sizeof v[6]);
        fmt_val(s->hmf2, "%.0f", v[7], sizeof v[7]);
        if (s->cs == -1) snprintf(v[8], sizeof v[8], "kézi");
        else             snprintf(v[8], sizeof v[8], "%d", s->cs);

        printf("  ");
        for (size_t k = 0; k < 9; k++)
            put_col(v[k], col[k].w, col[k].r);
        printf("%s\n", age_s > 3600 ? "*" : "");
        shown++;
    }
    if (skipped)
        printf("  (%d régi vagy bizonytalan mérést kihagytam)\n", skipped);
    printf("  * = egy óránál régebbi mérés\n\n");
}

/* Lokátor szabványos alakra: KN07sp */
static void normalize_locator(const char *in, char *out, size_t len)
{
    size_t i;
    for (i = 0; in[i] && i + 1 < len; i++)
        out[i] = (i < 2) ? (char)toupper((unsigned char)in[i])
               : (i < 4 || i >= 6) ? in[i] : (char)tolower((unsigned char)in[i]);
    out[i] = '\0';
}

/* ---------- színek (ANSI) ---------- */
static int g_color;
#define C_RESET  (g_color ? "\033[0m"  : "")
#define C_BOLD   (g_color ? "\033[1m"  : "")
#define C_DIM    (g_color ? "\033[2m"  : "")

static const char *grade_cell(int g)
{
    if (!g_color) {
        static const char *t[] = {"  . ", "  + ", " ++ ", "+++ "};
        return t[g];
    }
    static const char *c[] = {
        "\033[2m  · \033[0m",
        "\033[33m  ▪ \033[0m",
        "\033[1;33m ▪▪ \033[0m",
        "\033[1;32m▪▪▪ \033[0m",
    };
    return c[g];
}

static const char *day_text(double cosx)
{
    if (cosx > 0.2)   return "nappal";
    if (cosx > -0.05) return "napkelte/napnyugta";
    if (cosx > -0.3)  return "szürkület";
    return "éjszaka";
}

static void print_prediction(const Options *o, const PropData *d)
{
    g_color = o->color;
    time_t now = time(NULL);
    IonoModel *m = malloc(sizeof *m);
    if (!m) return;
    model_init(m, d, now, 3 * 3600, o->min_cs);

    /* --- rétegek a QTH fölött --- */
    IonoPoint q;
    model_point(m, o->lat, o->lon, &q);
    printf("--- Rétegek a QTH fölött (%s) ---\n", day_text(q.cosx));
    printf("  D réteg:  %s\n", q.cosx > 0.05
           ? "van - nappal elnyeli az alsó sávokat (160/80 m)"
           : "nincs - az alsó sávok nyitottak");
    printf("  E réteg:  foE  %.1f MHz%s\n", q.foe, q.cosx > 0.05 ? "" : " (éjjel elhanyagolható)");
    printf("  F2 réteg: foF2 %.2f MHz   hmF2 %.0f km   MUF(3000) %.1f MHz\n",
           q.fof2, q.hmf2, q.mufd);
    printf("  %sAlapja: %d ionoszonda, legközelebbi %.0f km, mérési arány %.0f%%%s\n\n",
           C_DIM, m->nref, q.near_km, q.conf * 100, C_RESET);

    /* --- célterületek --- */
    Target tg[TARGETS_MAX];
    int nt = predict_default_targets(tg, TARGETS_MAX - TARGETS_CUSTOM_MAX - 1);
    nt += targets_load_custom(&tg[nt], TARGETS_CUSTOM_MAX);      /* a grafikus programban felvettek */
    for (int i = 0; i < o->n_extra && nt < TARGETS_MAX; i++) tg[nt++] = o->extra[i];

    printf("--- Sávjavaslat most ---\n  ");
    put_col("Cél", 22, 0); put_col("km", 5, 1); put_col("irány", 5, 1);
    put_col("MUF", 5, 1); put_col("LUF", 4, 1);
    printf(" ");
    for (int b = 0; b < NBANDS; b++) put_col(BANDS[b].name, 3, 1);
    printf("\n");

    /* csoportok: 0 = közeli, 1 = Európa (< 4000 km), 2 = DX */
    int gsum[3][NBANDS] = {{0}}, gcnt[3] = {0};
    for (int i = 0; i < nt; i++) {
        PathResult r;
        predict_path(m, d, o->lat, o->lon, &tg[i], &r);

        char v[5][32];
        snprintf(v[0], sizeof v[0], "%.0f", r.dist_km);
        if (tg[i].nvis) snprintf(v[1], sizeof v[1], "-");
        else snprintf(v[1], sizeof v[1], "%.0f°", r.bearing);
        snprintf(v[2], sizeof v[2], "%.1f", r.muf);
        if (r.luf > 0) snprintf(v[3], sizeof v[3], "%.1f", r.luf);
        else snprintf(v[3], sizeof v[3], ">30");

        printf("  ");
        put_col(tg[i].name, 22, 0);
        put_col(v[0], 5, 1); put_col(v[1], 5, 1);
        put_col(v[2], 5, 1); put_col(v[3], 4, 1);
        printf(" ");
        for (int b = 0; b < NBANDS; b++) {
            printf("%s", grade_cell(r.grade[b]));
            int gr = tg[i].nvis ? 0 : r.dist_km < 4000 ? 1 : 2;
            gsum[gr][b] += r.grade[b];
        }
        /* megjegyzések */
        if (r.polar) printf("%s sarki%s", C_DIM, C_RESET);
        if (r.flare) printf("%s fler%s", C_DIM, C_RESET);
        if (r.conf < 0.3) printf("%s ~%s", C_DIM, C_RESET);
        printf("\n");
        gcnt[tg[i].nvis ? 0 : r.dist_km < 4000 ? 1 : 2]++;
    }
    printf("  Jelölés: ");
    for (int g = 3; g >= 0; g--) {
        static const char *lbl[] = {"zárva", "gyenge", "közepes", "jó"};
        printf("%s%s  ", grade_cell(g), lbl[g]);
    }
    printf("\n  %ssarki = a sarki fény övezetén át (geomágneses zavar)   fler = napkitörés\n"
           "  ~ = nincs közeli ionoszonda, csak becslés%s\n\n", C_DIM, C_RESET);

    /* --- összefoglaló: csoportonként a legjobb sávok --- */
    static const char *gname[3] = {"Közeli (0-300 km)", "Európa", "DX"};
    printf("%s--- Javaslat ---%s\n", C_BOLD, C_RESET);
    for (int g = 0; g < 3; g++) {
        if (!gcnt[g]) continue;
        printf("  ");
        put_col(gname[g], 18, 0);
        int shown = 0;
        /* legalább "közepes" átlagú sávok, a magasabb frekvencia előre
         * (kisebb zaj és elnyelés), legfeljebb 3 */
        for (int lvl = 3; lvl >= 2 && shown < 3; lvl--)
            for (int bb = NBANDS - 1; bb >= 0 && shown < 3; bb--) {
                double avg = (double)gsum[g][bb] / gcnt[g];
                int a2 = avg >= 2.5 ? 3 : avg >= 1.75 ? 2 : 0;
                if (a2 != lvl) continue;
                printf("%s%s%s (%s)  ", lvl == 3 ? C_BOLD : "", BANDS[bb].name, C_RESET,
                       lvl == 3 ? "jó" : "közepes");
                shown++;
            }
        if (!shown) {
            /* ha nincs jó/közepes, a legjobb "gyenge" sáv: érdemes próbálni */
            int best = -1;
            double bavg = 0.75;
            for (int bb = 0; bb < NBANDS; bb++) {
                double avg = (double)gsum[g][bb] / gcnt[g];
                if (avg >= bavg) { bavg = avg; best = bb; }
            }
            if (best >= 0)
                printf("%s (gyenge, de próbálható)", BANDS[best].name);
            else
                printf("most nincs használható sáv");
        }
        printf("\n");
    }
    printf("\n");
    free(m);
}

/* ---------- 24 órás előrejelzés ---------- */

static const char *fc_cell(int g)
{
    if (!g_color) {
        static const char *t[] = {" . ", " - ", " + ", " # "};
        return t[g];
    }
    static const char *c[] = {
        "\033[2m · \033[0m",
        "\033[33m░░ \033[0m",
        "\033[1;33m▓▓ \033[0m",
        "\033[1;32m██ \033[0m",
    };
    return c[g];
}

static int print_forecast(const Options *o, const PropData *d)
{
    g_color = o->color;
    Target tg[TARGETS_MAX];
    int nt = predict_default_targets(tg, TARGETS_MAX - TARGETS_CUSTOM_MAX - 1);
    nt += targets_load_custom(&tg[nt], TARGETS_CUSTOM_MAX);      /* a grafikus programban felvettek */
    for (int i = 0; i < o->n_extra && nt < TARGETS_MAX; i++) tg[nt++] = o->extra[i];

    const Target *t = NULL;
    Target custom;
    char *end;
    long idx = strtol(o->forecast, &end, 10);
    if (*end == '\0' && idx >= 1 && idx <= nt) {
        t = &tg[idx - 1];
    } else if (locator_to_latlon(o->forecast, &custom.lat, &custom.lon) == 0) {
        memset(&custom, 0, sizeof custom);
        locator_to_latlon(o->forecast, &custom.lat, &custom.lon);
        normalize_locator(o->forecast, custom.name, sizeof custom.name);
        t = &custom;
    }
    if (!t) {
        printf("Célterületek (-F SORSZÁM), vagy adj meg lokátort (-F FN20):\n");
        for (int i = 0; i < nt; i++) printf("  %2d  %s\n", i + 1, tg[i].name);
        return strcmp(o->forecast, "list") == 0 ? 0 : 1;
    }

    time_t now = time(NULL);
    IonoModel *m = malloc(sizeof *m);
    Forecast *fc = malloc(sizeof *fc);
    if (!m || !fc) { free(m); free(fc); return 1; }
    model_init(m, d, now, 3 * 3600, o->min_cs);
    predict_forecast(m, d, o->lat, o->lon, t, now - now % 3600, fc);

    PathResult r;
    predict_path(m, d, o->lat, o->lon, t, &r);
    printf("\n=== 24 órás előrejelzés: %s ===\n", t->name);
    if (t->nvis) printf("  QTH: %s, közeli összeköttetések (0-300 km)\n", o->locator);
    else printf("  QTH: %s   távolság: %.0f km   irány: %.0f°\n", o->locator, r.dist_km, r.bearing);
    printf("  %s(helyi idő; az első órák a mérésekre, a későbbiek egyre inkább a modellre épülnek)%s\n\n",
           C_DIM, C_RESET);

    /* fejléc: órák helyi időben */
    printf("  %-5s", "óra");
    for (int h = 0; h < FC_HOURS; h++) {
        time_t th = fc->t0 + h * 3600;
        printf("%3d", localtime(&th)->tm_hour);
    }
    printf("\n");
    for (int b = NBANDS - 1; b >= 0; b--) {
        printf("  %-5s", BANDS[b].name);
        for (int h = 0; h < FC_HOURS; h++) printf("%s", fc_cell(fc->grade[h][b]));
        printf("\n");
    }
    printf("  %-5s", "MUF");
    for (int h = 0; h < FC_HOURS; h++) printf("%3.0f", fc->muf[h]);
    printf("\n  %-5s", "LUF");
    for (int h = 0; h < FC_HOURS; h++) {
        if (fc->luf[h] > 0) printf("%3.0f", fc->luf[h]); else printf("  -");
    }
    printf("\n\n  Jelölés: %sjó  %sközepes  %sgyenge  %szárva\n\n",
           fc_cell(3), fc_cell(2), fc_cell(1), fc_cell(0));
    free(fc);
    free(m);
    return 0;
}

/* ---------- ellenőrzés valódi vételekkel ---------- */

static void report_and_save(const Options *o, const SpotEval *e, int n)
{
    ValReport r;
    validate_report(e, n, &r);
    validate_print(&r, stdout);
    if (o->write_cfg) {
        if (!r.have_suggest) {
            printf("Nem mentettem: még kevés az adat a javaslathoz.\n");
        } else {
            char note[96];
            snprintf(note, sizeof note, "%d spot alapján (%s)", r.n, o->locator);
            if (config_save(&r.suggest, note) == 0) {
                char path[600];
                config_path(path, sizeof path);
                printf("Elmentve: %s\n", path);
            } else {
                printf("Hiba: nem tudtam menteni a beállításokat.\n");
            }
        }
    }
}

static int run_validate(const Options *o)
{
    SpotEval *e = NULL;
    int n = validate_log_load(time(NULL) - (time_t)o->val_days * 86400, &e);
    if (n < 0) { fprintf(stderr, "Nem tudom olvasni a naplót.\n"); return 1; }
    if (n == 0) {
        printf("A naplóban nincs spot az utolsó %d napból.\n"
               "Gyűjts előbb: ionoprop -l %s -P 60   (vagy hagyd futni a grafikus programot)\n",
               o->val_days, o->locator);
        free(e);
        return 0;
    }
    report_and_save(o, e, n);
    free(e);
    return 0;
}

static volatile sig_atomic_t g_stop;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

static int run_live(const Options *o, PropData *d)
{
    Pskr *p = pskr_start(o->mqtt_host, o->mqtt_port, o->lat, o->lon, o->radius);
    if (!p) {
        fprintf(stderr, "A PSK Reporter gyűjtő nem indult el (libmosquitto nélkül fordítva?).\n");
        return 1;
    }
    signal(SIGINT, on_sigint);
    validate_log_trim(14);

    IonoModel *m = malloc(sizeof *m);
    PskSpot *buf = malloc(sizeof(PskSpot) * 1024);
    SpotEval *ev = malloc(sizeof(SpotEval) * 1024);
    SpotEval *sess = NULL;
    int nsess = 0, cap = 0;
    if (!m || !buf || !ev) return 1;
    model_init(m, d, time(NULL), 3 * 3600, o->min_cs);

    time_t start = time(NULL), end = start + (time_t)o->live_min * 60;
    time_t last_print = start, last_model = start, last_load = start;
    int cnt[NBANDS] = {0}, cnt_min = 0;

    printf("PSK Reporter gyűjtés %d percig, QTH %s, körzet %.0f km (Ctrl+C: leállítás)\n",
           o->live_min, o->locator, o->radius);
    {
        char dir[512];
        if (cache_dir(dir, sizeof dir) == 0)
            printf("A spotok a %s/spots_eval.csv naplóba is kerülnek.\n\n", dir);
    }

    while (!g_stop && time(NULL) < end) {
        usleep(500000);
        int n = pskr_poll(p, buf, 1024);
        for (int i = 0; i < n; i++) {
            validate_eval(m, d, &buf[i], &ev[i]);
            cnt[buf[i].band]++;
            cnt_min++;
            if (nsess == cap) {
                cap = cap ? cap * 2 : 4096;
                SpotEval *nv = realloc(sess, (size_t)cap * sizeof *nv);
                if (!nv) { g_stop = 1; break; }
                sess = nv;
            }
            sess[nsess++] = ev[i];
        }
        if (n > 0) validate_log_append(ev, n);

        time_t now = time(NULL);
        if (now - last_load >= 15 * 60) {           /* friss ionoszonda-adatok */
            load_all(d, o->lat, o->lon, 0);
            last_load = now;
            last_model = 0;
        }
        if (now - last_model >= 60) {
            model_init(m, d, now, 3 * 3600, o->min_cs);
            last_model = now;
        }
        if (now - last_print >= 60) {
            char tb[16], st[160];
            strftime(tb, sizeof tb, "%H:%M", localtime(&now));
            pskr_status(p, st, sizeof st);
            printf("%s  %4d új spot  ", tb, cnt_min);
            for (int b = 0; b < NBANDS; b++)
                if (cnt[b]) printf(" %s:%d", BANDS[b].name, cnt[b]);
            printf("\n       %s\n", st);
            fflush(stdout);
            memset(cnt, 0, sizeof cnt);
            cnt_min = 0;
            last_print = now;
        }
    }
    pskr_stop(p);
    signal(SIGINT, SIG_DFL);

    printf("\nGyűjtés vége: %d spot ebben a menetben.\n", nsess);
    report_and_save(o, sess, nsess);
    free(sess);
    free(ev);
    free(buf);
    free(m);
    return 0;
}

int main(int argc, char **argv)
{
    Options o = { .show_n = 8, .max_age_h = 6, .min_cs = 25, .force = 0 };
    snprintf(o.locator, sizeof o.locator, "%s", DEFAULT_LOCATOR);

    int c, loc_given = 0;
    console_init();
    o.color = isatty(STDOUT_FILENO);
    int mode_given = 0;
    while ((c = getopt(argc, argv, "l:n:a:c:T:F:P:V:WR:M:u:fsCh")) != -1) {
        switch (c) {
        case 'l': snprintf(o.locator, sizeof o.locator, "%s", optarg); loc_given = 1; break;
        case 'n': o.show_n = atoi(optarg); break;
        case 'a': o.max_age_h = atoi(optarg); break;
        case 'c': o.min_cs = atoi(optarg); break;
        case 'f': o.force = 1; break;
        case 's': o.stations_only = 1; break;
        case 'F': snprintf(o.forecast, sizeof o.forecast, "%s", optarg); break;
        case 'C': o.color = 0; break;
        case 'P': o.live_min = atoi(optarg); break;
        case 'V': o.val_days = atoi(optarg); if (o.val_days < 1) o.val_days = 1; break;
        case 'W': o.write_cfg = 1; break;
        case 'R': o.radius = atof(optarg); break;
        case 'M': {
            char *colon;
            snprintf(o.mqtt_host, sizeof o.mqtt_host, "%s", optarg);
            if ((colon = strrchr(o.mqtt_host, ':')) != NULL) { *colon = '\0'; o.mqtt_port = atoi(colon + 1); }
            break;
        }
        case 'T':
            if (o.n_extra < 8) {
                Target *t = &o.extra[o.n_extra];
                char err[160];
                if (target_resolve(optarg, t, err, sizeof err) != 0) {
                    fprintf(stderr, "%s\n", err);
                    return 1;
                }
                o.n_extra++;
            }
            break;
        case 'u':
            if (mode_from_name(optarg) < 0) { fprintf(stderr, "Ismeretlen üzemmód: %s (SSB, CW, FT8)\n", optarg); return 1; }
            g_mode = mode_from_name(optarg);
            mode_given = 1;
            break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }
    if (!mode_given) {
        char v[16];
        if (config_get("mode", v, sizeof v) && mode_from_name(v) >= 0) g_mode = mode_from_name(v);
    }
    /* -l nélkül a grafikus programban beállított (mentett) lokátor */
    if (!loc_given) config_load_locator(o.locator, sizeof o.locator);
    if (locator_to_latlon(o.locator, &o.lat, &o.lon) != 0) {
        fprintf(stderr, "Hibás lokátor: %s\n", o.locator);
        return 1;
    }
    {
        char tmp[12];
        normalize_locator(o.locator, tmp, sizeof tmp);
        snprintf(o.locator, sizeof o.locator, "%s", tmp);
    }
    if (o.show_n < 1) o.show_n = 1;
    if (o.radius <= 0) o.radius = 300.0;
    if (!o.mqtt_host[0]) snprintf(o.mqtt_host, sizeof o.mqtt_host, "%s", PSKR_HOST);
    if (o.mqtt_port <= 0) o.mqtt_port = PSKR_PORT;

    /* hangolt paraméterek (ha vannak) */
    config_load(&g_params);

    if (o.val_days > 0 && o.live_min <= 0)
        return run_validate(&o);

    PropData *d = &g_data;
    load_all(d, o.lat, o.lon, o.force);
    if (o.live_min > 0)
        return run_live(&o, d);
    if (o.forecast[0])
        return print_forecast(&o, d);
    print_report(&o, d);
    if (!o.stations_only)
        print_prediction(&o, d);

    return (d->have_stations || d->have_sfi || d->have_kp || d->have_xray) ? 0 : 2;
}
