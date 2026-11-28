/*
 * mapview.c - világtérkép (lásd mapview.h)
 *
 * Vetület: egyszerű hosszúság-szélesség (equirectangular), 80°É - 60°D.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mapview.h"
#include "i18n.h"
#include "map_data.h"
#include "geo.h"
#include "solar.h"

#define COL_OCEAN   0x0d1826
#define COL_LAND    0xffffff
#define COL_COAST   0xb8c4d2
#define COL_GRID    0x3a4a5e
#define COL_QTH     0xff4d4d
#define COL_PATH    0xffffff

/* ---------- színskálák ---------- */

Color muf_color(double v)
{
    /* 0..50 MHz: sötétkék -> kék -> türkiz -> zöld -> sárga -> narancs -> piros
     * -> bíbor (35) -> lila (40, 8 m) -> világoslila (45) -> fehér (50, 6 m) */
    static const double  stop[] = { 0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50 };
    static const Color   col[]  = { 0x1b2a6b, 0x2463b5, 0x1fa3b8, 0x3cbf5a,
                                    0xd8d43a, 0xf29a2e, 0xe0432b, 0xc01d6a,
                                    0x8a2bc2, 0xb98af0, 0xf4eeff };
    enum { NSTOP = sizeof stop / sizeof stop[0] };
    if (v <= stop[0]) return col[0];
    for (int i = 1; i < NSTOP; i++)
        if (v <= stop[i]) return color_mix(col[i - 1], col[i], (v - stop[i - 1]) / (stop[i] - stop[i - 1]));
    return col[NSTOP - 1];
}

Color grade_color(int g)
{
    static const Color c[4] = { 0x2a3340, 0xe07b39, 0xf2c94c, 0x3ccf6e };
    return c[g < 0 ? 0 : g > 3 ? 3 : g];
}

/* ---------- vetület ---------- */

void map_to_screen(const MapView *mv, double lat, double lon, double *x, double *y)
{
    *x = mv->x + (lon + 180.0) / 360.0 * mv->w;
    *y = mv->y + (MAP_LAT_TOP - lat) / (MAP_LAT_TOP - MAP_LAT_BOT) * mv->h;
}

int screen_to_map(const MapView *mv, int x, int y, double *lat, double *lon)
{
    if (x < mv->x || y < mv->y || x >= mv->x + mv->w || y >= mv->y + mv->h) return -1;
    *lon = (x - mv->x + 0.5) / mv->w * 360.0 - 180.0;
    *lat = MAP_LAT_TOP - (y - mv->y + 0.5) / mv->h * (MAP_LAT_TOP - MAP_LAT_BOT);
    return 0;
}

static void cell_center(int i, int j, double *lat, double *lon)
{
    *lon = -180.0 + (i + 0.5) * GRID_STEP;
    *lat = MAP_LAT_TOP - (j + 0.5) * GRID_STEP;
}

/* ---------- számítás ---------- */

void map_compute(MapView *mv, const IonoModel *m, const PropData *d,
                 double qlat, double qlon, int with_coverage)
{
    for (int j = 0; j < GRID_NY; j++)
        for (int i = 0; i < GRID_NX; i++) {
            double la, lo;
            IonoPoint p;
            cell_center(i, j, &la, &lo);
            model_point(m, la, lo, &p);
            mv->muf[j][i]  = (float)p.mufd;
            mv->fof2[j][i] = (float)p.fof2;
            mv->cosx[j][i] = (float)p.cosx;
        }
    mv->layer_t = m->t;

    if (!with_coverage) return;
    for (int j = 0; j < GRID_NY; j++)
        for (int i = 0; i < GRID_NX; i++) {
            Target t;
            PathResult r;
            memset(&t, 0, sizeof t);
            cell_center(i, j, &t.lat, &t.lon);
            /* a saját környék (300 km-en belül) NVIS útként */
            t.nvis = distance_km(qlat, qlon, t.lat, t.lon) < 300.0;
            predict_path(m, d, qlat, qlon, &t, &r);
            for (int b = 0; b < NBANDS; b++) mv->grade[j][i][b] = (uint8_t)r.grade[b];
        }
    mv->cover_t = m->t;
    mv->cover_valid = 1;
}

/* rácsérték bilineáris interpolációval (hosszúságban körbeér) */
static double grid_sample(const float g[GRID_NY][GRID_NX], double lat, double lon)
{
    double fx = (lon + 180.0) / GRID_STEP - 0.5;
    double fy = (MAP_LAT_TOP - lat) / GRID_STEP - 0.5;
    if (fy < 0) fy = 0;
    if (fy > GRID_NY - 1) fy = GRID_NY - 1;
    int x0 = (int)floor(fx), y0 = (int)floor(fy);
    double tx = fx - x0, ty = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1 < GRID_NY ? y0 + 1 : y0;
    x0 = (x0 % GRID_NX + GRID_NX) % GRID_NX;
    x1 = (x1 % GRID_NX + GRID_NX) % GRID_NX;
    double a = g[y0][x0] * (1 - tx) + g[y0][x1] * tx;
    double b = g[y1][x0] * (1 - tx) + g[y1][x1] * tx;
    return a * (1 - ty) + b * ty;
}

