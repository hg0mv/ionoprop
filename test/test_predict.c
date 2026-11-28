/*
 * test_predict.c - a számító modul tesztjei (internet nélkül)
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../src/data.h"
#include "../src/geo.h"
#include "../src/solar.h"
#include "../src/model.h"
#include "../src/predict.h"
#include "../src/parse.h"

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("HIBA %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static PropData pd;
static IonoModel im;

int main(void)
{
    /* --- geometria --- */
    CHECK(fabs(hop_muf(3000, 5.0, 3.2, 300) - 16.0) < 1e-6);   /* 3000 km-en = foF2 * M */
    CHECK(fabs(hop_muf(1, 5.0, 3.2, 300) - 5.0) < 0.01);       /* függőlegesen = foF2 */
    CHECK(hop_muf(1000, 5, 3, 300) < hop_muf(2000, 5, 3, 300));
    double hm = hop_max_km(300);
    CHECK(hm > 3000 && hm < 3700);
    CHECK(hop_max_km(250) < hm);

    double la, lo;
    gc_point(47.5, 19.0, 51.5, -0.1, 0.5, &la, &lo);
    double d1 = distance_km(47.5, 19.0, la, lo), d2 = distance_km(la, lo, 51.5, -0.1);
    CHECK(fabs(d1 - d2) < 1.0);

    /* --- Nap --- */
    time_t solstice = parse_iso_time("2026-06-21T12:00:00");
    double slat, slon;
    sun_subsolar(solstice, &slat, &slon);
    CHECK(fabs(slat - 23.44) < 0.2);
    CHECK(fabs(slon) < 2.0);                                     /* déli Nap kb. Greenwichnél */
    CHECK(sun_cos_zenith(47.5, 19.0, parse_iso_time("2026-12-21T23:00:00")) < -0.5);  /* éjfél */
    CHECK(sun_cos_zenith(47.5, 19.0, parse_iso_time("2026-06-21T10:45:00")) > 0.85);  /* nyári dél */
    CHECK(fabs(geomag_lat(80.8, -72.6) - 90.0) < 0.01);
    CHECK(fabs(geomag_lat(47.5, 19.0)) > 40 && fabs(geomag_lat(47.5, 19.0)) < 50);

    /* --- modell: egy mért állomás helyén a mérést kell visszaadnia --- */
    time_t now = parse_iso_time("2026-10-31T17:40:00");
    memset(&pd, 0, sizeof pd);
    IonoStation *s = &pd.st[0];
    snprintf(s->code, sizeof s->code, "SO148");
    s->lat = 47.6; s->lon = 16.7; s->time = now - 300;
    s->fof2 = 4.5; s->mufd = 15.65; s->md = 15.65 / 4.5; s->hmf2 = 248; s->cs = 95;
    pd.count = 1; pd.have_stations = 1;
    pd.solar.sfi = 91; pd.have_sfi = 1;

    model_init(&im, &pd, now, 3 * 3600, 25);
    CHECK(im.nref == 1);
    IonoPoint p;
    model_point(&im, 47.6, 16.7, &p);
    CHECK(fabs(p.fof2 - 4.5) / 4.5 < 0.15);
    CHECK(fabs(p.mufd - 15.65) / 15.65 < 0.15);
    CHECK(p.conf > 0.8);
    model_point(&im, -33.9, 151.2, &p);                          /* Ausztrália: nincs mérés */
    CHECK(p.conf < 0.05);

    /* régi és bizonytalan mérés kimarad */
    pd.st[1] = *s; pd.st[1].time = now - 5 * 3600; snprintf(pd.st[1].code, 16, "OLD");
    pd.st[1].lat = 10;
    pd.st[2] = *s; pd.st[2].cs = 5; pd.st[2].lat = 20; snprintf(pd.st[2].code, 16, "BAD");
    pd.count = 3;
    model_init(&im, &pd, now, 3 * 3600, 25);
    CHECK(im.nref == 1);

    /* --- útvonal --- */
    model_init(&im, &pd, now, 3 * 3600, 25);
    Target t[32];
    int nt = predict_default_targets(t, 32);
    CHECK(nt > 5 && t[0].nvis);
    PathResult r;
    double qlat, qlon;
    locator_to_latlon("KN07sp", &qlat, &qlon);
    predict_path(&im, &pd, qlat, qlon, &t[0], &r);              /* NVIS este */
    CHECK(r.hops == 1);
    CHECK(r.muf > 4.0 && r.muf < 5.5);
    CHECK(r.grade[1] >= 2);                                      /* 80 m nyitva */
    CHECK(r.grade[3] == 0);                                      /* 40 m NVIS zárva (foF2 4,5) */
    CHECK(r.grade[B10] == 0);                                    /* 10 m zárva */

    predict_path(&im, &pd, qlat, qlon, &t[14], &r);             /* Ausztrália */
    CHECK(r.hops >= 4);

    /* napkitörés: X1 nappal elnémítja az alsó sávokat */
    time_t noon = parse_iso_time("2026-10-31T10:30:00");
    model_init(&im, &pd, noon, 24 * 3600, 25);
    pd.solar.xray_flux = 1e-4; pd.have_xray = 1;
    predict_path(&im, &pd, qlat, qlon, &t[1], &r);              /* Németország, délben */
    CHECK(r.flare == 1);
    CHECK(r.grade[3] == 0);
    pd.solar.xray_flux = 2e-6;                                   /* C2: nincs hatás */
    predict_path(&im, &pd, qlat, qlon, &t[1], &r);
    CHECK(r.flare == 0);
    predict_path(&im, &pd, qlat, qlon, &t[0], &r);              /* NVIS délben */
    CHECK(r.grade[0] <= 1);                                      /* 160 m: erős D-elnyelés */
    CHECK(r.luf > 2.0);

    /* geomágneses vihar: Skandinávia irány sarki zavart kap */
    pd.solar.kp = 7; pd.have_kp = 1;
    predict_path(&im, &pd, qlat, qlon, &t[3], &r);
    CHECK(r.polar == 1);
    pd.solar.kp = 1;
    predict_path(&im, &pd, qlat, qlon, &t[3], &r);
    CHECK(r.polar == 0);

    /* 24 órás előrejelzés: NVIS-nél délben nagyobb a MUF, mint éjjel */
    {
        static Forecast fc;
        pd.solar.kp = 1; pd.solar.xray_flux = 1e-7;
        model_init(&im, &pd, now, 3 * 3600, 25);
        time_t t0 = parse_iso_time("2026-11-01T00:00:00");
        predict_forecast(&im, &pd, qlat, qlon, &t[0], t0, &fc);
        CHECK(fc.t0 == t0);
        CHECK(fc.muf[10] > fc.muf[1] * 1.3);          /* 10:30 UTC vs 01:30 UTC */
        CHECK(fc.grade[1][1] >= 2);                    /* éjjel 80 m */
        CHECK(fc.grade[10][0] <= 1);                   /* délben 160 m gyenge */
        CHECK(im.t == now);                            /* idő visszaállítva */
    }

    if (fails) { printf("\n%d teszt HIBÁS\n", fails); return 1; }
    printf("Számítás: minden teszt rendben.\n");
    return 0;
}
