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
          $(SRCDIR)/at_modem.c \
          $(SRCDIR)/ui.c

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

# Chemins surchargeables (portabilite : un contributeur les redefinit sans
# editer le Makefile, ex. `make run ORIC_ROMS=/opt/oric/roms`). Les cibles de
# build et de test (make / make test / make fuzz / make coverage) n'en
# dependent PAS : seules les cibles run-* utilisent l'emulateur et les ROM.
ORIC_ROMS ?= /home/bmarty/Oric1/roms

# ROM Microdisc pour booter une disquette Sedoric dans Phosphoric
DISK_ROM ?= $(ORIC_ROMS)/microdis.rom

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
EMU      ?= ./tools/oric1-emu-sdl
EMU_ROM  ?= $(ORIC_ROMS)/basic11b.rom

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
PICO_DEV  ?= /dev/ttyACM0
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
# son unique octet RX. ATTENTION : ce n'est PAS le montage reel. Le firmware
# LOCI place un anneau de 32 octets devant le registre de donnees
# (~/loci/firmware/src/mia/oric/acia.c, ACIA_RX_BUFFER_SIZE) : sans tampon on
# est donc PLUS PESSIMISTE que le materiel. Constate sur dongle physique :
# sans tampon la page Videotex arrive corrompue, avec --serial-buffer 32 elle
# est propre. LOCI_BUFFER vaut donc 32 par defaut (fidele au firmware) ;
# LOCI_BUFFER= (vide) donne le mode STRESS 1 octet, utile comme garde-fou.
EMU_LOCI_REAL  = $(EMU)
ROM_ORIC1      ?= $(ORIC_ROMS)/basic10.rom
LOCI_BUFFER    ?= 32
EMU_OPTS_LOCI_REAL = --loci --serial com:$(PICO_BAUD),8,N,1,$(PICO_DEV) \
                     $(if $(LOCI_BUFFER),--serial-buffer $(LOCI_BUFFER),)

# Scenario CO-SIM (run-loci-cosim) : l'ACIA $0380 est servie par le VRAI firmware
# RP2040 (emulateur LOCI de Phosphoric, --loci-emu) qui relaie le PicoWiFiModemUSB
# PHYSIQUE via --loci-cdc. C'est le chemin LE PLUS FIDELE : Oric -> ACIA $0380 ->
# oric/acia.c du vrai firmware LOCI -> USB-CDC -> dongle. Contrairement a run-loci
# (backend `com:` comportemental de Phosphoric), le 6551 est ici servi par le
# firmware reel co-simule. Necessite un Phosphoric bati AVEC --loci-cdc : par defaut
# le build live ~/Oric1/oric1-emu (le tools/ bundle ne le supporte pas encore).
# Sans dongle physique, remplacer $(PICO_DEV) par un PTY (faux modem AT).
EMU_COSIM ?= $(HOME)/Oric1/oric1-emu
FW_ELF    ?= $(HOME)/loci/firmware/build-xip/src/loci-firmware.elf
EMU_OPTS_LOCI_COSIM = --loci-emu $(FW_ELF) --loci-cdc $(PICO_DEV)

# Flags cc65
CC65FLAGS = -t $(TARGET) -O --add-source
CA65FLAGS = -t $(TARGET)

# ============================================================================
# Cibles principales
# ============================================================================

.PHONY: all clean run run-picowifi run-loci run-loci-emu run-loci-cosim run-loci-real run-ws run-dsk bridge dsk diag bench-render test test-videotex test-serial test-serial-noraw test-menus test-servers test-atmodem test-keyboard test-ui test-bridge fuzz coverage help

all: $(OUTPUT)

$(OUTPUT): $(OBJS) $(CFG)
	$(LD65) -C $(CFG) -o $@ $(OBJS) \
		-m $(MAPFILE) $(TARGET).lib
	@echo "=== OricTel compile: $(OUTPUT) ==="
	@ls -la $(OUTPUT)

# ============================================================================
# Programme de diagnostic ACIA 6551 / LOCI (diag.tap)
# Outil autonome : acces direct aux registres $0380-$0383, essai a chaud de
# toutes les configurations Control/Command, affichage du status en direct.
# Reutilise tous les modules SAUF main.o (diag.c fournit son propre main()).
# ============================================================================
DIAGOUT  = diag.tap
DIAG_OBJS = $(BLDDIR)/diag.o $(filter-out $(BLDDIR)/main.o,$(OBJS))

diag: $(DIAG_OBJS) $(CFG)
	$(LD65) -C $(CFG) -o $(DIAGOUT) $(DIAG_OBJS) \
		-m diag.map $(TARGET).lib
	@echo "=== Diag compile: $(DIAGOUT) ==="
	@ls -la $(DIAGOUT)

