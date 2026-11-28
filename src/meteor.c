/*
 * meteor.c - meteorraj-naptár (lásd meteor.h)
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "meteor.h"
#include "i18n.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define D2R (M_PI / 180.0)

/* IMO meteorraj-naptár (főbb rajok). A csúcs dátuma évről évre ±1 nap. */
/* IMO meteorraj-naptár (2026, 2027): főbb rajok. A csúcs: a Nap hosszúsága
 * (λ☉, J2000); a nap csak kiindulópont a számoláshoz. */
static const Shower SHOWERS[] = {
    { "QUA", "Quadrantidák",          "Quadrantids",              1,  3, 283.15,  6,  9,  80, 230.0, +49.0, 0 },
    { "LYR", "Lyridák",               "Lyrids",                   4, 22,  32.32,  8,  8,  18, 271.0, +34.0, 0 },
    { "ETA", "Éta Aquaridák",         "Eta Aquariids",            5,  6,  45.5,  17, 22,  50, 338.0,  -1.0, 0 },
    { "ARI", "Arietidák (nappali)",   "Daytime Arietids",         6,  7,  76.7,  24, 17,  30,  43.0, +24.0, 1 },
    { "SDA", "Déli Delta Aquaridák",  "Southern Delta Aquariids", 7, 30, 128.0,  18, 24,  25, 340.0, -16.0, 0 },
    { "PER", "Perseidák",             "Perseids",                 8, 12, 140.0,  26, 12, 110,  48.0, +58.0, 0 },
    { "DRA", "Drakonidák",            "Draconids",               10,  8, 195.4,   2,  2,   5, 263.0, +56.0, 0 },
    { "ORI", "Orionidák",             "Orionids",                10, 21, 208.0,  19, 17,  20,  95.0, +16.0, 0 },
    { "LEO", "Leonidák",              "Leonids",                 11, 17, 235.27, 11, 13,  15, 152.0, +22.0, 0 },
    { "GEM", "Geminidák",             "Geminids",                12, 14, 262.2,  10,  6, 150, 112.0, +33.0, 0 },
    { "URS", "Ursidák",               "Ursids",                  12, 22, 270.7,   5,  4,  10, 217.0, +76.0, 0 },
};
#define NSHOWERS ((int)(sizeof SHOWERS / sizeof SHOWERS[0]))

#define BEST_ALT 25.0          /* ennél magasabban álló rádiáns: jó meteorszórás */

int meteor_count(void) { return NSHOWERS; }
const Shower *meteor_get(int i) { return i >= 0 && i < NSHOWERS ? &SHOWERS[i] : NULL; }
const char *meteor_name(const Shower *sh) { return sh ? TR(sh->name_hu, sh->name_en) : ""; }

/* napok száma 1970-01-01 óta (timegm helyett, ami Windowson nincs) */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int year_of(time_t t)
{
    long days = (long)(t / 86400);
    /* Hinnant: civil_from_days, csak az év kell */
    days += 719468;
    long era = (days >= 0 ? days : days - 146096) / 146097;
    long doe = days - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    long m = mp < 10 ? mp + 3 : mp - 9;
    return (int)(y + (m <= 2));
}

/* a Nap ekliptikai hosszúsága (fok, J2000-es napéjegyenlőséghez, mint az IMO-nál),
 * kis pontosságú képlet (~0,01°, vagyis kb. negyed óra) */
static double sun_longitude(time_t t)
{
    double d = (double)t / 86400.0 + 2440587.5 - 2451545.0;
    double L = 280.460 + 0.9856474 * d;
    double g = (357.528 + 0.9856003 * d) * D2R;
    double lam = L + 1.915 * sin(g) + 0.020 * sin(2 * g);
    lam -= 1.397 * d / 36525.0;     /* a dátum napéjegyenlőségéről a J2000-re (precesszió) */
    lam = fmod(lam, 360.0);
    return lam < 0 ? lam + 360.0 : lam;
}

/* a csúcs időpontja egy adott évben: a nap közepétől indulva megkeressük,
 * mikor éri el a Nap a raj λ☉ értékét (a Nap kb. 0,9856°-ot halad naponta) */
time_t meteor_peak_time(const Shower *sh, int year)
{
    double t = (double)(days_from_civil(year, sh->peak_month, sh->peak_day) * 86400L + 12 * 3600L);
    for (int i = 0; i < 4; i++) {
        double dl = sh->sollong - sun_longitude((time_t)t);
        while (dl > 180) dl -= 360;
        while (dl < -180) dl += 360;
        t += dl / 0.9856 * 86400.0;
    }
    return (time_t)(t + 0.5);
}

static time_t peak_time(const Shower *sh, int year) { return meteor_peak_time(sh, year); }

