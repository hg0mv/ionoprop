/*
 * targets.c - saját célterületek (lásd targets.h)
 *
 * A prefix-táblázat a gyakori DXCC-területek közelítő középpontját adja
 * (általában a fővárost vagy a legnépesebb rész közepét). Terjedési
 * becsléshez ez bőven elég pontos.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "targets.h"
#include "i18n.h"
#include "geo.h"
#include "compat.h"

typedef struct {
    const char *pfx;     /* egymás után több prefix, szóközzel elválasztva */
    const char *name, *name_en;
    double lat, lon;
} Prefix;

static const Prefix PFX[] = {
    /* ---- Európa ---- */
    { "HA HG", "Magyarország", "Hungary", 47.2, 19.4 },
    { "OE", "Ausztria", "Austria", 47.6, 14.1 },
    { "OK OL", "Csehország", "Czech Republic", 49.8, 15.5 },
    { "OM", "Szlovákia", "Slovakia", 48.7, 19.7 },
    { "SP SQ SO SN 3Z HF", "Lengyelország", "Poland", 52.1, 19.4 },
    { "YO YP YQ YR", "Románia", "Romania", 45.9, 24.9 },
    { "YU YT", "Szerbia", "Serbia", 44.0, 20.9 },
    { "9A", "Horvátország", "Croatia", 45.1, 15.2 },
    { "S5", "Szlovénia", "Slovenia", 46.1, 14.8 },
    { "E7", "Bosznia-Hercegovina", "Bosnia and Herzegovina", 44.2, 17.8 },
    { "Z3", "Észak-Macedónia", "North Macedonia", 41.6, 21.7 },
    { "4O", "Montenegró", "Montenegro", 42.7, 19.4 },
    { "ZA", "Albánia", "Albania", 41.2, 20.0 },
    { "Z6", "Koszovó", "Kosovo", 42.6, 20.9 },
    { "LZ", "Bulgária", "Bulgaria", 42.7, 25.5 },
    { "SV SX SY SZ J4", "Görögország", "Greece", 39.1, 22.0 },
    { "SV9 J49", "Kréta", "Crete", 35.2, 24.9 },
    { "SV5 J45", "Dodekanészosz", "Dodecanese", 36.4, 28.0 },
    { "TA TB TC YM", "Törökország", "Turkey", 39.0, 35.2 },
    { "5B C4 H2 P3", "Ciprus", "Cyprus", 35.1, 33.4 },
    { "UR US UT UV UW UX UY UZ EM EN EO", "Ukrajna", "Ukraine", 49.0, 31.4 },
    { "EW EU EV", "Fehéroroszország", "Belarus", 53.7, 27.9 },
    { "ER", "Moldova", "Moldova", 47.0, 28.8 },
    { "R UA RA RK RN RU RV RW RX RZ", "Oroszország (európai)", "European Russia", 55.8, 37.6 },
    { "UA2 RA2 RK2 RN2 RU2 RV2 RW2 RX2 RZ2 R2", "Kalinyingrád", "Kaliningrad", 54.7, 20.5 },
    { "UA9 RA9 RK9 RN9 RU9 RV9 RW9 RX9 RZ9 R9 UA0 RA0 RK0 RN0 RU0 RV0 RW0 RX0 RZ0 R0",
      "Oroszország (ázsiai)", "Asiatic Russia", 55.0, 83.0 },
    { "ES", "Észtország", "Estonia", 58.6, 25.0 },
    { "YL", "Lettország", "Latvia", 56.9, 24.6 },
    { "LY", "Litvánia", "Lithuania", 55.2, 23.9 },
    { "OH", "Finnország", "Finland", 61.9, 25.7 },
    { "OH0", "Åland", "Aland Islands", 60.2, 20.0 },
    { "SM SA SB SC SD SE SF SG SH SI SJ SK SL 7S 8S", "Svédország", "Sweden", 59.3, 16.0 },
    { "LA LB LC LD LE LF LG LH LI LJ LN", "Norvégia", "Norway", 60.5, 8.5 },
    { "JW", "Spitzbergák", "Svalbard", 78.2, 15.6 },
    { "JX", "Jan Mayen", "Jan Mayen", 71.0, -8.3 },
    { "OZ OU OV 5P 5Q", "Dánia", "Denmark", 56.0, 10.0 },
    { "OY", "Feröer", "Faroe Islands", 62.0, -6.8 },
    { "OX", "Grönland", "Greenland", 64.2, -51.7 },
    { "TF", "Izland", "Iceland", 64.9, -19.0 },
    { "G M 2E", "Anglia", "England", 52.5, -1.5 },
    { "GM MM 2M", "Skócia", "Scotland", 56.5, -4.2 },
    { "GW MW 2W", "Wales", "Wales", 52.3, -3.7 },
    { "GI MI 2I", "Észak-Írország", "Northern Ireland", 54.6, -6.7 },
    { "GD MD", "Man-sziget", "Isle of Man", 54.2, -4.5 },
    { "GJ MJ", "Jersey", "Jersey", 49.2, -2.1 },
    { "GU MU", "Guernsey", "Guernsey", 49.5, -2.6 },
    { "EI EJ", "Írország", "Ireland", 53.4, -8.2 },
    { "F TM", "Franciaország", "France", 46.6, 2.4 },
    { "TK", "Korzika", "Corsica", 42.0, 9.0 },
    { "ON OO OP OQ OR OS OT", "Belgium", "Belgium", 50.6, 4.5 },
    { "PA PB PC PD PE PF PG PH PI", "Hollandia", "Netherlands", 52.1, 5.3 },
    { "LX", "Luxemburg", "Luxembourg", 49.8, 6.1 },
    { "DL DA DB DC DD DE DF DG DH DI DJ DK DM DN DO DP DQ DR", "Németország", "Germany", 51.2, 10.4 },
    { "HB HE", "Svájc", "Switzerland", 46.8, 8.2 },
    { "HB0", "Liechtenstein", "Liechtenstein", 47.2, 9.6 },
    { "I IK IZ IU IW IN IQ IR II IO IP IS IT IY", "Olaszország", "Italy", 42.8, 12.6 },
    { "IS0 IM0", "Szardínia", "Sardinia", 40.1, 9.0 },
    { "IT9 IH9", "Szicília", "Sicily", 37.5, 14.0 },
    { "9H", "Málta", "Malta", 35.9, 14.4 },
    { "3A", "Monaco", "Monaco", 43.7, 7.4 },
    { "C3", "Andorra", "Andorra", 42.5, 1.5 },
    { "ZB", "Gibraltár", "Gibraltar", 36.1, -5.4 },
    { "EA EB EC ED EE EF EG EH AM AN AO", "Spanyolország", "Spain", 40.4, -3.7 },
    { "EA6 EB6 EC6 ED6 EE6 EF6 EG6 EH6", "Baleár-szigetek", "Balearic Islands", 39.6, 2.9 },
    { "EA8 EB8 EC8 ED8 EE8 EF8 EG8 EH8", "Kanári-szigetek", "Canary Islands", 28.1, -15.4 },
    { "EA9 EB9 EC9 ED9", "Ceuta és Melilla", "Ceuta and Melilla", 35.9, -5.3 },
    { "CT CS CR CQ", "Portugália", "Portugal", 39.5, -8.2 },
    { "CT3 CR3 CQ3 CS3", "Madeira", "Madeira", 32.7, -16.9 },
    { "CU", "Azori-szigetek", "Azores", 38.5, -28.0 },
    { "4L", "Grúzia", "Georgia", 42.3, 43.4 },
    { "EK", "Örményország", "Armenia", 40.1, 45.0 },
    { "4J 4K", "Azerbajdzsán", "Azerbaijan", 40.4, 49.9 },
    /* ---- Ázsia ---- */
    { "UN UP UQ", "Kazahsztán", "Kazakhstan", 48.0, 67.0 },
    { "EX", "Kirgizisztán", "Kyrgyzstan", 41.2, 74.8 },
    { "EY", "Tádzsikisztán", "Tajikistan", 38.9, 71.3 },
    { "EZ", "Türkmenisztán", "Turkmenistan", 38.0, 59.6 },
    { "UK", "Üzbegisztán", "Uzbekistan", 41.4, 64.6 },
    { "4X 4Z", "Izrael", "Israel", 31.8, 35.0 },
    { "OD", "Libanon", "Lebanon", 33.9, 35.5 },
    { "YK", "Szíria", "Syria", 34.8, 38.9 },
    { "JY", "Jordánia", "Jordan", 31.2, 36.5 },
    { "HZ 7Z 8Z", "Szaúd-Arábia", "Saudi Arabia", 24.0, 45.0 },
    { "A4", "Omán", "Oman", 21.5, 56.0 },
    { "A6", "Egyesült Arab Emírségek", "United Arab Emirates", 24.4, 54.4 },
    { "A7", "Katar", "Qatar", 25.3, 51.2 },
    { "A9", "Bahrein", "Bahrain", 26.0, 50.6 },
    { "9K", "Kuvait", "Kuwait", 29.3, 47.5 },
    { "YI", "Irak", "Iraq", 33.3, 44.4 },
    { "EP EQ", "Irán", "Iran", 32.4, 53.7 },
    { "7O", "Jemen", "Yemen", 15.4, 44.2 },
    { "YA T6", "Afganisztán", "Afghanistan", 34.5, 69.2 },
    { "AP", "Pakisztán", "Pakistan", 30.4, 69.3 },
    { "VU AT", "India", "India", 21.0, 78.0 },
    { "4S", "Srí Lanka", "Sri Lanka", 7.9, 80.8 },
    { "S2", "Banglades", "Bangladesh", 23.7, 90.4 },
    { "9N", "Nepál", "Nepal", 28.4, 84.1 },
    { "8Q", "Maldív-szigetek", "Maldives", 3.2, 73.2 },
    { "B BA BD BG BH BI BJ BL BT BY BZ", "Kína", "China", 35.9, 104.2 },
    { "BV BX BM BN BO BP BQ BU BW", "Tajvan", "Taiwan", 23.7, 121.0 },
    { "VR", "Hongkong", "Hong Kong", 22.3, 114.2 },
    { "XX9", "Makaó", "Macao", 22.2, 113.5 },
    { "JA JE JF JG JH JI JJ JK JL JM JN JO JP JQ JR JS 7J 7K 7L 7M 7N 8J 8N", "Japán", "Japan", 36.2, 138.3 },
    { "HL DS DT 6K 6L 6M 6N D7 D8 D9", "Dél-Korea", "South Korea", 36.5, 127.9 },
    { "JT JU JV", "Mongólia", "Mongolia", 47.9, 106.9 },
    { "HS E2", "Thaiföld", "Thailand", 15.9, 100.9 },
    { "XV 3W", "Vietnám", "Vietnam", 14.1, 108.3 },
    { "XU", "Kambodzsa", "Cambodia", 12.6, 104.9 },
    { "XW", "Laosz", "Laos", 19.9, 102.5 },
    { "XZ", "Mianmar", "Myanmar", 21.9, 95.9 },
    { "9M2 9M4 9W2", "Malajzia (nyugat)", "West Malaysia", 4.2, 101.9 },
    { "9M6 9M8 9W6 9W8", "Malajzia (kelet)", "East Malaysia", 2.5, 113.0 },
    { "9V", "Szingapúr", "Singapore", 1.4, 103.8 },
    { "YB YC YD YE YF YG YH 8A 8B 8C 8D 8E 8F 8G 8H 8I", "Indonézia", "Indonesia", -2.5, 118.0 },
    { "DU DV DW DX DY DZ 4D 4E 4F 4G 4H 4I", "Fülöp-szigetek", "Philippines", 12.9, 121.8 },
    /* ---- Óceánia ---- */
    { "VK", "Ausztrália", "Australia", -25.3, 133.8 },
    { "VK1 VK2", "Ausztrália (Sydney)", "Australia (Sydney)", -33.9, 151.2 },
    { "VK3", "Ausztrália (Melbourne)", "Australia (Melbourne)", -37.8, 145.0 },
    { "VK4", "Ausztrália (Queensland)", "Australia (Queensland)", -27.5, 153.0 },
    { "VK5", "Ausztrália (Adelaide)", "Australia (Adelaide)", -34.9, 138.6 },
    { "VK6", "Nyugat-Ausztrália", "Western Australia", -31.9, 115.9 },
    { "VK7", "Tasmánia", "Tasmania", -42.0, 146.6 },
    { "VK8", "Ausztrália (Északi terület)", "Australia (Northern Territory)", -12.5, 130.8 },
    { "ZL ZM", "Új-Zéland", "New Zealand", -40.9, 174.9 },
    { "FK", "Új-Kaledónia", "New Caledonia", -21.3, 165.6 },
    { "P2", "Pápua Új-Guinea", "Papua New Guinea", -6.3, 147.0 },
    { "3D2", "Fidzsi-szigetek", "Fiji", -17.7, 178.1 },
    { "KH6 NH6 WH6 AH6", "Hawaii", "Hawaii", 20.8, -156.3 },
    { "KH2 NH2 WH2 AH2", "Guam", "Guam", 13.4, 144.8 },
    { "FO", "Francia Polinézia", "French Polynesia", -17.6, -149.4 },
    { "5W", "Szamoa", "Samoa", -13.8, -172.1 },
    { "A3", "Tonga", "Tonga", -21.2, -175.2 },
    { "E5", "Cook-szigetek", "Cook Islands", -21.2, -159.8 },
    { "V7", "Marshall-szigetek", "Marshall Islands", 7.1, 171.2 },
    /* ---- Afrika ---- */
    { "SU", "Egyiptom", "Egypt", 26.8, 30.8 },
    { "5A", "Líbia", "Libya", 26.3, 17.2 },
    { "3V", "Tunézia", "Tunisia", 34.0, 9.5 },
    { "7X", "Algéria", "Algeria", 28.0, 1.7 },
    { "CN", "Marokkó", "Morocco", 31.8, -7.1 },
    { "5T", "Mauritánia", "Mauritania", 20.3, -10.3 },
    { "6W", "Szenegál", "Senegal", 14.5, -14.5 },
    { "C5", "Gambia", "Gambia", 13.4, -15.3 },
    { "3X", "Guinea", "Guinea", 9.9, -9.7 },
    { "9L", "Sierra Leone", "Sierra Leone", 8.5, -11.8 },
    { "EL", "Libéria", "Liberia", 6.4, -9.4 },
    { "TU", "Elefántcsontpart", "Ivory Coast", 7.5, -5.5 },
    { "9G", "Ghána", "Ghana", 7.9, -1.0 },
    { "5V", "Togo", "Togo", 8.6, 0.8 },
    { "TY", "Benin", "Benin", 9.3, 2.3 },
    { "5N", "Nigéria", "Nigeria", 9.1, 8.7 },
    { "TJ", "Kamerun", "Cameroon", 7.4, 12.4 },
    { "TR", "Gabon", "Gabon", -0.8, 11.6 },
    { "TN", "Kongó", "Republic of the Congo", -0.2, 15.8 },
    { "9Q", "Kongói DK", "DR Congo", -4.0, 21.8 },
    { "D2", "Angola", "Angola", -11.2, 17.9 },
    { "V5", "Namíbia", "Namibia", -22.6, 17.1 },
    { "A2", "Botswana", "Botswana", -22.3, 24.7 },
    { "ZS ZR ZT ZU", "Dél-Afrika", "South Africa", -30.6, 22.9 },
    { "ZS1", "Dél-Afrika (Fokváros)", "South Africa (Cape Town)", -33.9, 18.4 },
    { "ZS6", "Dél-Afrika (Johannesburg)", "South Africa (Johannesburg)", -26.2, 28.0 },
    { "Z2", "Zimbabwe", "Zimbabwe", -19.0, 29.2 },
    { "9J", "Zambia", "Zambia", -13.1, 27.8 },
    { "C9", "Mozambik", "Mozambique", -18.7, 35.5 },
    { "7Q", "Malawi", "Malawi", -13.3, 34.3 },
    { "5H", "Tanzánia", "Tanzania", -6.4, 34.9 },
    { "5Z", "Kenya", "Kenya", 0.0, 37.9 },
    { "5X", "Uganda", "Uganda", 1.4, 32.3 },
    { "ET", "Etiópia", "Ethiopia", 9.1, 40.5 },
    { "J2", "Dzsibuti", "Djibouti", 11.8, 42.6 },
    { "6O T5", "Szomália", "Somalia", 5.2, 46.2 },
    { "ST", "Szudán", "Sudan", 12.9, 30.2 },
    { "TT", "Csád", "Chad", 15.5, 18.7 },
    { "5U", "Niger", "Niger", 17.6, 8.1 },
    { "TZ", "Mali", "Mali", 17.6, -4.0 },
    { "XT", "Burkina Faso", "Burkina Faso", 12.2, -1.6 },
    { "5R", "Madagaszkár", "Madagascar", -18.8, 46.9 },
    { "3B8", "Mauritius", "Mauritius", -20.3, 57.6 },
    { "FR", "Réunion", "Reunion", -21.1, 55.5 },
    { "S7", "Seychelle-szigetek", "Seychelles", -4.7, 55.5 },
    { "D4", "Zöld-foki Köztársaság", "Cape Verde", 16.0, -24.0 },
    { "ZD7", "Szent Ilona", "St Helena", -16.0, -5.7 },
    { "ZD8", "Ascension", "Ascension Island", -8.0, -14.4 },
    { "D6", "Comore-szigetek", "Comoros", -11.9, 43.9 },
    { "3C", "Egyenlítői-Guinea", "Equatorial Guinea", 1.6, 10.3 },
    { "S9", "São Tomé", "Sao Tome", 0.2, 6.6 },
    /* ---- Észak- és Közép-Amerika ---- */
    { "W K N AA AB AC AD AE AF AG AH AI AJ AK AL", "USA", "USA", 39.8, -98.6 },
    { "KL7 AL7 NL7 WL7 KL AL NL WL", "Alaszka", "Alaska", 61.2, -149.9 },
    { "KP4 NP4 WP4 KP3 NP3 WP3", "Puerto Rico", "Puerto Rico", 18.2, -66.5 },
    { "KP2 NP2 WP2", "Amerikai Virgin-szigetek", "US Virgin Islands", 18.3, -64.9 },
    { "VE VA CY CF CG CJ CK XJ XK XL XM XN XO", "Kanada", "Canada", 45.4, -75.7 },
    { "VE1 VA1", "Kanada (Új-Skócia)", "Canada (Nova Scotia)", 44.7, -63.6 },
    { "VE2 VA2", "Kanada (Québec)", "Canada (Quebec)", 46.8, -71.2 },
    { "VE3 VA3", "Kanada (Ontario)", "Canada (Ontario)", 43.7, -79.4 },
    { "VE4 VA4", "Kanada (Manitoba)", "Canada (Manitoba)", 49.9, -97.1 },
    { "VE5 VA5", "Kanada (Saskatchewan)", "Canada (Saskatchewan)", 52.1, -106.7 },
    { "VE6 VA6", "Kanada (Alberta)", "Canada (Alberta)", 51.0, -114.1 },
    { "VE7 VA7", "Kanada (Brit Columbia)", "Canada (British Columbia)", 49.3, -123.1 },
    { "VE8", "Kanada (Északnyugati terület)", "Canada (Northwest Territories)", 62.5, -114.4 },
    { "VE9", "Kanada (New Brunswick)", "Canada (New Brunswick)", 45.9, -66.6 },
    { "VO1", "Új-Fundland", "Newfoundland", 47.6, -52.7 },
    { "VO2", "Labrador", "Labrador", 53.3, -60.4 },
    { "VY0", "Kanada (Nunavut)", "Canada (Nunavut)", 63.7, -68.5 },
    { "VY1", "Kanada (Yukon)", "Canada (Yukon)", 60.7, -135.1 },
    { "VY2", "Kanada (Prince Edward-sziget)", "Canada (Prince Edward Island)", 46.2, -63.1 },
    { "XE XF 4A 6D", "Mexikó", "Mexico", 19.4, -99.1 },
    { "TG", "Guatemala", "Guatemala", 14.6, -90.5 },
    { "YS", "Salvador", "El Salvador", 13.7, -89.2 },
    { "HR", "Honduras", "Honduras", 14.1, -87.2 },
    { "YN", "Nicaragua", "Nicaragua", 12.1, -86.3 },
    { "TI", "Costa Rica", "Costa Rica", 9.9, -84.1 },
    { "HP", "Panama", "Panama", 9.0, -79.5 },
    { "V3", "Belize", "Belize", 17.3, -88.8 },
    { "CO CM CL T4", "Kuba", "Cuba", 23.1, -82.4 },
    { "HI", "Dominikai Köztársaság", "Dominican Republic", 18.5, -69.9 },
    { "HH", "Haiti", "Haiti", 18.6, -72.3 },
    { "6Y", "Jamaica", "Jamaica", 18.0, -76.8 },
    { "C6", "Bahama-szigetek", "Bahamas", 25.0, -77.3 },
    { "ZF", "Kajmán-szigetek", "Cayman Islands", 19.3, -81.4 },
    { "VP9", "Bermuda", "Bermuda", 32.3, -64.8 },
    { "V2", "Antigua", "Antigua", 17.1, -61.8 },
    { "FG", "Guadeloupe", "Guadeloupe", 16.2, -61.6 },
    { "FM", "Martinique", "Martinique", 14.6, -61.0 },
    { "J6", "Saint Lucia", "Saint Lucia", 13.9, -61.0 },
    { "8P", "Barbados", "Barbados", 13.1, -59.6 },
    { "9Y 9Z", "Trinidad és Tobago", "Trinidad and Tobago", 10.7, -61.2 },
    { "PJ2 PJ4", "Curaçao és Bonaire", "Curacao and Bonaire", 12.2, -68.9 },
    { "P4", "Aruba", "Aruba", 12.5, -70.0 },
    /* ---- Dél-Amerika ---- */
    { "PY PP PQ PR PS PT PU PV PW PX ZV ZW ZX ZY ZZ", "Brazília", "Brazil", -15.8, -47.9 },
    { "PY1 PY2 PU1 PU2", "Brazília (Rio / São Paulo)", "Brazil (Rio / Sao Paulo)", -23.0, -45.0 },
    { "LU LO LP LQ LR LS LT LV LW AY AZ", "Argentína", "Argentina", -34.6, -58.4 },
    { "CE CA CB CC CD XQ XR 3G", "Chile", "Chile", -33.4, -70.6 },
    { "CX CV CW", "Uruguay", "Uruguay", -34.9, -56.2 },
    { "ZP", "Paraguay", "Paraguay", -25.3, -57.6 },
    { "CP", "Bolívia", "Bolivia", -16.5, -68.1 },
    { "OA OB OC 4T", "Peru", "Peru", -12.0, -77.0 },
    { "HC HD", "Ecuador", "Ecuador", -0.2, -78.5 },
    { "HC8 HD8", "Galápagos", "Galapagos", -0.7, -90.3 },
    { "HK HJ 5J 5K", "Kolumbia", "Colombia", 4.7, -74.1 },
    { "YV YW YX YY 4M", "Venezuela", "Venezuela", 10.5, -66.9 },
    { "8R", "Guyana", "Guyana", 6.8, -58.2 },
    { "PZ", "Suriname", "Suriname", 5.9, -55.2 },
    { "FY", "Francia Guyana", "French Guiana", 4.9, -52.3 },
    { "VP8", "Falkland-szigetek", "Falkland Islands", -51.7, -57.9 },
};
#define NPFX (sizeof PFX / sizeof PFX[0])

