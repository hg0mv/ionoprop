# IonoProp 0.9 – ionosphere map, HF band advisor, checked against real spots

*Magyar leírás: [README.md](README.md)*

IonoProp shows the state of the ionosphere and tells radio amateurs which
HF band is open, and where to. It downloads current ionosonde and solar
data, models the ionospheric layers above your QTH, and gives band-by-band
advice for 15 target areas (plus any you add), from 160 m up to 6 m.

It runs on Linux (including Raspberry Pi), Windows and Android, and is
free, with no ads and no registration.

## What it downloads

| Data | Source | Refresh |
|---|---|---|
| Ionosondes: foF2, MUF(3000), hmF2, confidence | prop.kc2g.com (GIRO network) | every 15 min |
| SFI (10.7 cm solar flux) + 90-day mean | NOAA SWPC | every 5 min |
| Kp index (1-minute estimate) | NOAA SWPC | every 5 min |
| X-ray flux, flare class (GOES, 0.1–0.8 nm) | NOAA SWPC | every 5 min |
| Kp forecast (3 days, 3-hour blocks) | NOAA SWPC | hourly |

Downloaded files go to `~/.cache/ionoprop/`. While a file is still fresh
the program does not download it again (KC2G asks users not to overload
their server). If a download fails, the last good data is used.

## Building (Ubuntu / Debian)

```
sudo apt install build-essential libcurl4-openssl-dev libx11-dev libfreetype-dev \
                 libmosquitto-dev fonts-dejavu-core
make             # both programs: ionoprop and ionoprop-gui
make test        # tests on sample data, no internet needed
make install     # copies to ~/.local/bin, adds a menu entry and icon (optional)
make uninstall   # removes it
```

Command-line program only (no X11 or FreeType): `make ionoprop`.
Without the PSK Reporter check (no libmosquitto): `make NOPSK=1`.
The JSON parser (cJSON) is included in `third_party/`, nothing else to install.

## Raspberry Pi: shack display

IonoProp runs on the Raspberry Pi too (Pi 3, 4, 5; 64-bit Raspberry Pi OS
recommended), so with a small screen it makes an always-on shack display,
like HamClock used to. Building is the same as on Ubuntu:

```
sudo apt install build-essential libcurl4-openssl-dev libx11-dev libfreetype-dev \
                 libmosquitto-dev fonts-dejavu-core
make
make autostart   # installs it and starts it full screen when you log in
```

`make autostart` puts the program in `~/.local/bin` and adds an autostart
entry (`~/.config/autostart/ionoprop.desktop`) with these options: `-F`
full screen, `-C 30` the map changes every 30 seconds (MUF, foF2, 40, 20,
15, 10 m). Other options: `make autostart KIOSK_ARGS="-F -C 60 -N"`.
To turn it off: `make no-autostart`.

- **Small screens** (1024x600, 800x480): the program scales itself down so
  everything fits. At 800x480 the text is small; 1024x600 or larger is
  recommended. Manual setting: `IONOPROP_SCALE=0.8`.
- **Full screen:** `F11` on/off, `Esc` leaves full screen (it does not quit).
  The mouse pointer disappears after 4 seconds without movement.
- **At power-on**, if the network is not up yet, the program retries the
  download every minute instead of waiting 15 minutes.
- **Keep the screen on:** `sudo raspi-config` → Display Options →
  Screen Blanking → No.
- If autostart does not work (older or custom desktop), add this line to
  `~/.config/labwc/autostart`: `~/.local/bin/ionoprop-gui -F -C 30 &`
- Without a window manager (bare X, kiosk) it also fills the whole screen.

## Windows version

It is cross-compiled on Linux with MinGW-w64:

```
sudo apt install mingw-w64 zip
make windows     # win/IonoProp.exe and win/ionoprop-cli.exe
make win-zip     # win/IonoProp-0.9-windows.zip (both programs + OLVASSEL.txt + README.txt)
```

Each `.exe` is a single file, with no DLLs and no installer
(Windows 10 and 11, 64-bit). What is different on Windows:

| | Linux | Windows |
|---|---|---|
| window, keyboard, mouse | X11 (`src/platform_x11.c`) | Win32 (`src/platform_win32.c`) |
| text | FreeType, DejaVu Sans | GDI, Segoe UI |
| download | libcurl | WinHTTP (part of Windows) |
| PSK Reporter | libmosquitto | built-in MQTT client (as on Android) |
| settings | `~/.config/ionoprop` | `%APPDATA%\IonoProp` |
| cache | `~/.cache/ionoprop` | `%LOCALAPPDATA%\IonoProp\cache` |

