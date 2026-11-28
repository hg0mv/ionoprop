/*
 * test_parse.c - a feldolgozó modul tesztje mintaadatokon (internet nélkül)
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

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("HIBA %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 0.01)

static char *slurp(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("Nem találom: %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc((size_t)n + 1);
    b[fread(b, 1, (size_t)n, f)] = 0;
    fclose(f);
    return b;
}

static PropData pd;

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "test/data";
    double qlat, qlon;

    /* lokátor */
    CHECK(locator_to_latlon("JN97mm", &qlat, &qlon) == 0);
    CHECK(NEAR(qlat, 47.521) && NEAR(qlon, 19.042));
    char loc[8];
    latlon_to_locator(qlat, qlon, loc);
    CHECK(strcmp(loc, "JN97mm") == 0);
    CHECK(locator_to_latlon("ZZ99", &qlat, &qlon) == -1);
    locator_to_latlon("JN97mm", &qlat, &qlon);

    /* időformátumok */
    time_t t1 = parse_iso_time("2026-10-31T17:15:00");
    CHECK(t1 == 1793466900);
    CHECK(parse_iso_time("2026-10-31 17:15:00") == t1);
    CHECK(parse_iso_time("2026-10-31T17:15:00.000Z") == t1);
    CHECK(parse_iso_time("szemét") == 0);

    /* KC2G */
    char *j = slurp(dir, "kc2g_stations.json");
    int n = parse_kc2g_stations(j, &pd, qlat, qlon);
    free(j);
    CHECK(n == 4);                              /* a hely nélküli kimarad */
    CHECK(strcmp(pd.st[0].code, "PQ052") == 0); /* legközelebbi: Průhonice */
    CHECK(NEAR(pd.st[0].fof2, 6.45) && NEAR(pd.st[0].mufd, 20.12));
    CHECK(pd.st[0].cs == 75);
    CHECK(pd.st[0].dist_km > 400 && pd.st[0].dist_km < 500);
    CHECK(strcmp(pd.st[3].code, "MHJ45") == 0); /* legtávolabbi */
    CHECK(NEAR(pd.st[3].lon, -71.5));           /* 288.5 -> -71.5 */
    CHECK(isnan(pd.st[3].mufd));                /* null -> NaN */
    for (int i = 0; i < n; i++) {
        if (strcmp(pd.st[i].code, "RO041") == 0) CHECK(pd.st[i].cs == -1);
        if (strcmp(pd.st[i].code, "JR055") == 0) CHECK(isnan(pd.st[i].hmf2));
    }

    /* SWPC */
    j = slurp(dir, "swpc_f107.json");
    CHECK(parse_swpc_f107(j, &pd.solar) == 0);
    free(j);
    CHECK(NEAR(pd.solar.sfi, 90.0));            /* a legújabb, nem az első "Noon" */
    CHECK(NEAR(pd.solar.sfi_90day, 111.0));

    j = slurp(dir, "swpc_kp.json");
    CHECK(parse_swpc_kp(j, &pd.solar) == 0);
    free(j);
    CHECK(NEAR(pd.solar.kp, 2.33));

    j = slurp(dir, "swpc_xray.json");
    CHECK(parse_swpc_xray(j, &pd.solar) == 0);
    free(j);
    CHECK(strcmp(pd.solar.xray_class, "C2.4") == 0);  /* a legújabb, hosszú csatorna */

    char cls[8];
    xray_flux_class(5.0e-5, cls, sizeof cls); CHECK(strcmp(cls, "M5.0") == 0);
    xray_flux_class(1.2e-4, cls, sizeof cls); CHECK(strcmp(cls, "X1.2") == 0);
    xray_flux_class(3.0e-8, cls, sizeof cls); CHECK(strcmp(cls, "A3.0") == 0);

    /* hibás bemenet ne omoljon össze */
    CHECK(parse_kc2g_stations("nem json", &pd, qlat, qlon) == -1);
    CHECK(parse_swpc_kp("{}", &pd.solar) == -1);
    CHECK(parse_swpc_xray("[]", &pd.solar) == -1);

    if (fails) { printf("\n%d teszt HIBÁS\n", fails); return 1; }
    printf("Minden teszt rendben.\n");
    return 0;
}
