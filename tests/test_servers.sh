#!/usr/bin/env bash
# =============================================================================
# test_servers.sh - Fidelite du decodage contre de VRAIS serveurs Minitel
#
# Se connecte pour de bon (PicoWiFiModemUSB physique) via la chaine CO-SIMULEE,
# la plus fidele : Oric -> ACIA $0380 -> vrai firmware LOCI -> USB-CDC -> dongle.
# Puis extrait la page decodee de la RAM et verifie qu'elle contient les ancres
# attendues.
#
# POURQUOI CE TEST : le bug de "premiere page illisible" n'avait ete detecte que
# parce qu'un humain a regarde une capture. Ici, une regression du decodeur ou du
# moteur de rendu fait disparaitre les ancres et le test tombe.
#
# ANCRES STABLES UNIQUEMENT. La page PAVI affiche une periode tarifaire et un
# tarif qui dependent de l'HEURE ("PERIODE ROUGE", "0,77F"), la page MiniPavi un
# numero de session ("Pin:9738") : aucun de ces elements ne sert d'ancre, sinon
# le test serait instable. On s'appuie sur la structure du service, qui, elle,
# ne bouge pas.
#
# Lent par construction (~2 min par serveur, liaison a 1200 bauds en temps reel)
# et tributaire du reseau + du dongle : HORS de `make test`, cible dediee
# `make test-servers`.
#
# Surcharges : ALL=1 (les deux serveurs)  EMU=...  PICO_DEV=...
# =============================================================================
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${EMU:-$HOME/Oric1/oric1-emu}"
ROM="${ROM:-$HOME/Oric1/roms/basic11b.rom}"
FW_ELF="${FW_ELF:-$HOME/loci/firmware/build-xip/src/loci-firmware.elf}"
PICO_DEV="${PICO_DEV:-/dev/ttyACM0}"
TAP="$HERE/orictel.tap"
ALL="${ALL:-0}"

skip() { echo "SKIP : $1"; exit 0; }

[ -x "$EMU" ]     || skip "emulateur Phosphoric introuvable ($EMU)"
[ -f "$ROM" ]     || skip "ROM Oric introuvable ($ROM)"
[ -f "$TAP" ]     || skip "orictel.tap absent (make)"
[ -f "$FW_ELF" ]  || skip "firmware LOCI introuvable ($FW_ELF)"
[ -c "$PICO_DEV" ] || skip "PicoWiFiModemUSB non branche ($PICO_DEV)"
"$EMU" --help 2>&1 | grep -q -- "--loci-emu" || \
    skip "Phosphoric sans co-simulation (--loci-emu requis, >= v1.118.1)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fails=0
check() {
    if [ "$1" -eq 0 ]; then echo "  ok   : $2"
    else echo "  FAIL : $2"; fails=$((fails + 1)); fi
}

# $1 = touche du menu serveur, $2 = nom, $3.. = ancres attendues
run_server() {
    local key="$1" name="$2"; shift 2
    local dump="$TMP/page$key.bin"

    echo "--- $name (choix $key) ---"
    "$EMU" --rom "$ROM" --tape "$TAP" -f \
        --loci-emu "$FW_ELF" --loci-cdc "$PICO_DEV" \
        --headless --realtime \
        --type-keys "14000000:A" --type-keys "16000000:A" \
        --type-keys "19000000:1" --type-keys "22000000:$key" \
        --dump-ram-at "115000000:$dump" -c 116000000 >/dev/null 2>&1

    if [ ! -s "$dump" ]; then
        check 1 "$name : vidage memoire produit"; return
    fi

    # La connexion a-t-elle abouti ? (sinon on est sur l'ecran d'echec)
    "$HERE/tests/vtx_page.py" "$dump" "ECHEC DE CONNEXION"
    if [ $? -eq 0 ]; then
        check 1 "$name : connexion etablie (ecran d'echec affiche)"
        return
    fi
    check 0 "$name : connexion etablie"

    for anchor in "$@"; do
        "$HERE/tests/vtx_page.py" "$dump" "$anchor"
        check $? "$name : \"$anchor\""
    done
}

echo "=== OricTel - Fidelite du decodage contre de vrais serveurs ==="
echo "    chaine co-simulee : firmware LOCI reel + $PICO_DEV"
echo

run_server 1 "PAVI 3617" \
    "TELETEL 1" "TELETEL 2" "TELETEL 3" \
    "DU SERVICE" "FIN DE COMMUNICATION" "LISTE DES SERVICES"

if [ "$ALL" = "1" ]; then
    run_server 2 "MiniPavi" \
        "Minitel is still alive" "www.minipavi.fr" \
        "Code du service" "Annuaire des services"
fi

echo
if [ "$fails" -eq 0 ]; then echo "=== Resultats: fidelite OK ==="; exit 0; fi
echo "=== Resultats: ECHEC ($fails) ==="
exit 1
