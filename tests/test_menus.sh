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
#   4. reboucler sur une nouvelle tentative si on choisit "1 Reessayer",
#   5. revenir au menu sur ESC (ecran d'echec), et en session : ESC pose la
#      question dans la barre de statut, une autre touche reprend, ESC ESC
#      raccroche et revient au menu, ESC sur le menu principal sort vers le
#      BASIC,
#   6. afficher la barre de statut (3 lignes texte) avec son jeu de caracteres
#      en $9800 intact, la pile C restant au-dessus de $9E00.
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
# Requis : >= v1.118. "Phosphoric v1.118.0-alpha" -> 1 et 118 ; depuis la
# v2.0.0 le mineur repart a 0, d'ou la comparaison sur (majeur, mineur) : ne
# lire que le mineur faisait SKIPper le test en silence a partir de la v2.
VER="$($EMU --help 2>&1 | head -1 | sed -nE 's/.*v([0-9]+)\.([0-9]+)\..*/\1 \2/p')"
MAJ="${VER%% *}"; MIN="${VER##* }"
case "$MAJ$MIN" in
    ''|*[!0-9]*) skip "version de Phosphoric illisible" ;;
esac
if [ "$MAJ" -lt 1 ] || { [ "$MAJ" -eq 1 ] && [ "$MIN" -lt 118 ]; }; then
    skip "Phosphoric trop ancien (--type-keys renvoie au BASIC avant v1.118)"
fi
VER="$MAJ.$MIN"

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

find_status() {  # $1 = dump, $2 = texte -> 0 si present dans la barre de statut
    python3 - "$1" "$2" <<'PY'
import sys
ram = open(sys.argv[1], 'rb').read()
# 3 lignes texte a $BF68-$BFDF (pas de 1) ; bit 7 = video inverse, ignore.
bar = bytes(b & 0x7F for b in ram[0xBF68:0xBFE0])
sys.exit(0 if sys.argv[2].encode() in bar else 1)
PY
}

fails=0
check() {
    if [ "$1" -eq 0 ]; then echo "ok   : $2"; else echo "FAIL : $2"; fails=$((fails + 1)); fi
}

run_to 18000000 "$TMP/menu.bin"
find_text "$TMP/menu.bin" "Mode de connexion"; check $? "menu Mode de connexion atteint"

# Barre de statut (3 lignes texte sous la page HIRES) : version + aide touches,
# et son jeu de caracteres en $9800, que la pile C (reduite a $9C00-$9FFF)
# ne doit plus ecraser.
find_status "$TMP/menu.bin" "OricTel v0."; check $? "barre de statut : version affichee (ligne 0)"
find_status "$TMP/menu.bin" "^A Annul"; check $? "barre de statut : aide touches (ligne 2)"
python3 - "$TMP/menu.bin" <<'PY'; check $? "jeu de caracteres de la barre intact en \$9800 (glyphe 'A' = font_g0)"
import sys, re
ram = open(sys.argv[1], 'rb').read()
src = open("src/fonts.c").read()
m = re.search(r"/\* \$41 A \*/\s*((?:0x[0-9A-Fa-f]{2},?\s*){8})", src)
glyph = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))
sys.exit(0 if ram[0x9800 + 0x41 * 8:0x9800 + 0x41 * 8 + 8] == glyph else 1)
PY

run_to 60000000 "$TMP/echec.bin"
find_text "$TMP/echec.bin" "ECHEC DE CONNEXION"; check $? "ecran d'echec affiche (pas d'entree en session aveugle)"
find_text "$TMP/echec.bin" "1 Reessayer"; check $? "option 1 Reessayer presente"
find_text "$TMP/echec.bin" "3 Entrer quand meme"; check $? "option 3 entiere (non tronquee a 40 colonnes)"

run_to 64000000 "$TMP/retry.bin" --type-keys "62000000:1"
find_text "$TMP/retry.bin" "ATZ"; check $? "Reessayer relance une tentative (ATZ)"

