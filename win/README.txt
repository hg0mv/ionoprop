IonoProp 0.9.1 - Windows version
==============================

Ionosphere map and HF band advisor for radio amateurs.
From live ionosonde and solar data it shows which HF band is open, and
where to, from 160 m up to 6 m.

GETTING STARTED
---------------
1. Unpack the zip file into any folder
   (e.g. Documents\IonoProp).
2. Double-click IonoProp.exe.
3. On first start the program asks for your QTH locator
   (e.g. KN07sp). Type it and press Enter.
4. To switch the program to English, press N
   (or click the HU / EN button in the header). It is remembered.

No installation and nothing else is needed.
Works on Windows 10 and 11 (64-bit).

IF WINDOWS WARNS YOU
--------------------
On first start Windows SmartScreen may say
"Windows protected your PC". This is because the program is new and has
no paid code-signing certificate.
Click "More info", then "Run anyway".

KEYS
----
  <- ->  or mouse wheel  time: -1 / +1 hour (up to 24 hours ahead)
  Up / Down              select a target
  M / F                  MUF / foF2 map
  1 ... 9, 0, -, =       band map: 160, 80, 60, 40, 30, 20, 17, 15,
                         12, 10, 8, 6 m   (C: 11 m)
  U                      mode: SSB -> CW -> FT8
  T                      new target (locator, callsign or prefix)
  S                      save the point selected on the map
  Delete                 delete the selected own target
  H                      long path on/off
  X                      radio setup (Yaesu CAT, e.g. FT-891)
  right-click            tune the radio to the clicked band
  L                      change your locator
                         (or click "QTH: ..." in the header)
  P                      real spots (PSK Reporter) on/off
  A                      band-opening alerts on/off
  N                      language: Hungarian / English
  F11                    full screen on/off (Esc leaves full screen)
  K                      distances: km / miles
  ?                      explanations: what the values mean
                         (or click the ? button in the header)
  R                      refresh data now
  Q, Esc                 quit
  click on the map       new target at that point

The program refreshes itself every 15 minutes.

METEOR SHOWERS
--------------
Under "Suggestion" the "Meteor" line shows the active or next meteor
shower (e.g. Geminids, 14 December), the time to its peak, the ZHR
(hourly meteor rate) and the UTC hours when the radiant is high: that is
when meteor scatter works on 6 and 2 m. Around the peak you also get an
alert.

ALERTS
------
When something opens, a purple bar appears at the top of the map (and the
computer beeps): sporadic E on 10 / 8 / 6 m, F2 on 8 / 6 m, or a band from
20 to 10 m opening towards your own (*) targets.
At most once every 3 hours for the same opening. Click it to close.
On/off: the A key, or click "Alerts" in the header.

FULL SCREEN, SHACK DISPLAY
--------------------------
F11: full screen on/off. To run it all the time on a separate monitor,
add this after IonoProp.exe in the "Target" field of the shortcut:  -F -C 30
(-F: start full screen, -C 30: the map changes every 30 seconds between
MUF, foF2, 40, 20, 15 and 10 m).

SCALED DISPLAYS
---------------
If Windows display scaling is 125% or 150%, the program follows it and
the text stays sharp.

RADIO CONTROL (FT-891)
----------------------
Press X and enter the radio's port and speed, e.g. COM5,38400
(in Device Manager the "Silicon Labs Dual CP2105 ... Enhanced" port; the
speed is the radio's 05-06 CAT RATE menu). Then right-click a band in the
table and the radio tunes to that band. The program only sets frequency
and mode, it never keys the transmitter.

SPORADIC E (Es)
---------------
Purple marks on the map are sporadic E clouds: the middle of real spot
paths that could not have come via the F2 layer (short path, above the
MUF). Under "Suggestion" the "Es now" line shows which band has an Es
opening, and under "Layers" you see foEs from the nearest ionosonde.
Es cannot be forecast; only openings happening now are shown.

The Kp row at the bottom of the 24-hour forecast is the NOAA forecast:
above 5 (red) a geomagnetic storm is expected, and the upper bands get
weaker.

WHERE ARE THE SETTINGS?
-----------------------
  locator, settings:  %APPDATA%\IonoProp
  downloaded data:    %LOCALAPPDATA%\IonoProp\cache
To remove the program, delete its folder and these two folders.

COMMAND-LINE VERSION
--------------------
ionoprop-cli.exe prints the same as text (in Hungarian). Start it from
a command prompt (cmd or PowerShell), e.g.:
  ionoprop-cli.exe -l KN07sp
  ionoprop-cli.exe -F 2      (24-hour forecast for target 2)
  ionoprop-cli.exe -h        (help)

DATA SOURCES
------------
KC2G (prop.kc2g.com), GIRO ionosonde network, NOAA SWPC,
PSK Reporter, Natural Earth. Many thanks to them!

LICENSE
-------
IonoProp is free software under the GNU GPL 3.0 (LICENSE.txt).
You may use, copy and share it freely.

73 de HG0MV
