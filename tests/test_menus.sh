#!/usr/bin/env bash
# =============================================================================
# test_menus.sh - Parcours de menus de bout en bout, sous Phosphoric headless
#
# Longtemps IMPOSSIBLE : --type-keys renvoyait la machine au BASIC, OricTel
# scannant la matrice VIA/PSG directement et non la ROM. Corrige cote Phosphoric
# (>= v1.118.0-alpha), ce test devient realisable - d'ou son ajout.
#
# Scenario : aucun modem ne repond (backend `file:` sur /dev/null). OricTel doit
#   1. traverser splash -> interface -> mode -> serveur,
#   2. echouer sur ATZ, tenter le raccrochage (at_hangup), re-echouer,
#   3. AFFICHER L'ECRAN D'ECHEC au lieu d'entrer en session a l'aveugle,
#   4. reboucler sur une nouvelle tentative si on choisit "1 Reessayer".
#
# Le point 3 est la non-regression du bug de "premiere page illisible" : le
# retour de modem_connect etait ignore et la session demarrait sur un flux
# inexistant ou commence en cours de page.
#
# ASSERTION : on ne compare pas des pixels (fragile), on cherche le texte dans
# le BUFFER ECRAN Videotex de la RAM. Chaque cellule fait 6 octets et le
# caractere est a l'offset 0, donc la chaine apparait avec un PAS DE 6.
#
# Surcharges : EMU=... ROM=... ./tests/test_menus.sh
# =============================================================================
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
# Le bundle tools/ est en v1.27.6 (sans le correctif --type-keys) : on prend
# d'abord le build live, qui est celui qui sait injecter des touches.
EMU="${EMU:-$HOME/Oric1/oric1-emu}"
[ -x "$EMU" ] || EMU="$HERE/tools/oric1-emu-sdl"
ROM="${ROM:-$HOME/Oric1/roms/basic11b.rom}"
TAP="$HERE/orictel.tap"

skip() { echo "SKIP : $1"; exit 0; }

[ -x "$EMU" ] || skip "emulateur Phosphoric introuvable ($EMU)"
[ -f "$ROM" ] || skip "ROM Oric introuvable ($ROM)"
[ -f "$TAP" ] || skip "orictel.tap absent (make)"

# Le correctif --type-keys est necessaire : sans lui le test ne prouve rien.
# "Phosphoric v1.118.0-alpha" -> 118 (le MINEUR porte la version reelle).
VER="$($EMU --help 2>&1 | head -1 | sed -E 's/.*v[0-9]+\.([0-9]+)\..*/\1/')"
case "$VER" in
    ''|*[!0-9]*) skip "version de Phosphoric illisible" ;;
esac
[ "$VER" -ge 118 ] 2>/dev/null || \
    skip "Phosphoric trop ancien (--type-keys renvoie au BASIC avant v1.118)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "=== OricTel - Parcours de menus jusqu'a l'ecran d'echec ==="
echo "    EMU=$EMU (v$VER)"

# Touches : splash, interface, mode "1" (Modem AT), serveur "1", puis "1"
# (Reessayer) une fois l'ecran d'echec affiche.
run_to() {   # $1 = cycle du dump, $2 = fichier, $3.. = --type-keys en plus
    local at="$1" out="$2"; shift 2
    "$EMU" --rom "$ROM" --tape "$TAP" -f \
        --loci --serial "file:/dev/null:$TMP/null.bin" --headless \
        --type-keys "14000000:A" --type-keys "16000000:A" \
        --type-keys "19000000:1" --type-keys "22000000:1" "$@" \
        --dump-ram-at "$at:$out" -c $((at + 500000)) >/dev/null 2>&1
}

find_text() {  # $1 = dump, $2 = texte -> 0 si trouve
    python3 - "$1" "$2" <<'PY'
import sys
ram = open(sys.argv[1], 'rb').read()
want = sys.argv[2].encode()
# Cellule Videotex = 6 octets, caractere a l'offset 0 -> pas de 6.
for base in range(len(ram) - 6 * len(want)):
    if all(ram[base + 6 * k] == want[k] for k in range(len(want))):
        sys.exit(0)
sys.exit(1)
PY
}

fails=0
check() {
    if [ "$1" -eq 0 ]; then echo "ok   : $2"; else echo "FAIL : $2"; fails=$((fails + 1)); fi
}

run_to 18000000 "$TMP/menu.bin"
find_text "$TMP/menu.bin" "Mode de connexion"; check $? "menu Mode de connexion atteint"

run_to 60000000 "$TMP/echec.bin"
find_text "$TMP/echec.bin" "ECHEC DE CONNEXION"; check $? "ecran d'echec affiche (pas d'entree en session aveugle)"
find_text "$TMP/echec.bin" "1 Reessayer"; check $? "option 1 Reessayer presente"
find_text "$TMP/echec.bin" "3 Entrer quand meme"; check $? "option 3 entiere (non tronquee a 40 colonnes)"

run_to 64000000 "$TMP/retry.bin" --type-keys "62000000:1"
find_text "$TMP/retry.bin" "ATZ"; check $? "Reessayer relance une tentative (ATZ)"

if [ "$fails" -eq 0 ]; then
    echo "=== Resultats: 5/5 passes ==="
    exit 0
fi
echo "=== Resultats: ECHEC ($fails) ==="
exit 1
