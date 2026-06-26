# ============================================================================
# OricTel - Makefile
# Emulateur Minitel 1B pour Oric 1/Atmos
# ============================================================================

# Toolchain cc65
CC65    = cc65
CA65    = ca65
LD65    = ld65
CL65    = cl65
TARGET  = atmos
CFG     = cfg/orictel.cfg

# Repertoires
SRCDIR  = src
CFGDIR  = cfg
BLDDIR  = build
BRDIR   = bridge
TESTDIR = tests

# Sources C
C_SRCS  = $(SRCDIR)/main.c \
          $(SRCDIR)/videotex.c \
          $(SRCDIR)/display.c \
          $(SRCDIR)/fonts.c \
          $(SRCDIR)/keyboard.c \
          $(SRCDIR)/serial.c \
          $(SRCDIR)/serial_tx.c \
          $(SRCDIR)/at_modem.c

# Sources assembleur
ASM_SRCS = $(SRCDIR)/tapehdr.s \
           $(SRCDIR)/serial_asm.s \
           $(SRCDIR)/display_asm.s

# Objets
C_OBJS   = $(patsubst $(SRCDIR)/%.c,$(BLDDIR)/%.o,$(C_SRCS))
ASM_OBJS = $(patsubst $(SRCDIR)/%.s,$(BLDDIR)/%.o,$(ASM_SRCS))
OBJS     = $(C_OBJS) $(ASM_OBJS)

# Cible principale
OUTPUT   = orictel.tap
MAPFILE  = orictel.map

# Image disquette Sedoric 3 (.dsk au format MFM_DISK)
DSK         = orictel.dsk
DSKTOOLSDIR = tools/dsktools
TAP2DSK     = $(BLDDIR)/tap2dsk
OLD2MFM     = $(BLDDIR)/old2mfm
# Init string Sedoric executee au boot : charge et auto-lance ORICTEL.COM
# (programme BASIC auto-executable -> LOAD declenche le RUN automatiquement).
DSK_LABEL   = ORICTEL DISK
DSK_INIT    = LOAD"ORICTEL"
HOSTCC      = cc

# ROM Microdisc pour booter une disquette Sedoric dans Phosphoric
DISK_ROM = /home/bmarty/Oric1/roms/microdis.rom

# Emulateur Phosphoric (oric1-emu).
# IMPORTANT: le binaire fourni par l'equipe Phosphoric est compile SANS SDL
# (cible headless uniquement, aucune fenetre). Pour l'affichage graphique, on
# utilise une copie locale compilee avec SDL2 (make SDL2=1 cote Phosphoric),
# stockee dans tools/ pour ne PAS modifier le depot Phosphoric.
# Regenerer si besoin :
#   cd /home/bmarty/Oric1 && make clean && make SDL2=1 -j
#   cp /home/bmarty/Oric1/oric1-emu tools/oric1-emu-sdl
#   cd /home/bmarty/Oric1 && make clean   # restaure l'etat headless de l'equipe
# Version actuelle de la copie locale : Phosphoric 1.27.6-alpha + SDL2.
EMU      = ./tools/oric1-emu-sdl
EMU_ROM  = /home/bmarty/Oric1/roms/basic11b.rom

# OricTel pilote l'ACIA 6551 a la base LOCI ($0380) uniquement. Le flag
# `--loci` de Phosphoric (>= 1.27) mappe justement l'ACIA modem a $0380 (cf.
# main.c:3121 cote Phosphoric) : on l'utilise pour TOUS les lancements, le
# binaire local tools/oric1-emu-sdl etant desormais en 1.27.6. (--acia-addr
# 0380 n'est plus necessaire ; --loci est la forme canonique.)

# Lancement par defaut: emule fidelement le seul device reel, le
# PicoWiFiModemUSB (backend `picowifi`), pas un modem Hayes abstrait. Ainsi la
# page Config WiFi (AT$SCAN -> liste les vrais reseaux de l'hote via nmcli) et
# la numerotation ATD fonctionnent d'emblee : lance avec `picowifi:SSID`, le
# Pico emule DEMARRE deja associe (simule) a ce SSID (serial_picowifi.c:1692).
# L'ancien backend `--serial modem` (modem Hayes generique sans WiFi) ne
# correspondait a aucun montage OricTel reel et ne repondait pas a AT$SCAN.
EMU_OPTS = --loci --serial picowifi:$(PICOWIFI_SSID) --serial-buffer 4096