User guide for Windows: [win/README.txt](win/README.txt)
(Hungarian: [win/OLVASSEL.txt](win/OLVASSEL.txt)).

## Android app

It is in the `android/` folder and uses the same C core. Build and install
steps (in Hungarian): [android/README-android.md](android/README-android.md).

## Graphical program: `ionoprop-gui`

```
./ionoprop-gui                  # asks for your locator on first start
./ionoprop-gui -g 1600x950      # window size
./ionoprop-gui -N               # without PSK Reporter
./ionoprop-gui -F -C 30         # full screen, the map changes every 30 s
```

**Your QTH locator** can be set inside the program: click "QTH: …" in the
header (or press `L`), type it, and press Enter. It is saved
(`~/.config/ionoprop/settings.conf`) and used next time, by the
command-line `ionoprop` too. The `-l` option still works; it applies to
that one run and is not saved.

- **Sporadic E:** real spots that came over a short path, well above the F2
  MUF, are counted as Es. The middle of their path shows on the map as a
  purple "cloud" (on a band map for that band, on the MUF map for 10 m and
  up). Under "Suggestion", the "Es now" line shows which band has an Es
  opening (at least 2 different station pairs). foEs from nearby ionosondes
  (within 1200 km, at most 90 minutes old) is shown under "Layers", with the
  Es-MUF calculated from it (about 5.3 × foEs on a 2000 km path).
- **Kp forecast:** the NOAA 3-day forecast (downloaded hourly). It has its
  own row at the bottom of the 24-hour forecast. Beyond 1.5 hours the
  program uses it for polar disturbance, and if a stronger storm than now is
  expected, it lowers the MUF of paths through middle and high latitudes.
- **Bands:** 160 m to 10 m, plus 11 m (27 MHz, CB / "freeband"),
  8 m (40.68 MHz) and 6 m (50 MHz).
- **Mode:** SSB, CW or FT8 (click it in the header, or press `u`). CW copes
  with about 8 dB and FT8 with about 16 dB more absorption, and can go a bit
  above the MUF, so the same path may still be open on FT8 when it is
  closed on SSB. The choice is saved. The PSK Reporter statistics always use
  the SSB limits so they stay comparable.
- **Your own targets:** `t` (or `+`): a locator (`PM95`), a callsign
  (`JA1ABC`, `EA8/HA5X`) or a prefix (`VK6`, `ZS`). About 250 prefixes are
  known, and for the USA the call area digit too (W6 = California). `s`
  saves the point selected on the map. To delete: select it, then `Delete`.
  Up to 10, saved in `targets.conf`; the command-line program uses them too.
- **Long path:** `h` switches the selected target to long path: the map
  shows the other half of the great circle, the table marks it "(LP)".
- **Grey line:** the sunrise/sunset band shows in pale gold on the map; the
  forecast header shows sunrise and sunset at your QTH and at the target
  (local time).
- **Radio (Yaesu CAT, e.g. FT-891):** `x` (or click "Radio" in the header):
  port and speed, e.g. `/dev/ttyUSB0,38400` or `COM5,38400`. Then right-click
  a band cell in the table (or a band button) and the radio tunes to that
  band, to the usual frequency of the chosen mode (FT8: 14.074 etc., in
  DATA-USB). It only sends frequency and mode and never keys the
  transmitter; DTR and RTS are switched off when the port opens. On Linux
  your user must be in the `dialout` group.
- **Map:** MUF(3000) or foF2 colour map, or per band "where my signal
  reaches" (green good, yellow fair, orange poor). Night shading, the Sun,
  the ionosondes (coloured by measured MUF), your QTH (red) and the path to
  the selected target with its hop points. The MUF colour scale goes up to
  50 MHz (purple above 35 MHz, white at 50 MHz), so high low-VHF F2 MUFs
  show too.
- **Right side:** solar activity, layers above your QTH, suggestion (local /
  Europe / DX), and the target table band by band.
- **Bottom:** 24-hour forecast for the selected target (bands × hours, MUF,
  LUF).
- **Time slider:** with the ◀ ▶ buttons or the arrow keys you can step
  0–23 hours ahead; the map, layers and table are calculated for that time.
