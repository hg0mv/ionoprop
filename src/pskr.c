/*
 * pskr.c - PSK Reporter MQTT-gyűjtő (lásd pskr.h)
 *
 * Témastruktúra (mqtt.pskreporter.info):
 *   pskr/filter/v2/{sáv}/{mód}/{adó}/{vevő}/{adó_lok4}/{vevő_lok4}/{adó_dxcc}/{vevő_dxcc}
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "../third_party/cJSON.h"
#include "pskr.h"
#include "i18n.h"
#include "geo.h"
#include "predict.h"

/* ---------- feldolgozás (mosquitto nélkül is) ---------- */

int pskr_band(double f)
{
    /* a 11 m (CB / "freeband") nem amatőrsáv, de a 27 MHz-es FT8-forgalom
     * ugyanúgy terjed, ezért külön sávként kezeljük */
    static const double lo[] = { 1.8, 3.5, 5.25, 7.0, 10.1, 14.0, 18.068, 21.0, 24.89, 26.0, 28.0, 40.0, 50.0 };
    static const double hi[] = { 2.0, 4.0, 5.45, 7.3, 10.15, 14.35, 18.168, 21.45, 24.99, 27.99, 29.7, 41.0, 54.0 };
    for (int b = 0; b < NBANDS; b++)
        if (f >= lo[b] && f <= hi[b]) return b;
    return -1;
}

static int parse_loc(const char *loc, double *lat, double *lon)
{
    if (!loc) return -1;
    char buf[12];
    size_t n = strlen(loc);
    if (n > 8) n = 8;                 /* 10 karakteresnél elég 8 */
    if (n & 1) n--;                   /* páratlan hossz: levágjuk */
    if (n < 4) return -1;
    memcpy(buf, loc, n);
    buf[n] = '\0';
    while (n >= 4) {                  /* ha a vége hibás, rövidebben próbáljuk */
        buf[n] = '\0';
        if (locator_to_latlon(buf, lat, lon) == 0) return 0;
        n -= 2;
    }
    return -1;
}

static void copy_str(char *dst, size_t len, const cJSON *it)
{
    if (cJSON_IsString(it) && it->valuestring) snprintf(dst, len, "%s", it->valuestring);
    else dst[0] = '\0';
}

int pskr_parse(const char *json, PskSpot *s, long long *sq)
{
    memset(s, 0, sizeof *s);
    cJSON *r = cJSON_Parse(json);
    if (!r) return -1;
    int ok = -1;

    const cJSON *f  = cJSON_GetObjectItemCaseSensitive(r, "f");
    const cJSON *rp = cJSON_GetObjectItemCaseSensitive(r, "rp");
    const cJSON *t  = cJSON_GetObjectItemCaseSensitive(r, "t");
    const cJSON *q  = cJSON_GetObjectItemCaseSensitive(r, "sq");
    const cJSON *sl = cJSON_GetObjectItemCaseSensitive(r, "sl");
    const cJSON *rl = cJSON_GetObjectItemCaseSensitive(r, "rl");

    if (!cJSON_IsNumber(f)) goto out;
    s->f_mhz = f->valuedouble / 1e6;
    s->band = pskr_band(s->f_mhz);
    if (s->band < 0) goto out;

    if (parse_loc(cJSON_IsString(sl) ? sl->valuestring : NULL, &s->slat, &s->slon) != 0) goto out;
    if (parse_loc(cJSON_IsString(rl) ? rl->valuestring : NULL, &s->rlat, &s->rlon) != 0) goto out;

    s->snr = cJSON_IsNumber(rp) ? (int)rp->valuedouble : -30;
    double tt = cJSON_IsNumber(t) ? t->valuedouble : 0;
    if (tt > 1e11) tt /= 1000.0;                     /* ezredmásodpercben jött */
    time_t now = time(NULL);
    s->t = (tt > 0 && fabs(tt - (double)now) < 86400) ? (time_t)tt : now;
    if (sq) *sq = cJSON_IsNumber(q) ? (long long)q->valuedouble : 0;

    copy_str(s->mode, sizeof s->mode, cJSON_GetObjectItemCaseSensitive(r, "md"));
    copy_str(s->sc, sizeof s->sc, cJSON_GetObjectItemCaseSensitive(r, "sc"));
    copy_str(s->rc, sizeof s->rc, cJSON_GetObjectItemCaseSensitive(r, "rc"));
    ok = 0;
out:
    cJSON_Delete(r);
    return ok;
}

