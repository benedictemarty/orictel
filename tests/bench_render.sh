#!/usr/bin/env bash
# =============================================================================
# bench_render.sh - Mesure en CYCLES 6502 du cout des passes de rendu HIRES
#
# Lance bench.tap sous Phosphoric headless. Le programme encadre chaque region
# mesuree par un octet MARQUEUR ecrit dans le registre DATA de l'ACIA ($0380).
# --serial-trace horodate chaque acces en cycles emules : la difference entre
# marqueur de debut ($10+id) et marqueur de fin ($60+id) donne le cout exact
# de la region, IRQ ROM comprises.
#
# Le resultat est compare au BUDGET d'un octet a 1200 bauds : a 1 MHz, un octet
# arrive toutes les 8333 cycles. Toute region plus longue que ce budget peut
# faire deborder le registre RX d'UN octet du 6551 (aucune IRQ) -> caracteres
# perdus a l'ecran.
#
# Surcharges : EMU=... ROM=... CYCLES=... ./tests/bench_render.sh
# =============================================================================
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${EMU:-$HERE/tools/oric1-emu-sdl}"
[ -x "$EMU" ] || EMU="${EMU_FALLBACK:-/home/bmarty/Oric1/oric1-emu}"
ROM="${ROM:-/home/bmarty/Oric1/roms/basic11b.rom}"
BENCH="$HERE/bench.tap"
CYCLES="${CYCLES:-40000000}"

skip() { echo "SKIP : $1"; exit 0; }

[ -x "$EMU" ] || skip "emulateur Phosphoric introuvable ($EMU) - mesure locale uniquement"
[ -f "$ROM" ] || skip "ROM Oric introuvable ($ROM)"
[ -f "$BENCH" ] || skip "bench.tap absent (make bench.tap)"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
TRACE="$TMP/bench.log"

echo "=== OricTel - Banc de mesure du rendu HIRES (cycles 6502) ==="
echo "    EMU=$EMU"
echo "    ROM=$ROM  budget 1 octet @1200 bauds = 8333 cycles @1 MHz"
echo

# --dump-ram-at bien APRES la fin du banc (le marqueur $FF) : le programme
# boucle ensuite a vide, l'image memoire est donc stable. Un point de dump trop
# tot donnerait deux etats de programme differents entre un build lent et un
# build rapide -> fausse difference de framebuffer.
"$EMU" --rom "$ROM" --tape "$BENCH" -f \
    --acia-addr 0380 --serial "file:/dev/null:$TMP/out.bin" \
    --headless --serial-trace "$TRACE" \
    --dump-ram-at "60000000:$TMP/ram.bin" -c 61000000 >/dev/null 2>&1

[ -s "$TRACE" ] || { echo "FAIL : trace serie vide (le bench n'a rien emis)"; exit 1; }

python3 - "$TRACE" <<'PY'
import sys, collections

LABELS = {
    1: "display_render()  page pleine (passe budgetee main.c, 2 lignes)",
    2: "1 ligne hybride   PIRE CAS (couleur differente a chaque colonne)",
   16: "1 ligne hybride   REALISTE (mots de couleur constante)",
    3: "1 ligne brute     (monochrome, 40 col)",
    4: "1 ligne brute     (span 1 colonne, cas incremental)",
    5: "1 ligne G1        (mosaiques, dithering, 40 col)",
    6: "1 ligne double hauteur (40 col)",
    7: "vtx_process() x40 caracteres G0 (drain seul, sans rendu)",
   17: "  dont: dispatch seul (x40 octets C0 neutres, sans put_char)",
    9: "  forme NON hissee : pre-scan screen[row][col] (avant optim.)",
   15: "  forme HISSEE    : meme pre-scan (== code actuel)",
   10: "  dont: blit pur       1 cellule G0",
   11: "  dont: blit pur      40 cellules G0",
   12: "  dont: blit+dither   40 cellules G1",
   13: "  forme NON hissee : row_has_dblh (avant optim.)",
   14: "  dont: blit_run ASM  40 cellules G0 (chemin rapide seul)",
    8: "CALIBRATION boucle asm 256 tours (attendu ~1280 cycles)",
}
BUDGET = 8333  # cycles pour 1 octet a 1200 bauds, 6502 @ 1 MHz