- **Real spots (PSK Reporter):** on a band map, white dots show where signals
  from your area actually got in the last 30 minutes. The target table marks
  targets that had spots: **solid white dot** = strong signal (≥ −10 dB,
  would be audible on SSB), **grey ring** = only weak, FT8-level signals
  (these can get through by scatter or a short opening when the band is not
  properly open). So you can see directly whether the prediction matches
  reality. A mark needs at least 2 different station pairs, the far end must
  be within 800 km of the target, and the path length must be similar
  (±25–35%); for the "Local" row only 100–300 km paths count.
- **Band-opening alerts:** the program checks every minute, and if something
  opened it shows a pop-up bar at the top of the map (and beeps): sporadic E
  on 10 / 8 / 6 m (real spots or a nearby ionosonde), F2 on 8 / 6 m towards
  any target, or a band from 20 to 10 m becoming at least fair towards your
  own (★) targets. At most once every 3 hours for the same opening. On/off:
  `a`, or click "Alerts" in the header. The state is kept in `alerts.state`,
  so it does not repeat an alert after a restart.
- **Language:** Hungarian or English (`n`, or the HU / EN button in the
  header). The choice is saved. The command-line program stays Hungarian.
- **High-resolution screens:** on Windows it follows the system scaling
  (e.g. 150%) with sharp text. On Linux set the `IONOPROP_SCALE`
  environment variable, e.g. `IONOPROP_SCALE=1.5 ./ionoprop-gui`.
- It refreshes itself every 15 minutes.

| Key / mouse | What it does |
|---|---|
| ← → (or mouse wheel) | time −1 / +1 hour |
| ↑ ↓ | select a target in the list |
| m / f | MUF / foF2 map |
| 1 … 9, 0, -, = | band coverage: 160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 8, 6 m |
| c | 11 m (CB) |
| u, or click the mode | mode: SSB → CW → FT8 |
| t or + | new target (locator, callsign, prefix) |
| s | save the point selected on the map as a target |
| Delete | delete the selected own target |
| h | long path on/off for the selected target |
| x, or click "Radio" | radio (CAT) setup |
| right-click a band | tune the radio to that band |
| L, or click "QTH" | enter your locator |
| p | real spots (spots, Es clouds) on/off |
| a, or click "Alerts" | band-opening alerts on/off |
| n, or click HU / EN | language: Hungarian / English |
| r | refresh data now |
| F11 | full screen on/off |
| Esc | in full screen: leave full screen; otherwise quit |
| q | quit |
| click on the map | new target at that point (path + forecast) |
| click a table row | select that target |
| click the forecast | jump to that hour |

## Command-line program: `ionoprop`

The command-line program prints in Hungarian.

```
./ionoprop                 # with the locator set in the graphical program
./ionoprop -l JN97xx       # another locator (this run only)
./ionoprop -n 12 -a 3      # 12 stations, measurements at most 3 hours old
./ionoprop -T FN20 -T VK6  # extra targets: locator, callsign or prefix
./ionoprop -u FT8          # mode (SSB, CW, FT8)
./ionoprop -F list         # list of targets
./ionoprop -F 2            # 24-hour forecast for target 2 (or -F FN20)
./ionoprop -s              # data only, no band advice
./ionoprop -C              # no colours (e.g. when writing to a file)
./ionoprop -f              # download even if fresh data exists
./ionoprop -h              # help
```

Options: `-l` locator, `-n` number of stations, `-a` maximum age in hours,
`-c` minimum confidence (0–100, default 25; manually scaled measurements
are always shown).

## Checking against real spots (PSK Reporter)

The program subscribes to the PSK Reporter MQTT server
(`mqtt.pskreporter.info`) for the grid squares around your QTH (300 km by
default), both as transmitter and receiver. Every spot with one end near
you arrives, not only those of your own callsign. For each spot it works
out what it would have predicted for that path, frequency and time, and
logs it (`~/.cache/ionoprop/spots_eval.csv`). The graphical program
collects spots while it runs.

```
./ionoprop -l KN07sp -P 60      # collect for 60 minutes, then evaluate
./ionoprop -l KN07sp -V 7       # evaluate the last 7 days of the log
./ionoprop -l KN07sp -V 7 -W    # ... and save the suggested parameters
```

The report shows, band by band, what the prediction was for the paths of
real spots, the hit rate, why "closed" predictions happened, and suggests a
MUF factor and absorption limits. Saved values go to
`~/.config/ionoprop/params.conf` and both programs use them (delete the file
to go back to the defaults).

Important limits:
- Spots only show what **worked**. No spots on a band does not tell you it
  was closed (maybe nobody transmitted). So tuning can only **loosen** the
  limits, never tighten them.
