# IonoProp a Google Play-en – lépésről lépésre

Ebben a mappában minden van, ami a Play-hez kell:

| Fájl | Mire kell |
|---|---|
| `README-play.md` | ez a leírás |
| `store-listing.md` | a bolti oldal szövegei (magyarul és angolul), és mit válaszolj a kérdőívekre |
| `privacy.html` | adatvédelmi tájékoztató – fel kell tenni egy weboldalra |
| `icon-512.png` | az app ikonja (512×512) |
| `feature-1024x500.png` | fejléckép a bolti oldal tetejére (1024×500) |

A program 0.6-es változata már megfelel a Play előírásainak: Android 16-ot
(API 36) céloz, és a 16 KB-os memórialapos telefonokon is fut.

---

## 1. Saját aláíró kulcs készítése (egyszeri)

A kulccsal bizonyítod, hogy a frissítés tőled jön. **Ha elveszíted, vagy
elfelejted a jelszavát, a weboldalon terjesztett APK-hoz soha többé nem tudsz
frissítést kiadni.** Ezért mentsd el a kulcsfájlt legalább két helyre
(pl. pendrive + felhő), a jelszót pedig írd fel.

Android Studióban:

1. **Build → Generate Signed App Bundle or APK…**
2. Válaszd az **Android App Bundle**-t → **Next**.
3. **Key store path** mellett: **Create new…**
   - **Key store path:** a projekten **kívül** legyen, pl.
     `/home/<felhasználó>/ionoprop-kulcs/ionoprop-upload.jks`
   - **Password:** egy erős jelszó (kétszer)
   - **Alias:** `upload`
   - **Password** (a kulcsé): lehet ugyanaz
   - **Validity:** 25 év (az alapérték jó)
   - **Certificate:** elég a név (**First and Last Name**) és az ország (**HU**)
   - **OK**
4. Pipáld be: **Remember passwords** → **Next**.
5. Válaszd a **release**-t → **Create**.

A kész fájl: `ionoprop/android/app/release/app-release.aab` – ezt kell
feltölteni a Play-re.

Ugyanígy készül a saját kulccsal aláírt APK is (a weboldalra): a 2. lépésben
**APK**-t válassz. A fájl neve `ionoprop-0.8.apk` lesz.

> **Fontos:** a mostani, próbakulccsal aláírt app nem frissíthető az új
> kulccsal aláírttal. Aki már telepítette, annak egyszer törölnie kell a régit,
> és utána telepíteni az újat. (A lokátort újra be kell írni.)

### Parancssorból (nem kötelező)

Ha a `./gradlew`-val fordítasz, készíts egy `ionoprop/android/keystore.properties`
fájlt:

```
storeFile=/home/<felhasználó>/ionoprop-kulcs/ionoprop-upload.jks
storePassword=<jelszó>
keyAlias=upload
keyPassword=<jelszó>
```

Utána:

```
cd ionoprop/android
./gradlew bundleRelease     # app/build/outputs/bundle/release/app-release.aab
./gradlew assembleRelease   # app/build/outputs/apk/release/ionoprop-0.8.apk
```

Ez a fájl jelszót tartalmaz: **soha ne oszd meg, és ne tedd fel GitHubra**
(a `.gitignore` már kizárja).

---

## 2. Fejlesztői fiók

1. Menj a **play.google.com/console** oldalra, jelentkezz be a Google-fiókoddal.
2. Válaszd a **személyes** (Personal) fiókot.
3. Fizesd ki a 25 dolláros egyszeri díjat.
4. Igazold magad: személyi igazolvány, telefonszám, és egy Android-telefonon
   a **Play Console** app telepítése.

Az ellenőrzés néhány nap is lehet.

---

## 3. Az app létrehozása a Play Console-ban

**Alkalmazás létrehozása** gomb:

- Név: `IonoProp`
- Alapértelmezett nyelv: magyar
- Alkalmazás vagy játék: **Alkalmazás**
- Ingyenes vagy fizetős: **Ingyenes**

Utána a **Kezelőpult** (Dashboard) végigvezet a teendőkön. A válaszokat a
`store-listing.md` fájlban megtalálod:

- **Adatvédelmi irányelvek:** a `privacy.html` webcíme (lásd 4. pont)
- **Alkalmazás-hozzáférés:** minden funkció elérhető, nincs bejelentkezés
- **Hirdetések:** nem tartalmaz
- **Tartalom besorolása:** kérdőív – mindenre „nem” a válasz
- **Célközönség:** 18 év felett
- **Adatbiztonság:** lásd `store-listing.md`
- **Bolti oldal:** szövegek, ikon, fejléckép, képernyőképek

---

## 4. Az adatvédelmi tájékoztató feltöltése

A Play egy **webcímet** kér. Tedd fel a `privacy.html`-t bárhová, ahol
mindenki elérheti, például:

- a saját honlapodra (pl. `https://<a-te-oldalad>/ionoprop/privacy.html`), vagy
- GitHubra: ha ott tárolod a programot, a GitHub Pages ingyen kiteszi.

Előtte írd be a fájlba a nevedet / hívójeledet és az e-mail-címedet (a
`[hívójel]` és `[e-mail]` helyére).

---

## 5. Képernyőképek

Legalább 2, legfeljebb 8 kép kell a telefonról. A telefonon:
**bekapcsológomb + hangerő le** egyszerre. Jó témák:

