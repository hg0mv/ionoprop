# IonoProp - Makefile
#
# Szükséges csomagok Ubuntun:
#   sudo apt install build-essential libcurl4-openssl-dev libx11-dev libfreetype-dev \
#                    libmosquitto-dev fonts-dejavu-core
#
# PSK Reporter-es ellenőrzés nélkül (libmosquitto nélkül):  make NOPSK=1
#
# Fordítás:   make            (parancssori + grafikus program)
#             make ionoprop   (csak a parancssori, X11/FreeType nélkül)
# Teszt:      make test       (internet nélkül, mintaadatokon)
# Windows:    make windows    (Linuxon, MinGW-w64-gyel: sudo apt install mingw-w64)

CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE

ifdef NOPSK
CFLAGS    += -DIONOPROP_NO_PSK
PSK_LIBS   =
else
PSK_LIBS   = -lmosquitto -lpthread
endif

FT_CFLAGS  = $(shell pkg-config --cflags freetype2)
GUI_LIBS   = $(shell pkg-config --libs x11 freetype2)

# közös számító rész
CORE = src/loader.o src/fetch.o src/parse.o src/geo.o src/solar.o src/model.o \
       src/predict.o src/config.o src/pskr.o src/validate.o src/cache.o src/compat.o src/targets.o src/i18n.o src/meteor.o \
       third_party/cJSON.o

CLI_OBJ = src/main.o $(CORE)
GUI_OBJ = src/gui.o src/platform_x11.o src/canvas.o src/mapview.o src/map_data.o src/spots.o src/rig.o src/alerts.o $(CORE)

all: ionoprop ionoprop-gui

ionoprop: $(CLI_OBJ)
	$(CC) $(CFLAGS) -o $@ $(CLI_OBJ) -lcurl $(PSK_LIBS) -lm

ionoprop-gui: $(GUI_OBJ)
	$(CC) $(CFLAGS) -o $@ $(GUI_OBJ) $(GUI_LIBS) -lcurl $(PSK_LIBS) -lm

# a FreeType-ot használó fájlok
src/canvas.o src/gui.o src/mapview.o src/spots.o: EXTRA_CFLAGS = $(FT_CFLAGS)