/* ---------- rajzolás ---------- */

static void draw_land(Canvas *c, const MapView *mv)
{
    static double buf[2 * 2048];
    for (int r = 0; r < MAP_NRINGS; r++) {
        int s = MAP_RING_START[r], e = MAP_RING_START[r + 1];
        int n = e - s;
        if (n > 2048) n = 2048;
        for (int k = 0; k < n; k++)
            map_to_screen(mv, MAP_PTS[2 * (s + k) + 1] / 100.0, MAP_PTS[2 * (s + k)] / 100.0,
                          &buf[2 * k], &buf[2 * k + 1]);
        cv_polygon(c, buf, n, COL_LAND, 28);
        for (int k = 0; k < n; k++) {
            int k2 = (k + 1) % n;
            if (fabs(buf[2 * k] - buf[2 * k2]) > mv->w / 2) continue;   /* dátumvonal */
            cv_line(c, buf[2 * k], buf[2 * k + 1], buf[2 * k2], buf[2 * k2 + 1], COL_COAST, 170);
        }
    }
}

static void draw_path(Canvas *c, const MapView *mv, double qlat, double qlon,
                      const Target *t, Color col, double width)
{
    const int N = t->lp ? 400 : 160;
    double px = 0, py = 0;
    for (int k = 0; k <= N; k++) {
        double la, lo, x, y;
        target_path_point(qlat, qlon, t, (double)k / N, &la, &lo);
        map_to_screen(mv, la, lo, &x, &y);
        if (k > 0 && fabs(x - px) < mv->w / 2)
            cv_line_thick(c, px, py, x, y, width, col, 220);
        px = x; py = y;
    }
}

static void legend(Canvas *c, const MapView *mv)
{
    int lx = mv->x + 10, ly = mv->y + mv->h - 34;
    if (mv->mode < 0) {
        int w = mv->mode == MAP_MUF ? 260 : 220;
        double maxv = mv->mode == MAP_MUF ? 50.0 : 15.0;
        /* a foF2-térkép ugyanazt a skálát használja, 15 MHz = 35 MHz-es szín */
        double cmax = mv->mode == MAP_MUF ? 50.0 : 35.0;
        cv_blend_rect(c, lx - 6, ly - 20, w + 12, 50, 0x000000, 150);
        cv_text(c, lx, ly - 17, 12, FONT_BOLD, 0xffffff,
                mv->mode == MAP_MUF ? "MUF(3000), MHz" : "foF2, MHz");
        for (int x = 0; x < w; x++)
            cv_fill_rect(c, lx + x, ly, 1, 10, muf_color((double)x / w * cmax));
        for (int v = 0; v <= (int)maxv; v += (mv->mode == MAP_MUF ? 5 : 3)) {
            char s[16];
            snprintf(s, sizeof s, "%d", v);
            int x = lx + (int)(v / maxv * w);
            cv_fill_rect(c, x, ly + 10, 1, 3, 0xffffff);
            cv_text(c, x - text_width(11, 0, s) / 2, ly + 13, 11, FONT_NORMAL, 0xdddddd, s);
        }
    } else {
        const char *lbl[] = { TR("zárva", "closed"), TR("gyenge", "poor"),
                              TR("közepes", "fair"), TR("jó", "good") };
        char title[64];
        snprintf(title, sizeof title, TR("Hová jut el a jel: %s", "Signal coverage: %s"), BANDS[mv->mode].name);
        cv_blend_rect(c, lx - 6, ly - 20, 290, 50, 0x000000, 150);
        cv_text(c, lx, ly - 17, 12, FONT_BOLD, 0xffffff, title);
        int x = lx;
        for (int g = 3; g >= 1; g--) {
            cv_fill_rect(c, x, ly + 2, 12, 12, grade_color(g));
            x += 16;
            x += cv_text(c, x, ly + 1, 12, FONT_NORMAL, 0xdddddd, lbl[g]) + 14;
        }
    }
}

