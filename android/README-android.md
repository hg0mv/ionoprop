# IonoProp Android – fordítás és telepítés

Az Android-app ugyanazt a C-kódot használja, mint az asztali program
(`../src`), a felület Kotlinban készült. **Az `ionoprop` mappát egyben
kell tartani**, mert az app a `../src` és `../third_party` mappából fordítja
a C-magot.

## 1. Android Studio telepítése (Ubuntu, egyszeri)

```
sudo snap install android-studio --classic
android-studio
```

Az első indításkor válaszd a **Standard** telepítést. Ez letölti az Android
SDK-t (kb. 3–5 GB).

## 2. NDK és CMake (egyszeri)

Android Studio: **Settings → Languages & Frameworks → Android SDK →
SDK Tools** fül. Pipáld be:

- **NDK (Side by side)**
- **CMake** (a **3.22.1**-es; ha nem látod, pipáld be jobb alul a
  „Show Package Details”-t)

Utána **Apply**.

## 3. A projekt megnyitása

**File → Open** → válaszd az `ionoprop/android` mappát. Az első
megnyitáskor a Gradle letölti a hozzá szükséges részeket („Gradle sync”),
ez néhány percig tart.

Ha az Android Studio felajánlja az Android Gradle Plugin frissítését
(„AGP Upgrade Assistant”), nyugodtan elfogadhatod.

## 4. Az APK elkészítése

**Build → Build App Bundle(s) / APK(s) → Build APK(s)**

A kész fájl:
```
ionoprop/android/app/build/outputs/apk/debug/ionoprop-0.9.1-debug.apk
```

Parancssorból is megy (ha az SDK útvonala be van állítva a
`local.properties`-ben, amit az Android Studio az első megnyitáskor
létrehoz):
```
cd ionoprop/android
./gradlew assembleDebug
```

## 5. Telepítés a telefonra

**A) USB-kábellel (ajánlott):** a telefonon kapcsold be a fejlesztői
módot (Beállítások → A telefonról → 7× koppintás a Build-számra), utána
a Fejlesztői beállításokban az **USB-hibakeresést**. Csatlakoztasd a
telefont, az Android Studióban válaszd ki fent, és nyomd meg a zöld
**▶ Run** gombot.

**B) Fájlként:** másold át az `ionoprop-0.9.1-debug.apk`-t a telefonra, és nyisd
meg. A telefon megkérdezi, hogy engedélyezed-e az ismeretlen forrásból
származó alkalmazások telepítését – engedélyezd.

## Saját kulccsal aláírt APK, Google Play

A fenti APK a fejlesztői próbakulccsal van aláírva – saját használatra jó,
de terjesztésre és a Play-re saját kulcs kell. Ennek lépései, a Play-hez
szükséges szövegek, adatvédelmi tájékoztató és grafikák a
[play/README-play.md](play/README-play.md) leírásban vannak.

Az app Android 16-ot (API 36) céloz. Ha a Gradle sync azt írja, hogy
hiányzik az `android-36`, az Android Studióban: **Tools → SDK Manager →
SDK Platforms → Android 16 (API 36)** → Apply.

## Ha hiba van

Küldd el a **Build** ablak (alul) első piros hibaüzenetét. Gyakori esetek:

| Hibaüzenet | Megoldás |
|---|---|
| `NDK is not installed` / `No version of NDK matched` | 2. lépés: NDK telepítése |
| `CMake '3.22.1' was not found` | 2. lépés: CMake 3.22.1 telepítése |
| `SDK location not found` | nyisd meg egyszer a projektet az Android Studióban |
| `Unsupported class file major version` | Settings → Build Tools → Gradle → Gradle JDK: válaszd a beépített „jbr-17” vagy „jbr-21”-et |

## Az app használata

- **Első indításkor** megkérdezi a QTH lokátort; később a fejlécben a
  „QTH: … ✎” feliratra koppintva módosítható.
- **Térkép:** MUF / foF2 színtérkép vagy sávonkénti lefedettség (gombok a
  térkép alatt). Koppintás a térképen: új cél azon a ponton.
