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
EMU      = ./tools/oric1-emu-sdl
EMU_ROM  = /home/bmarty/Oric1/roms/basic11b.rom

# OricTel pilote desormais l'ACIA 6551 a la base LOCI ($0380) uniquement.
# Les backends emules de Phosphoric (modem/tcp/picowifi/digitelec) sont donc
# tous relocalises a $0380 via --acia-addr 0380 pour rester en phase.
ACIA_ADDR = --acia-addr 0380

# Modem AT: OricTel choisit le serveur via ATD (recommande)
EMU_OPTS = --serial modem $(ACIA_ADDR) --serial-buffer 4096

# TCP direct vers un serveur fixe (pas de choix serveur dans OricTel)
MINITEL_SERVER = pavi.3617.fr:3617
EMU_OPTS_DIRECT = --serial tcp:$(MINITEL_SERVER) --serial-v23 $(ACIA_ADDR) --serial-buffer 4096

# Backend Digitelec de Phosphoric (modem V23 sur ACIA 6551, gestion porteuse
# DCD/CTS, pas de commandes AT). Le mode V23 1200/75 est auto-active par le
# backend. A utiliser avec OricTel en mode Direct (menu 2).
EMU_OPTS_DIGITELEC = --serial digitelec:$(MINITEL_SERVER) $(ACIA_ADDR) --serial-buffer 4096

# Backend PicoWiFiModemUSB (sodiumlb) de Phosphoric : modem AT WiFi sur ACIA
# 6551, association WiFi simulee, connexions data = vraies sockets TCP. A
# utiliser avec OricTel en mode Modem AT (menu 1), puis ATD vers un serveur.
PICOWIFI_SSID = OricTel
EMU_OPTS_PICOWIFI = --serial picowifi:$(PICOWIFI_SSID) $(ACIA_ADDR) --serial-buffer 4096

# PicoWiFiModemUSB PHYSIQUE branche en USB (et non l'emulation ci-dessus).
# Phosphoric route l'ACIA vers le vrai port serie via le backend
# `com:B,D,P,S,DEV`. OricTel pilote l'ACIA en 8N1 -> format ligne 8,N,1.
# Le PicoWiFiModemUSB dialogue a 9600 bauds cote DTE (config validee par
# Phosphoric sprint 60b avec le vrai modem).
PICO_DEV  = /dev/ttyACM0
PICO_BAUD = 9600

# Scenario B : montage reel Oric + LOCI + Pico. La cartouche LOCI expose l'ACIA
# 6551 a $0380 et relaie le PicoWiFiModemUSB branche sur son port USB. Config
# authentique Phosphoric (sprint 60b). Choisir l'interface 1 (LOCI) dans OricTel.
EMU_OPTS_LOCI = --loci --serial com:$(PICO_BAUD),8,N,1,$(PICO_DEV) --serial-buffer 4096

# Scenario C : test du chemin LOCI ($0380) SANS materiel, avec le modem
# PicoWiFi emule par Phosphoric relocalise a $0380. Choisir l'interface 2.
EMU_OPTS_LOCI_EMU = --serial picowifi:$(PICOWIFI_SSID) --acia-addr 0380 --serial-buffer 4096

# Scenario B' (run-loci-real) : montage REEL Oric-1 + LOCI + Pico, avec le
# binaire Phosphoric FIDELE. Le binaire local tools/ (1.19.1) place seulement
# la MIA LOCI a $03A0 et laisse l'ACIA a $031C ; la modelisation fidele du
# modem LOCI a $0380 n'arrive qu'en Phosphoric >= 1.27 (--loci => ACIA $0380).
# On vise donc l'emulateur du depot /home/bmarty/Oric1 (1.27.3, SDL), ROM
# Oric-1 (basic10), et AUCUN --serial-buffer : le 6551 garde son unique octet
# RX, conditions proches du vrai materiel (le correctif anti-overrun a un sens).
# Ajouter LOCI_BUFFER=512 (ou autre) si la reception est trop instable.
EMU_LOCI_REAL  = /home/bmarty/Oric1/oric1-emu
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

.PHONY: all clean run run-direct run-digitelec run-picowifi run-loci run-loci-emu run-loci-real run-ws run-dsk bridge dsk test test-videotex test-serial test-atmodem test-bridge help

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
	@echo "=== OricTel -> modem AT (serveur choisi dans le menu) ==="
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS)

run-direct: $(OUTPUT)
	@echo "=== OricTel -> $(MINITEL_SERVER) (TCP direct V23) ==="
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS_DIRECT)

run-digitelec: $(OUTPUT)
	@echo "=== OricTel -> $(MINITEL_SERVER) (backend Digitelec DTL 2000, V23) ==="
	@echo "    Dans OricTel : choisir le mode Direct (touche 2)"
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS_DIGITELEC)

run-picowifi: $(OUTPUT)
	@echo "=== OricTel -> modem AT WiFi PicoWiFiModemUSB (SSID=$(PICOWIFI_SSID)) ==="
	@echo "    Dans OricTel : mode Modem AT (touche 1), puis ATD vers un serveur"
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS_PICOWIFI)

run-loci: $(OUTPUT)
	@echo "=== OricTel -> LOCI reel ($(PICO_DEV) @ $(PICO_BAUD) baud, ACIA \$$0380) ==="
	@echo "    Dans OricTel : interface 1 (LOCI), mode Modem AT, ATD"
	@test -c $(PICO_DEV) || { echo "ERREUR: $(PICO_DEV) introuvable (Pico branche ?)"; exit 1; }
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS_LOCI)

run-loci-emu: $(OUTPUT)
	@echo "=== OricTel -> ACIA LOCI emulee \$$0380 + modem PicoWiFi emule (test sans materiel) ==="
	@echo "    Dans OricTel : interface 2 (LOCI + PicoWiFi), mode Modem AT, ATD"
	$(EMU) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS_LOCI_EMU)

# Montage REEL Oric-1 + LOCI + PicoWiFiModemUSB physique, emulateur FIDELE
# (Phosphoric 1.27.3 du depot /home/bmarty/Oric1, --loci => ACIA $0380).
run-loci-real: $(OUTPUT)
	@echo "=== OricTel -> LOCI reel FIDELE (Oric-1, ACIA \$$0380, $(PICO_DEV)) ==="
	@echo "    Emulateur : $(EMU_LOCI_REAL)"
	@echo "    Dans OricTel : interface 1 (LOCI), mode Modem AT"
	@echo "    (Pico non associe au WiFi ? -> menu 3 Config WiFi d'abord)"
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
	@echo "  run           Emulateur en mode modem AT (serveur choisi au menu)"
	@echo "  run-direct    Emulateur en TCP direct V23 vers $(MINITEL_SERVER)"
	@echo "  run-digitelec Backend Digitelec DTL 2000 (V23) vers $(MINITEL_SERVER)"
	@echo "  run-picowifi  Modem AT WiFi PicoWiFiModemUSB EMULE (SSID=$(PICOWIFI_SSID))"
	@echo "  run-loci      LOCI reel + Pico physique (ACIA \$$0380, $(PICO_DEV))"
	@echo "  run-loci-emu  Chemin LOCI \$$0380 teste sans materiel (PicoWiFi emule)"
	@echo "  run-loci-real Oric-1 + LOCI + Pico physique, emulateur fidele 1.27.3 (\$$0380)"
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
