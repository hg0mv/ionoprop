/*
 * validate.c - az előrejelzés ellenőrzése valódi spotokkal (lásd validate.h)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include "validate.h"
#include "i18n.h"
#include "fetch.h"
#include "geo.h"
#include "compat.h"

#define MIN_SPOTS   300     /* ennyi spot alatt nem adunk hangolási javaslatot */
#define MIN_STRONG  100
#define MIN_LONG    100     /* ennyi hosszú (> 2000 km) út kell a MUF-hangoláshoz */
#define MIN_SPAN_H  6       /* legalább ennyi órányi adat kell a javaslathoz */
#define LONG_KM     2000.0  /* e fölött az F2 réteg biztosan (Es egy ugrással kb. 2300 km-ig) */
#define ES_KM       2300.0

void validate_eval(IonoModel *m, const PropData *d, const PskSpot *s, SpotEval *e)
{
    time_t keep = m->t;
    model_set_time(m, s->t);

    Target t;
    memset(&t, 0, sizeof t);
    t.lat = s->flat;
    t.lon = s->flon;
    t.nvis = s->dist_km < 300.0;
    PathResult r;
    predict_path(m, d, s->nlat, s->nlon, &t, &r);
    predict_at_freq(&r, s->f_mhz, &e->ratio_raw, &e->absorb);

    e->t = s->t;
    e->band = s->band;
    e->f = s->f_mhz;
    e->snr = s->snr;
    snprintf(e->mode, sizeof e->mode, "%s", s->mode);
    e->dist = s->dist_km;
    e->polar_pen = r.polar_pen;
    e->flare = r.haf_eff > 0 && s->f_mhz < r.haf_eff;
    e->conf = r.conf;

    model_set_time(m, keep);
}

int validate_is_es(const SpotEval *e)
{
    return e->band >= B20 && e->dist >= PSKR_MIN_KM && e->dist < ES_KM &&
           e->ratio_raw / g_params.muf_scale > 1.3;
}

int validate_grade(const SpotEval *e)
{
    /* az ellenőrzés és a hangolás mindig az alap (SSB) határokkal számol,
     * hogy a választott üzemmód ne torzítsa a statisztikát */
    return predict_grade_mode(e->ratio_raw, e->absorb, e->polar_pen, e->flare, MODE_SSB);
}

/* ---------- napló ---------- */

static int log_path(char *buf, size_t len)
{
    char dir[512];
    if (cache_dir(dir, sizeof dir) != 0) return -1;
    snprintf(buf, len, "%s/spots_eval.csv", dir);
    return 0;
}

int validate_log_append(const SpotEval *e, int n)
{
    char path[600];
    if (n <= 0 || log_path(path, sizeof path) != 0) return -1;
    struct stat st;
    int is_new = stat(path, &st) != 0;
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    if (is_new) fprintf(f, "time,band,freq_mhz,snr,mode,dist_km,ratio_raw,absorb_db,polar,flare,conf\n");
    for (int i = 0; i < n; i++) {
        char mode[12];
        snprintf(mode, sizeof mode, "%s", e[i].mode);
        for (char *c = mode; *c; c++) if (*c == ',' || *c == '\n') *c = '_';
        fprintf(f, "%ld,%d,%.4f,%d,%s,%.0f,%.4f,%.2f,%d,%d,%.2f\n", (long)e[i].t, e[i].band,
                e[i].f, e[i].snr, mode, e[i].dist, e[i].ratio_raw, e[i].absorb,
                e[i].polar_pen, e[i].flare, e[i].conf);
    }
    return fclose(f);
}

static int parse_line(const char *line, SpotEval *e)
{
    long t;
    memset(e, 0, sizeof *e);
    int n = sscanf(line, "%ld,%d,%lf,%d,%11[^,],%lf,%lf,%lf,%d,%d,%lf", &t, &e->band, &e->f,
                   &e->snr, e->mode, &e->dist, &e->ratio_raw, &e->absorb, &e->polar_pen,
                   &e->flare, &e->conf);
    if (n != 11) return -1;
    /* a sáv sorszáma verziónként változhat (pl. a 11 m beillesztésekor),
     * ezért a frekvenciából számoljuk újra */
    e->band = pskr_band(e->f);
    if (e->band < 0 || e->band >= NBANDS) return -1;
    e->t = (time_t)t;
    return 0;
}