int pskr_classify(PskSpot *s, double qlat, double qlon, double radius_km)
{
    double ds = distance_km(qlat, qlon, s->slat, s->slon);
    double dr = distance_km(qlat, qlon, s->rlat, s->rlon);
    if (ds > radius_km && dr > radius_km) return -1;

    s->local_tx = ds <= dr;
    if (s->local_tx) {
        s->nlat = s->slat; s->nlon = s->slon; s->flat = s->rlat; s->flon = s->rlon;
    } else {
        s->nlat = s->rlat; s->nlon = s->rlon; s->flat = s->slat; s->flon = s->slon;
    }
    s->dist_km = distance_km(s->slat, s->slon, s->rlat, s->rlon);
    return s->dist_km >= PSKR_MIN_KM ? 0 : -1;      /* földi hullám: kihagyjuk */
}

#ifdef IONOPROP_NO_PSK
/* ---------- gyűjtő nélküli változat ---------- */
Pskr *pskr_start(const char *h, int p, double a, double b, double r)
{
    (void)h; (void)p; (void)a; (void)b; (void)r;
    return NULL;
}
int  pskr_poll(Pskr *p, PskSpot *o, int m) { (void)p; (void)o; (void)m; return 0; }
void pskr_status(Pskr *p, char *b, int l) { (void)p; snprintf(b, (size_t)l, "%s", TR("PSK Reporter: nincs beépítve", "PSK Reporter: not built in")); }
int  pskr_connected(Pskr *p) { (void)p; return 0; }
void pskr_stop(Pskr *p) { (void)p; }

#else
/* ---------- élő gyűjtés: közös rész ----------
 * Két átvitel közül fordításkor lehet választani:
 *   alapból:              libmosquitto (asztali gép)
 *   -DIONOPROP_MINI_MQTT: saját kis MQTT 3.1.1 kliens sima socketen
 *                         (Android és Windows)
 */
#if defined(IONOPROP_MINI_MQTT) && defined(_WIN32)
/* Windows: a winsock2.h-nak a windows.h (és a pthread.h) előtt kell lennie */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
typedef SOCKET sock_t;
#define SOCK_NONE        INVALID_SOCKET
#define sock_close(s)    closesocket(s)
#define sock_poll        WSAPoll
#define get_pid()        _getpid()
#define msleep(ms)       Sleep(ms)
#define SEND_FLAGS       0
#endif

#include <pthread.h>
#include <unistd.h>

#ifdef IONOPROP_MINI_MQTT
#include <errno.h>
#ifndef _WIN32
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
typedef int sock_t;
#define SOCK_NONE        (-1)
#define sock_close(s)    close(s)
#define sock_poll        poll
#define get_pid()        getpid()
#define msleep(ms)       usleep((ms) * 1000)
#define SEND_FLAGS       MSG_NOSIGNAL
#endif
#else
#include <mosquitto.h>
#endif

#define RING     8192
#define DEDUP    2048
#define MAXGRID  80

struct Pskr {
    pthread_mutex_t lock;
    double qlat, qlon, radius;
    char   grid[MAXGRID][5];
    int    ngrid;

    PskSpot ring[RING];
    int     head, tail;          /* head: írás, tail: olvasás */
    long long seen[DEDUP];
    int     seen_pos;

    int     connected;
    long    n_msg, n_used, n_drop;
    time_t  started;
    char    host[128];
    int     port;

#ifdef IONOPROP_MINI_MQTT
    pthread_t thread;
    int       thread_ok;
    volatile int stop;
    sock_t    sock;
    char      last_err[192];
#else
    struct mosquitto *mq;
#endif
};

