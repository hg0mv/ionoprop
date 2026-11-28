/*
 * alerts.c - riasztás sávnyitáskor (lásd alerts.h)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "alerts.h"
#include "config.h"
#include "compat.h"
#include "i18n.h"
#include "meteor.h"

#define STATE_KEEP  (24 * 3600)    /* ennyi ideig őrizzük a régi kulcsokat */
#define NSTATE      128

typedef struct {
    char   key[64];
    time_t seen;      /* utoljára teljesült */
    time_t alerted;   /* utoljára szóltunk miatta */
} StateRow;

int alerts_enabled(void)
{
    char v[16];
    if (!config_get("alerts", v, sizeof v)) return 1;
    return atoi(v) != 0;
}

void alerts_set_enabled(int on) { config_set("alerts", on ? "1" : "0"); }

static int add(Alert *out, int n, int max, const char *key, const char *text)
{
    for (int i = 0; i < n; i++)
        if (strcmp(out[i].key, key) == 0) return n;      /* már benne van */
    if (n >= max) return n;
    snprintf(out[n].key, sizeof out[n].key, "%s", key);
    snprintf(out[n].text, sizeof out[n].text, "%s", text);
    return n + 1;
}

int alerts_collect(const Target *tg, const PathResult *res, int nt,
                   int first_custom, int n_custom, const EsStatus *es,
                   Alert *out, int max)
{
    int n = 0;
    char key[64], txt[160];
    static const int es_bands[] = { B6, B8, B10 };

    /* 1. sporadikus E: előbb a valódi vételek, aztán az ionoszonda */
    if (es) {
        for (int k = 0; k < 3; k++) {
            int b = es_bands[k];
            if (es->live.pairs[b] < SPOT_MINPAIR) continue;
            snprintf(key, sizeof key, "es:%s", BANDS[b].name);
            snprintf(txt, sizeof txt, TR("Sporadikus E nyitás %s-en (%d állomáspár)",
                                         "Sporadic E opening on %s (%d station pairs)"),
                     BANDS[b].name, es->live.pairs[b]);
            n = add(out, n, max, key, txt);
        }
        if (es->have_foes && !isnan(es->es_muf) && es->es_muf >= 28.0) {
            int b = es->es_muf >= 50.3 ? B6 : es->es_muf >= 40.7 ? B8 : B10;
            snprintf(key, sizeof key, "es:%s", BANDS[b].name);
            snprintf(txt, sizeof txt, TR("Sporadikus E lehet %s-en (foEs %.1f MHz, %s)",
                                         "Sporadic E possible on %s (foEs %.1f MHz, %s)"),
                     BANDS[b].name, es->foes, es->foes_name);
            n = add(out, n, max, key, txt);
        }
    }

    /* 2. F2 nyitás 8 / 6 m-en: melyik célok felé */
    static const int vhf[] = { B6, B8 };
    for (int k = 0; k < 2; k++) {
        int b = vhf[k], cnt = 0;
        char names[100] = "";
        for (int i = 0; i < nt; i++) {
            if (tg[i].nvis || res[i].grade[b] < 2) continue;
            if (cnt < 2) {
                size_t l = strlen(names);
                snprintf(names + l, sizeof names - l, "%s%.40s", cnt ? ", " : "", tg[i].name);
            }
            cnt++;
        }
        if (!cnt) continue;
        snprintf(key, sizeof key, "f2:%s", BANDS[b].name);
        if (cnt > 2)
            snprintf(txt, sizeof txt, TR("F2 nyitás %s-en: %s és még %d", "F2 opening on %s: %s and %d more"),
                     BANDS[b].name, names, cnt - 2);
        else
            snprintf(txt, sizeof txt, TR("F2 nyitás %s-en: %s", "F2 opening on %s: %s"), BANDS[b].name, names);
        n = add(out, n, max, key, txt);
    }

    /* 3. saját célterületek 20-10 m között (a 11 m kivételével) */
    static const int hf[] = { B10, B12, B15, B17, B20 };
    for (int i = first_custom; i < first_custom + n_custom && i < nt; i++) {
        if (tg[i].nvis) continue;
        int best = -1;                 /* a legmagasabb nyitott sáv számít */
        for (int k = 0; k < 5; k++)
            if (res[i].grade[hf[k]] >= 2) { best = hf[k]; break; }
        if (best < 0) continue;
        snprintf(key, sizeof key, "tg:%.40s%s:%s", tg[i].name, tg[i].lp ? "/LP" : "", BANDS[best].name);
        snprintf(txt, sizeof txt, TR("%s nyitva: %s%s", "%s open: %s%s"),
                 BANDS[best].name, tg[i].name, tg[i].lp ? TR(" (hosszú út)", " (long path)") : "");
        n = add(out, n, max, key, txt);
    }
    return n;
}

