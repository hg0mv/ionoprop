/*
 * model.c - ionoszféra-modell (lásd model.h)
 */
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "model.h"
#include "solar.h"
#include "geo.h"

/* A korrekció hatótávolsága: ennyi km-en belül számít egy ionoszonda */
#define CORR_SCALE_KM 1500.0
/* Az alapmodell "súlya" - ennyire bízunk benne mérés nélkül */
#define PRIOR_W 0.15

/* a mért korrekció "emlékezete" előrejelzésnél, másodperc */
#define CORR_TAU   (5.0 * 3600.0)
/* az általános (nagy területű) eltérés lassabban változik */
#define PRIOR_TAU  (24.0 * 3600.0)

/* SFI -> napfoltszám (az SFI = 63.7 + 0.728 R + 0.00089 R^2 képlet megfordítása) */
static double sfi_to_ssn(double sfi)
{
    double a = 0.00089, b = 0.728, c = 63.7 - sfi;
    double disc = b * b - 4 * a * c;
    if (disc < 0) return 0;
    double r = (-b + sqrt(disc)) / (2 * a);
    return r < 0 ? 0 : r;
}

/*
 * Alapmodell: foF2 a napaktivitásból és a napállásból, három ismert
 * jelenséggel kiegészítve (ezek nélkül a modell télen és az egyenlítő
 * környékén jóval alacsonyabb MUF-ot adott a mértnél):
 *   - téli anomália: a közepes szélességeken télen nappal MAGASABB a foF2,
 *     mint nyáron, pedig a Nap alacsonyabban jár (nyáron viszont az
 *     éjszakák "jobbak");
 *   - egyenlítői anomália: a mágneses egyenlítő két oldalán (kb. ±15°)
 *     délután és este kiemelkedő foF2 - innen jönnek a 10 m / 6 m-es
 *     transzekvatoriális (TEP) nyitások;
 *   - telítődés: nagyon magas SFI fölött a foF2 már lassabban nő.
 */
double model_base_fof2(double lat, double lon, time_t t, double sfi)
{
    if (sfi < 60)  sfi = 60;
    if (sfi > 300) sfi = 300;
    double sfi_eff = sfi > 200 ? 200 + 0.5 * (sfi - 200) : sfi;

    /* Az F2 réteg késve követi a Napot: a csúcs kb. 13-14 óra helyi időben.
     * Ezért a 1,5 órával korábbi napállást használjuk. */
    double c = sun_cos_zenith(lat, lon, t - 5400);

    double noon  = 4.5 + 0.035 * sfi_eff;   /* délben, közepes szélességen */
    double night = 0.40 * noon;             /* éjszakai minimum */
    /* a negyedik gyök: a foF2 a napmagasságot csak gyengén követi.
     * A napfordulónál (|c| < 0,1, kb. ±6°) lineárisan megy át, különben a
     * negyedik gyök miatt a foF2 a terminátoron ugrásszerűen a felére esne
     * (két szomszédos ionoszonda között is). */
    double g;
    if (c >= 0.1) g = pow(c, 0.25);
    else if (c <= -0.1) g = 0;
    else g = pow(0.1, 0.25) * (c + 0.1) / 0.2;

    /* évszak: +1 = helyi tél közepe, -1 = helyi nyár közepe */
    double doy = fmod((double)t / 86400.0, 365.2425);         /* 0 = jan. 1. körül */
    double season = cos(2.0 * M_PI * (doy + 10.0) / 365.2425); /* +1 dec. 21-én */
    double winter = lat >= 0 ? season : -season;
    double alat = fabs(lat);
    double midlat = alat < 20 ? 0 : alat < 30 ? (alat - 20) / 10 : alat < 60 ? 1 : alat < 70 ? (70 - alat) / 10 : 0;
    double act = (sfi_eff - 70.0) / 100.0;
    if (act < 0.3) act = 0.3;
    if (act > 1.3) act = 1.3;
    double day_f   = 1.0 + 0.18 * winter * midlat * act;     /* téli anomália nappal */
    double night_f = 1.0 - 0.12 * winter * midlat;            /* nyáron jobb éjszakák */

    double f = night * night_f + (noon * day_f - night * night_f) * g;
    if (f < night * night_f) f = night * night_f;

    /* egyenlítői anomália: helyi idő szerint délután-este */
    double mlat = geomag_lat(lat, lon);
    double lt = fmod((double)(t % 86400) / 3600.0 + lon / 15.0 + 48.0, 24.0);
    double dl = lt - 17.0;
    if (dl > 12) dl -= 24;
    if (dl < -12) dl += 24;
    double dvar = exp(-(dl * dl) / (2 * 3.5 * 3.5));
    double crest  = exp(-pow((fabs(mlat) - 15.0) / 7.0, 2));
    double trough = exp(-pow(mlat / 5.0, 2));
    double amp = 0.10 + 0.0025 * (sfi_eff - 70.0);
    if (amp < 0.05) amp = 0.05;
    if (amp > 0.45) amp = 0.45;
    f *= 1.0 + amp * crest * dvar - 0.08 * trough * dvar;

    /* sarki tájakon általában alacsonyabb */
    double glat = fabs(mlat);
    double polar = glat > 60 ? 1.0 - (glat - 60) * 0.01 : 1.0;

    return f * polar;
}