- **Időcsúszka:** 0–23 órával előre; a „Most” feliratra koppintva vissza.
- **Célterületek:** koppints egy sorra – lent megjelenik a 24 órás
  előrejelzése. Fehér pötty = erős valódi vétel, szürke karika = csak
  gyenge (FT8), piros pont = zavar (sarki fény / napkitörés).
- **24 órás előrejelzés:** koppints egy oszlopra, és arra az órára ugrik.
- **Üzemmód** (a fejlécben, pl. „SSB”): koppintásra SSB → CW → FT8. A
  gyengébb jelekkel is működő üzemmódokkal több sáv lesz nyitott.
- **＋ Új cél:** saját célterület lokátorral, hívójellel vagy prefixszel.
  Hosszú nyomás egy soron: hosszú út, mentés (a térképen kijelölt pontnál),
  törlés (saját célnál).
- **Napkelte / napnyugta:** az előrejelzés fejlécében a QTH-n és a célnál;
  a térképen a szürkevonal halvány arany sáv.
- **⟳** = adatok frissítése most, **Stat** = ellenőrzés a gyűjtött
  valódi vételekkel (7 nap), „Javaslat mentése” gombbal.
- **PSK Reporter** (lent kapcsolható): csak amíg az app a képernyőn van
  (akkumulátorkímélés). Mobilneten is kicsi a forgalom.
- **Értesítés sávnyitáskor** (lent kapcsolható): az app félóránként a
  háttérben is ránéz az adatokra, és értesítést küld, ha sporadikus E van
  10 / 8 / 6 m-en (a közeli ionoszonda szerint), F2 nyitás 8 / 6 m-en, vagy
  a saját (★) célterületeid felé 20 és 10 m között kinyílik egy sáv.
  Ugyanarra legfeljebb 3 óránként szól. Bekapcsoláskor az Android
  engedélyt kér az értesítésekhez.
- **Widget:** a kezdőképernyőn hosszan nyomva → Widgetek → IonoProp. Mutatja
  a napaktivitást és a sávjavaslatot (közeli / Európa / DX, Es), félóránként
  frissül, koppintásra megnyílik az app.
- **Meteorraj:** a javaslat alatt a „Meteor” sor: az aktív vagy a következő
  meteorraj, a csúcsig hátralévő idő, a ZHR és az UTC-órák, amikor a rádiáns
  magasan áll (meteorszórás 6 és 2 m-en). A csúcs körül értesítés is jön, ha
  az értesítés be van kapcsolva.
- **Magyarázatok:** lent a „❓ Mit jelentenek az értékek?” gomb.
- **Távolság:** lent a „Távolság mérföldben” kapcsoló. Első indításkor az
  USA-ban és az Egyesült Királyságban mérföld, máshol km.
- **Nyelv:** lent a „🌐 English” / „🌐 Magyar” gomb. Első indításkor a
  telefon nyelve dönt (magyar telefonon magyar, máshol angol).

## Felépítés

```
android/app/src/main/cpp/CMakeLists.txt   a C-mag fordítása (../src fájljaiból)
android/app/src/main/java/hu/ionoprop/app/
    MainActivity.kt   a képernyő, vezérlés, időzítők
    Engine.kt         a C-mag kezelése egy háttérszálon
    Native.kt         JNI-függvények (src/jni_bridge.c)
    Fetcher.kt        letöltés gyorsítótárral
    State.kt          a C-mag JSON-válaszának feldolgozása
    MapPanel.kt       térkép (a képet a C rajzolja) + jelmagyarázat
    TargetTable.kt    célterületek táblázata
    ForecastView.kt   24 órás előrejelzés
    BackgroundJob.kt  félórás háttér-ellenőrzés: értesítés + widget
    BandWidget.kt     kezdőképernyő-widget
    L.kt              magyar / angol szövegek
src/appcore.c         felülettől független app-mag (állapot, JSON, térkép)
src/jni_bridge.c      JNI-híd
src/pskr.c            PSK Reporter: Androidon saját kis MQTT-kliens
```