/* az USA hívókörzetei (a hívójel első számjegye szerint) */
static const struct { const char *name, *name_en; double lat, lon; } US_AREA[10] = {
    { "USA (0: középnyugat)", "USA (0: Midwest)",     41.0,  -97.0 },
    { "USA (1: Új-Anglia)", "USA (1: New England)",       42.4,  -71.4 },
    { "USA (2: New York)", "USA (2: New York)",        41.0,  -74.5 },
    { "USA (3: Pennsylvania)", "USA (3: Pennsylvania)",    40.3,  -76.9 },
    { "USA (4: délkelet)", "USA (4: Southeast)",        33.5,  -84.5 },
    { "USA (5: Texas)", "USA (5: Texas)",           31.5,  -97.0 },
    { "USA (6: Kalifornia)", "USA (6: California)",      36.8, -119.4 },
    { "USA (7: északnyugat)", "USA (7: Northwest)",     43.5, -114.0 },
    { "USA (8: Ohio, Michigan)", "USA (8: Ohio, Michigan)",  41.0,  -83.5 },
    { "USA (9: Illinois)", "USA (9: Illinois)",        41.5,  -88.0 },
};

int prefix_lookup(const char *call, char *name, size_t namelen, double *lat, double *lon)
{
    size_t best_len = 0;
    const Prefix *best = NULL;
    for (size_t i = 0; i < NPFX; i++) {
        const char *p = PFX[i].pfx;
        while (*p) {
            size_t n = strcspn(p, " ");
            if (n > best_len && strncmp(call, p, n) == 0) {
                best_len = n;
                best = &PFX[i];
            }
            p += n;
            while (*p == ' ') p++;
        }
    }
    if (!best) return 0;
    snprintf(name, namelen, "%s", TR(best->name, best->name_en));
    *lat = best->lat;
    *lon = best->lon;

    /* USA: a hívójel számjegye adja a körzetet (pl. W6 = Kalifornia) */
    if (strcmp(best->name, "USA") == 0) {
        for (const char *c = call; *c; c++)
            if (isdigit((unsigned char)*c)) {
                int d = *c - '0';
                snprintf(name, namelen, "%s", TR(US_AREA[d].name, US_AREA[d].name_en));
                *lat = US_AREA[d].lat;
                *lon = US_AREA[d].lon;
                break;
            }
    }
    return 1;
}