void map_draw(Canvas *c, const MapView *mv, const PropData *d, const IonoModel *m,
              double qlat, double qlon, const Target *sel)
{
    cv_clip(c, mv->x, mv->y, mv->w, mv->h);
    cv_fill_rect(c, mv->x, mv->y, mv->w, mv->h, COL_OCEAN);

    /* színtérkép vagy lefedettség + éjszakai árnyék, pixelenként - a vászon
     * valódi (nagyított) felbontásában, hogy nagyított kijelzőn is éles legyen */
    double sc = c->s > 0 ? c->s : 1.0;
    int py0 = c->cy0, py1 = c->cy1, px0 = c->cx0, px1 = c->cx1;
    for (int y = py0; y < py1; y++) {
        uint32_t *row = &c->px[(size_t)y * c->w];
        double ly = (y + 0.5) / sc;
        double la = MAP_LAT_TOP - (ly - mv->y) / mv->h * (MAP_LAT_TOP - MAP_LAT_BOT);
        for (int x = px0; x < px1; x++) {
            double lo = ((x + 0.5) / sc - mv->x) / mv->w * 360.0 - 180.0;
            Color px = row[x];
            if (mv->mode < 0) {
                double v = grid_sample(mv->mode == MAP_MUF ? mv->muf : mv->fof2, la, lo);
                if (mv->mode == MAP_FOF2) v *= 35.0 / 15.0;      /* ugyanaz a skála */
                px = color_mix(px, muf_color(v), 0.62);
            } else if (mv->cover_valid) {
                int i = (int)((lo + 180.0) / GRID_STEP);
                int j = (int)((MAP_LAT_TOP - la) / GRID_STEP);
                if (i >= GRID_NX) i = GRID_NX - 1;
                if (j >= GRID_NY) j = GRID_NY - 1;
                int g = mv->grade[j][i][mv->mode];
                if (g > 0) px = color_mix(px, grade_color(g), 0.55);
            }
            /* éjszaka: sötétítés a szürkületi sávban fokozatosan */
            double cz = grid_sample(mv->cosx, la, lo);
            double night = cz > 0.05 ? 0 : cz < -0.2 ? 1 : (0.05 - cz) / 0.25;
            if (night > 0) px = color_mix(px, 0x000000, 0.42 * night);
            /* szürkevonal (a napkelte/napnyugta sávja, kb. ±3°): halvány arany */
            double gl = fabs(cz + 0.01) / 0.05;
            if (gl < 1.0) px = color_mix(px, 0xffc14d, 0.22 * (1.0 - gl * gl));
            row[x] = px;
        }
    }

    /* fokhálózat 30 fokonként */
    for (int lo = -150; lo < 180; lo += 30) {
        double x, y;
        map_to_screen(mv, 0, lo, &x, &y);
        cv_blend_rect(c, (int)x, mv->y, 1, mv->h, COL_GRID, 120);
    }
    for (int la = -60; la <= 60; la += 30) {
        double x, y;
        map_to_screen(mv, la, 0, &x, &y);
        cv_blend_rect(c, mv->x, (int)y, mv->w, 1, la == 0 ? 0x5a6a7e : COL_GRID, 120);
    }

    draw_land(c, mv);

    /* a Nap helye */
    {
        double sla, slo, x, y;
        sun_subsolar(m->t, &sla, &slo);
        map_to_screen(mv, sla, slo, &x, &y);
        cv_circle(c, x, y, 7, 0xffd23f, 230);
        for (int k = 0; k < 8; k++) {
            double a = k * M_PI / 4;
            cv_line(c, x + cos(a) * 9, y + sin(a) * 9, x + cos(a) * 13, y + sin(a) * 13, 0xffd23f, 230);
        }
    }

    /* útvonal a kijelölt célig, ugrásokkal */
    if (sel && !sel->nvis) {
        draw_path(c, mv, qlat, qlon, sel, 0x000000, 4.0);
        draw_path(c, mv, qlat, qlon, sel, COL_PATH, 2.0);
        PathResult r;
        predict_path(m, d, qlat, qlon, sel, &r);
        for (int k = 0; k < r.hops; k++) {          /* vezérlőpontok (ugrások közepe) */
            double la, lo, x, y;
            target_path_point(qlat, qlon, sel, (k + 0.5) / r.hops, &la, &lo);
            map_to_screen(mv, la, lo, &x, &y);
            cv_circle(c, x, y, 4, 0x000000, 200);
            cv_circle(c, x, y, 3, 0xffffff, 255);
        }
        double x, y;
        map_to_screen(mv, sel->lat, sel->lon, &x, &y);
        cv_circle(c, x, y, 7, 0x000000, 200);
        cv_circle(c, x, y, 5, 0x4fb3ff, 255);
    }

    /* ionoszondák */
    for (int i = 0; i < d->count; i++) {
        const IonoStation *s = &d->st[i];
        if (!s->time || m->now - s->time > 3 * 3600) continue;
        double x, y;
        map_to_screen(mv, s->lat, s->lon, &x, &y);
        Color col = isnan(s->mufd) ? 0x888888 : muf_color(s->mufd);
        cv_circle(c, x, y, 5, 0x000000, 220);
        cv_circle(c, x, y, 4, 0xffffff, 255);
        cv_circle(c, x, y, 3, col, 255);
    }

    /* saját QTH */
    {
        double x, y;
        map_to_screen(mv, qlat, qlon, &x, &y);
        cv_circle(c, x, y, 8, 0x000000, 200);
        cv_circle(c, x, y, 6, COL_QTH, 255);
        cv_circle(c, x, y, 2.5, 0xffffff, 255);
    }

    if (!mv->no_legend) legend(c, mv);
    cv_noclip(c);
}