1. a fő képernyő MUF-térképpel és a táblázattal
2. sávos térkép (pl. 20 m), bekapcsolt PSK Reporterrel
3. egy célterület 24 órás előrejelzése
4. a „Stat” ablak

---

## 6. Zárt teszt (14 nap, 12 tesztelő)

Az új személyes fiókoknál ez kötelező a nyilvános megjelenés előtt.

1. Készíts egy **Google-csoportot** (groups.google.com), pl.
   `ionoprop-teszt`, és hívd meg a tesztelőket (rádióamatőr ismerősök, klub).
   Mindenkinek Google-fiók kell, amivel a telefonján a Play-t használja.
2. Play Console: **Tesztelés → Zárt tesztelés → Track létrehozása**.
3. **Tesztelők:** add meg a Google-csoport e-mail-címét.
4. **Új kiadás létrehozása:** töltsd fel az `app-release.aab`-t.
   Amikor először töltesz fel, a Play felajánlja a **Play alkalmazás-aláírást**
   – fogadd el.
5. **Ellenőrzésre küldés.** Pár nap múlva elfogadják.
6. A tesztelők kapnak egy linket (a **Tesztelők** fülön), ott a
   **Tesztelővé válok** gomb után a Play-ből telepíthetik az appot.

**Legalább 12 tesztelőnek 14 napig megszakítás nélkül bent kell maradnia.**
Érdemes 15–20 embert hívni, hátha valaki kilép. Kérd meg őket, hogy néha
nyissák meg az appot, és szóljanak, ha valami nem jó.

A 14 nap után a Kezelőpulton megjelenik: **Hozzáférés kérése az éles
verzióhoz**. Pár kérdésre kell válaszolni (hányan teszteltek, mit javítottál).
Az elfogadás után az **Éles verzió** fülön kiadhatod mindenkinek.

---

## 7. Frissítés később

1. `app/build.gradle.kts`: növeld a `versionCode`-ot (pl. 10 → 11) és a
   `versionName`-et (pl. `"0.8.1"`). A `versionCode`-nak mindig nagyobbnak kell
   lennie, mint az előzőnek.
2. **Build → Generate Signed App Bundle** – ugyanazzal a kulccsal.
3. Play Console → **Éles verzió → Új kiadás** → feltöltés.

---

## Play és weboldal egyszerre?

A Play a feltöltött appot **a saját kulcsával** írja alá újra. Ezért a
Play-ről telepített és a weboldalról letöltött (a te kulcsoddal aláírt)
változat nem frissíthető egymásra: aki váltani akar, annak előbb törölnie
kell a régit.

A legegyszerűbb: miután az app kint van a Play-en, a weboldalon is a
**Play-linket** add meg. Akinek nincs Play-e, annak a Play Console-ból
letölthetsz egy Play-kulccsal aláírt APK-t: **App bundle explorer →
Letöltések → Univerzális APK**. Ez a Play-es változattal frissíthető
egymásra.

---

## Ha hiba van

| Üzenet | Megoldás |
|---|---|
| `Failed to find target with hash string 'android-36'` | Android Studio: **Tools → SDK Manager → SDK Platforms** → pipáld be az **Android 16 (API 36)**-ot → Apply |
| A Gradle frissítést kér (AGP Upgrade Assistant) | nyugodtan elfogadhatod |
| A Play: „A verziókód már használatban van” | növeld a `versionCode`-ot (7. pont) |
| A Play: „Az APK/AAB hibakereső módban van aláírva” | nem a **release**-t választottad, vagy nem a saját kulccsal írtad alá (1. pont) |
| A telefon: „Az alkalmazás nincs telepítve” | előbb töröld a régi, más kulccsal aláírt appot |

---

## A weboldalról letölthető APK regisztrálása (fejlesztő-ellenőrzés)

A Google 2027-től világszerte csak olyan appot enged telepíteni a hivatalos
Android-telefonokra, amelyet egy ellenőrzött fejlesztő regisztrált. Ez a
Playen kívül (pl. a hg0mv.hu-ról) letöltött APK-ra is vonatkozik.

1. Play Console → **Android developer verification** (Android-fejlesztői
   ellenőrzés) → **Package names** (Csomagnevek).
2. Nézd meg, ott van-e már a `hu.ionoprop.app` (a Play-en lévő appokat a
   Google magától regisztrálja, a Play saját kulcsával).
3. A weboldalas APK-t a saját feltöltési kulcsod írja alá, ezt külön kell
   hozzáadni: az app mellett **kulcs hozzáadása** / ha nincs a listában,
   **Other keys** (egyéb kulcsok). A kulcs ujjlenyomata (SHA-256):

   ```
   keytool -list -v -keystore ~/ionoprop-kulcs/ionoprop-upload.jks -alias upload
   ```

4. A Play Console ad egy rövid szöveget (registration snippet). Ezt egy új
   fájlba kell tenni:
   `ionoprop/android/app/src/main/assets/adi-registration.properties`
   (az `assets` mappát létre kell hozni).
5. Készíts egy APK-t a saját kulccsal (Build → Generate Signed App Bundle or
   APK… → APK → release), és töltsd fel ugyanazon az oldalon.
6. A fájl maradhat a projektben, a későbbi kiadásokba is belekerül.
