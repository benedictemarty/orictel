#!/usr/bin/env bash
# =============================================================================
# test_carrier.sh - Delai de confirmation de perte de porteuse, MESURE sur cible
#
# CARRIER_CONFIRM_MS (main.c) est compte sur le Timer 2 du VIA : ce test
# verifie que 4 s demandees font bien ~4 s, quel que soit le cout de la boucle
# de session (la constante etait auparavant un nombre d'iterations, mesuree a
# 3,6-5,6 s sur materiel et que toute optimisation raccourcissait).
#
# Montage : Phosphoric --loci + backend tcp: vers tests/fake_modem.py (ATZ/ATI/
# ATDT repondus, page envoyee, puis "NO CARRIER" et silence). --realtime pour
# que le delai du faux modem (temps mur) tombe dans la fenetre de vidages.
#
# Mesure : cycle du dernier octet RX (trace serie) -> premier vidage RAM
# (toutes les 0,2 s) ou la page a disparu (l'ecran PERTE DE PORTEUSE l'efface).
# Attendu : 4,0 s +/- 0,4 s. Dure ~40 s (temps reel).
# =============================================================================
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${EMU:-$HOME/Oric1/oric1-emu}"
[ -x "$EMU" ] || EMU="$HERE/tools/oric1-emu-sdl"
ROM="${ROM:-$HOME/Oric1/roms/basic11b.rom}"
TAP="$HERE/orictel.tap"
PORT="${PORT:-4761}"

skip() { echo "SKIP : $1"; exit 0; }
[ -x "$EMU" ] || skip "emulateur Phosphoric introuvable ($EMU)"
[ -f "$ROM" ] || skip "ROM Oric introuvable ($ROM)"
[ -f "$TAP" ] || skip "orictel.tap absent (make)"
"$EMU" --help 2>&1 | grep -q -- "--realtime" || skip "Phosphoric sans --realtime"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"; kill $MODEM 2>/dev/null' EXIT

echo "=== OricTel - Delai de confirmation de perte de porteuse (Timer 2 VIA) ==="
python3 "$HERE/tests/fake_modem.py" "$PORT" 3 & MODEM=$!
sleep 0.5

# Touches : splash, interface, mode "1", serveur "1". CONNECT vers ~25 s,
# NO CARRIER 3 s plus tard, fenetre de vidages 28-38 s par pas de 0,2 s.
DUMPS=""
for c in $(seq 280 2 380); do DUMPS="$DUMPS --dump-ram-at ${c}00000:$TMP/d_$c.bin"; done
timeout 120 "$EMU" --rom "$ROM" --tape "$TAP" -f --loci \
    --serial "tcp:127.0.0.1:$PORT" --serial-buffer 4096 --serial-trace "$TMP/trace.txt" \
    --headless --realtime \
    --type-keys "14000000:A" --type-keys "16000000:A" \
    --type-keys "19000000:1" --type-keys "22000000:1" \
    $DUMPS -c 38500000 >"$TMP/emu.log" 2>&1

LAST="$(grep ' RX ' "$TMP/trace.txt" | tail -1 | awk '{print $1}')"
[ -n "$LAST" ] || { echo "FAIL : aucun octet RX trace (faux modem non joint ?)"; exit 1; }

python3 - "$TMP" "$LAST" <<'PY'
import sys
tmp, last = sys.argv[1], int(sys.argv[2]) / 1e6
def vt(ram, s):
    w = s.encode()
    return any(all(ram[b + 6 * k] == w[k] for k in range(len(w))) for b in range(0x6000, 0x9800))
seen_page = False
for c in range(280, 381, 2):
    ram = open(f"{tmp}/d_{c}.bin", "rb").read()
    page = vt(ram, "PAGE DE TEST")
    if page:
        seen_page = True
        continue
    if seen_page:
        lo, hi = c / 10 - 0.2 - last, c / 10 - last
        print(f"dernier octet RX a {last:.2f} s, page effacee entre {c/10-0.2:.1f} et {c/10:.1f} s")
        print(f"delai mesure : {lo:.1f} a {hi:.1f} s (attendu 4,0 +/- 0,4)")
        ok = 3.6 <= hi and lo <= 4.4
        print("ok   : delai de confirmation ~4 s" if ok else "FAIL : delai hors tolerance")
        print("=== Resultats: 1/1 passes ===" if ok else "=== Resultats: ECHEC ===")
        sys.exit(0 if ok else 1)
print("FAIL : " + ("page jamais affichee (connexion ratee)" if not seen_page else "ecran de perte de porteuse jamais affiche"))
sys.exit(1)
PY
