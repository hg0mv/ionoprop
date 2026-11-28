/*
 * test_validate.c - PSK spot feldolgozás, kiértékelés, napló, beállítófájl
 * (internet, libcurl és libmosquitto nélkül)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "../src/data.h"
#include "../src/geo.h"
#include "../src/parse.h"
#include "../src/model.h"
#include "../src/predict.h"
#include "../src/pskr.h"
#include "../src/validate.h"
#include "../src/config.h"

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("HIBA %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static PropData pd;
static IonoModel im;

int main(void)
{
    /* saját, ideiglenes könyvtárak, hogy a teszt ne nyúljon a valódi fájlokhoz */
    char tmpl[] = "/tmp/ionoprop_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    setenv("XDG_CACHE_HOME", dir, 1);
    setenv("XDG_CONFIG_HOME", dir, 1);

    time_t now = time(NULL);
    char json[512];

    /* --- spot feldolgozás --- */
    snprintf(json, sizeof json,
             "{\"sq\":30142870791,\"f\":14074653,\"md\":\"FT8\",\"rp\":-5,\"t\":%ld,"
             "\"sc\":\"HG0MV\",\"sl\":\"KN07sp\",\"rc\":\"W1AW\",\"rl\":\"FN31pr\","
             "\"sa\":239,\"ra\":291,\"b\":\"20m\"}", (long)now - 30);
    PskSpot s;
    long long sq = 0;
    CHECK(pskr_parse(json, &s, &sq) == 0);
    CHECK(sq == 30142870791LL);
    CHECK(s.band == 5);                              /* 20m */
    CHECK(fabs(s.f_mhz - 14.074653) < 1e-6);
    CHECK(s.snr == -5);
    CHECK(strcmp(s.mode, "FT8") == 0 && strcmp(s.sc, "HG0MV") == 0);
    CHECK(s.t == now - 30);

    double qlat, qlon;
    locator_to_latlon("KN07sp", &qlat, &qlon);
    CHECK(pskr_classify(&s, qlat, qlon, 300) == 0);
    CHECK(s.local_tx == 1);
    CHECK(s.dist_km > 7000 && s.dist_km < 7500);
    CHECK(fabs(s.flat - s.rlat) < 1e-9);

    /* fordított irány: a közeli vég a vevő */
    snprintf(json, sizeof json, "{\"f\":7074000,\"rp\":-12,\"t\":%ld000,"
             "\"sl\":\"IO91\",\"rl\":\"JN97lm\"}", (long)now);   /* ezredmásodperc */
    CHECK(pskr_parse(json, &s, NULL) == 0);
    CHECK(s.t == now);
    CHECK(pskr_classify(&s, qlat, qlon, 300) == 0 && s.local_tx == 0);

    /* földi hullám: 100 km alatt kihagyjuk */
    snprintf(json, sizeof json, "{\"f\":28074000,\"sl\":\"KN07sp\",\"rl\":\"KN07tq\"}");
    CHECK(pskr_parse(json, &s, NULL) == 0);
    CHECK(pskr_classify(&s, qlat, qlon, 300) == -1);

    /* távoli spot (mindkét vég messze) */
    snprintf(json, sizeof json, "{\"f\":7074000,\"sl\":\"IO91\",\"rl\":\"FN31\"}");
    CHECK(pskr_parse(json, &s, NULL) == 0);
    CHECK(pskr_classify(&s, qlat, qlon, 300) == -1);

    /* hibás / nem HF */
    CHECK(pskr_parse("{\"f\":144174000,\"sl\":\"JN97\",\"rl\":\"JN97\"}", &s, NULL) == -1);  /* 2 m */
    CHECK(pskr_parse("{\"f\":14074000,\"sl\":\"XX\",\"rl\":\"JN97\"}", &s, NULL) == -1);
    CHECK(pskr_parse("szemét", &s, NULL) == -1);
    CHECK(pskr_parse("{\"f\":14074000,\"sl\":\"JN97lm4\",\"rl\":\"KN07sp12xy\"}", &s, NULL) == 0);
    CHECK(pskr_band(10.136) == 4 && pskr_band(5.357) == 2 && pskr_band(27.5) == B11 && pskr_band(28.074) == B10 && pskr_band(26.0 - 0.1) == -1);

    /* --- kiértékelés: este, Sopron-szerű mérésből --- */
    memset(&pd, 0, sizeof pd);
    IonoStation *st = &pd.st[0];
    snprintf(st->code, sizeof st->code, "SO148");
    /* a mérés a QTH-n: így a napszaktól (terminátor helyétől) független a teszt */
    st->lat = qlat; st->lon = qlon; st->time = now - 300;
    st->fof2 = 4.5; st->mufd = 15.6; st->md = 15.6 / 4.5; st->hmf2 = 250; st->cs = 95;
    pd.count = 1; pd.have_stations = 1;
    pd.solar.sfi = 91; pd.have_sfi = 1;
    model_init(&im, &pd, now, 3 * 3600, 25);

    /* NVIS 80 m: a foF2 alatt -> nyitott; NVIS 20 m: messze a MUF fölött -> zárva */
    PskSpot a;
    memset(&a, 0, sizeof a);
    a.t = now; a.nlat = qlat; a.nlon = qlon; a.flat = qlat + 1.0; a.flon = qlon;
    a.dist_km = 111; a.f_mhz = 3.573; a.band = 1; a.snr = -3;
    snprintf(a.mode, sizeof a.mode, "FT8");
    SpotEval e80, e20;
    validate_eval(&im, &pd, &a, &e80);
    CHECK(e80.ratio_raw < 1.0);
    CHECK(im.t == now);                              /* idő visszaállítva */
    a.f_mhz = 14.074; a.band = 5;
    validate_eval(&im, &pd, &a, &e20);
    CHECK(e20.ratio_raw > 2.0);
    CHECK(validate_grade(&e20) == 0);

    /* --- napló: írás, visszaolvasás --- */
    SpotEval two[2] = { e80, e20 };
    snprintf(two[1].mode, sizeof two[1].mode, "MS,K");   /* vessző a módban se rontsa el */
    CHECK(validate_log_append(two, 2) == 0);
    SpotEval *loaded = NULL;
    int n = validate_log_load(now - 3600, &loaded);
    CHECK(n == 2);
    if (n == 2) {
        CHECK(loaded[0].band == 1 && fabs(loaded[0].ratio_raw - e80.ratio_raw) < 1e-3);
        CHECK(loaded[1].band == 5 && strcmp(loaded[1].mode, "MS_K") == 0);
    }
    free(loaded);
    CHECK(validate_log_load(now + 3600, &loaded) == 0);
    free(loaded);

    /* --- statisztika és javaslat: sok spot, amelyek a MUF fölött is működtek --- */
    {
        int N = 1000;
        SpotEval *v = calloc((size_t)N, sizeof *v);
        for (int i = 0; i < N; i++) {
            v[i].t = now - i * 60;                   /* kb. 16 óra */
            v[i].dist = 2500 + i;                    /* hosszú utak */
            v[i].band = 5;
            v[i].f = 14.07;
            v[i].snr = (i % 3) ? -15 : -5;
            v[i].ratio_raw = 0.6 + 0.8 * i / N;      /* 0,6 .. 1,4 */
            v[i].absorb = 5.0 + 40.0 * i / N;        /* 5 .. 45 dB */
            snprintf(v[i].mode, sizeof v[i].mode, "FT8");
        }
        ValReport r;
        validate_report(v, N, &r);
        CHECK(r.n == N);
        CHECK(r.n_ft8 == N);
        CHECK(r.have_suggest);
        /* a 90. percentilis ~1,32 -> szorzó ~1,15 */
        CHECK(r.suggest.muf_scale > 1.10 && r.suggest.muf_scale < 1.20);
        CHECK(r.suggest.abs_closed >= 50.0);         /* csak lazít */
        CHECK(r.suggest.abs_poor > 25.0 && r.suggest.abs_poor < 0.8 * r.suggest.abs_closed + 0.01);
        CHECK(r.suggest.abs_fair < r.suggest.abs_poor);
        int sum = r.tot[0] + r.tot[1] + r.tot[2] + r.tot[3];
        CHECK(sum == N);

        /* kevés adatnál nincs javaslat */
        validate_report(v, 50, &r);
        CHECK(!r.have_suggest);
        /* rövid időszakból (5 óra) sincs */
        validate_report(v, 300, &r);
        CHECK(!r.have_suggest);

        /* rövid utak a MUF fölött (Es): a MUF-szorzót nem emelik */
        {
            SpotEval *w = calloc((size_t)N, sizeof *w);
            memcpy(w, v, sizeof(SpotEval) * (size_t)N);
            for (int i = 0; i < N; i++) { w[i].dist = 1200; w[i].ratio_raw = 2.0; w[i].band = 7; }
            for (int i = 0; i < 200; i++) { w[i].dist = 3000; w[i].ratio_raw = 0.8; }
            ValReport re;
            validate_report(w, N, &re);
            CHECK(re.n_es == N - 200);
            CHECK(re.n == 200);                      /* a nem F2 spotok kimaradnak */
            CHECK(!re.have_suggest || re.suggest.muf_scale == 1.0);
            free(w);
        }

        /* --- beállítófájl: mentés, betöltés, hatás az értékelésre --- */
        ValReport full;
        validate_report(v, N, &full);
        CHECK(config_save(&full.suggest, "teszt") == 0);
        PredictParams p;
        predict_params_default(&p);
        CHECK(config_load(&p) == 1);
        CHECK(fabs(p.muf_scale - full.suggest.muf_scale) < 0.001);
        CHECK(fabs(p.abs_poor - full.suggest.abs_poor) < 0.1);

        /* nagyobb MUF-szorzóval a "határon túli" spot jobb értékelést kap */
        SpotEval edge = v[N - 1];
        edge.ratio_raw = 1.30;                       /* 1,15-ös szorzó nélkül a határ fölött */
        edge.absorb = 5.0;
        int g_before = validate_grade(&edge);
        PredictParams keep = g_params;
        g_params = p;
        int g_after = validate_grade(&edge);
        g_params = keep;
        CHECK(g_after > g_before);
        free(v);
    }

    /* hibás beállítófájl: alapértékek */
    {
        char path[600];
        config_path(path, sizeof path);
        FILE *f = fopen(path, "w");
        fprintf(f, "abs_fair = 40\nabs_poor = 20\nabs_closed = 50\n");
        fclose(f);
        PredictParams p;
        predict_params_default(&p);
        CHECK(config_load(&p) == -1);
        CHECK(p.abs_fair == 12.0);
    }

    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0) printf("(a teszt-könyvtárat nem tudtam törölni: %s)\n", dir);

    if (fails) { printf("\n%d teszt HIBÁS\n", fails); return 1; }
    printf("Ellenőrzés: minden teszt rendben.\n");
    return 0;
}