/* egy beérkezett üzenet (JSON) feldolgozása: szűrés, duplikátum, pufferbe */
static void handle_payload(Pskr *p, const char *payload, int len)
{
    if (!payload || len <= 0 || len > 4000) return;
    char buf[4096];
    memcpy(buf, payload, (size_t)len);
    buf[len] = '\0';

    PskSpot s;
    long long sq = 0;
    int ok = pskr_parse(buf, &s, &sq) == 0 &&
             pskr_classify(&s, p->qlat, p->qlon, p->radius) == 0;

    pthread_mutex_lock(&p->lock);
    p->n_msg++;
    if (ok && sq) {                           /* duplikátum? */
        for (int i = 0; i < DEDUP; i++)
            if (p->seen[i] == sq) { ok = 0; break; }
        if (ok) { p->seen[p->seen_pos] = sq; p->seen_pos = (p->seen_pos + 1) % DEDUP; }
    }
    if (ok) {
        int next = (p->head + 1) % RING;
        if (next == p->tail) { p->tail = (p->tail + 1) % RING; p->n_drop++; }  /* tele: a legrégebbi megy */
        p->ring[p->head] = s;
        p->head = next;
        p->n_used++;
    }
    pthread_mutex_unlock(&p->lock);
}

static void set_connected(Pskr *p, int c)
{
    pthread_mutex_lock(&p->lock);
    p->connected = c;
    pthread_mutex_unlock(&p->lock);
}

/* a QTH körüli 4 karakteres lokátornégyzetek */
static void make_grids(Pskr *p)
{
    double dlat = p->radius / 111.0 + 1.0;
    double dlon = dlat / cos(p->qlat * M_PI / 180.0) + 2.0;
    p->ngrid = 0;
    for (double la = floor(p->qlat - dlat); la <= p->qlat + dlat; la += 1.0)
        for (double lo = floor((p->qlon - dlon) / 2) * 2; lo <= p->qlon + dlon; lo += 2.0) {
            double cla = la + 0.5, clo = lo + 1.0;     /* a négyzet közepe */
            if (cla < -89 || cla > 89) continue;
            if (distance_km(p->qlat, p->qlon, cla, clo) > p->radius + 120.0) continue;
            if (p->ngrid >= MAXGRID) return;
            char loc[8];
            latlon_to_locator(cla, norm_lon(clo), loc);
            memcpy(p->grid[p->ngrid], loc, 4);
            p->grid[p->ngrid][4] = '\0';
            int dup = 0;
            for (int i = 0; i < p->ngrid; i++)
                if (!strcmp(p->grid[i], p->grid[p->ngrid])) dup = 1;
            if (!dup) p->ngrid++;
        }
}

/* a feliratkozási témák: i. négyzet, adó (tx=1) vagy vevő oldalon */
static void grid_topic(const Pskr *p, int i, int tx, char *topic, size_t len)
{
    if (tx) snprintf(topic, len, "pskr/filter/v2/+/+/+/+/%s/#", p->grid[i]);
    else    snprintf(topic, len, "pskr/filter/v2/+/+/+/+/+/%s/#", p->grid[i]);
}

static Pskr *pskr_alloc(const char *host, int port, double qlat, double qlon, double radius_km)
{
    Pskr *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    pthread_mutex_init(&p->lock, NULL);
    p->qlat = qlat;
    p->qlon = qlon;
    p->radius = radius_km;
    p->started = time(NULL);
    snprintf(p->host, sizeof p->host, "%s", host ? host : PSKR_HOST);
    p->port = port > 0 ? port : PSKR_PORT;
    make_grids(p);
    return p;
}

int pskr_poll(Pskr *p, PskSpot *out, int max)
{
    if (!p) return 0;
    int n = 0;
    pthread_mutex_lock(&p->lock);
    while (p->tail != p->head && n < max) {
        out[n++] = p->ring[p->tail];
        p->tail = (p->tail + 1) % RING;
    }
    pthread_mutex_unlock(&p->lock);
    return n;
}

int pskr_connected(Pskr *p)
{
    if (!p) return 0;
    pthread_mutex_lock(&p->lock);
    int c = p->connected;
    pthread_mutex_unlock(&p->lock);
    return c;
}