/* A hívójelből a prefixet adó rész: "EA8/HA5X" -> "EA8", "HA5X/EA8" -> "EA8",
 * "HA5X/P" -> "HA5X". A "/P", "/M", "/QRP", "/MM" és a puszta szám nem prefix;
 * két lehetséges rész közül a rövidebb a prefix. */
static void call_part(const char *in, char *out, size_t len)
{
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%s", in);
    const char *pick = NULL;
    for (char *p = strtok(tmp, "/"); p; p = strtok(NULL, "/")) {
        int has_alpha = 0;
        for (const char *c = p; *c; c++) if (isalpha((unsigned char)*c)) has_alpha = 1;
        if (!has_alpha || !strcmp(p, "P") || !strcmp(p, "M") || !strcmp(p, "QRP") ||
            !strcmp(p, "MM") || !strcmp(p, "AM"))
            continue;
        if (!pick || strlen(p) < strlen(pick)) pick = p;
    }
    snprintf(out, len, "%s", pick ? pick : in);
}

static int is_locator(const char *s)
{
    size_t n = strlen(s);
    if (n != 4 && n != 6 && n != 8) return 0;
    if (s[0] < 'A' || s[0] > 'R' || s[1] < 'A' || s[1] > 'R') return 0;
    if (!isdigit((unsigned char)s[2]) || !isdigit((unsigned char)s[3])) return 0;
    if (n >= 6 && (s[4] < 'A' || s[4] > 'X' || s[5] < 'A' || s[5] > 'X')) return 0;
    if (n == 8 && (!isdigit((unsigned char)s[6]) || !isdigit((unsigned char)s[7]))) return 0;
    return 1;
}