- Most spots are FT8, which decodes very weak signals.
- Spots under 100 km (ground wave) are dropped.
- On short paths (under 2300 km), on 20 m and up, spots well above the MUF
  do not come from F2 propagation (sporadic E, scatter, wrong locator);
  these are counted separately and left out of the statistics and tuning.
  MUF tuning only uses paths longer than 2000 km anyway.
- A suggestion is given only after at least 300 spots, 100 strong signals
  and **6 hours** of data, so it is not tuned to a single moment.

## Files

```
src/data.h     shared data structures (PropData, IonoStation, SolarData)
src/fetch.c    download (libcurl / WinHTTP), cache
src/parse.c    JSON parsing
src/geo.c      locator, distance, bearing, great-circle points
src/solar.c    Sun position, geomagnetic latitude
src/model.c    ionosphere model for any point (foF2, M3000, hmF2, foE)
src/predict.c  hop geometry, MUF/LUF, absorption, band grading
src/loader.c   downloads every source in one step
src/main.c     command-line program, output
src/gui.c      graphical interface (drawing, controls)
src/platform_x11.c   window and events on Linux (X11)
src/platform_win32.c window and events on Windows
src/compat.c   Linux / Windows differences (folders, files, console)
src/canvas.c   drawing into a pixel buffer, text with FreeType
src/mapview.c  world map, colour map, coverage
src/map_data.c coastlines (Natural Earth 1:110m, public domain)
src/pskr.c     PSK Reporter MQTT collector (libmosquitto / built-in client)
src/spots.c    real spots: target marks, sporadic E, drawing
src/targets.c  own targets, prefix table
src/rig.c      radio control (Yaesu CAT) over a serial port
src/alerts.c   band-opening alerts (conditions, "new only" filter)
src/i18n.c     Hungarian / English interface
src/validate.c spot evaluation, log, statistics, tuning suggestion
src/config.c   saving / loading tuned parameters and settings
src/cache.c    cache folder, file reading
src/appcore.c  UI-independent app core (used by the Android app)
src/jni_bridge.c JNI bridge for the Android app
android/       Android project (Kotlin + the C core above)
dist/          menu entry and icon for Linux
win/           Windows: icon, manifest, resource file, user guides
test/          tests and sample data
```

## How it calculates

1. **Model for any point:** a simple base model (SFI and the Sun's
   position, with the F2 layer's 1.5-hour lag, the winter anomaly, the
   equatorial anomaly, and saturation at very high SFI), corrected by
   measurements from nearby ionosondes (within about 1500 km). Where there
   is no measurement it is only an estimate (marked `~` in the table).
2. **Path:** split into hops (F2: at most about 3500 km, at a 3° take-off
   angle), and the model is queried at the middle of each hop. The hop MUF
   is scaled so that at 3000 km it equals the measured MUF(3000). In
   daytime the E layer is also considered (hops up to 2000 km).
3. **Absorption (D layer):** from the Sun's elevation and the frequency,
   hop by hop (a simplified formula in the style of ITU-R P.533). The LUF
   comes from this.
4. **Disturbances:** a flare above M1 silences the low bands on the day
   side; at Kp ≥ 4 paths through the auroral zone get worse.
5. **Forecast:** for future hours the measured correction fades into the
   base model over about 5 hours; geomagnetic conditions are taken from the
   Kp forecast; flares are not predicted.
6. **Grading per band:** +++ good (the optimum frequency, below 0.85 × MUF),
   ++ fair, + poor / possible, . closed.

The default absorption limits are empirical; they can be tuned with the
PSK Reporter check (see above).

## Changes

- **0.9:** full-screen shack display mode (`F11`, `-F`, cycling map `-C`),
  Raspberry Pi support (small screens, `make autostart`), menu entry and
  icon on install
- **0.8:** band-opening alerts (desktop pop-up, Android notification),
  English interface, Android home screen widget, sharp display on scaled
  Windows screens (and `IONOPROP_SCALE` on Linux)
- **0.7:** own targets (locator, callsign, prefix), SSB / CW / FT8,
  grey line and long path, 11 m, Yaesu CAT
- **0.6:** sporadic E, better model for high MUFs, Kp forecast
- **0.5:** 8 m and 6 m, MUF scale up to 50 MHz, Windows version

## Data sources

KC2G (prop.kc2g.com), the GIRO ionosonde network, NOAA SWPC, PSK Reporter,
Natural Earth. Many thanks to them!

73 de HG0MV
