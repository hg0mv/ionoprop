/*
 * spots.c - valódi vételek tárolása és felhasználása (lásd spots.h)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spots.h"
#include "geo.h"

/* Es-felhő színe a térképen */
#define ES_COLOR 0xd65cff

int spots_init(SpotStore *st)
{
    st->n = 0;
    st->s = malloc(sizeof(RecentSpot) * SPOT_MAX);
    return st->s ? 0 : -1;
}

void spots_free(SpotStore *st)
{
    free(st->s);
    st->s = NULL;
    st->n = 0;
}

static unsigned pair_hash(const char *sc, const char *rc)
{
    unsigned h = 2166136261u;                   /* FNV-1a */
    for (const char *c = sc; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
    h = (h ^ '>') * 16777619u;
    for (const char *c = rc; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
    return h;
}

void spots_add(SpotStore *st, IonoModel *m, const PropData *d,
               const PskSpot *in, int n, SpotEval *ev)
{
    if (!st->s || n <= 0) return;

    /* a régieket kidobjuk */
    time_t now = time(NULL);
    int k = 0;
    for (int i = 0; i < st->n; i++)
        if (now - st->s[i].t < SPOT_KEEP) st->s[k++] = st->s[i];
    st->n = k;

    /* a kiértékeléshez a "most" modell kell, nem az előrejelzés ideje */
    time_t keep = m->t;
    model_set_time(m, m->now);
    for (int i = 0; i < n; i++) {
        const PskSpot *s = &in[i];
        validate_eval(m, d, s, &ev[i]);
        if (st->n >= SPOT_MAX) {                 /* tele: a legrégebbi negyed megy */
            memmove(st->s, st->s + SPOT_MAX / 4, sizeof(RecentSpot) * (SPOT_MAX - SPOT_MAX / 4));
            st->n -= SPOT_MAX / 4;
        }
        RecentSpot *r = &st->s[st->n++];
        r->t = s->t;
        r->band = s->band;
        r->snr = s->snr;
        r->flat = s->flat;
        r->flon = s->flon;
        gc_point(s->nlat, s->nlon, s->flat, s->flon, 0.5, &r->mlat, &r->mlon);
        r->dist = s->dist_km;
        r->pair = pair_hash(s->sc, s->rc);
        r->es = validate_is_es(&ev[i]);
    }
    model_set_time(m, keep);
}

int spots_recent(const SpotStore *st, time_t now)
{
    int n = 0;
    for (int i = 0; i < st->n; i++) if (now - st->s[i].t <= SPOT_SHOW) n++;
    return n;
}

int spots_on_target(const SpotStore *st, double qlat, double qlon,
                    const Target *t, int band, time_t now)
{
    double tdist = t->nvis ? 0 : distance_km(qlat, qlon, t->lat, t->lon);
    unsigned seen[SPOT_MINPAIR], seen_s[SPOT_MINPAIR];
    int nseen = 0, nstrong = 0;
    for (int i = 0; i < st->n; i++) {
        const RecentSpot *r = &st->s[i];
        if (r->band != band || now - r->t > SPOT_SHOW) continue;
        if (t->nvis) {
            if (r->dist > 300.0) continue;
        } else {
            /* csak a céllal hasonló hosszúságú út számít */
            if (r->dist < 0.75 * tdist || r->dist > 1.35 * tdist) continue;
            if (distance_km(t->lat, t->lon, r->flat, r->flon) > SPOT_TARGET_KM) continue;
        }
        int dup = 0;
        for (int k = 0; k < nseen; k++) if (seen[k] == r->pair) dup = 1;
        if (!dup && nseen < SPOT_MINPAIR) seen[nseen++] = r->pair;
        if (r->snr >= STRONG_SNR) {
            int dups = 0;
            for (int k = 0; k < nstrong; k++) if (seen_s[k] == r->pair) dups = 1;
            if (!dups && nstrong < SPOT_MINPAIR) seen_s[nstrong++] = r->pair;
            if (nstrong >= SPOT_MINPAIR) return 2;
        }
    }
    return nseen >= SPOT_MINPAIR ? 1 : 0;
}

void spots_es_now(const SpotStore *st, time_t now, EsNow *es)
{
    memset(es, 0, sizeof *es);
    for (int b = 0; b < NBANDS; b++) es->best_snr[b] = -99;
    /* különböző állomáspárok sávonként (egyszerű, kis tömbös keresés) */
    enum { MAXP = 64 };
    unsigned seen[NBANDS][MAXP];
    for (int i = 0; i < st->n; i++) {
        const RecentSpot *r = &st->s[i];
        if (!r->es || now - r->t > SPOT_SHOW || r->band < 0 || r->band >= NBANDS) continue;
        int b = r->band, dup = 0;
        int lim = es->pairs[b] < MAXP ? es->pairs[b] : MAXP;
        for (int k = 0; k < lim; k++) if (seen[b][k] == r->pair) { dup = 1; break; }
        if (dup) continue;
        if (es->pairs[b] < MAXP) seen[b][es->pairs[b]] = r->pair;
        es->pairs[b]++;
        if (r->snr > es->best_snr[b]) es->best_snr[b] = r->snr;
    }
}

void es_status(const PropData *d, const SpotStore *st, double qlat, double qlon,
               time_t now, EsStatus *es)
{
    memset(es, 0, sizeof *es);
    es->es_muf = NAN;
    if (st) spots_es_now(st, now, &es->live);
    else for (int b = 0; b < NBANDS; b++) es->live.best_snr[b] = -99;

    for (int i = 0; i < d->count; i++) {
        const IonoStation *s = &d->st[i];
        if (isnan(s->foes) || !s->time) continue;
        if (now - s->time > ES_MAX_AGE || s->time - now > 600) continue;
        double km = distance_km(qlat, qlon, s->lat, s->lon);
        if (km > ES_STATION_KM) continue;
        if (!es->have_foes || s->foes > es->foes) {
            es->have_foes = 1;
            es->foes = s->foes;
            es->foes_km = km;
            es->foes_age_min = (int)((now - s->time) / 60);
            snprintf(es->foes_name, sizeof es->foes_name, "%s", s->name);
            char *comma = strchr(es->foes_name, ',');      /* "Sopron, Hungary" -> "Sopron" */
            if (comma) *comma = '\0';
        }
    }
    if (es->have_foes) es->es_muf = es_muf(es->foes, 2000.0);
}

void spots_draw(Canvas *c, const MapView *mv, const SpotStore *st, time_t now, double scale)
{
    if (!st->s) return;
    int band_mode = mv->mode >= 0;
    cv_clip(c, mv->x, mv->y, mv->w, mv->h);
    /* először az Es-felhők (alul), utána a távoli végek */
    for (int i = 0; i < st->n; i++) {
        const RecentSpot *r = &st->s[i];
        if (!r->es || now - r->t > SPOT_SHOW) continue;
        if (band_mode ? r->band != mv->mode : r->band < B11) continue;
        double x, y;
        map_to_screen(mv, r->mlat, r->mlon, &x, &y);
        cv_circle(c, x, y, 7.0 * scale, ES_COLOR, 70);
        cv_circle(c, x, y, 2.6 * scale, ES_COLOR, 230);
    }
    if (band_mode) {
        for (int i = 0; i < st->n; i++) {
            const RecentSpot *r = &st->s[i];
            if (r->band != mv->mode || now - r->t > SPOT_SHOW) continue;
            double x, y;
            map_to_screen(mv, r->flat, r->flon, &x, &y);
            /* erősebb jel = nagyobb pont */
            double rad = (r->snr >= 0 ? 3.5 : r->snr >= -10 ? 3.0 : 2.3) * scale;
            cv_circle(c, x, y, rad + 1.2, 0x000000, 190);
            cv_circle(c, x, y, rad, 0xffffff, 235);
        }
    }
    cv_noclip(c);
}