src/%.o: src/%.c src/*.h
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

third_party/cJSON.o: third_party/cJSON.c third_party/cJSON.h
	$(CC) $(CFLAGS) -w -c -o $@ $<

# ---- tesztek (nem kell hozzájuk internet, libcurl, X11) ----
test/test_parse: test/test_parse.c src/parse.c src/geo.c third_party/cJSON.c src/*.h
	$(CC) $(CFLAGS) -o $@ test/test_parse.c src/parse.c src/geo.c third_party/cJSON.c -lm

CALC = src/parse.c src/geo.c src/solar.c src/model.c src/predict.c third_party/cJSON.c
test/test_predict: test/test_predict.c $(CALC) src/*.h
	$(CC) $(CFLAGS) -o $@ test/test_predict.c $(CALC) -lm

VALID = $(CALC) src/config.c src/pskr.c src/validate.c src/cache.c src/compat.c src/i18n.c src/meteor.c
test/test_validate: test/test_validate.c $(VALID) src/*.h
	$(CC) $(CFLAGS) -DIONOPROP_NO_PSK -o $@ test/test_validate.c $(VALID) -lm

ESTEST = $(VALID) src/alerts.c src/spots.c src/targets.c src/canvas.c src/mapview.c src/map_data.c
test/test_es: test/test_es.c $(ESTEST) src/*.h
	$(CC) $(CFLAGS) -DIONOPROP_NO_PSK -DIONOPROP_NO_FONT -o $@ test/test_es.c $(ESTEST) -lm

test: test/test_parse test/test_predict test/test_validate test/test_es
	./test/test_parse test/data
	./test/test_predict
	./test/test_validate
	./test/test_es

# ---- Windows: keresztfordítás Linuxon, MinGW-w64-gyel ----
# Eredmény: win/IonoProp.exe (grafikus) és win/ionoprop-cli.exe (parancssori).
# Egyetlen fájl mindkettő, külön DLL nem kell hozzájuk (Windows 7 SP1 - 11, 64 bit).
WIN_PREFIX ?= x86_64-w64-mingw32-
WIN_CC     = $(WIN_PREFIX)gcc
WIN_RC     = $(WIN_PREFIX)windres
WIN_CFLAGS = -O2 -Wall -Wextra -std=gnu11 -D_WIN32_WINNT=0x0601 -D__USE_MINGW_ANSI_STDIO=1 -DIONOPROP_MINI_MQTT
WIN_LIBS   = -static -lwinhttp -lws2_32 -lgdi32 -luser32 -lpthread -lm
WIN_CORE   = src/loader.c src/fetch.c src/parse.c src/geo.c src/solar.c src/model.c \
             src/predict.c src/config.c src/pskr.c src/validate.c src/cache.c src/compat.c src/targets.c src/i18n.c src/meteor.c
WIN_GUI    = src/gui.c src/platform_win32.c src/canvas.c src/mapview.c src/map_data.c src/spots.c src/rig.c src/alerts.c
VERSION    = 0.9.1

windows: win/IonoProp.exe win/ionoprop-cli.exe

win/cJSON.o: third_party/cJSON.c third_party/cJSON.h
	$(WIN_CC) $(WIN_CFLAGS) -w -c -o $@ $<

win/ionoprop.res.o: win/ionoprop.rc win/ionoprop.ico win/ionoprop.manifest
	$(WIN_RC) -O coff -o $@ $<

win/IonoProp.exe: $(WIN_CORE) $(WIN_GUI) src/*.h win/cJSON.o win/ionoprop.res.o
	$(WIN_CC) $(WIN_CFLAGS) -mwindows -o $@ $(WIN_CORE) $(WIN_GUI) win/cJSON.o win/ionoprop.res.o $(WIN_LIBS)

win/ionoprop-cli.exe: $(WIN_CORE) src/main.c src/*.h win/cJSON.o win/ionoprop.res.o
	$(WIN_CC) $(WIN_CFLAGS) -o $@ $(WIN_CORE) src/main.c win/cJSON.o win/ionoprop.res.o $(WIN_LIBS)

# letölthető csomag: IonoProp-$(VERSION)-windows.zip
win-zip: windows
	rm -rf win/IonoProp && mkdir -p win/IonoProp
	cp win/IonoProp.exe win/ionoprop-cli.exe win/OLVASSEL.txt win/README.txt win/IonoProp/
	cp LICENSE win/IonoProp/LICENSE.txt
	cd win && rm -f IonoProp-$(VERSION)-windows.zip && zip -r IonoProp-$(VERSION)-windows.zip IonoProp

# telepítés a saját felhasználónak: program, menüpont, ikon
BIN_DIR  = $(HOME)/.local/bin
APP_DIR  = $(HOME)/.local/share/applications
ICON_DIR = $(HOME)/.local/share/icons/hicolor/128x128/apps
AUTO_DIR = $(HOME)/.config/autostart
# shack-kijelző: teljes képernyő, a térkép 30 mp-enként vált
KIOSK_ARGS ?= -F -C 30

install: all
	install -d $(BIN_DIR) $(APP_DIR) $(ICON_DIR)
	install -m 755 ionoprop ionoprop-gui $(BIN_DIR)/
	install -m 644 dist/ionoprop.png $(ICON_DIR)/ionoprop.png
	sed -e 's|@BIN@|$(BIN_DIR)|' -e 's|@ICON@|ionoprop|' dist/ionoprop.desktop > $(APP_DIR)/ionoprop.desktop

# automatikus indítás bejelentkezéskor, teljes képernyőn (Raspberry Pi shack-kijelző)
autostart: install
	install -d $(AUTO_DIR)
	sed -e 's|@BIN@/ionoprop-gui|$(BIN_DIR)/ionoprop-gui $(KIOSK_ARGS)|' -e 's|@ICON@|ionoprop|' \
	    dist/ionoprop.desktop > $(AUTO_DIR)/ionoprop.desktop

no-autostart:
	rm -f $(AUTO_DIR)/ionoprop.desktop

uninstall: no-autostart
	rm -f $(BIN_DIR)/ionoprop $(BIN_DIR)/ionoprop-gui $(APP_DIR)/ionoprop.desktop $(ICON_DIR)/ionoprop.png

clean:
	rm -f ionoprop ionoprop-gui src/*.o third_party/*.o test/test_parse test/test_predict test/test_validate test/test_es
	rm -rf win/*.o win/*.exe win/IonoProp win/*.zip

.PHONY: all test install autostart no-autostart uninstall clean windows win-zip