int validate_log_load(time_t since, SpotEval **out)
{
    char path[600];
    *out = NULL;
    if (log_path(path, sizeof path) != 0) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int n = 0, cap = 0;
    SpotEval *v = NULL;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        SpotEval e;
        if (parse_line(line, &e) != 0 || e.t < since) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 4096;
            SpotEval *nv = realloc(v, (size_t)cap * sizeof *v);
            if (!nv) break;
            v = nv;
        }
        v[n++] = e;
    }
    fclose(f);
    *out = v;
    return n;
}

void validate_log_trim(int days)
{
    char path[600], tmp[620];
    struct stat st;
    if (log_path(path, sizeof path) != 0 || stat(path, &st) != 0) return;
    if (st.st_size < 20L * 1024 * 1024) return;       /* 20 MB alatt nem kell */

    time_t since = time(NULL) - (time_t)days * 86400;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *in = fopen(path, "r"), *out = fopen(tmp, "w");
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return; }
    char line[256];
    int first = 1;
    while (fgets(line, sizeof line, in)) {
        SpotEval e;
        if (first) { fputs(line, out); first = 0; continue; }   /* fejléc */
        if (parse_line(line, &e) == 0 && e.t >= since) fputs(line, out);
    }
    fclose(in);
    if (fclose(out) == 0) replace_file(tmp, path);
}

/* ---------- statisztika ---------- */

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double percentile(double *v, int n, double p)
{
    if (n <= 0) return 0;
    qsort(v, (size_t)n, sizeof *v, cmp_d);
    int i = (int)(p * (n - 1) + 0.5);
    return v[i];
}

void validate_report(const SpotEval *e, int n, ValReport *r)
{
    memset(r, 0, sizeof *r);
    double *ratios = malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    double *abs_all = malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    double *abs_strong = malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
    int nr = 0, na = 0, ns = 0;

    for (int i = 0; i < n; i++) {
        const SpotEval *s = &e[i];
        if (!r->t_first || s->t < r->t_first) r->t_first = s->t;
        if (s->t > r->t_last) r->t_last = s->t;
        if (s->dist < 100.0) { r->n_ground++; continue; }   /* régi naplóban lehet ilyen */

        /* rövid úton jóval a MUF fölött, 20 m-en vagy feljebb: nem F2 terjedés
         * (sporadikus E, szórás, hibás lokátor) - ezt a program nem modellezi,
         * ezért se a statisztikába, se a hangolásba nem számít bele */
        if (validate_is_es(s)) {
            r->n_es++;
            continue;
        }
        int g = validate_grade(s);
        r->n++;
        r->cnt[s->band][g]++;
        r->snr[s->band][g] += s->snr;
        r->tot[g]++;
        r->tot_snr[g] += s->snr;
        if (s->snr >= STRONG_SNR) { r->n_strong++; r->strong_tot[g]++; }
        if (!strncmp(s->mode, "FT8", 3)) r->n_ft8++;

        if (g == 0) {
            if (s->flare || s->polar_pen) r->zero_other++;
            else if (s->absorb >= g_params.abs_closed) r->zero_abs++;
            else r->zero_muf++;
        }
        if (s->flare) continue;
        /* MUF-hangoláshoz csak a hosszú utak (ott az F2 réteg számít, nem az Es) */
        if (ratios && s->dist >= LONG_KM) { ratios[nr++] = s->ratio_raw; r->n_long++; }
        if (abs_all) abs_all[na++] = s->absorb;
        if (abs_strong && s->snr >= STRONG_SNR) abs_strong[ns++] = s->absorb;
    }

    /* Hangolási javaslat. A spotok csak a működő utakat mutatják, ezért a
     * határokat csak lazítani tudjuk az alapértékekhez képest. */
    PredictParams def;
    predict_params_default(&def);
    r->suggest = def;
    int span_ok = r->t_last - r->t_first >= MIN_SPAN_H * 3600;
    if (r->n >= MIN_SPOTS && ns >= MIN_STRONG && span_ok && abs_all && abs_strong) {
        /* a hosszú utak spotjainak 90%-a essen a "gyenge" határon (1,15 * MUF) belülre */
        if (nr >= MIN_LONG && ratios) {
            double k = percentile(ratios, nr, 0.90) / 1.15;
            r->suggest.muf_scale = k < 1.0 ? 1.0 : k > 1.6 ? 1.6 : k;
        }
        /* a spotok 95%-a ne essen a "zárva" elnyelés fölé */
        double c = percentile(abs_all, na, 0.95);
        r->suggest.abs_closed = c < def.abs_closed ? def.abs_closed : c > 150 ? 150 : c;
        /* az erős jelek 90%-a ne essen a "két fokozattal rosszabb" határ fölé */
        double p = percentile(abs_strong, ns, 0.90);
        if (p < def.abs_poor) p = def.abs_poor;
        if (p > 0.8 * r->suggest.abs_closed) p = 0.8 * r->suggest.abs_closed;
        r->suggest.abs_poor = p;
        r->suggest.abs_fair = p * def.abs_fair / def.abs_poor;
        r->have_suggest = 1;
    }
    free(ratios);
    free(abs_all);
    free(abs_strong);
}