double meteor_radiant_alt(const Shower *sh, time_t t, double lat, double lon)
{
    /* csillagidő Greenwichben (óra), aztán helyi óraszög */
    double d = (double)t / 86400.0 + 2440587.5 - 2451545.0;
    double gmst = fmod(18.697374558 + 24.06570982441908 * d, 24.0);
    if (gmst < 0) gmst += 24.0;
    double ha = (gmst * 15.0 + lon - sh->ra) * D2R;
    double s = sin(lat * D2R) * sin(sh->dec * D2R) + cos(lat * D2R) * cos(sh->dec * D2R) * cos(ha);
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    return asin(s) / D2R;
}

int meteor_status(time_t t, double lat, double lon, MeteorStatus *st)
{
    memset(st, 0, sizeof *st);
    st->idx = -1;
    st->best_from = st->best_to = -1;
    int y = year_of(t);
    int best_active = -1, best_next = -1;
    double active_dt = 0, next_dt = 1e9;
    time_t active_peak = 0, next_peak = 0;

    for (int i = 0; i < NSHOWERS; i++) {
        const Shower *sh = &SHOWERS[i];
        /* a legközelebbi csúcs (tavalyi, idei, jövő évi) */
        for (int dy = -1; dy <= 1; dy++) {
            double dt = (double)(peak_time(sh, y + dy) - t) / 86400.0;   /* nap */
            if (dt <= sh->before && dt >= -sh->after) {
                if (best_active < 0 || sh->zhr > SHOWERS[best_active].zhr) {
                    best_active = i;
                    active_dt = dt;
                    active_peak = peak_time(sh, y + dy);
                }
            } else if (dt > 0 && dt < next_dt) {
                next_dt = dt;
                best_next = i;
                next_peak = peak_time(sh, y + dy);
            }
        }
    }

    if (best_active >= 0) {
        st->idx = best_active;
        st->active = 1;
        st->days_to_peak = active_dt;
        st->peak = active_peak;
    } else if (best_next >= 0 && next_dt <= 60) {
        st->idx = best_next;
        st->days_to_peak = next_dt;
        st->peak = next_peak;
    } else {
        return 0;
    }
    st->sh = &SHOWERS[st->idx];
    st->valid = 1;
    st->now = t;
    st->alt_now = (int)lround(meteor_radiant_alt(st->sh, t, lat, lon));

    /* melyik UTC-órákban áll a rádiáns legalább 25°-on (a mai napon) */
    time_t day0 = t - t % 86400;
    int up[24];
    int any = 0, all = 1;
    for (int h = 0; h < 24; h++) {
        up[h] = meteor_radiant_alt(st->sh, day0 + h * 3600 + 1800, lat, lon) >= BEST_ALT;
        any |= up[h];
        all &= up[h];
    }
    if (all) {
        st->best_from = 0;
        st->best_to = 24;
    } else if (any) {
        /* a fenti szakasz eleje: ahol előtte lent volt */
        int start = 0;
        for (int h = 0; h < 24; h++)
            if (up[h] && !up[(h + 23) % 24]) { start = h; break; }
        int end = start;
        while (up[end % 24] && end < start + 24) end++;
        st->best_from = start;
        st->best_to = end % 24;
    }
    return 1;
}

void meteor_line(const MeteorStatus *st, char *buf, size_t len)
{
    if (!st || !st->valid) {
        snprintf(buf, len, "%s", TR("nincs közeli meteorraj", "no meteor shower soon"));
        return;
    }
    const Shower *sh = st->sh;
    char when[64];
    /* 2 napon belül órában (a csúcs kb. negyedórás pontosságú), azon túl napban */
    double hrs = (double)(st->peak - st->now) / 3600.0;
    int h = (int)lround(fabs(hrs));
    if (h == 0) snprintf(when, sizeof when, "%s", TR("csúcs most", "peak now"));
    else if (hrs > 0 && h < 48) snprintf(when, sizeof when, TR("csúcs %d ó múlva", "peak in %d h"), h);
    else if (hrs < 0 && h < 48) snprintf(when, sizeof when, TR("csúcs %d órája volt", "peak %d h ago"), h);
    else if (hrs > 0) snprintf(when, sizeof when, TR("csúcs %d nap múlva", "peak in %d days"), (int)lround(hrs / 24));
    else snprintf(when, sizeof when, TR("csúcs %d napja volt", "peak %d days ago"), (int)lround(-hrs / 24));

    /* a rádiáns ezekben az UTC-órákban áll elég magasan */
    char hours[48] = "";
    if (st->best_from == 0 && st->best_to == 24)
        snprintf(hours, sizeof hours, "%s", TR(", egész nap", ", all day"));
    else if (st->best_from >= 0)
        snprintf(hours, sizeof hours, ", %02d-%02d UTC", st->best_from, st->best_to);
    else
        snprintf(hours, sizeof hours, "%s", TR(", innen alacsony", ", low from here"));

    if (st->active)
        snprintf(buf, len, "%s: %s, ZHR %d%s", meteor_name(sh), when, sh->zhr, hours);
    else
        snprintf(buf, len, TR("%s: %s (ZHR %d)", "%s: %s (ZHR %d)"), meteor_name(sh), when, sh->zhr);
}