# ============================================================================
# Banc de mesure du cout CPU du rendu (bench.tap)
# Chiffre en CYCLES 6502 reels le cout des passes de rendu de display.c, pour
# le comparer au budget d'un octet a 1200 bauds (8333 cycles a 1 MHz). Sert a
# dimensionner le correctif anti-overrun RX (cf. ROADMAP).
# Reutilise tous les modules SAUF main.o (bench_render.c fournit son main()).
# ============================================================================
BENCHOUT   = bench.tap
BENCH_OBJS = $(BLDDIR)/bench_render.o $(filter-out $(BLDDIR)/main.o,$(OBJS))

$(BENCHOUT): $(BENCH_OBJS) $(CFG)
	$(LD65) -C $(CFG) -o $(BENCHOUT) $(BENCH_OBJS) \
		-m bench.map $(TARGET).lib
	@echo "=== Bench compile: $(BENCHOUT) ==="
	@ls -la $(BENCHOUT)

# Execute le banc sous Phosphoric headless et depouille la trace serie.
bench-render: $(BENCHOUT)
	@$(TESTDIR)/bench_render.sh

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

# CO-SIM : ACIA $0380 servie par le VRAI firmware LOCI (--loci-emu) relayant le Pico
# physique via --loci-cdc. Chemin le plus fidele (Oric -> firmware reel -> dongle).
run-loci-cosim: $(OUTPUT)
	@echo "=== OricTel -> ACIA \$$0380 servie par le firmware LOCI REEL (co-sim) + Pico $(PICO_DEV) ==="
	@echo "    Emulateur : $(EMU_COSIM) ; firmware : $(FW_ELF)"
	@echo "    Dans OricTel : ecran Interface (une touche), mode Modem AT, ATD"
	@test -x $(EMU_COSIM) || { echo "ERREUR: $(EMU_COSIM) absent (bati ~/Oric1 avec --loci-cdc ?)"; exit 1; }
	@test -f $(FW_ELF) || { echo "ERREUR: firmware $(FW_ELF) introuvable"; exit 1; }
	@test -c $(PICO_DEV) || { echo "ERREUR: $(PICO_DEV) introuvable (Pico branche ?)"; exit 1; }
	$(EMU_COSIM) --rom $(EMU_ROM) --tape $(OUTPUT) -f $(EMU_OPTS_LOCI_COSIM)

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

test: test-videotex test-serial test-atmodem test-keyboard test-ui test-serial-noraw test-menus test-bridge

# Garde-fou reception FIDELE au 6551 reel : rejoue une rafale sur $0380 via le
# backend `file:` de Phosphoric SANS --serial-buffer (RX 1 octet), verifie via
# la trace serie que tout est recu byte-exact (FIFO=0). Necessite l'emulateur +
# une ROM (surcharge EMU=/ROM=) ; en leur absence (CI host-only) -> SKIP (rc 0).
test-serial-noraw: diag.tap
	@$(TESTDIR)/test_serial_noraw.sh

# Parcours de menus de bout en bout sous Phosphoric headless : jusqu'a l'ecran
# d'echec de connexion (non-regression du retour de modem_connect ignore, qui
# faisait entrer en session sur un flux inexistant). Necessite Phosphoric
# >= v1.118 (avant, --type-keys renvoyait la machine au BASIC) ; sinon SKIP.
test-menus: $(OUTPUT)
	@$(TESTDIR)/test_menus.sh

# Fidelite du decodage contre de VRAIS serveurs Minitel, via la chaine
# co-simulee (firmware LOCI reel + PicoWiFiModemUSB physique). Lent (~2 min par
# serveur, 1200 bauds en temps reel) et tributaire du reseau + du dongle : HORS
# de `make test`. ALL=1 pour tester aussi MiniPavi. SKIP si materiel absent.
test-servers: $(OUTPUT)
	@ALL=$(if $(ALL),$(ALL),0) $(TESTDIR)/test_servers.sh

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

# Mapping clavier Oric -> Minitel (clavier scripte): touches speciales,
# CTRL+lettre, CTRL locaux, FUNCT Atmos, ASCII, emission SEP+code et fleches.
test-keyboard: $(TESTDIR)/test_keyboard.c $(SRCDIR)/keyboard.c | $(BLDDIR)
	gcc -Wall -Wextra -I$(SRCDIR) -o $(BLDDIR)/test_keyboard \
		$(TESTDIR)/test_keyboard.c $(SRCDIR)/keyboard.c -DTEST_HOST
	$(BLDDIR)/test_keyboard

# Helpers UI (ui.c): clip ui_print + bornes de saisie ui_text_input
# (non-regression des findings revue #2-#5). Clavier scripte, rendu neutralise.
test-ui: $(TESTDIR)/test_ui.c $(SRCDIR)/ui.c | $(BLDDIR)
	gcc -Wall -Wextra -I$(SRCDIR) -o $(BLDDIR)/test_ui \
		$(TESTDIR)/test_ui.c $(SRCDIR)/ui.c
	$(BLDDIR)/test_ui

# Le runner integre du script gere les tests async (pytest sans
# pytest-asyncio ne sait pas les executer et echouait silencieusement
# avant de retomber sur le script: double execution trompeuse).
test-bridge:
	python3 $(TESTDIR)/test_bridge.py

