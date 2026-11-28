/*
 * fetch.c - adatok letöltése HTTP-n, helyi gyorsítótárral
 *   Linux:   libcurl
 *   Windows: WinHTTP (a Windows része, nem kell hozzá külön könyvtár)
 *
 * A letöltés először egy ideiglenes fájlba megy, és csak sikeres
 * letöltés után lesz átnevezve - így egy félbeszakadt letöltés nem
 * rontja el a már meglévő jó adatot.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

#include "fetch.h"
#include "compat.h"

#define USER_AGENT "IonoProp/0.9.1 (amateur radio propagation tool)"

#ifdef _WIN32
int fetch_init(void) { return 0; }
void fetch_cleanup(void) { }
#else
int fetch_init(void)
{
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}

void fetch_cleanup(void)
{
    curl_global_cleanup();
}
#endif

/* Hány másodperce módosult a fájl? -1, ha nem létezik. */
static long file_age(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)(time(NULL) - st.st_mtime);
}

#ifdef _WIN32
/* ---------- Windows: WinHTTP ---------- */

static void win_err(const char *url, const char *what)
{
    DWORD e = GetLastError();
    fprintf(stderr, "Letöltési hiba: %s\n  %s (Windows hibakód %lu)\n", url, what, (unsigned long)e);
}

static int download(const char *url, const char *dest)
{
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.part", dest);

    wchar_t wurl[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1024)) return -1;

    URL_COMPONENTS uc;
    wchar_t host[256], path[768];
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host;     uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;      uc.dwUrlPathLength = 768;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { win_err(url, "hibás cím"); return -1; }

    /* a Windows proxy-beállításait használja (Win 8.1-től automatikusan) */
    HINTERNET ses = WinHttpOpen(L"IonoProp/0.9.1 (amateur radio propagation tool)",
                                4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY */,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses)
        ses = WinHttpOpen(L"IonoProp/0.9.1 (amateur radio propagation tool)",
                          WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { win_err(url, "WinHTTP nem indul"); return -1; }
    WinHttpSetTimeouts(ses, 10000, 10000, 30000, 60000);
    {
        DWORD flags = 0x3;   /* WINHTTP_DECOMPRESSION_FLAG_ALL (gzip, deflate), Win 8.1-től */
        WinHttpSetOption(ses, 118 /* WINHTTP_OPTION_DECOMPRESSION */, &flags, sizeof flags);
    }

    int ok = -1;
    HINTERNET con = NULL, req = NULL;
    FILE *f = NULL;
    con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (!con) { win_err(url, "nem érhető el"); goto out; }
    req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { win_err(url, "kérés"); goto out; }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        win_err(url, "nincs kapcsolat");
        goto out;
    }
    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        fprintf(stderr, "Letöltési hiba: %s\n  HTTP %lu\n", url, (unsigned long)status);
        goto out;
    }
    f = fopen(tmp, "wb");
    if (!f) { fprintf(stderr, "Nem tudom írni: %s (%s)\n", tmp, strerror(errno)); goto out; }
    for (;;) {
        char buf[16384];
        DWORD got = 0;
        if (!WinHttpReadData(req, buf, sizeof buf, &got)) { win_err(url, "olvasás"); goto out; }
        if (got == 0) break;
        if (fwrite(buf, 1, got, f) != got) { fprintf(stderr, "Írási hiba: %s\n", tmp); goto out; }
    }
    if (fclose(f) != 0) { f = NULL; goto out; }
    f = NULL;
    if (replace_file(tmp, dest) != 0) {
        fprintf(stderr, "Nem tudom átnevezni: %s\n", tmp);
        goto out;
    }
    ok = 0;
out:
    if (f) fclose(f);
    if (ok != 0) remove(tmp);
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return ok;
}

#else
/* ---------- Linux: libcurl ---------- */

static int download(const char *url, const char *dest)
{
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.part", dest);

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        fprintf(stderr, "Nem tudom írni: %s (%s)\n", tmp, strerror(errno));
        return -1;
    }

    CURL *c = curl_easy_init();
    if (!c) { fclose(f); remove(tmp); return -1; }

    char errbuf[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);          /* alap: fwrite */
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");   /* gzip, ha a szerver tudja */
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);

    if (fclose(f) != 0) rc = CURLE_WRITE_ERROR;

    if (rc != CURLE_OK) {
        fprintf(stderr, "Letöltési hiba: %s\n  %s\n", url,
                errbuf[0] ? errbuf : curl_easy_strerror(rc));
        remove(tmp);
        return -1;
    }
    if (http != 200) {
        fprintf(stderr, "Letöltési hiba: %s\n  HTTP %ld\n", url, http);
        remove(tmp);
        return -1;
    }
    if (replace_file(tmp, dest) != 0) {
        fprintf(stderr, "Nem tudom átnevezni: %s (%s)\n", tmp, strerror(errno));
        remove(tmp);
        return -1;
    }
    return 0;
}

#endif

/* Kíméletes letöltés: hiba után egyre hosszabb szünet (1, 2, 4, 8, 15 perc),
 * hogy egy nem válaszoló szervert ne terheljen sok program percenként. */
#define BACKOFF_MAX   (15 * 60)
#define FORCE_MIN_AGE (5 * 60)     /* kézi frissítésnél is ennyi idő (lassan változó adatnál) */

typedef struct { char name[64]; int fails; time_t next_try; } Backoff;
static Backoff g_backoff[16];

static Backoff *backoff_for(const char *fname)
{
    for (int i = 0; i < 16; i++)
        if (strcmp(g_backoff[i].name, fname) == 0) return &g_backoff[i];
    for (int i = 0; i < 16; i++)
        if (!g_backoff[i].name[0]) {
            snprintf(g_backoff[i].name, sizeof g_backoff[i].name, "%s", fname);
            return &g_backoff[i];
        }
    return NULL;
}

int fetch_cached(const char *url, const char *fname, int max_age, int force,
                 char *out_path, size_t out_len)
{
    char dir[512];
    if (cache_dir(dir, sizeof dir) != 0) return -1;
    snprintf(out_path, out_len, "%s/%s", dir, fname);

    long age = file_age(out_path);
    if (!force && age >= 0 && age < max_age)
        return 1;                       /* még friss */
    /* a lassan változó adatot (pl. KC2G, 15 perc) kézi frissítéskor sem
     * kérjük le 5 percen belül újra */
    if (force && max_age > FORCE_MIN_AGE && age >= 0 && age < FORCE_MIN_AGE)
        return 1;

    Backoff *bo = backoff_for(fname);
    time_t now = time(NULL);
    if (bo && bo->fails > 0 && now < bo->next_try) {
        /* nemrég nem sikerült: most nem próbáljuk újra */
        return age >= 0 ? 1 : -1;
    }

    if (download(url, out_path) == 0) {
        if (bo) bo->fails = 0;
        return 0;
    }
    if (bo) {
        bo->fails++;
        long wait = 60L << (bo->fails > 5 ? 5 : bo->fails - 1);
        if (wait > BACKOFF_MAX) wait = BACKOFF_MAX;
        bo->next_try = now + wait;
    }

    if (age >= 0) {
        fprintf(stderr, "  -> a korábbi (%ld perces) adatot használom.\n", age / 60);
        return 1;
    }
    return -1;
}