void pskr_status(Pskr *p, char *buf, int len)
{
    if (!p) { snprintf(buf, (size_t)len, "%s", TR("PSK Reporter: ki", "PSK Reporter: off")); return; }
    pthread_mutex_lock(&p->lock);
#ifdef IONOPROP_MINI_MQTT
    if (!p->connected && p->last_err[0]) {
        snprintf(buf, (size_t)len, TR("PSK Reporter: újrapróbálja (%s)", "PSK Reporter: retrying (%s)"), p->last_err);
        pthread_mutex_unlock(&p->lock);
        return;
    }
#endif
    snprintf(buf, (size_t)len, TR("PSK Reporter: %s, %d négyzet, %ld spot a közelből (%ld üzenet)",
                                   "PSK Reporter: %s, %d squares, %ld nearby spots (%ld messages)"),
             p->connected ? TR("kapcsolódva", "connected") : TR("kapcsolódás...", "connecting..."),
             p->ngrid, p->n_used, p->n_msg);
    pthread_mutex_unlock(&p->lock);
}

#ifndef IONOPROP_MINI_MQTT
/* ---------- átvitel: libmosquitto ---------- */

static int lib_inited;

static void on_connect(struct mosquitto *mq, void *ud, int rc)
{
    Pskr *p = ud;
    set_connected(p, rc == 0);
    if (rc != 0) return;
    /* (újra)feliratkozás: adó- és vevőoldali lokátornégyzetekre */
    for (int i = 0; i < p->ngrid; i++)
        for (int tx = 1; tx >= 0; tx--) {
            char topic[96];
            grid_topic(p, i, tx, topic, sizeof topic);
            mosquitto_subscribe(mq, NULL, topic, 0);
        }
}

static void on_disconnect(struct mosquitto *mq, void *ud, int rc)
{
    (void)mq; (void)rc;
    set_connected((Pskr *)ud, 0);
}

static void on_message(struct mosquitto *mq, void *ud, const struct mosquitto_message *msg)
{
    (void)mq;
    handle_payload((Pskr *)ud, msg->payload, msg->payloadlen);
}

Pskr *pskr_start(const char *host, int port, double qlat, double qlon, double radius_km)
{
    if (!lib_inited) { mosquitto_lib_init(); lib_inited = 1; }
    Pskr *p = pskr_alloc(host, port, qlat, qlon, radius_km);
    if (!p) return NULL;

    char id[48];
    snprintf(id, sizeof id, "ionoprop-%d-%ld", (int)getpid(), (long)time(NULL) % 100000);
    p->mq = mosquitto_new(id, true, p);
    if (!p->mq) { free(p); return NULL; }
    mosquitto_connect_callback_set(p->mq, on_connect);
    mosquitto_disconnect_callback_set(p->mq, on_disconnect);
    mosquitto_message_callback_set(p->mq, on_message);
    mosquitto_reconnect_delay_set(p->mq, 5, 120, true);

    int rc = mosquitto_connect_async(p->mq, p->host, p->port, 60);
    if (rc != MOSQ_ERR_SUCCESS)
        fprintf(stderr, "PSK Reporter: kapcsolódási hiba: %s (újrapróbálja)\n", mosquitto_strerror(rc));
    if (mosquitto_loop_start(p->mq) != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(p->mq);
        free(p);
        return NULL;
    }
    return p;
}

void pskr_stop(Pskr *p)
{
    if (!p) return;
    mosquitto_disconnect(p->mq);
    mosquitto_loop_stop(p->mq, true);
    mosquitto_destroy(p->mq);
    pthread_mutex_destroy(&p->lock);
    free(p);
}

#else
/* ---------- átvitel: saját kis MQTT 3.1.1 kliens ----------
 * Csak amit a PSK Reporter olvasásához kell: CONNECT, SUBSCRIBE (QoS 0),
 * PUBLISH fogadása, PINGREQ. Saját szálon fut, kapcsolat bontásakor
 * 5 mp-től 2 percig növekvő várakozással újrakapcsolódik.
 */
#define KEEPALIVE 60

static int sock_send_all(sock_t fd, const unsigned char *b, size_t n)
{
    while (n > 0) {
        int w = (int)send(fd, (const char *)b, (int)n, SEND_FLAGS);
        if (w < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            return -1;
        }
        b += w;
        n -= (size_t)w;
    }
    return 0;
}

/* pontosan n bájt olvasása; a stop jelzőt fél másodpercenként figyeli.
 * Visszatérés: 0 rendben, -1 hiba/bontás, -2 leállítás */
