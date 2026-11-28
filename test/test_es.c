/*
 * test_es.c - sporadikus E, Kp-előrejelzés, új alapmodell (internet nélkül)
 *
 * Futtatás:  make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/data.h"
#include "../src/parse.h"
#include "../src/geo.h"
#include "../src/model.h"
#include "../src/predict.h"
#include "../src/spots.h"
#include "../src/targets.h"
#include "../src/alerts.h"
#include "../src/meteor.h"
#include "../src/i18n.h"

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("HIBA %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static PropData pd;

int main(void)
{
    /* ---- Kp-előrejelzés: régi (tömbös) és új (objektumos) formátum ---- */
    const char *kp_old =
        "[[\"time_tag\",\"kp\",\"observed\",\"noaa_scale\"],"
        "[\"2026-11-10 00:00:00\",\"2.33\",\"observed\",null],"
        "[\"2026-11-10 03:00:00\",\"5.67\",\"predicted\",\"G2\"]]";
    const char *kp_new =
        "[{\"time_tag\":\"2026-11-10T00:00:00\",\"kp\":2.33,\"observed\":\"observed\",\"noaa_scale\":null},"
        " {\"time_tag\":\"2026-11-10T03:00:00\",\"kp\":5.67,\"observed\":\"predicted\",\"noaa_scale\":\"G2\"}]";
    SolarData sd;
    memset(&sd, 0, sizeof sd);
    CHECK(parse_swpc_kp_forecast(kp_old, &sd) == 0);
    CHECK(sd.nkpf == 2 && fabs(sd.kpf[1] - 5.67) < 1e-6);
    CHECK(sd.kpf_t[1] == parse_iso_time("2026-11-10T03:00:00"));
    memset(&sd, 0, sizeof sd);
    CHECK(parse_swpc_kp_forecast(kp_new, &sd) == 0);
    CHECK(sd.nkpf == 2 && fabs(sd.kpf[0] - 2.33) < 1e-6);
    SolarData empty;
    memset(&empty, 0, sizeof empty);
    CHECK(parse_swpc_kp_forecast("[]", &empty) != 0);

    /* kp_at: közel a mért, később az előrejelzés */
    memset(&pd, 0, sizeof pd);
    pd.solar = sd;
    pd.have_kpf = 1;
    pd.have_kp = 1;
    pd.solar.kp = 3.0;
    time_t now = parse_iso_time("2026-11-10T00:30:00");
    CHECK(fabs(kp_at(&pd, now, now + 600) - 3.0) < 1e-9);
    CHECK(fabs(kp_at(&pd, now, parse_iso_time("2026-11-10T04:00:00")) - 5.67) < 1e-9);
    CHECK(fabs(kp_at(&pd, now, parse_iso_time("2026-11-12T04:00:00")) - 3.0) < 1e-9);   /* nincs adat: mért */

    /* ---- foEs a KC2G-adatból ---- */
    const char *st =
        "[{\"cs\":90,\"fof2\":6.1,\"mufd\":19.5,\"foes\":7.6,\"hmf2\":250,\"md\":3.2,"
        "  \"time\":\"2026-11-10T00:20:00\","
        "  \"station\":{\"code\":\"JR055\",\"name\":\"Juliusruh\",\"latitude\":\"54.6\",\"longitude\":\"13.4\"}},"
        " {\"cs\":90,\"fof2\":6.0,\"mufd\":19.0,\"foes\":null,\"hmf2\":250,\"md\":3.2,"
        "  \"time\":\"2026-11-10T00:20:00\","
        "  \"station\":{\"code\":\"PQ052\",\"name\":\"Pruhonice\",\"latitude\":\"50.0\",\"longitude\":\"14.6\"}}]";
    double qlat, qlon;
    locator_to_latlon("JN97", &qlat, &qlon);
    CHECK(parse_kc2g_stations(st, &pd, qlat, qlon) == 2);
    int jr = strcmp(pd.st[0].code, "JR055") == 0 ? 0 : 1;
    CHECK(fabs(pd.st[jr].foes - 7.6) < 1e-9);
    CHECK(isnan(pd.st[1 - jr].foes));

    /* Es-MUF: 2000 km-en kb. 5,3-szoros, 7,6 MHz -> a 8 m-es sáv körül */
    double em = es_muf(7.6, 2000.0);
    CHECK(em > 37.0 && em < 44.0);
    CHECK(es_muf(5.0, 2000.0) > 25.0);                  /* foEs 5 MHz: 10 m Es */
    CHECK(es_muf(5.0, 500.0) < es_muf(5.0, 2000.0));    /* rövid úton kisebb */
    CHECK(isnan(es_muf(NAN, 1000.0)));

    EsStatus es;
    es_status(&pd, NULL, qlat, qlon, now, &es);
    CHECK(es.have_foes && fabs(es.foes - 7.6) < 1e-9);
    CHECK(strstr(es.foes_name, "Juliusruh") != NULL);
    es_status(&pd, NULL, qlat, qlon, now + 3 * 3600, &es);   /* 3 óra múlva már régi */
    CHECK(!es.have_foes);

    /* ---- Es-felismerés a valódi vételekből ---- */
    SpotStore ss;
    CHECK(spots_init(&ss) == 0);
    IonoModel m;
    time_t tnow = time(NULL);
    PropData d2;
    memset(&d2, 0, sizeof d2);
    d2.have_sfi = 1;
    d2.solar.sfi = 100;
    model_init(&m, &d2, tnow, 3 * 3600, 25);
    /* éjjel 6 m-en 1500 km-es út: ez csak Es lehet; 20 m-en 8000 km: F2 */
    PskSpot sp[3];
    memset(sp, 0, sizeof sp);
    for (int i = 0; i < 3; i++) {
        sp[i].t = tnow;
        sp[i].nlat = qlat; sp[i].nlon = qlon;
        snprintf(sp[i].sc, sizeof sp[i].sc, "HA%dAAA", i);
        snprintf(sp[i].rc, sizeof sp[i].rc, "DL%dBBB", i);
        snprintf(sp[i].mode, sizeof sp[i].mode, "FT8");
        sp[i].snr = -5;
    }
    sp[0].band = 11; sp[0].f_mhz = 50.313; sp[0].flat = 45.0; sp[0].flon = 0.0;   /* ~1600 km */
    sp[1].band = 11; sp[1].f_mhz = 50.313; sp[1].flat = 41.0; sp[1].flon = 2.0;
    sp[2].band = 5;  sp[2].f_mhz = 14.074; sp[2].flat = 40.0; sp[2].flon = -74.0; /* USA */
    for (int i = 0; i < 3; i++) sp[i].dist_km = distance_km(qlat, qlon, sp[i].flat, sp[i].flon);
    SpotEval ev[3];
    spots_add(&ss, &m, &d2, sp, 3, ev);
    CHECK(ss.n == 3);
    CHECK(ss.s[0].es == 1 && ss.s[1].es == 1);
    CHECK(ss.s[2].es == 0);
    /* a felhő az út közepén van */
    CHECK(distance_km(ss.s[0].mlat, ss.s[0].mlon, qlat, qlon) < 0.6 * ss.s[0].dist);
    EsNow en;
    spots_es_now(&ss, tnow, &en);
    CHECK(en.pairs[11] == 2);
    CHECK(en.pairs[5] == 0);
    spots_free(&ss);

    /* ---- alapmodell: téli délben a közepes szélességen magasabb, mint nyáron ---- */
    double w = model_base_fof2(47.5, 19.0, parse_iso_time("2026-12-15T11:30:00"), 150);
    double s = model_base_fof2(47.5, 19.0, parse_iso_time("2026-06-15T11:00:00"), 150);
    CHECK(w > s);
    /* egyenlítői anomália: délután a csúcs (±15° mágneses) magasabb, mint a mágneses egyenlítő */
    double crest = model_base_fof2(15.0, -17.0, parse_iso_time("2026-03-20T18:00:00"), 180);
    double eq = model_base_fof2(4.0, -17.0, parse_iso_time("2026-03-20T18:00:00"), 180);
    CHECK(crest > eq);

    /* ---- saját célterület: lokátor, hívójel, prefix ---- */
    Target tg;
    char err[128];
    CHECK(target_resolve("pm95", &tg, err, sizeof err) == 0 && strcmp(tg.name, "PM95") == 0);
    CHECK(tg.lat > 35 && tg.lat < 36 && tg.lon > 138 && tg.lon < 140);
    CHECK(target_resolve("JA1ABC", &tg, err, sizeof err) == 0 && strcmp(tg.name, "Japán") == 0);
    CHECK(target_resolve("vk6xyz", &tg, err, sizeof err) == 0 && tg.lon < 120);      /* Perth */
    CHECK(target_resolve("W6ABC", &tg, err, sizeof err) == 0 && tg.lon < -115);      /* Kalifornia */
    CHECK(target_resolve("K1XX", &tg, err, sizeof err) == 0 && tg.lon > -75);        /* Új-Anglia */
    CHECK(target_resolve("EA8/HA5X", &tg, err, sizeof err) == 0 && tg.lat < 30);     /* Kanári */
    CHECK(target_resolve("HA5X/P", &tg, err, sizeof err) == 0 && strcmp(tg.name, "Magyarország") == 0);
    CHECK(target_resolve("RA9AA", &tg, err, sizeof err) == 0 && tg.lon > 60);        /* ázsiai Oroszország */
    CHECK(target_resolve("R1ABC", &tg, err, sizeof err) == 0 && tg.lon < 40);
    CHECK(target_resolve("KH6ZZ", &tg, err, sizeof err) == 0 && tg.lon < -150);      /* Hawaii */
    CHECK(target_resolve("zs", &tg, err, sizeof err) == 0 && tg.lat < -20);
    CHECK(target_resolve("QQ1", &tg, err, sizeof err) != 0);
    CHECK(target_resolve("", &tg, err, sizeof err) != 0);

    /* ---- hosszú út ---- */
    {
        double la, lo;
        gc_point_lp(47.5, 19.0, -33.9, 151.2, 1.0, &la, &lo);          /* a végpont a cél */
        CHECK(fabs(la + 33.9) < 0.01 && fabs(lo - 151.2) < 0.01);
        gc_point_lp(47.5, 19.0, -33.9, 151.2, 0.0, &la, &lo);
        CHECK(fabs(la - 47.5) < 0.01 && fabs(lo - 19.0) < 0.01);
        /* a hosszú út közepe a rövid út közepének átellenes pontja */
        double sla, slo;
        gc_point(47.5, 19.0, -33.9, 151.2, 0.5, &sla, &slo);
        gc_point_lp(47.5, 19.0, -33.9, 151.2, 0.5, &la, &lo);
        CHECK(fabs(la + sla) < 0.5 && distance_km(la, lo, -sla, slo + 180) < 60);
        Target lp = { "Ausztrália LP", -33.9, 151.2, 0, 1 };
        Target sp2 = { "Ausztrália", -33.9, 151.2, 0, 0 };
        PathResult rl, rs;
        PropData d3;
        memset(&d3, 0, sizeof d3);
        IonoModel m3;
        model_init(&m3, &d3, tnow, 3600, 25);
        predict_path(&m3, &d3, 47.5, 19.0, &sp2, &rs);
        predict_path(&m3, &d3, 47.5, 19.0, &lp, &rl);
        CHECK(fabs(rs.dist_km + rl.dist_km - 40030.0) < 30.0);
        CHECK(fabs(fmod(rs.bearing + 180.0, 360.0) - rl.bearing) < 0.01);
    }

    /* ---- napkelte, napnyugta: Budapest, 2026. nov. 11. (kb. 5:45 és 15:15 UTC) ---- */
    {
        time_t rise, set;
        CHECK(sun_rise_set(47.5, 19.05, parse_iso_time("2026-11-11T10:00:00"), &rise, &set) == 0);
        CHECK(llabs((long long)(rise - parse_iso_time("2026-11-11T05:46:00"))) < 8 * 60);
        CHECK(llabs((long long)(set - parse_iso_time("2026-11-11T15:15:00"))) < 8 * 60);
        CHECK(sun_rise_set(78.2, 15.6, parse_iso_time("2026-12-21T12:00:00"), &rise, &set) == -1);  /* sarki éj */
        CHECK(sun_rise_set(78.2, 15.6, parse_iso_time("2026-06-21T12:00:00"), &rise, &set) == 1);   /* éjféli nap */
    }

    /* ---- riasztás: feltételek és "csak az új" logika ---- */
    {
        Target tg[3] = { { "Helyi", 47.5, 19.0, 1, 0 }, { "Japán", 35.7, 139.7, 0, 0 }, { "VK", -33.9, 151.2, 0, 1 } };
        PathResult res[3];
        memset(res, 0, sizeof res);
        res[0].grade[B6] = 3;                 /* NVIS: nem számít */
        res[1].grade[B6] = 2;                 /* 6 m F2 Japán felé */
        res[2].grade[B15] = 3;                /* saját cél: 15 m */
        res[2].grade[B20] = 3;
        EsStatus es;
        memset(&es, 0, sizeof es);
        es.live.pairs[B10] = 3;
        es.have_foes = 1; es.foes = 9.0; es.es_muf = 45.0;
        snprintf(es.foes_name, sizeof es.foes_name, "Juliusruh");
        Alert cur[ALERT_MAX], nw[ALERT_MAX];
        int n = alerts_collect(tg, res, 3, 2, 1, &es, cur, ALERT_MAX);
        CHECK(n == 4);                        /* es:10m, es:8m, f2:6m, tg:VK/LP:15m */
        int k10 = 0, k8 = 0, kf = 0, kt = 0;
        for (int i = 0; i < n; i++) {
            k10 += strcmp(cur[i].key, "es:10m") == 0;
            k8 += strcmp(cur[i].key, "es:8m") == 0;
            kf += strcmp(cur[i].key, "f2:6m") == 0 && strstr(cur[i].text, "Japán") != NULL;
            kt += strcmp(cur[i].key, "tg:VK/LP:15m") == 0;
        }
        CHECK(k10 == 1 && k8 == 1 && kf == 1 && kt == 1);

        char path[] = "/tmp/ionoprop_alert_test.state";
        remove(path);
        time_t t0 = 1800000000;
        CHECK(alerts_new(cur, n, t0, path, nw, ALERT_MAX) == 0);          /* első futás: csak megjegyzi */
        CHECK(alerts_new(cur, 1, t0 + 600, path, nw, ALERT_MAX) == 0);    /* még nyitva: nem új */
        /* bezárt, majd 20 perc múlva újra nyit (de a többi nem): új, de csak egyszer */
        CHECK(alerts_new(cur, 0, t0 + 1200, path, nw, ALERT_MAX) == 0);
        CHECK(alerts_new(cur + 1, 1, t0 + 2400, path, nw, ALERT_MAX) == 1 && strcmp(nw[0].key, cur[1].key) == 0);
        CHECK(alerts_new(cur + 1, 1, t0 + 3000, path, nw, ALERT_MAX) == 0);
        /* bezár és újra nyit 3 órán belül: nem szól újra */
        CHECK(alerts_new(cur, 0, t0 + 3600, path, nw, ALERT_MAX) == 0);
        CHECK(alerts_new(cur + 1, 1, t0 + 4200, path, nw, ALERT_MAX) == 0);
        /* 3 óra múlva igen */
        CHECK(alerts_new(cur, 0, t0 + 2400 + ALERT_COOLDOWN, path, nw, ALERT_MAX) == 0);
        CHECK(alerts_new(cur + 1, 1, t0 + 3000 + ALERT_COOLDOWN, path, nw, ALERT_MAX) == 1);
        char joined[400];
        alerts_join(cur, 2, joined, sizeof joined);
        CHECK(strchr(joined, '\n') != NULL);
        remove(path);
    }

    /* ---- meteorraj-naptár ---- */
    {
        double la = 47.65, lo = 21.54;               /* KN07sp */
        MeteorStatus ms;
        time_t dec13 = 1797163200;                   /* 2026-12-13 12:00 UTC */
        CHECK(meteor_status(dec13, la, lo, &ms) && ms.active && strcmp(ms.sh->code, "GEM") == 0);
        CHECK(fabs(ms.days_to_peak - 1.08) < 0.05);              /* csúcs: dec. 14. kb. 14h UT */
        CHECK(ms.best_from >= 0);
        /* a Geminidák rádiánsa helyi éjfél körül magasan, délben lent van */
        CHECK(meteor_radiant_alt(ms.sh, 1797202800, la, lo) > 60);   /* 2026-12-13 23:00 UTC */
        CHECK(meteor_radiant_alt(ms.sh, 1797163200, la, lo) < 0);    /* 12:00 UTC */
        /* évforduló: január 1-jén a Quadrantidák aktívak */
        CHECK(meteor_status(1798804800, la, lo, &ms) && ms.active && strcmp(ms.sh->code, "QUA") == 0);
        CHECK(ms.days_to_peak > 2.5 && ms.days_to_peak < 2.8);   /* csúcs: 2027. jan. 4. 03:25 UT */
        /* március 1.: nincs aktív, a következő a Lyridák (április 22.) */
        CHECK(meteor_status(1803902400, la, lo, &ms) && !ms.active && strcmp(ms.sh->code, "LYR") == 0);
        /* február 1.: 60 napon belül nincs semmi */
        CHECK(!meteor_status(1801483200, la, lo, &ms));
        /* a csúcsok időpontja az IMO 2026/2027-es naptára szerint (±20 perc) */
        for (int i = 0; i < meteor_count(); i++) {
            const Shower *sh = meteor_get(i);
            if (!strcmp(sh->code, "LEO")) CHECK(labs((long)(meteor_peak_time(sh, 2026) - 1794959100)) < 1200); /* 2026-11-17 23:45 */
            if (!strcmp(sh->code, "QUA")) CHECK(labs((long)(meteor_peak_time(sh, 2027) - 1799033100)) < 1200); /* 2027-01-04 03:25 */
            if (!strcmp(sh->code, "LYR")) CHECK(labs((long)(meteor_peak_time(sh, 2026) - 1776886800)) < 1200); /* 2026-04-22 19:40 */
        }
        char line[160];
        meteor_status(dec13, la, lo, &ms);
        meteor_line(&ms, line, sizeof line);
        CHECK(strstr(line, "ZHR 150") != NULL);
        /* riasztás a csúcs körül, egyszer: kulcs az évvel */
        Alert al[ALERT_MAX];
        int n = alerts_add_meteor(al, 0, ALERT_MAX, dec13, la, lo);
        CHECK(n == 1 && strcmp(al[0].key, "ms:GEM:2026") == 0);
        CHECK(alerts_add_meteor(al, 0, ALERT_MAX, dec13 - 5 * 86400, la, lo) == 0);   /* még messze a csúcs */
        /* km / mérföld */
        g_units_mi = 1;
        CHECK(fabs(dist_out(1000) - 621.371) < 0.01 && strcmp(dist_unit(), "mi") == 0);
        g_units_mi = 0;
        CHECK(dist_out(1000) == 1000 && strcmp(dist_unit(), "km") == 0);
        CHECK(strstr(help_text(), "MUF") != NULL);
    }

    if (fails) { printf("Es / Kp: %d hiba\n", fails); return 1; }
    printf("Es / Kp: minden teszt rendben.\n");
    return 0;
}
