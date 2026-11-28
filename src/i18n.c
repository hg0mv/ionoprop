/*
 * i18n.c - nyelvválasztás (lásd i18n.h)
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "i18n.h"
#include "config.h"

/* a g_lang a predict.c-ben van (minden programrész használja) */

int lang_from_name(const char *s)
{
    if (!s) return -1;
    if (tolower((unsigned char)s[0]) == 'h' && tolower((unsigned char)s[1]) == 'u') return LANG_HU;
    if (tolower((unsigned char)s[0]) == 'e' && tolower((unsigned char)s[1]) == 'n') return LANG_EN;
    return -1;
}

const char *lang_name(int lang) { return lang == LANG_EN ? "en" : "hu"; }

int lang_system(void)
{
#ifdef _WIN32
    LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID(id) == LANG_HUNGARIAN ? LANG_HU : LANG_EN;
#else
    const char *v = getenv("LC_ALL");
    if (!v || !*v) v = getenv("LC_MESSAGES");
    if (!v || !*v) v = getenv("LANG");
    if (!v || !*v) return LANG_HU;              /* nincs beállítva: a program eredeti nyelve */
    return strncmp(v, "hu", 2) == 0 ? LANG_HU : LANG_EN;
#endif
}

void lang_load(void)
{
    char v[16];
    int l = -1;
    if (config_get("lang", v, sizeof v)) l = lang_from_name(v);
    g_lang = l >= 0 ? l : lang_system();
}

/* ---------- km / mérföld ---------- */

void units_load(void)
{
    char v[16];
    g_units_mi = config_get("units", v, sizeof v) && (v[0] == 'm' || v[0] == 'M');
}

void units_set(int mi)
{
    g_units_mi = mi ? 1 : 0;
    config_set("units", g_units_mi ? "mi" : "km");
}

double dist_out(double km) { return g_units_mi ? km * 0.621371 : km; }
const char *dist_unit(void) { return g_units_mi ? "mi" : "km"; }

/* ---------- magyarázatok ---------- */

const char *help_text(void)
{
    return TR(
"SFI - napfluxus (10,7 cm): a Nap ionizáló sugárzásának mértéke. 70 körül alacsony, 150 fölött magas. Minél nagyobb, annál magasabb sávok nyílnak ki.\n"
"Kp - a földmágneses tér zavarossága 0-tól 9-ig. 0-2 nyugodt, 3-4 zavart, 5 fölött geomágneses vihar: a sarkvidéken át menő utak és a felső sávok gyengülnek.\n"
"Röntgen (A, B, C, M, X) - a napkitörés erőssége. M és X osztálynál a nappali oldalon az alsó sávok percekre-órákra elnémulhatnak.\n"
"foF2 - az F2 réteg kritikus frekvenciája. Ennél alacsonyabb frekvencián a jel függőlegesen is visszaverődik: közeli (NVIS) összeköttetés.\n"
"hmF2 - az F2 réteg magassága. Minél magasabb, annál hosszabb egy ugrás.\n"
"MUF - a legnagyobb használható frekvencia egy útra. A MUF(3000) a 3000 km-es útra vonatkozik. A legjobb sáv kicsivel a MUF alatt van.\n"
"LUF - a legkisebb használható frekvencia. Ez alatt a D réteg elnyeli a jelet.\n"
"D és E réteg - nappali alsó rétegek. A D réteg nappal elnyeli az alsó sávokat (160, 80 m), napnyugta után eltűnik.\n"
"Es, foEs - sporadikus E: hirtelen, foltszerű erős E réteg, főleg nyáron. 10, 8 és 6 m-en rövid, erős nyitásokat okoz. Előre nem jelezhető.\n"
"Ionoszonda - mérőállomás, amely rádióimpulzusokkal méri a rétegeket. A '~' jel: a közelben nincs ionoszonda, az érték becslés.\n"
"Színek - zöld: jó, sárga: közepes, narancs: gyenge, de próbálható, sötét: zárva. Fehér pont: valódi erős vétel, szürke kör: csak gyenge (FT8) vétel.\n"
"SSB / CW / FT8 - a CW kb. 8 dB-lel, az FT8 kb. 16 dB-lel gyengébb jelet is visz, ezért velük több sáv nyitott.\n"
"Meteorraj, ZHR - meteorszórás: a meteorok ionizált nyoma másodpercekre visszaveri a 6 és 2 m-es jeleket (500-2000 km). A ZHR az óránkénti meteorszám a csúcson. Akkor jó, ha a rádiáns (ahonnan a meteorok jönnek) fent van: a sor végén az UTC-órák, amikor a QTH-ról legalább 25 fokon áll.\n"
"Szürkevonal - a napkelte és napnyugta sávja. Itt az alsó sávokon gyakran különösen jó a terjedés.",
"SFI - solar flux (10.7 cm): how strongly the Sun ionizes the atmosphere. Around 70 is low, above 150 is high. The higher it is, the higher the bands that open.\n"
"Kp - disturbance of the Earth's magnetic field, 0 to 9. 0-2 quiet, 3-4 unsettled, 5 and above a geomagnetic storm: polar paths and the upper bands get weaker.\n"
"X-ray (A, B, C, M, X) - strength of a solar flare. With M and X class flares the low bands can go silent on the day side for minutes or hours.\n"
"foF2 - critical frequency of the F2 layer. Below it, signals are reflected even straight up: short-range (NVIS) contacts.\n"
"hmF2 - height of the F2 layer. The higher it is, the longer one hop.\n"
"MUF - maximum usable frequency for a path. MUF(3000) is for a 3000 km path. The best band is a little below the MUF.\n"
"LUF - lowest usable frequency. Below it, the D layer absorbs the signal.\n"
"D and E layers - low layers present in daytime. The D layer absorbs the low bands (160, 80 m) during the day and disappears after sunset.\n"
"Es, foEs - sporadic E: sudden, patchy, strong E layer, mostly in summer. It causes short, strong openings on 10, 8 and 6 m. It cannot be forecast.\n"
"Ionosonde - a station that measures the layers with radio pulses. A '~' sign: no ionosonde nearby, the value is an estimate.\n"
"Colours - green: good, yellow: fair, orange: poor but worth a try, dark: closed. White dot: real strong spot, grey ring: weak (FT8) spots only.\n"
"SSB / CW / FT8 - CW copes with about 8 dB and FT8 with about 16 dB weaker signals, so more bands are open with them.\n"
"Meteor shower, ZHR - meteor scatter: the ionized trails of meteors reflect 6 and 2 m signals for seconds (500-2000 km). ZHR is the hourly meteor rate at the peak. Best when the radiant (where the meteors come from) is up: the UTC hours at the end of the line are when it stands at least 25 degrees high at your QTH.\n"
"Grey line - the band of sunrise and sunset. Propagation on the low bands is often especially good here.");
}