# ESC sur l'ecran d'echec : abandon et retour au menu Mode de connexion (la
# touche de sortie universelle). L'ecran d'echec doit avoir disparu.
find_text "$TMP/echec.bin" "ESC Retour au menu"; check $? "ecran d'echec : ESC propose"
# 85 Mcycles : ESC declenche at_hangup (deux gardes de 1,1 s + attentes OK 2 s
# et NO CARRIER 3 s, sans modem ici), soit ~7,5 s avant le menu.
run_to 85000000 "$TMP/esc.bin" --type-keys '62000000:\e'
find_text "$TMP/esc.bin" "Mode de connexion"; check $? "ESC sur l'echec -> retour au menu Mode de connexion"
if find_text "$TMP/esc.bin" "ECHEC DE CONNEXION"; then check 1 "ecran d'echec efface apres ESC"; else check 0 "ecran d'echec efface apres ESC"; fi

# ESC en SESSION : "3 Entrer quand meme" entre en session ; un premier ESC
# pose la question dans la barre de statut sans effacer la page, un second quitte
# (raccroche) et revient au menu ; toute autre touche reprend.
run_to 70000000 "$TMP/ask.bin" --type-keys "62000000:3" --type-keys '66000000:\e'
find_status "$TMP/ask.bin" "ESC: quitter?"; check $? "ESC en session : question posee dans la barre de statut"
if find_text "$TMP/ask.bin" "ESC: quitter?"; then check 1 "la page Videotex n'est pas touchee par la question"; else check 0 "la page Videotex n'est pas touchee par la question"; fi
python3 - "$TMP/ask.bin" <<'PY'; check $? "pile C : releve au-dessus de \$9E00 (512 octets de marge sur 1 Ko)"
import sys
ram = open(sys.argv[1], 'rb').read()
# La bascule HIRES de la ROM a rempli $9800-$9FFF de $40 : tout octet
# different sous $9E00 signifierait que la pile y est descendue.
sys.exit(0 if all(b == 0x40 for b in ram[0x9C00:0x9E00]) else 1)
PY
run_to 74000000 "$TMP/resume.bin" --type-keys "62000000:3" --type-keys '66000000:\e' --type-keys "70000000:x"
if find_status "$TMP/resume.bin" "ESC: quitter?"; then check 1 "autre touche : question retiree, session reprise"; else check 0 "autre touche : question retiree, session reprise"; fi
run_to 95000000 "$TMP/quit.bin" --type-keys "62000000:3" --type-keys '66000000:\e' --type-keys '70000000:\e'
find_text "$TMP/quit.bin" "Mode de connexion"; check $? "ESC ESC en session -> raccroche et retour au menu"

# ESC sur le menu principal : sortie d'OricTel par le vecteur de reset ROM.
# Preuve : la RAM texte ($BB80) porte le "Ready" du BASIC (pas de 1, pas de 6)
# et la page Videotex "Mode de connexion" n'est plus la.
# Appel direct : les --type-keys doivent etre donnes par cycle CROISSANT, et
# run_to envoie deja "1" a 19 et 22 Mcycles (ici, pas de "1" : on reste au menu et ESC tombe a 24 Mcycles).
"$EMU" --rom "$ROM" --tape "$TAP" -f \
    --loci --serial "file:/dev/null:$TMP/null.bin" --headless \
    --type-keys "14000000:A" --type-keys "16000000:A" --type-keys '24000000:\e' \
    --dump-ram-at "30000000:$TMP/basic.bin" -c 30500000 >/dev/null 2>&1
python3 - "$TMP/basic.bin" <<'PY'; check $? "ESC sur le menu -> redemarrage a froid, BASIC Ready"
import sys
ram = open(sys.argv[1], 'rb').read()
sys.exit(0 if b"Ready" in ram[0xBB80:0xBFE0] else 1)
PY

if [ "$fails" -eq 0 ]; then
    echo "=== Resultats: 17/17 passes ==="
    exit 0
fi
echo "=== Resultats: ECHEC ($fails) ==="
exit 1