# Backend PicoWiFiModemUSB (sodiumlb) de Phosphoric : modem AT WiFi sur ACIA
# 6551, association WiFi simulee, connexions data = vraies sockets TCP. A
# utiliser avec OricTel en mode Modem AT (menu 1), puis ATD vers un serveur.
PICOWIFI_SSID = OricTel
EMU_OPTS_PICOWIFI = --loci --serial picowifi:$(PICOWIFI_SSID) --serial-buffer 4096

# PicoWiFiModemUSB PHYSIQUE branche en USB (et non l'emulation ci-dessus).
# Phosphoric route l'ACIA ($0380 via --loci) vers le vrai port serie via le
# backend `com:B,D,P,S,DEV` (baud,databits,parite,stop,device - baud EN PREMIER,
# device EN DERNIER). OricTel pilote l'ACIA en 8N1. Le PicoWiFiModemUSB en
# USB-CDC dialogue a 115200 bauds cote DTE.
PICO_DEV  = /dev/ttyACM0
PICO_BAUD = 115200

# Scenario B : montage reel Oric + LOCI + Pico. La cartouche LOCI expose l'ACIA
# 6551 a $0380 et relaie le PicoWiFiModemUSB branche sur son port USB. Config
# authentique Phosphoric (sprint 60b).
EMU_OPTS_LOCI = --loci --serial com:$(PICO_BAUD),8,N,1,$(PICO_DEV) --serial-buffer 4096

# Scenario C : test du chemin LOCI ($0380) SANS materiel, avec le modem
# PicoWiFi emule par Phosphoric (--loci => ACIA $0380).
EMU_OPTS_LOCI_EMU = --loci --serial picowifi:$(PICOWIFI_SSID) --serial-buffer 4096

# Scenario B' (run-loci-real) : montage REEL Oric-1 + LOCI + Pico. Le binaire
# local tools/oric1-emu-sdl est desormais en 1.27.6 (>= 1.27 -> --loci mappe
# l'ACIA a $0380), il sert donc aussi pour ce scenario. Specificites : ROM
# Oric-1 (basic10) au lieu d'Atmos, et AUCUN --serial-buffer : le 6551 garde
# son unique octet RX, conditions proches du vrai materiel (le correctif
# anti-overrun a un sens). Ajouter LOCI_BUFFER=512 si la reception est instable.
EMU_LOCI_REAL  = $(EMU)
ROM_ORIC1      = /home/bmarty/Oric1/roms/basic10.rom
LOCI_BUFFER    =
EMU_OPTS_LOCI_REAL = --loci --serial com:$(PICO_BAUD),8,N,1,$(PICO_DEV) \
                     $(if $(LOCI_BUFFER),--serial-buffer $(LOCI_BUFFER),)

# Flags cc65
CC65FLAGS = -t $(TARGET) -O --add-source
CA65FLAGS = -t $(TARGET)

# ============================================================================
# Cibles principales
# ============================================================================

.PHONY: all clean run run-picowifi run-loci run-loci-emu run-loci-real run-ws run-dsk bridge dsk test test-videotex test-serial test-atmodem test-bridge help

all: $(OUTPUT)

$(OUTPUT): $(OBJS) $(CFG)
	$(LD65) -C $(CFG) -o $@ $(OBJS) \
		-m $(MAPFILE) $(TARGET).lib
	@echo "=== OricTel compile: $(OUTPUT) ==="
	@ls -la $(OUTPUT)

# ============================================================================
# Image disquette Sedoric 3 (.dsk)
#
# Chaine : orictel.tap --tap2dsk--> .dsk (ancien format ORICDISK)
#                      --old2mfm--> .dsk (format MFM_DISK des emulateurs)
# Les outils (OSDK de F.Frances) sont compiles avec le compilateur HOST.
# Le fichier ORICTEL.COM (auto-executable) est lance au boot via l'init string.
# ============================================================================

$(TAP2DSK): $(DSKTOOLSDIR)/tap2dsk.c $(DSKTOOLSDIR)/sedoric3.h | $(BLDDIR)
	$(HOSTCC) -O2 -w -I$(DSKTOOLSDIR) -o $@ $(DSKTOOLSDIR)/tap2dsk.c

$(OLD2MFM): $(DSKTOOLSDIR)/old2mfm.c | $(BLDDIR)
	$(HOSTCC) -O2 -w -o $@ $(DSKTOOLSDIR)/old2mfm.c

dsk: $(DSK)