int target_resolve(const char *input, Target *out, char *err, size_t errlen)
{
    char s[32];
    size_t k = 0;
    for (const char *p = input; *p && k < sizeof s - 1; p++)
        if (!isspace((unsigned char)*p)) s[k++] = (char)toupper((unsigned char)*p);
    s[k] = '\0';
    memset(out, 0, sizeof *out);
    if (!*s) {
        snprintf(err, errlen, "%s", TR("Írj be egy lokátort, hívójelet vagy prefixet.",
                                       "Enter a locator, callsign or prefix."));
        return -1;
    }

    if (is_locator(s)) {
        if (locator_to_latlon(s, &out->lat, &out->lon) != 0) {
            snprintf(err, errlen, TR("Hibás lokátor: %s", "Invalid locator: %s"), s);
            return -1;
        }
        snprintf(out->name, sizeof out->name, "%s", s);
        return 0;
    }

    char cp[32], name[64];
    call_part(s, cp, sizeof cp);
    double lat, lon;
    if (!prefix_lookup(cp, name, sizeof name, &lat, &lon)) {
        snprintf(err, errlen, TR("Ismeretlen prefix: %s. Írj be lokátort (pl. PM95).",
                                 "Unknown prefix: %s. Enter a locator instead (e.g. PM95)."), cp);
        return -1;
    }
    out->lat = lat;
    out->lon = lon;
    snprintf(out->name, sizeof out->name, "%.31s", name);
    return 0;
}