static int sock_recv_all(Pskr *p, unsigned char *b, size_t n, time_t *last_tx)
{
    while (n > 0) {
        if (p->stop) return -2;
        struct pollfd pf = { p->sock, POLLIN, 0 };
        int r = sock_poll(&pf, 1, 500);
        if (r < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            return -1;
        }
        if (r == 0) {
            /* csend: ha kell, PINGREQ, hogy a szerver ne bontson */
            if (time(NULL) - *last_tx >= KEEPALIVE / 2) {
                static const unsigned char ping[2] = { 0xC0, 0x00 };
                if (sock_send_all(p->sock, ping, 2) != 0) return -1;
                *last_tx = time(NULL);
            }
            continue;
        }
        int got = (int)recv(p->sock, (char *)b, (int)n, 0);
        if (got == 0) return -1;
        if (got < 0) {
#ifndef _WIN32
            if (errno == EINTR || errno == EAGAIN) continue;
#endif
            return -1;
        }
        b += got;
        n -= (size_t)got;
    }
    return 0;
}

/* MQTT "remaining length" kódolása */
static size_t put_len(unsigned char *b, size_t len)
{
    size_t i = 0;
    do {
        unsigned char d = len % 128;
        len /= 128;
        if (len > 0) d |= 0x80;
        b[i++] = d;
    } while (len > 0);
    return i;
}

static size_t put_str(unsigned char *b, const char *s)
{
    size_t n = strlen(s);
    b[0] = (unsigned char)(n >> 8);
    b[1] = (unsigned char)(n & 0xff);
    memcpy(b + 2, s, n);
    return n + 2;
}

