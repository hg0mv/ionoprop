/*
 * config.c - hangolt paraméterek mentése / betöltése
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#include "config.h"
#include "compat.h"

static int config_dir(char *dir, size_t len)
{
    return user_dir(USERDIR_CONFIG, dir, len);
}

/* ---------- settings.conf: egyszerű "kulcs = érték" sorok ---------- */

static int settings_path(char *path, size_t len)
{
    char dir[512];
    if (config_dir(dir, sizeof dir) != 0) return -1;
    snprintf(path, len, "%s/settings.conf", dir);
    return 0;
}

int config_get(const char *key, char *buf, size_t len)
{
    char path[600];
    if (settings_path(path, sizeof path) != 0) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[700];
    int found = 0;
    size_t kl = strlen(key);
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || strncmp(p, key, kl) != 0) continue;
        p += kl;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        size_t n = strcspn(p, "\r\n");
        while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        snprintf(buf, len, "%.*s", (int)n, p);
        found = 1;
    }
    fclose(f);
    return found;
}

int config_set(const char *key, const char *value)
{
    char path[600], tmp[620];
    if (settings_path(path, sizeof path) != 0) return -1;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    /* a többi sort megtartjuk, a kulcs régi sorát kihagyjuk */
    FILE *in = fopen(path, "r");
    FILE *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return -1; }
    int header = 0;
    size_t kl = strlen(key);
    if (in) {
        char line[700];
        while (fgets(line, sizeof line, in)) {
            const char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "# IonoProp", 10) == 0) header = 1;
            if (*p != '#' && strncmp(p, key, kl) == 0) {
                const char *q = p + kl;
                while (*q == ' ' || *q == '\t') q++;
                if (*q == '=') continue;
            }
            fputs(line, out);
            size_t n = strlen(line);
            if (n && line[n - 1] != '\n') fputc('\n', out);
        }
        fclose(in);
    }
    if (!header && !in) fprintf(out, "# IonoProp beállítások\n");
    if (value) fprintf(out, "%s = %s\n", key, value);
    if (fclose(out) != 0) { remove(tmp); return -1; }
    return replace_file(tmp, path);
}

int config_load_locator(char *buf, size_t len)
{
    char v[64];
    if (!config_get("locator", v, sizeof v) || !*v) return 0;
    snprintf(buf, len, "%s", v);
    return 1;
}

int config_save_locator(const char *loc)
{
    return config_set("locator", loc);
}

int config_path(char *buf, size_t len)
{
    char dir[512];
    if (config_dir(dir, sizeof dir) != 0) return -1;
    snprintf(buf, len, "%s/params.conf", dir);
    return 0;
}

int config_load(PredictParams *p)
{
    char path[600];
    if (config_path(path, sizeof path) != 0) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char key[64];
        double v;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, " %63[a-z_] = %lf", key, &v) != 2) continue;
        if (!strcmp(key, "muf_scale")  && v >= 0.5 && v <= 2.0)  { p->muf_scale = v; n++; }
        if (!strcmp(key, "abs_fair")   && v > 0 && v < 200)       { p->abs_fair = v; n++; }
        if (!strcmp(key, "abs_poor")   && v > 0 && v < 200)       { p->abs_poor = v; n++; }
        if (!strcmp(key, "abs_closed") && v > 0 && v < 300)       { p->abs_closed = v; n++; }
    }
    fclose(f);

    /* a sorrendnek stimmelnie kell: fair < poor < closed */
    if (!(p->abs_fair < p->abs_poor && p->abs_poor < p->abs_closed)) {
        fprintf(stderr, "Hibás params.conf (%s) - alapértékeket használok.\n", path);
        predict_params_default(p);
        return -1;
    }
    return n > 0 ? 1 : 0;
}

int config_save(const PredictParams *p, const char *comment)
{
    char path[600], tmp[620];
    if (config_path(path, sizeof path) != 0) return -1;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;

    char tb[32];
    time_t now = time(NULL);
    strftime(tb, sizeof tb, "%Y-%m-%d %H:%M", localtime(&now));
    fprintf(f, "# IonoProp hangolt paraméterek - mentve: %s\n", tb);
    if (comment && *comment) fprintf(f, "# %s\n", comment);
    fprintf(f, "# Törölheted ezt a fájlt, akkor az alapértékek érvényesek.\n\n");
    fprintf(f, "# a számolt MUF szorzója (1.0 = változatlan)\n");
    fprintf(f, "muf_scale = %.3f\n\n", p->muf_scale);
    fprintf(f, "# D-réteg elnyelés határai dB-ben: e fölött 1 / 2 fokozattal rosszabb, ill. zárva\n");
    fprintf(f, "abs_fair = %.1f\n", p->abs_fair);
    fprintf(f, "abs_poor = %.1f\n", p->abs_poor);
    fprintf(f, "abs_closed = %.1f\n", p->abs_closed);
    if (fclose(f) != 0) { remove(tmp); return -1; }
    return replace_file(tmp, path);
}