int alerts_add_meteor(Alert *out, int n, int max, time_t now, double lat, double lon)
{
    MeteorStatus ms;
    if (!meteor_status(now, lat, lon, &ms) || !ms.active) return n;
    if (fabs(ms.days_to_peak) > 1.25 || ms.sh->zhr < 15) return n;
    char key[64], txt[160], line[128];
    /* a kulcsban az év is benne van, így minden évben egyszer szól */
    struct tm *g = gmtime(&now);
    snprintf(key, sizeof key, "ms:%s:%d", ms.sh->code, g ? g->tm_year + 1900 : 0);
    meteor_line(&ms, line, sizeof line);
    snprintf(txt, sizeof txt, TR("Meteorraj: %s (meteorszórás 6 és 2 m-en)", "Meteor shower: %s (meteor scatter on 6 and 2 m)"), line);
    return add(out, n, max, key, txt);
}

static int state_path_default(char *buf, size_t len)
{
    char dir[512];
    if (user_dir(USERDIR_CONFIG, dir, sizeof dir) != 0) return -1;
    snprintf(buf, len, "%s/alerts.state", dir);
    return 0;
}

/* állapotfájl: 1. sor "checked <idő>", utána "<seen> <alerted> <kulcs>" */
static int state_load(const char *path, StateRow *rows, int max, time_t *checked)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    int n = 0;
    *checked = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        long long a, b;
        int off = 0;
        if (strncmp(line, "checked ", 8) == 0) { *checked = (time_t)atoll(line + 8); continue; }
        if (n < max && sscanf(line, "%lld %lld %n", &a, &b, &off) == 2 && off > 0 && line[off]) {
            rows[n].seen = (time_t)a;
            rows[n].alerted = (time_t)b;
            snprintf(rows[n].key, sizeof rows[n].key, "%s", line + off);
            n++;
        }
    }
    fclose(f);
    return n;
}

static void state_save(const char *path, const StateRow *rows, int n, time_t checked)
{
    char tmp[620];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "checked %lld\n", (long long)checked);
    for (int i = 0; i < n; i++)
        fprintf(f, "%lld %lld %s\n", (long long)rows[i].seen, (long long)rows[i].alerted, rows[i].key);
    if (fclose(f) == 0) replace_file(tmp, path);
}

int alerts_new(const Alert *cur, int n, time_t now, const char *state_path,
               Alert *out, int max)
{
    char path[600];
    if (state_path) snprintf(path, sizeof path, "%s", state_path);
    else if (state_path_default(path, sizeof path) != 0) return 0;

    static StateRow rows[NSTATE];
    time_t checked = 0;
    int nr = state_load(path, rows, NSTATE, &checked);
    int first = nr < 0;
    if (nr < 0) nr = 0;

    int nout = 0;
    for (int i = 0; i < n; i++) {
        int r = -1;
        for (int j = 0; j < nr; j++)
            if (strcmp(rows[j].key, cur[i].key) == 0) { r = j; break; }
        /* az előző ellenőrzéskor is teljesült -> nem új */
        int was_open = r >= 0 && checked > 0 && rows[r].seen >= checked;
        int cooling = r >= 0 && now - rows[r].alerted < ALERT_COOLDOWN;
        if (r < 0) {
            if (nr >= NSTATE) {                    /* tele: a legrégebbit cseréljük */
                r = 0;
                for (int j = 1; j < nr; j++) if (rows[j].seen < rows[r].seen) r = j;
            } else r = nr++;
            snprintf(rows[r].key, sizeof rows[r].key, "%s", cur[i].key);
            rows[r].alerted = 0;
        }
        rows[r].seen = now;
        if (!first && !was_open && !cooling && nout < max) {
            out[nout++] = cur[i];
            rows[r].alerted = now;
        }
    }
    /* a régieket elfelejtjük */
    int k = 0;
    for (int j = 0; j < nr; j++)
        if (now - rows[j].seen < STATE_KEEP || now - rows[j].alerted < ALERT_COOLDOWN) rows[k++] = rows[j];
    state_save(path, rows, k, now);
    return nout;
}

void alerts_join(const Alert *a, int n, char *buf, size_t len)
{
    size_t l = 0;
    if (len) buf[0] = '\0';
    for (int i = 0; i < n && l + 1 < len; i++) {
        snprintf(buf + l, len - l, "%s%s", i ? "\n" : "", a[i].text);
        l = strlen(buf);
    }
}
