/*
 * rig.c - Yaesu CAT (FT-891) vezérlés soros porton (lásd rig.h)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#include "rig.h"
#include "predict.h"
#include "i18n.h"

struct Rig {
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
};

/* Szokásos frekvenciák kHz-ben: [sáv][SSB, CW, FT8] */
static const double FREQ_KHZ[NBANDS][NMODES] = {
    /* 160m */ {  1850.0,  1830.0,  1840.0 },
    /*  80m */ {  3700.0,  3530.0,  3573.0 },
    /*  60m */ {  5363.0,  5352.0,  5357.0 },
    /*  40m */ {  7150.0,  7025.0,  7074.0 },
    /*  30m */ { 10120.0, 10120.0, 10136.0 },   /* 30 m-en nincs SSB: CW */
    /*  20m */ { 14250.0, 14040.0, 14074.0 },
    /*  17m */ { 18140.0, 18080.0, 18100.0 },
    /*  15m */ { 21300.0, 21040.0, 21074.0 },
    /*  12m */ { 24950.0, 24900.0, 24915.0 },
    /*  11m */ { 27555.0, 27025.0, 27265.0 },
    /*  10m */ { 28500.0, 28040.0, 28074.0 },
    /*   8m */ { 40680.0, 40670.0, 40680.0 },
    /*   6m */ { 50150.0, 50090.0, 50313.0 },
};

long rig_band_freq(int band, int mode)
{
    if (band < 0 || band >= NBANDS) return 0;
    if (mode < 0 || mode >= NMODES) mode = MODE_SSB;
    return (long)(FREQ_KHZ[band][mode] * 1000.0 + 0.5);
}

int rig_parse_spec(const char *spec, char *port, size_t portlen, int *baud)
{
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s", spec ? spec : "");
    char *p = tmp;
    while (isspace((unsigned char)*p)) p++;
    char *comma = strchr(p, ',');
    *baud = 4800;
    if (comma) {
        *comma = '\0';
        int b = atoi(comma + 1);
        if (b != 4800 && b != 9600 && b != 19200 && b != 38400 && b != 57600 && b != 115200) return -1;
        *baud = b;
    }
    size_t n = strlen(p);
    while (n > 0 && isspace((unsigned char)p[n - 1])) p[--n] = '\0';
    if (!*p) return -1;
    snprintf(port, portlen, "%s", p);
    return 0;
}

#ifdef _WIN32

Rig *rig_open(const char *port, int baud, char *err, size_t errlen)
{
    char path[64];
    /* COM10 fölött csak így nyitható meg */
    if (strncmp(port, "\\\\.\\", 4) == 0) snprintf(path, sizeof path, "%s", port);
    else snprintf(path, sizeof path, "\\\\.\\%s", port);
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(err, errlen, TR("Nem nyitható meg: %s (hibakód %lu)", "Cannot open %s (error %lu)"), port, (unsigned long)GetLastError());
        return NULL;
    }
    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(h, &dcb);
    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = TWOSTOPBITS;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;     /* ne kapcsoljon adásba */
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = dcb.fInX = FALSE;
    if (!SetCommState(h, &dcb)) {
        snprintf(err, errlen, TR("Nem állítható be a port: %s", "Cannot configure port %s"), port);
        CloseHandle(h);
        return NULL;
    }
    COMMTIMEOUTS to = { 0, 0, 200, 0, 1000 };
    SetCommTimeouts(h, &to);
    Rig *r = calloc(1, sizeof *r);
    if (!r) { CloseHandle(h); return NULL; }
    r->h = h;
    return r;
}

static int rig_write(Rig *r, const char *s)
{
    DWORD n = (DWORD)strlen(s), w = 0;
    return (WriteFile(r->h, s, n, &w, NULL) && w == n) ? 0 : -1;
}

void rig_close(Rig *r)
{
    if (!r) return;
    CloseHandle(r->h);
    free(r);
}

#else

static speed_t to_speed(int baud)
{
    switch (baud) {
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    }
    return B4800;
}

Rig *rig_open(const char *port, int baud, char *err, size_t errlen)
{
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        if (errno == EACCES)
            snprintf(err, errlen, TR("Nincs jogod a porthoz: %s (sudo usermod -aG dialout $USER, majd újra be kell jelentkezni)",
                        "No permission for %s (sudo usermod -aG dialout $USER, then log in again)"), port);
        else
            snprintf(err, errlen, TR("Nem nyitható meg: %s (%s)", "Cannot open %s (%s)"), port, strerror(errno));
        return NULL;
    }
    /* a DTR/RTS azonnal ki: egy "PTT = DTR/RTS" beállítású rádió ne adjon */
    int bits = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIC, &bits);

    struct termios t;
    if (tcgetattr(fd, &t) != 0) {
        snprintf(err, errlen, TR("Ez nem soros port: %s", "Not a serial port: %s"), port);
        close(fd);
        return NULL;
    }
    cfmakeraw(&t);
    cfsetispeed(&t, to_speed(baud));
    cfsetospeed(&t, to_speed(baud));
    t.c_cflag |= CLOCAL | CREAD | CSTOPB;      /* 8N2 */
    t.c_cflag &= ~(PARENB | CRTSCTS | HUPCL);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 2;
    tcsetattr(fd, TCSANOW, &t);
    fcntl(fd, F_SETFL, 0);                     /* innentől blokkoló írás */
    ioctl(fd, TIOCMBIC, &bits);

    Rig *r = calloc(1, sizeof *r);
    if (!r) { close(fd); return NULL; }
    r->fd = fd;
    return r;
}

static int rig_write(Rig *r, const char *s)
{
    size_t n = strlen(s);
    while (n > 0) {
        ssize_t w = write(r->fd, s, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        s += w;
        n -= (size_t)w;
    }
    tcdrain(r->fd);
    return 0;
}

void rig_close(Rig *r)
{
    if (!r) return;
    close(r->fd);
    free(r);
}

#endif

int rig_tune_band(Rig *r, int band, int mode, long *hz, char *err, size_t errlen)
{
    if (!r) { snprintf(err, errlen, "%s", TR("Nincs rádió beállítva.", "No radio set up.")); return -1; }
    long f = rig_band_freq(band, mode);
    if (f <= 0) { snprintf(err, errlen, "%s", TR("Ismeretlen sáv.", "Unknown band.")); return -1; }

    /* Yaesu MD0x: 1 = LSB, 2 = USB, 3 = CW-U, C = DATA-USB (FT8) */
    char md;
    if (mode == MODE_FT8) md = 'C';
    else if (mode == MODE_CW || band == B30) md = '3';
    else md = (f < 10000000L && band != B60) ? '1' : '2';   /* 60 m-en USB */

    char cmd[64];
    /* előbb a frekvencia, aztán az üzemmód (sávváltáskor a rádió a sáv
     * utolsó üzemmódját venné elő) */
    snprintf(cmd, sizeof cmd, "FA%09ld;MD0%c;", f, md);
    if (rig_write(r, cmd) != 0) {
        snprintf(err, errlen, "%s", TR("Írási hiba a soros porton.", "Write error on the serial port."));
        return -1;
    }
    if (hz) *hz = f;
    return 0;
}
