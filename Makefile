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
          $(SRCDIR)/serial_tx.c

# Sources assembleur
ASM_SRCS = $(SRCDIR)/serial_asm.s \
           $(SRCDIR)/display_asm.s

# Objets
C_OBJS   = $(patsubst $(SRCDIR)/%.c,$(BLDDIR)/%.o,$(C_SRCS))
ASM_OBJS = $(patsubst $(SRCDIR)/%.s,$(BLDDIR)/%.o,$(ASM_SRCS))
OBJS     = $(C_OBJS) $(ASM_OBJS)

# Cible principale
OUTPUT   = orictel.tap
MAPFILE  = orictel.map

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

# Modem AT: OricTel choisit le serveur via ATD (recommande)
EMU_OPTS = --serial modem --serial-buffer 4096

# TCP direct vers un serveur fixe (pas de choix serveur dans OricTel)
MINITEL_SERVER = pavi.3617.fr:3617
EMU_OPTS_DIRECT = --serial tcp:$(MINITEL_SERVER) --serial-v23 --serial-buffer 4096

# Backend Digitelec DTL 2000 de Phosphoric (modem V23 sur ACIA 6551 @ $031C,
# gestion porteuse DCD/CTS, pas de commandes AT). Le mode V23 1200/75 est
# auto-active par le backend. A utiliser avec OricTel en mode Direct (menu 2).
EMU_OPTS_DIGITELEC = --serial digitelec:$(MINITEL_SERVER) --serial-buffer 4096

# Backend PicoWiFiModemUSB (sodiumlb) de Phosphoric : modem AT WiFi sur ACIA
# 6551, association WiFi simulee, connexions data = vraies sockets TCP. A
# utiliser avec OricTel en mode Modem AT (menu 1), puis ATD vers un serveur.
PICOWIFI_SSID = OricTel
EMU_OPTS_PICOWIFI = --serial picowifi:$(PICOWIFI_SSID) --serial-buffer 4096

# Flags cc65
CC65FLAGS = -t $(TARGET) -O --add-source
CA65FLAGS = -t $(TARGET)

# ============================================================================
# Cibles principales
# ============================================================================

.PHONY: all clean run run-direct run-digitelec run-picowifi run-ws bridge test help

all: $(OUTPUT)

$(OUTPUT): $(OBJS) $(CFG)
	$(LD65) -C $(CFG) -o $@ $(OBJS) \
		-m $(MAPFILE) $(TARGET).lib
	@echo "=== OricTel compile: $(OUTPUT) ==="
	@ls -la $(OUTPUT)

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

test: test-videotex test-bridge

test-videotex: $(TESTDIR)/test_videotex.c $(SRCDIR)/videotex.c
	gcc -Wall -Wextra -I$(SRCDIR) -o $(BLDDIR)/test_videotex \
		$(TESTDIR)/test_videotex.c $(SRCDIR)/videotex.c \
		$(SRCDIR)/fonts.c -DTEST_HOST
	$(BLDDIR)/test_videotex

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
	rm -rf $(BLDDIR) $(OUTPUT) $(MAPFILE)
	@echo "=== Nettoye ==="

# ============================================================================
# Aide
# ============================================================================

help:
	@echo "OricTel - Emulateur Minitel 1B pour Oric"
	@echo ""
	@echo "Cibles:"
	@echo "  all           Compiler orictel.tap (defaut)"
	@echo "  run           Emulateur en mode modem AT (serveur choisi au menu)"
	@echo "  run-direct    Emulateur en TCP direct V23 vers $(MINITEL_SERVER)"
	@echo "  run-digitelec Backend Digitelec DTL 2000 (V23) vers $(MINITEL_SERVER)"
	@echo "  run-picowifi  Modem AT WiFi PicoWiFiModemUSB (SSID=$(PICOWIFI_SSID))"
	@echo "  run-ws        Bridge WebSocket + emulateur (ws://3617.fr)"
	@echo "  bridge        Lancer uniquement le bridge"
	@echo "  test          Executer tous les tests"
	@echo "  test-videotex Tests du decodeur Videotex"
	@echo "  test-bridge   Tests du bridge"
	@echo "  test-server   Serveur Videotex local de demo (test manuel)"
	@echo "  clean         Nettoyer les fichiers generes"
	@echo "  help          Afficher cette aide"