double model_foe(double cosx, double ssn)
{
    if (cosx <= 0.02) return 0.5;           /* éjszaka: gyakorlatilag nincs E réteg */
    return 0.9 * pow((180.0 + 1.44 * ssn) * cosx, 0.25);
}

static double ref_weight(const IonoStation *s, time_t now)
{
    double w = (s->cs == -1) ? 0.9 : s->cs / 100.0;
    if (w < 0.2) w = 0.2;
    double age_h = (double)(now - s->time) / 3600.0;
    if (age_h < 0) age_h = 0;
    return w / (1.0 + age_h);               /* régebbi mérés kisebb súllyal */
}

void model_init(IonoModel *m, const PropData *d, time_t now, int max_age_s, int min_cs)
{
    memset(m, 0, sizeof *m);
    m->now = now;
    m->t = now;
    m->decay = 1.0;
    m->sfi = d->have_sfi ? d->solar.sfi : 100.0;
    m->ssn = sfi_to_ssn(m->sfi);

    for (int i = 0; i < d->count; i++) {
        const IonoStation *s = &d->st[i];
        if (!s->time || now - s->time > max_age_s || now < s->time - 600) continue;
        if (!(s->cs == -1 || s->cs >= min_cs)) continue;
        if (isnan(s->fof2) || s->fof2 <= 0.5) continue;

        double w = ref_weight(s, now);

        /* Ugyanazon a helyen több állomás (pl. Róma kétszer): a jobbat tartjuk meg */
        int dup = -1;
        for (int k = 0; k < m->nref; k++)
            if (distance_km(s->lat, s->lon, m->ref[k].lat, m->ref[k].lon) < 20.0) { dup = k; break; }
        if (dup >= 0 && m->ref[dup].w >= w) continue;
        if (dup < 0 && m->nref >= MODEL_MAX_REF) continue;

        ModelRef *r = &m->ref[dup >= 0 ? dup : m->nref++];
        r->lat  = s->lat;
        r->lon  = s->lon;
        /* az alapmodellt a MÉRÉS idejére számoljuk - így a régebbi mérés is jól használható */
        r->r_f2 = log(s->fof2 / model_base_fof2(s->lat, s->lon, s->time, m->sfi));
        r->md   = (s->md > 1.5 && s->md < 5.0) ? s->md : NAN;
        r->hmf2 = (s->hmf2 > 100 && s->hmf2 < 800) ? s->hmf2 : NAN;
        r->w    = w;
        snprintf(r->code, sizeof r->code, "%s", s->code);
    }

    /* általános korrekció: a mért arányok súlyozott átlagának fele */
    double sw = 0, sr = 0;
    for (int k = 0; k < m->nref; k++) { sw += m->ref[k].w; sr += m->ref[k].w * m->ref[k].r_f2; }
    m->prior_r = sw > 0 ? 0.5 * sr / sw : 0.0;
}

void model_set_time(IonoModel *m, time_t t)
{
    m->t = t;
    m->decay = exp(-fabs((double)(t - m->now)) / CORR_TAU);
}

void model_point(const IonoModel *m, double lat, double lon, IonoPoint *out)
{
    double sw = 0, sr = 0;
    double swm = 0, smd = 0, swh = 0, shm = 0;
    double near = 1e9;

    for (int k = 0; k < m->nref; k++) {
        const ModelRef *r = &m->ref[k];
        double dk = distance_km(lat, lon, r->lat, r->lon);
        if (dk < near) near = dk;
        double x = dk / CORR_SCALE_KM;
        double w = r->w * m->decay * exp(-x * x);
        if (w < 1e-6) continue;
        sw += w; sr += w * r->r_f2;
        if (!isnan(r->md))   { swm += w; smd += w * r->md; }
        if (!isnan(r->hmf2)) { swh += w; shm += w * r->hmf2; }
    }

    double prior = m->prior_r * exp(-fabs((double)(m->t - m->now)) / PRIOR_TAU);
    double corr = (PRIOR_W * prior + sr) / (PRIOR_W + sw);

    /* nappal alacsonyabb az F2 réteg és nagyobb az M(3000) */
    double cl = sun_cos_zenith(lat, lon, m->t - 5400);
    double day = cl > 0 ? (cl > 1 ? 1 : cl) : 0;
    double prior_md = 2.9 + 0.4 * day;
    double prior_hm = 320.0 - 70.0 * day;

    out->fof2 = model_base_fof2(lat, lon, m->t, m->sfi) * exp(corr);
    out->md   = (PRIOR_W * prior_md + smd) / (PRIOR_W + swm);
    out->hmf2 = (PRIOR_W * prior_hm + shm) / (PRIOR_W + swh);
    out->mufd = out->fof2 * out->md;
    out->cosx = sun_cos_zenith(lat, lon, m->t);
    out->foe  = model_foe(out->cosx, m->ssn);
    out->near_km = near;
    out->conf = sw / (PRIOR_W + sw);
}