# Serveur Videotex local interactif (test manuel, pas dans 'test'):
# lance un serveur TCP qui envoie des sequences Videotex de demo.
test-server:
	python3 $(TESTDIR)/test_server.py --test all

# Fuzzing du decodeur Videotex (vtx_process) = surface reseau d'OricTel, sous
# AddressSanitizer + UndefinedBehaviorSanitizer via libFuzzer (clang requis).
# FUZZ_TIME borne la duree (defaut 30 s). Un crash laisse un fichier crash-*
# a rejouer : `build/fuzz_videotex crash-xxxx`.
FUZZCC    ?= clang
FUZZ_TIME ?= 30
fuzz: $(TESTDIR)/fuzz_videotex.c $(SRCDIR)/videotex.c | $(BLDDIR)
	$(FUZZCC) -O1 -g -DTEST_HOST -I$(SRCDIR) \
		-fsanitize=fuzzer,address,undefined \
		$(TESTDIR)/fuzz_videotex.c $(SRCDIR)/videotex.c $(SRCDIR)/fonts.c \
		-o $(BLDDIR)/fuzz_videotex
	@# -print_funcs=0 : sans llvm-symbolizer installe, libFuzzer se BLOQUE en
	@# tentant de symboliser chaque "NEW_FUNC". Le fuzzing tournait alors a ~4
	@# executions au lieu de ~365 000 (22 800/s) : garde-fou silencieusement mort.
	$(BLDDIR)/fuzz_videotex -max_total_time=$(FUZZ_TIME) -print_funcs=0 \
		-print_final_stats=1

# Couverture host (gcov) du decodeur Videotex et du modem AT : compile les
# tests avec --coverage, les execute, puis affiche le % de lignes couvertes.
coverage: | $(BLDDIR)
	@mkdir -p $(BLDDIR)/cov
	gcc --coverage -O0 -I$(SRCDIR) -DTEST_HOST -c $(SRCDIR)/videotex.c   -o $(BLDDIR)/cov/videotex.o
	gcc --coverage -O0 -I$(SRCDIR) -DTEST_HOST -c $(SRCDIR)/fonts.c      -o $(BLDDIR)/cov/fonts.o
	gcc --coverage -O0 -I$(SRCDIR) -DTEST_HOST -c $(TESTDIR)/test_videotex.c -o $(BLDDIR)/cov/tv.o
	gcc --coverage -o $(BLDDIR)/cov/test_videotex $(BLDDIR)/cov/videotex.o $(BLDDIR)/cov/fonts.o $(BLDDIR)/cov/tv.o
	$(BLDDIR)/cov/test_videotex >/dev/null
	gcc --coverage -O0 -I$(SRCDIR) -DTEST_HOST -c $(SRCDIR)/at_modem.c   -o $(BLDDIR)/cov/at_modem.o
	gcc --coverage -O0 -I$(SRCDIR) -DTEST_HOST -c $(TESTDIR)/test_atmodem.c -o $(BLDDIR)/cov/ta.o
	gcc --coverage -o $(BLDDIR)/cov/test_atmodem $(BLDDIR)/cov/at_modem.o $(BLDDIR)/cov/ta.o
	$(BLDDIR)/cov/test_atmodem >/dev/null
	@echo "=== Couverture lignes (gcov) ==="
	@gcov -n -o $(BLDDIR)/cov $(SRCDIR)/videotex.c $(SRCDIR)/at_modem.c 2>/dev/null \
		| grep -E "File '.*(videotex|at_modem)\.c'|Lines executed"

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
	@echo "  run-loci-cosim ACIA \$$0380 servie par le firmware LOCI REEL (co-sim) + Pico $(PICO_DEV)"
	@echo "  run-loci-real Oric-1 + LOCI + Pico physique, emulateur 1.27.6 (\$$0380)"
	@echo "  run-ws        Bridge WebSocket + emulateur (ws://3617.fr)"
	@echo "  bridge        Lancer uniquement le bridge"
	@echo "  test          Executer tous les tests"
	@echo "  test-videotex Tests du decodeur Videotex"
	@echo "  test-serial   Tests coherence bases ACIA (emu/LOCI, SMC)"
	@echo "  test-atmodem  Tests machine d'etats modem AT (faux modem)"
	@echo "  test-keyboard Tests mapping clavier Oric -> Minitel (clavier scripte)"
	@echo "  test-ui       Tests helpers UI (clip ui_print + bornes saisie)"
	@echo "  test-bridge   Tests du bridge"
	@echo "  test-server   Serveur Videotex local de demo (test manuel)"
	@echo "  fuzz          Fuzzing du decodeur Videotex (ASAN/UBSAN, FUZZ_TIME=30)"
	@echo "  coverage      Couverture host (gcov) Videotex + modem AT"
	@echo "  clean         Nettoyer les fichiers generes"
	@echo "  help          Afficher cette aide"
