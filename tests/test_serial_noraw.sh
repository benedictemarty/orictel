#!/usr/bin/env bash
# =============================================================================
# test_serial_noraw.sh - Garde-fou reception ACIA FIDELE au 6551 reel
#
# Rejoue une rafale d'octets connue sur l'ACIA $0380 via le backend `file:` de
# Phosphoric, SANS --serial-buffer : l'emulateur se comporte alors comme le
# 6551 physique (registre RX de 1 octet, aucun FIFO logiciel). On verifie, via
# la trace serie (oracle byte-exact), que le chemin de reception d'OricTel
# (config $18/$0B + DCD + driver recv sous SEI/PLP, v0.3.6) delivre TOUS les
# octets sans perte et que le FIFO emule reste a 0 (preuve "sans buffer").
#
# Complementaire au test HOST test_atmodem.c #7 (invariant de timing : aucun
# rendu intercale en pleine rafale). Ici on valide le materiel emule ; la, la
# logique de drain.
#
# CI : ce test necessite le binaire Phosphoric + une ROM Oric. Absents (cas
# GitHub Actions host-only), il se termine en SKIP (code 0), sans echec.
#
# Surcharges : EMU=... ROM=... CYCLES=... ./tests/test_serial_noraw.sh
# =============================================================================
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${EMU:-$HERE/tools/oric1-emu-sdl}"
[ -x "$EMU" ] || EMU="${EMU_FALLBACK:-/home/bmarty/Oric1/oric1-emu}"
ROM="${ROM:-/home/bmarty/Oric1/roms/basic10.rom}"   # Oric-1 (BASIC 1.0), montage reel
DIAG="$HERE/diag.tap"
CYCLES="${CYCLES:-13000000}"

skip() { echo "SKIP : $1"; exit 0; }

[ -x "$EMU" ] || skip "emulateur Phosphoric introuvable ($EMU) - test local uniquement"
[ -f "$ROM" ] || skip "ROM Oric introuvable ($ROM)"
[ -f "$DIAG" ] || { (cd "$HERE" && make diag >/dev/null 2>&1) || skip "diag.tap non constructible"; }
[ -f "$DIAG" ] || skip "diag.tap absent apres build"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
STREAM="$TMP/rx.bin"
TRACE="$TMP/at.log"

# Rafale deterministe : uniquement [A-Z0-9] (pas de CR/LF ni d'espace) pour une
# reconstruction sans ambiguite depuis la colonne CHR/HEX de la trace.
PAYLOAD=""
for i in $(seq 1 12); do
    PAYLOAD="${PAYLOAD}OVERRUNGUARD0123456789ABCDEF"   # 28 c * 12 = 336 octets
done
printf '%s' "$PAYLOAD" > "$STREAM"
EXPECT_LEN=${#PAYLOAD}

echo "=== OricTel - Garde-fou reception 6551 (sans --serial-buffer) ==="
echo "    EMU=$EMU"
echo "    ROM=$ROM  payload=${EXPECT_LEN} octets @ \$0380 (1200 8N1, FIFO 1 octet)"

# NB: PAS de --serial-buffer -> RX 1 octet fidele. PAS de --realtime : le pacing
# baud est en cycles emules, independant du wall-clock (test rapide).
"$EMU" --rom "$ROM" --tape "$DIAG" -f \
    --acia-addr 0380 --serial "file:$STREAM" \
    --headless --serial-trace "$TRACE" -c "$CYCLES" >/dev/null 2>&1

[ -s "$TRACE" ] || { echo "FAIL : trace serie vide (reception nulle ?)"; exit 1; }

python3 - "$TRACE" "$PAYLOAD" <<'PY'
import sys
trace, payload = sys.argv[1], sys.argv[2]
rx_hex, fifos = [], []
with open(trace, encoding="latin-1") as f:
    for line in f:
        if line.startswith('#'):
            continue
        parts = line.split()
        # format : CYCLE DIR HEX CHR STATUS FIFO SIGNALS...
        if len(parts) >= 6 and parts[1] == 'RX':
            rx_hex.append(parts[2].upper())
            fifos.append(parts[5])

recv = bytes(int(h, 16) for h in rx_hex)
recv_ascii = recv.decode('latin-1')
want = payload

fails = 0
def check(cond, msg):
    global fails
    print(("ok   : " if cond else "FAIL : ") + msg)
    if not cond:
        fails += 1

# 1) Aucun octet perdu : la rafale complete est recue de bout en bout.
#    (Tolerance d'1 octet initial consomme par le clear-RDR de apply_cfg.)
check(want in recv_ascii or want[1:] in recv_ascii,
      "rafale complete recue byte-exact (aucun overrun sur 6551 1 octet)")

# 2) Volume : au moins tous les octets attendus (moins 1 tolere).
check(len(recv) >= len(want) - 1,
      "volume RX >= payload (%d recu / %d attendu)" % (len(recv), len(want)))

# 3) Mode "sans buffer" : le FIFO emule reste a 0 sur tous les octets
#    (si quelqu'un ajoutait --serial-buffer, la colonne FIFO deviendrait > 0).
check(all(x == '0' for x in fifos),
      "FIFO emule = 0 partout (mode 1 octet, sans --serial-buffer)")

sys.exit(1 if fails else 0)
PY
RC=$?
if [ "$RC" -eq 0 ]; then
    echo "=== Resultats: reception fidele OK (sans buffer) ==="
else
    echo "=== Resultats: ECHEC (perte d'octets sur 6551 1 octet) ==="
fi
exit "$RC"
