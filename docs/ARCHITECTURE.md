# Architecture technique - OricTel

## Vue d'ensemble

OricTel est compose de deux sous-systemes independants:

1. **Programme Oric** (6502 C/ASM) - le terminal Minitel
2. **Bridge Python** - passerelle WebSocket <-> TCP (chemin SECONDAIRE)

Le montage de reference est aujourd'hui **LOCI + PicoWiFiModemUSB** : le modem AT
ouvre lui-meme les sockets TCP vers le serveur Minitel, le bridge n'est plus dans
le chemin. Celui-ci reste utile pour les serveurs exposes uniquement en WebSocket.

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
| serial_asm.s      |  <-- Driver ACIA bas niveau (polling, SMC de la base)
| serial.c/tx.c     |  <-- Couche fine + file d'emission non bloquante
| at_modem.c        |  <-- Commandes AT, raccrochage, veille de porteuse
| videotex.c        |  <-- Machine a etats protocole
| display.c         |  <-- Rendu HIRES (budget 1 ligne/passe)
| display_asm.s     |  <-- Blits assembleur (cellule, course, double hauteur)
| fonts.c           |  <-- Tables G0 / G1 / G2
| keyboard.c        |  <-- Scan clavier + mapping Minitel
| ui.c              |  <-- Helpers de menus (testables hote)
| main.c            |  <-- Boucle principale, menus, session
+-------------------+
```

`main.c` est un cycle *menus -> connexion -> session* : la session ne se quitte
que par ESC (confirme sur la ligne 0, sans effacer la page), qui raccroche
(`at_hangup`) et repart au menu Mode de connexion avec `vtx_init`. Les ecrans
d'echec de connexion et de perte de porteuse rendent 2 sur ESC pour le meme
retour. Il n'y a pas de sortie vers le BASIC (zone programme ecrasee).

### Montage de reference (materiel)

```
Oric --> ACIA 6551 $0380 (cartouche LOCI)
             |
             | firmware RP2040 : anneau RX de 32 OCTETS devant le registre
             |                   (src/mia/oric/acia.c, ACIA_RX_BUFFER_SIZE)
             v
        USB-CDC --> PicoWiFiModemUSB --> WiFi --> serveur Minitel (TCP)
```

L'anneau de 32 octets est un element de conception IMPORTANT : le 6551 nu ne
tamponne qu'un octet, et OricTel n'utilise aucune IRQ. C'est ce tampon qui donne
au logiciel une tolerance d'environ 250 000 cycles (30 octets a 1200 bauds) entre
deux lectures. Emuler ce montage SANS `--serial-buffer` est donc plus pessimiste
que le materiel reel.

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

### Configuration effective (base LOCI $0380)
- Controle: **$18** = 1200 bauds, **8N1**, horloge interne
- Commande: **$0B** = DTR, IRQ RX desactivee, TIC=10 (RTS bas SANS IRQ TX)

> Piege datasheet 6551 : TIC=01 (valeurs $05/$07) ACTIVE l'IRQ TX. TDRE etant
> leve en permanence, l'ACIA reassertait /IRQ sans fin et la machine gelait
> (regressions v0.3.3/v0.3.4). $0B est la bonne valeur.

L'ancienne configuration V23 7E1 ($28/$69) ne correspond plus a aucun montage
supporte : le PicoWiFiModemUSB dialogue en 8N1.

### Bits du registre Status
- Bit 3 (RDRF=$08): Donnee recue disponible
- Bit 4 (TDRE=$10): Transmetteur pret
- Bit 5 (=$20): **/DCD, logique INVERSEE** — non nul = PAS de porteuse. Sur LOCI
  ce bit ne suit que le montage USB et DTR, jamais l'etat de l'appel : il est
  donc inutilisable pour detecter une perte de porteuse (voir `serial.h`). La
  detection passe par la reponse `NO CARRIER` du modem (`at_carrier_watch`).

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

## Rendu optimise

Chaque ligne modifiee est marquee "dirty" avec la plage de colonnes touchee.
Le rendu n'est PAS cadence sur la trame video : il est **budgete a UNE ligne par
passe** depuis la boucle principale, qui rappelle `display_render()` tant qu'il
reste des lignes sales *et* qu'aucun octet n'attend en reception.

Ce budget est un compromis mesure, pas arbitraire. Pendant une passe de rendu la
reception n'est pas relue ; si la passe depasse la tolerance du tampon RX, des
octets sont perdus et la page arrive mutilee.

### Couts mesures (`make bench-render`, cycles 6502 reels)

| region | cout | en temps-octet @1200 bauds |
|---|---|---|
| passe `display_render()` (1 ligne) | ~151 000 | 18,8 |
| ligne hybride realiste | ~79 000 | 10,1 |
| ligne double hauteur | ~134 000 | 16,7 |
| mise a jour d'une cellule | ~30 000 | 3,6 |
| `vtx_process()` (40 caracteres) | ~105 000 | 13,2 |

Budget d'un octet a 1200 bauds : **8 333 cycles**. Tolerance du montage reel :
~250 000 cycles (anneau de 32 octets du firmware LOCI).

Le banc dumpe aussi l'empreinte SHA-256 du framebuffer apres une page canonique :
toute optimisation qui se veut sans effet visuel doit la laisser INCHANGEE.

### Chemins rapides assembleur (`display_asm.s`)

| routine | role |
|---|---|
| `blit_cell8` | une cellule taille normale (~190 cy contre ~2 000 en C) |
| `blit_run` | course de cellules eligibles ; s'arrete sur toute taille non normale |
| `blit_cell4x2` | double hauteur : 4 lignes source etirees sur 8 lignes pixel |
| `display_clear` | remplissage du framebuffer par pages (~5 ms) |

### Piege cc65 a connaitre

`ctx->screen[row][col]` fait generer une multiplication 16 bits par 240 A CHAQUE
acces, et un pointeur *parametre* est recharge depuis la pile logicielle par un
`jsr ldptr1ysp` (~50 cycles) a chaque dereferencement. Les boucles chaudes
hissent donc un pointeur de ligne hors de la boucle, et `videotex.c` passe par un
pointeur de contexte de portee fichier plus des tables d'offsets. Ces deux
transformations ont donne 4,0x sur le rendu et 1,50x sur le decodeur.