# Trace : CYCLE DIR HEX CHR STATUS FIFO SIGNALS...
events = []
with open(sys.argv[1], encoding="latin-1") as f:
    for line in f:
        if line.startswith('#'):
            continue
        p = line.split()
        if len(p) >= 3 and p[1] == 'TX':
            try:
                events.append((int(p[0]), int(p[2], 16)))
            except ValueError:
                pass

if not events:
    print("FAIL : aucun marqueur TX dans la trace"); sys.exit(1)

runs = collections.defaultdict(list)
open_at = {}
for cyc, b in events:
    # Marqueurs : debut = $10+id, fin = $60+id, pour id 0..31.
    if 0x10 <= b <= 0x2F:
        open_at[b - 0x10] = cyc
    elif 0x60 <= b <= 0x7F:
        i = b - 0x60
        if i in open_at:
            runs[i].append(cyc - open_at.pop(i))

if not runs:
    print("FAIL : aucune paire de marqueurs complete (%d evenements TX)" % len(events))
    sys.exit(1)

cal = runs.get(8)
if cal:
    c = min(cal)
    ok = 1200 <= c <= 1400
    print("CALIBRATION : boucle asm 256 tours = %d cycles (attendu 1280) -> %s"
          % (c, "chaine de mesure VALIDE" if ok else "CHAINE DE MESURE SUSPECTE"))
    if not ok:
        print("  Les chiffres ci-dessous ne sont PAS exploitables.")
    print()

print("%-58s %8s %8s %8s %10s" % ("Region mesuree", "min", "moy", "max", "octets*"))
print("-" * 96)
worst = 0
for i in sorted(runs):
    v = runs[i]
    mn, mx = min(v), max(v)
    av = sum(v) // len(v)
    worst = max(worst, mx)
    print("%-58s %8d %8d %8d %10.1f" % (LABELS.get(i, "id %d" % i), mn, av, mx, mx / BUDGET))
print("-" * 96)
print("* octets = combien d'octets a 1200 bauds arrivent pendant la region (max).")
print()
print("SEUILS (deux montages, deux tolerances tres differentes) :")
print("  - Vrai LOCI / --loci-emu : le firmware place un anneau de 32 octets devant")
print("    le registre (src/mia/oric/acia.c, ACIA_RX_BUFFER_SIZE), ~30 utilisables")
print("    -> tolerance ~%d cycles par passe de rendu." % (30 * BUDGET))
print("  - run-loci-real (backend com: SANS --serial-buffer) : registre d'UN octet,")
print("    tolerance %d cycles. Plus pessimiste que le materiel reel." % BUDGET)
print()
p1 = runs.get(1)
if p1:
    lost = max(p1) / BUDGET
    print("VERDICT : une passe display_render() = %d cycles au pire = %.1f temps-octet."
          % (max(p1), lost))
    print("  vrai LOCI (anneau ~30 octets) : %s"
          % ("OK, sous le tampon" if lost <= 30 else
             "OVERRUN, ~%d octets perdus" % int(lost - 30 + 0.5)))
    print("  run-loci-real (1 octet)       : %s"
          % ("OK" if lost <= 1.0 else
             "overrun, ~%d octets perdus (montage plus pessimiste que le reel)"
             % int(lost - 1 + 0.5)))
PY

# Empreinte du framebuffer HIRES ($A000-$BF3F). Le banc rend une sequence
# deterministe : toute optimisation du rendu qui se veut SANS changement visuel
# doit laisser cette empreinte INCHANGEE. C'est le garde-fou qui a valide le
# hissage des pointeurs de ligne (gain 1,9x, rendu strictement pixel-exact).
#
# NB : le dump est pris bien APRES la fin du banc (marqueur $FF), quand le
# programme boucle a vide et que l'image memoire est stable. Un point de dump
# trop tot comparerait deux etats de programme differents entre un build lent
# et un build rapide -> fausse difference.
if [ -f "$TMP/ram.bin" ]; then
    echo
    python3 -c "
import hashlib
fb = open('$TMP/ram.bin','rb').read()[0xA000:0xBF40]
print('Empreinte framebuffer HIRES : %s' % hashlib.sha256(fb).hexdigest())
print('  (doit rester identique apres toute optimisation sans effet visuel)')
"
fi