/* A sorok legfeljebb kb. 50 karakteresek, hogy telefonon is kiférjenek. */
void validate_print(const ValReport *r, FILE *o)
{
    if (r->n == 0) {
        fprintf(o, "%s", TR("Nincs még kiértékelhető spot.\n", "No spots to evaluate yet.\n"));
        if (r->n_es || r->n_ground)
            fprintf(o, TR("(%d rövid, nem ionoszférikus spot kihagyva)\n", "(%d short, non-ionospheric spots skipped)\n"), r->n_es + r->n_ground);
        return;
    }
    char t1[32], t2[32];
    strftime(t1, sizeof t1, "%Y-%m-%d %H:%M", localtime(&r->t_first));
    strftime(t2, sizeof t2, "%Y-%m-%d %H:%M", localtime(&r->t_last));
    fprintf(o, TR("Időszak: %s\n      -> %s\n", "Period: %s\n     -> %s\n"), t1, t2);
    fprintf(o, TR("Spotok: %d\n", "Spots: %d\n"), r->n);
    fprintf(o, TR("  ebből erős (>= %d dB): %d\n", "  strong (>= %d dB): %d\n"), STRONG_SNR, r->n_strong);
    fprintf(o, "  FT8: %.0f%%\n", 100.0 * r->n_ft8 / r->n);
    if (r->n_es || r->n_ground)
        fprintf(o, TR("Kihagyva (a program nem modellezi):\n"
                      "  %d rövid úton jóval a MUF fölött\n"
                      "    (sporadikus E, szórás, hibás lokátor)\n"
                      "  %d földi hullám (100 km alatt)\n",
                      "Skipped (not modelled):\n"
                      "  %d on short paths far above the MUF\n"
                      "    (sporadic E, scatter, wrong locator)\n"
                      "  %d ground wave (under 100 km)\n"),
                r->n_es, r->n_ground);

    fprintf(o, "%s", TR("\nMit jósolt a program ott, ahol valódi\nvétel volt?\n\n", "\nWhat did the program predict where\nreal reception happened?\n\n"));
    /* a fejléc kézzel igazítva: az ékezetes betűk miatt a %s szélesség nem jó */
    fprintf(o, "%s", TR("Sáv     spot    jó  köz. gyen. zárva\n", "Band    spot  good  fair  poor closed\n"));
    for (int b = 0; b < NBANDS; b++) {
        int nb = r->cnt[b][0] + r->cnt[b][1] + r->cnt[b][2] + r->cnt[b][3];
        if (!nb) continue;
        fprintf(o, "%-5s %6d", BANDS[b].name, nb);
        for (int g = 3; g >= 0; g--) {
            if (nb) fprintf(o, " %4.0f%%", 100.0 * r->cnt[b][g] / nb);
        }
        fputc('\n', o);
    }

    fprintf(o, "%s", TR("\nÁtlagos jelerősség (SNR, dB):\n\n", "\nAverage signal (SNR, dB):\n\n"));
    fprintf(o, "%s", TR("Sáv      jó  köz. gyen. zárva\n", "Band   good  fair  poor closed\n"));
    for (int b = 0; b < NBANDS; b++) {
        int nb = r->cnt[b][0] + r->cnt[b][1] + r->cnt[b][2] + r->cnt[b][3];
        if (!nb) continue;
        fprintf(o, "%-5s", BANDS[b].name);
        for (int g = 3; g >= 0; g--) {
            if (r->cnt[b][g]) fprintf(o, " %5.0f", r->snr[b][g] / r->cnt[b][g]);
            else fprintf(o, " %5s", "-");
        }
        fputc('\n', o);
    }

    int open = r->tot[1] + r->tot[2] + r->tot[3];
    int sgood = r->strong_tot[2] + r->strong_tot[3];
    fprintf(o, "%s", TR("\nTalálati arány:\n", "\nHit rate:\n"));
    fprintf(o, TR("- a spotok %.0f%%-a olyan úton volt, amit\n  a program nyitottnak jósolt\n", "- %.0f%% of spots were on paths the\n  program predicted as open\n"),
            100.0 * open / r->n);
    if (r->n_strong)
        fprintf(o, TR("- az erős jelek %.0f%%-át jónak vagy\n  közepesnek jósolta\n", "- %.0f%% of strong signals were\n  predicted good or fair\n"),
                100.0 * sgood / r->n_strong);
    if (r->tot[0]) {
        fprintf(o, TR("- a \"zárva\" jóslatok oka:\n"
                      "    MUF túl alacsony: %d\n"
                      "    elnyelés túl nagy: %d\n"
                      "    zavar (fler/sarki fény): %d\n",
                      "- reasons for \"closed\" predictions:\n"
                      "    MUF too low: %d\n"
                      "    absorption too high: %d\n"
                      "    disturbance (flare/aurora): %d\n"), r->zero_muf, r->zero_abs, r->zero_other);
    }
    /* ellenőrzés: jobb értékeléshez erősebb jel tartozik-e átlagosan? */
    double prev = -1e9;
    int mono = 1, levels = 0;
    for (int g = 1; g <= 3; g++) {
        if (r->tot[g] < 20) continue;
        double avg = r->tot_snr[g] / r->tot[g];
        if (avg < prev - 1.0) mono = 0;
        prev = avg;
        levels++;
    }
    if (levels >= 2)
        fprintf(o, "%s\n", mono ? TR("- a jobb értékelésű utakon átlagosan\n  erősebbek a jelek - ez jó jel",
                                      "- signals are stronger on better-rated\n  paths - a good sign")
                                : TR("- FIGYELEM: a jobb értékelésű utakon\n  nem erősebbek a jelek",
                                      "- WARNING: signals are not stronger\n  on better-rated paths"));

    fprintf(o, "%s", TR("\nHangolási javaslat:\n", "\nTuning suggestion:\n"));
    if (!r->have_suggest) {
        fprintf(o, TR("  még kevés az adat. Kell legalább:\n"
                      "  %d spot, %d erős jel, %d óra\n"
                      "  most: %d spot, %d erős, %.1f óra\n",
                      "  not enough data yet. Needed at least:\n"
                      "  %d spots, %d strong, %d hours\n"
                      "  now: %d spots, %d strong, %.1f hours\n"), MIN_SPOTS, MIN_STRONG, MIN_SPAN_H,
                r->n, r->n_strong, (r->t_last - r->t_first) / 3600.0);
    } else {
        const PredictParams *s = &r->suggest;
        fprintf(o, "  %-11s %6s %9s\n", "", TR("most", "now"), TR("javasolt", "suggested"));
        fprintf(o, "  %-11s %6.2f %9.2f\n", "muf_scale", g_params.muf_scale, s->muf_scale);
        fprintf(o, "  %-11s %6.1f %9.1f\n", "abs_fair", g_params.abs_fair, s->abs_fair);
        fprintf(o, "  %-11s %6.1f %9.1f\n", "abs_poor", g_params.abs_poor, s->abs_poor);
        fprintf(o, "  %-11s %6.1f %9.1f\n", "abs_closed", g_params.abs_closed, s->abs_closed);
        if (r->n_long < MIN_LONG)
            fprintf(o, "%s", TR("  (kevés hosszú út: a MUF-szorzó marad)\n", "  (few long paths: MUF factor unchanged)\n"));
        if (!r->for_app)
            fprintf(o, "%s", TR("  Mentés: ionoprop -V -W\n", "  Save: ionoprop -V -W\n"));
    }
    fprintf(o, "%s", TR("\nMegjegyzés: a spotok főleg FT8-ból\n"
                        "jönnek, ami nagyon gyenge jeleket is vesz.\n"
                        "A spot hiánya nem jelenti, hogy a sáv\n"
                        "zárva volt (lehet, hogy senki sem adott),\n"
                        "ezért a hangolás csak lazítani tud a\n"
                        "határokon.\n",
                        "\nNote: most spots come from FT8, which\n"
                        "decodes very weak signals. No spots does\n"
                        "not mean the band was closed (maybe\n"
                        "nobody transmitted), so tuning can only\n"
                        "loosen the limits.\n"));
}