$(DSK): $(OUTPUT) $(TAP2DSK) $(OLD2MFM)
	$(TAP2DSK) -n'$(DSK_LABEL)' -i'$(DSK_INIT)' $(OUTPUT) $@
	$(OLD2MFM) $@
	@echo "=== OricTel disquette Sedoric: $(DSK) ==="
	@ls -la $(DSK)

# ============================================================================
# Compilation C -> objet
# ============================================================================

$(BLDDIR)/%.o: $(SRCDIR)/%.c | $(BLDDIR)
	$(CC65) $(CC65FLAGS) -o $(BLDDIR)/$*.s $<
	$(CA65) $(CA65FLAGS) -o $@ $(BLDDIR)/$*.s

# ============================================================================
# Assemblage ASM -> objet
# ============================================================================

$(BLDDIR)/%.o: $(SRCDIR)/%.s | $(BLDDIR)
	$(CA65) $(CA65FLAGS) -o $@ $<

# ============================================================================
# Repertoire build
# ============================================================================

$(BLDDIR):
	mkdir -p $(BLDDIR)

# ============================================================================
# Execution
# ============================================================================

run: $(OUTPUT)
	@echo "=== OricTel -> PicoWiFiModemUSB emule (modem AT, Config WiFi OK) ==="
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS)

run-picowifi: $(OUTPUT)
	@echo "=== OricTel -> modem AT WiFi PicoWiFiModemUSB (SSID=$(PICOWIFI_SSID)) ==="
	@echo "    Dans OricTel : mode Modem AT (touche 1), puis ATD vers un serveur"
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS_PICOWIFI)

run-loci: $(OUTPUT)
	@echo "=== OricTel -> LOCI reel ($(PICO_DEV) @ $(PICO_BAUD) baud, ACIA \$$0380) ==="
	@echo "    Dans OricTel : ecran Interface (une touche), mode Modem AT, ATD"
	@test -c $(PICO_DEV) || { echo "ERREUR: $(PICO_DEV) introuvable (Pico branche ?)"; exit 1; }
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS_LOCI)

run-loci-emu: $(OUTPUT)
	@echo "=== OricTel -> ACIA LOCI emulee \$$0380 + modem PicoWiFi emule (test sans materiel) ==="
	@echo "    Dans OricTel : ecran Interface (une touche), mode Modem AT, ATD"
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS_LOCI_EMU)

# Montage REEL Oric-1 + LOCI + PicoWiFiModemUSB physique, emulateur 1.27.6
# (tools/oric1-emu-sdl, --loci => ACIA $0380), ROM Oric-1 (basic10).
run-loci-real: $(OUTPUT)
	@echo "=== OricTel -> LOCI reel (Oric-1, ACIA \$$0380, $(PICO_DEV)) ==="
	@echo "    Emulateur : $(EMU_LOCI_REAL)"
	@echo "    Dans OricTel : ecran Interface (une touche), mode Modem AT"
	@echo "    (Pico non associe au WiFi ? -> menu 2 Config WiFi d'abord)"
	@test -x $(EMU_LOCI_REAL) || { echo "ERREUR: $(EMU_LOCI_REAL) introuvable/non executable"; exit 1; }
	@test -f $(ROM_ORIC1) || { echo "ERREUR: ROM Oric-1 $(ROM_ORIC1) introuvable"; exit 1; }
	@test -c $(PICO_DEV) || { echo "ERREUR: $(PICO_DEV) introuvable (Pico branche ?)"; exit 1; }
	$(EMU_LOCI_REAL) --rom $(ROM_ORIC1) --tape $(OUTPUT) -f $(EMU_OPTS_LOCI_REAL)

# Booter la disquette Sedoric (Microdisc) : OricTel se lance automatiquement.
# Mode modem AT par defaut (serveur choisi dans le menu).
run-dsk: $(DSK)
	@echo "=== OricTel depuis disquette Sedoric (boot Microdisc) ==="
	@test -f $(DISK_ROM) || { echo "ERREUR: ROM Microdisc introuvable: $(DISK_ROM)"; exit 1; }
	$(EMU) --rom $(EMU_ROM) --disk-rom $(DISK_ROM) -d $(DSK) $(EMU_OPTS)

