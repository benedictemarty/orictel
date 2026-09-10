#!/usr/bin/env python3
"""Extrait la page Videotex 25x40 d'un vidage RAM Oric (--dump-ram-at).

Le buffer ecran (vtx_context_t.screen) est un tableau de 25*40 cellules de
6 octets : {ch, charset, fg, bg, flags, size}. On le localise par SIGNATURE et
non par adresse, la map de liens changeant a chaque build : sur 1000 cellules
consecutives, charset<=2, fg<=7, bg<=7 et size<=3 -- des contraintes qu'une
zone memoire quelconque ne satisfait pas sur 6 Ko d'affilee.

ATTENTION : ne PAS contraindre `ch` a 0x7F. Les caracteres accentues sont
stockes avec des codes G2 INTERNES >= 0x80 (0x80 = e aigu, cf. videotex.c,
etat VTX_STATE_SS2_ACC). Ils sont translitteres ici vers la lettre de base,
pour que les ancres de test s'ecrivent en ASCII simple ("TELETEL").

Usage :  vtx_page.py dump.bin           -> affiche la page
         vtx_page.py dump.bin "ANCRE"   -> code 0 si l'ancre est presente
"""
import sys

ROWS, COLS, CELL = 25, 40, 6
NCELLS = ROWS * COLS

# Codes G2 internes -> lettre de base (videotex.c, VTX_STATE_SS2_ACC)
ACCENTS = {
    0x80: 'e', 0x81: 'e', 0x82: 'e', 0x83: 'a', 0x84: 'u', 0x85: 'c',
    0x86: 'a', 0x87: 'i', 0x88: 'o', 0x89: 'u', 0x8A: 'e', 0x8B: 'i',
    0x8C: 'u', 0x8D: 'A', 0x8E: 'E', 0x8F: 'E', 0x90: 'E', 0x91: 'E',
    0x93: 'A',
}


def _valid(ram, base, n):
    for k in range(n):
        o = base + k * CELL
        if ram[o + 1] > 2 or ram[o + 2] > 7 or ram[o + 3] > 7 or ram[o + 5] > 3:
            return False
    return True


def find_screen(ram):
    """Retourne (base, nb_imprimables) du buffer le plus dense, ou None."""
    best = None
    limit = len(ram) - NCELLS * CELL
    base = 0x0400
    while base < limit:
        if _valid(ram, base, 120) and _valid(ram, base, NCELLS):
            n = sum(1 for k in range(NCELLS)
                    if 0x20 <= ram[base + k * CELL] <= 0x7E)
            if best is None or n > best[1]:
                best = (base, n)
        base += 1
    return best


def render(ram, base):
    lines = []
    for r in range(ROWS):
        row = []
        for c in range(COLS):
            ch = ram[base + (r * COLS + c) * CELL]
            if 0x20 <= ch <= 0x7E:
                row.append(chr(ch))
            else:
                row.append(ACCENTS.get(ch, ' '))
        lines.append(''.join(row).rstrip())
    return lines


def main():
    ram = open(sys.argv[1], 'rb').read()
    found = find_screen(ram)
    if not found:
        print("buffer ecran introuvable", file=sys.stderr)
        return 2
    base, n = found
    lines = render(ram, base)
    if len(sys.argv) > 2:                      # mode assertion
        # Comparaison INSENSIBLE A LA CASSE : le serveur envoie "e aigu"
        # minuscule meme dans un mot en capitales ("TELETEL" arrive "TeLeTEL"
        # apres translitteration).
        text = '\n'.join(lines).upper()
        return 0 if sys.argv[2].upper() in text else 1
    print("buffer ecran a $%04X (%d cellules imprimables)\n" % (base, n))
    for i, l in enumerate(lines):
        print("%2d |%s|" % (i, l))
    return 0


if __name__ == '__main__':
    sys.exit(main())