static int mqtt_connect_sock(Pskr *p)
{
    char port[16];
    snprintf(port, sizeof port, "%d", p->port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(p->host, port, &hints, &res);
    if (rc != 0) {
        snprintf(p->last_err, sizeof p->last_err, TR("névfeloldás: %s", "name lookup: %s"), gai_strerror(rc));
        return -1;
    }
    sock_t fd = SOCK_NONE;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd == SOCK_NONE) continue;
        if (connect(fd, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        sock_close(fd);
        fd = SOCK_NONE;
    }
    freeaddrinfo(res);
    if (fd == SOCK_NONE) {
        snprintf(p->last_err, sizeof p->last_err, TR("nem érhető el: %s:%d", "unreachable: %s:%d"), p->host, p->port);
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    p->sock = fd;
    return 0;
}

/* egy MQTT-kapcsolat teljes élete: kapcsolódás, feliratkozás, olvasás.
 * Visszatérés: -2 ha leállítást kértek */
static int mqtt_session(Pskr *p)
{
    if (mqtt_connect_sock(p) != 0) return -1;

    unsigned char buf[512], hdr[8];
    time_t last_tx = time(NULL);

    /* CONNECT */
    char id[48];
    snprintf(id, sizeof id, "ionoprop-%d-%ld", (int)get_pid(), (long)time(NULL) % 100000);
    unsigned char var[64];
    size_t vn = put_str(var, "MQTT");
    var[vn++] = 4;                       /* protokollszint 3.1.1 */
    var[vn++] = 0x02;                    /* clean session */
    var[vn++] = 0;
    var[vn++] = KEEPALIVE;
    vn += put_str(var + vn, id);
    size_t n = 0;
    buf[n++] = 0x10;
    n += put_len(buf + n, vn);
    memcpy(buf + n, var, vn);
    n += vn;
    if (sock_send_all(p->sock, buf, n) != 0) goto fail;

    /* CONNACK */
    if (sock_recv_all(p, hdr, 4, &last_tx) != 0) goto fail;
    if (hdr[0] != 0x20 || hdr[3] != 0) {
        snprintf(p->last_err, sizeof p->last_err, TR("a szerver elutasította (kód %d)", "rejected by server (code %d)"), hdr[3]);
        goto fail;
    }

    /* SUBSCRIBE: négyzetenként adó- és vevőoldal, csomagonként 8 téma */
    {
        int total = p->ngrid * 2, idx = 0;
        unsigned short pid = 1;
        while (idx < total) {
            unsigned char pay[400];
            size_t pn = 0;
            pay[pn++] = (unsigned char)(pid >> 8);
            pay[pn++] = (unsigned char)(pid & 0xff);
            for (int k = 0; k < 8 && idx < total; k++, idx++) {
                char topic[96];
                grid_topic(p, idx / 2, idx % 2 == 0, topic, sizeof topic);
                pn += put_str(pay + pn, topic);
                pay[pn++] = 0;                     /* QoS 0 */
            }
            n = 0;
            buf[n++] = 0x82;
            n += put_len(buf + n, pn);
            memcpy(buf + n, pay, pn);
            n += pn;
            if (sock_send_all(p->sock, buf, n) != 0) goto fail;
            pid++;
        }
        last_tx = time(NULL);
    }
    set_connected(p, 1);
    p->last_err[0] = '\0';

    /* olvasási ciklus */
    unsigned char *msg = malloc(65536);
    if (!msg) goto fail;
    for (;;) {
        unsigned char b0;
        int r = sock_recv_all(p, &b0, 1, &last_tx);
        if (r != 0) { free(msg); if (r == -2) goto stopped; goto fail; }
        size_t len = 0, mul = 1;
        for (int i = 0; i < 4; i++) {
            unsigned char d;
            if ((r = sock_recv_all(p, &d, 1, &last_tx)) != 0) { free(msg); if (r == -2) goto stopped; goto fail; }
            len += (d & 0x7F) * mul;
            mul *= 128;
            if (!(d & 0x80)) break;
        }
        if (len > 65535) { free(msg); snprintf(p->last_err, sizeof p->last_err, "%s", TR("túl nagy üzenet", "message too large")); goto fail; }
        if (len && (r = sock_recv_all(p, msg, len, &last_tx)) != 0) { free(msg); if (r == -2) goto stopped; goto fail; }

        if ((b0 & 0xF0) == 0x30 && len >= 2) {          /* PUBLISH */
            size_t tl = ((size_t)msg[0] << 8) | msg[1];
            size_t off = 2 + tl;
            if (((b0 >> 1) & 3) > 0) off += 2;          /* QoS>0: csomagazonosító */
            if (off <= len) handle_payload(p, (const char *)msg + off, (int)(len - off));
        }
        /* SUBACK (0x90), PINGRESP (0xD0): nincs vele teendő */
    }

stopped:
    {
        static const unsigned char disc[2] = { 0xE0, 0x00 };
        sock_send_all(p->sock, disc, 2);
        sock_close(p->sock);
        p->sock = SOCK_NONE;
        set_connected(p, 0);
        return -2;
    }
fail:
    if (!p->last_err[0]) snprintf(p->last_err, sizeof p->last_err, "%s", TR("a kapcsolat megszakadt", "connection lost"));
    sock_close(p->sock);
    p->sock = SOCK_NONE;
    set_connected(p, 0);
    return -1;
}

static void *mqtt_thread(void *arg)
{
    Pskr *p = arg;
    int delay = 5;
    while (!p->stop) {
        time_t t0 = time(NULL);
        if (mqtt_session(p) == -2) break;
        if (time(NULL) - t0 > 120) delay = 5;          /* sokáig ment: elölről */
        for (int i = 0; i < delay * 2 && !p->stop; i++) msleep(500);
        delay = delay * 2 > 120 ? 120 : delay * 2;
    }
    return NULL;
}

Pskr *pskr_start(const char *host, int port, double qlat, double qlon, double radius_km)
{
#ifdef _WIN32
    static int wsa_ok;
    if (!wsa_ok) {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return NULL;
        wsa_ok = 1;
    }
#endif
    Pskr *p = pskr_alloc(host, port, qlat, qlon, radius_km);
    if (!p) return NULL;
    p->sock = SOCK_NONE;
    if (pthread_create(&p->thread, NULL, mqtt_thread, p) != 0) {
        pthread_mutex_destroy(&p->lock);
        free(p);
        return NULL;
    }
    p->thread_ok = 1;
    return p;
}

void pskr_stop(Pskr *p)
{
    if (!p) return;
    p->stop = 1;
    if (p->thread_ok) pthread_join(p->thread, NULL);   /* max. kb. fél mp */
    pthread_mutex_destroy(&p->lock);
    free(p);
}
#endif /* IONOPROP_MINI_MQTT */
#endif /* IONOPROP_NO_PSK */
