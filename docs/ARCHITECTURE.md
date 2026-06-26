# Architecture technique - OricTel

## Vue d'ensemble

OricTel est compose de deux sous-systemes independants:

1. **Programme Oric** (6502 C/ASM) - le terminal Minitel
2. **Bridge Python** - passerelle WebSocket <-> TCP

## Flux de donnees

```
Serveur Minitel (ws://3617.fr/ws)
        |
        | WebSocket (frames binaires, octets Videotex bruts)
        v
+-------------------+
| orictel_bridge.py |  Python asyncio
| Port TCP 3615     |  Relais bidirectionnel transparent
+-------------------+
        |
        | TCP socket (octets bruts, pas de trame)
        v
+-------------------+
| Phosphoric        |  Emulateur Oric
| Backend TCP ACIA  |  --serial tcp:127.0.0.1:3615
| Mode V23          |  --serial-v23 (1200/75 baud)
+-------------------+
        |
        | Registres ACIA 6551 @ $0380-$0383 (base LOCI)
        | Timing cycle-accurate
        v
+-------------------+
| Programme OricTel |  6502 code (cc65)
|                   |
| serial_asm.s      |  <-- Driver ACIA bas niveau
| videotex.c        |  <-- Machine a etats protocole
| display.c         |  <-- Rendu HIRES
| keyboard.c        |  <-- Scan clavier + mapping
| main.c            |  <-- Boucle principale
+-------------------+
```

## Carte memoire Oric

```
$0000-$00FB  Zero Page (cc65: $00E2-$00FB)
$0100-$01FF  Pile 6502
$0200-$02FF  Variables systeme
$0300-$030F  VIA 6522 (miroir $0300-$03FF)
$0380-$0383  ACIA 6551 (serie, base LOCI)
$0400-$0500  Zone systeme Oric
$0501-$97FF  CODE + DATA OricTel (~37 Ko)
$9800-$9FFF  BSS / Pile cc65 (2 Ko)
$A000-$BF3F  Framebuffer HIRES (8000 octets)
$BB80-$BFDF  Ecran texte (lignes 25-27 = barre statut)
$C000-$FFFF  ROM (16 Ko)
```

## Registres ACIA 6551 (cote Oric)

| Adresse | Lecture          | Ecriture          |
|---------|------------------|-------------------|
| $0380   | Donnee recue     | Donnee a envoyer  |
| $0381   | Registre statut  | Reset programme   |
| $0382   | Registre commande| Registre commande |
| $0383   | Registre controle| Registre controle |

### Configuration V23 Minitel
- Controle: $28 = 1200 baud, 7 bits, 1 stop
- Commande: $69 = DTR on, IRQ RX off, parite paire activee

### Bits du registre Status
- Bit 3 (RDRF=$08): Donnee recue disponible
- Bit 4 (TDRE=$10): Transmetteur pret

## Protocole Videotex - Machine a etats

```
NORMAL ──ESC($1B)──> ESC_SEQ
       ──US($1F)───> US_ROW
       ──SO($0E)───> (basculer G1)
       ──SI($0F)───> (basculer G0)
       ──$20-$7F──> (afficher caractere)
       ──$08-$0B──> (deplacement curseur)
       ──$0C─────> (effacer ecran)
       ──$0D─────> (retour chariot)

ESC_SEQ ──$40-$47──> (couleur encre) -> NORMAL
        ──$50-$57──> (couleur fond) -> NORMAL
        ──$48/$49──> (flash on/off) -> NORMAL
        ──$4C-$4F──> (taille) -> NORMAL
        ──$5B─────> CSI
        ──$39─────> PRO

US_ROW ──$40-$57──> US_COL (memorise ligne)

US_COL ──$41-$68──> NORMAL (positionne curseur)

CSI ──params+lettre──> NORMAL (commande ANSI-like)
```

## Affichage HIRES

- Resolution: 240x200 pixels = 40 colonnes x 25 lignes
- Chaque cellule: 6x8 pixels
- 3 jeux de caracteres:
  - G0: alphanumerique (ASCII Minitel, accents francais)
  - G1: mosaiques semi-graphiques (2x3 blocs = 64 motifs)
  - G2: caracteres supplementaires (diacritiques)

### Attributs par cellule
- Couleur encre (0-7)
- Couleur fond (0-7)
- Clignotement (flash)
- Inversion video
- Soulignement / separation mosaique
- Taille (normal, double hauteur, double largeur, double taille)
- Masquage (concealed)

## Rendu optimise (dirty rectangles)

Chaque cellule modifiee est marquee "dirty". A chaque trame (50 Hz),
seules les cellules modifiees sont re-rendues dans le framebuffer HIRES.
Cela reduit considerablement la charge CPU du 6502.