/* ---------- mentés: targets.conf, soronként "név;szélesség;hosszúság" ---------- */

static int targets_path(char *path, size_t len)
{
    char dir[512];
    if (user_dir(USERDIR_CONFIG, dir, sizeof dir) != 0) return -1;
    snprintf(path, len, "%s/targets.conf", dir);
    return 0;
}

int targets_load_custom(Target *t, int max)
{
    char path[600];
    if (targets_path(path, sizeof path) != 0) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    int n = 0;
    while (n < max && fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char *semi = strrchr(line, ';');
        if (!semi) continue;
        *semi = '\0';
        char *semi2 = strrchr(line, ';');
        if (!semi2) continue;
        *semi2 = '\0';
        double lat = atof(semi2 + 1), lon = atof(semi + 1);
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || !*line) continue;
        memset(&t[n], 0, sizeof t[n]);
        snprintf(t[n].name, sizeof t[n].name, "%.31s", line);
        t[n].lat = lat;
        t[n].lon = lon;
        n++;
    }
    fclose(f);
    return n;
}

int targets_save_custom(const Target *t, int n)
{
    char path[600], tmp[620];
    if (targets_path(path, sizeof path) != 0) return -1;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "# IonoProp saját célterületek: név;szélesség;hosszúság\n");
    for (int i = 0; i < n; i++) {
        char nm[40];
        snprintf(nm, sizeof nm, "%s", t[i].name);
        for (char *p = nm; *p; p++) if (*p == ';' || *p == '\n') *p = ' ';
        fprintf(f, "%s;%.4f;%.4f\n", nm, t[i].lat, t[i].lon);
    }
    if (fclose(f) != 0) { remove(tmp); return -1; }
    return replace_file(tmp, path);
}