# Lancer avec le bridge WebSocket (pour ws://3617.fr).
# Une seule ligne shell: le PID du bridge est connu et tue a la sortie
# (chaque ligne de recette make tourne dans son propre shell, un kill %1
# sur une ligne separee ne tuerait jamais le processus).
run-ws: $(OUTPUT)
	@echo "=== Bridge WebSocket + emulateur ==="
	python3 $(BRDIR)/orictel_bridge.py & BRIDGE_PID=$$!; \
	sleep 2; \
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f \
		--serial tcp:127.0.0.1:3615 --serial-buffer 256 --serial-irq-on-rdrf; \
	kill $$BRIDGE_PID 2>/dev/null || true

# Lancer uniquement le bridge
bridge:
	python3 $(BRDIR)/orictel_bridge.py -v

# ============================================================================
# Tests
# ============================================================================

test: test-videotex test-serial test-atmodem test-bridge

test-videotex: $(TESTDIR)/test_videotex.c $(SRCDIR)/videotex.c
	gcc -Wall -Wextra -I$(SRCDIR) -o $(BLDDIR)/test_videotex \
		$(TESTDIR)/test_videotex.c $(SRCDIR)/videotex.c \
		$(SRCDIR)/fonts.c -DTEST_HOST
	$(BLDDIR)/test_videotex

# Coherence des bases ACIA / offsets registres (self-modifying code driver).
test-serial: $(TESTDIR)/test_serial_smc.c $(SRCDIR)/serial.h
	gcc -Wall -Wextra -I$(SRCDIR) -o $(BLDDIR)/test_serial_smc \
		$(TESTDIR)/test_serial_smc.c -DTEST_HOST
	$(BLDDIR)/test_serial_smc

# Machine d'etats modem AT (faux modem en memoire): OK/CONNECT/NO CARRIER,
# timeout, matcher ancre (anti-faux-positif), regression overrun, ATI/IP.
test-atmodem: $(TESTDIR)/test_atmodem.c $(SRCDIR)/at_modem.c
	gcc -Wall -Wextra -I$(SRCDIR) -o $(BLDDIR)/test_atmodem \
		$(TESTDIR)/test_atmodem.c $(SRCDIR)/at_modem.c -DTEST_HOST
	$(BLDDIR)/test_atmodem

# Le runner integre du script gere les tests async (pytest sans
# pytest-asyncio ne sait pas les executer et echouait silencieusement
# avant de retomber sur le script: double execution trompeuse).
test-bridge:
	python3 $(TESTDIR)/test_bridge.py

# Serveur Videotex local interactif (test manuel, pas dans 'test'):
# lance un serveur TCP qui envoie des sequences Videotex de demo.
test-server:
	python3 $(TESTDIR)/test_server.py --test all

# ============================================================================
# Nettoyage
# ============================================================================

clean:
	rm -rf $(BLDDIR) $(OUTPUT) $(MAPFILE) $(DSK)
	@echo "=== Nettoye ==="

# ============================================================================
# Aide
# ============================================================================

help:
	@echo "OricTel - Emulateur Minitel 1B pour Oric"
	@echo ""
	@echo "Cibles:"
	@echo "  all           Compiler orictel.tap (defaut)"
	@echo "  dsk           Construire la disquette Sedoric orictel.dsk"
	@echo "  run-dsk       Booter la disquette Sedoric (Microdisc, auto-lance OricTel)"
	@echo "  run           PicoWiFiModemUSB EMULE (modem AT + Config WiFi, SSID=$(PICOWIFI_SSID))"
	@echo "  run-picowifi  Alias de 'run' (PicoWiFiModemUSB emule)"
	@echo "  run-loci      LOCI reel + Pico physique (ACIA \$$0380, $(PICO_DEV))"
	@echo "  run-loci-emu  Alias de 'run' (chemin LOCI \$$0380 sans materiel)"
	@echo "  run-loci-real Oric-1 + LOCI + Pico physique, emulateur 1.27.6 (\$$0380)"
	@echo "  run-ws        Bridge WebSocket + emulateur (ws://3617.fr)"
	@echo "  bridge        Lancer uniquement le bridge"
	@echo "  test          Executer tous les tests"
	@echo "  test-videotex Tests du decodeur Videotex"
	@echo "  test-serial   Tests coherence bases ACIA (emu/LOCI, SMC)"
	@echo "  test-atmodem  Tests machine d'etats modem AT (faux modem)"
	@echo "  test-bridge   Tests du bridge"
	@echo "  test-server   Serveur Videotex local de demo (test manuel)"
	@echo "  clean         Nettoyer les fichiers generes"
	@echo "  help          Afficher cette aide"
